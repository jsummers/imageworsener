// imagew-png.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#if IW_SUPPORT_PNG == 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <png.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iwpngrcontext {
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	png_structp png_ptr;
	png_infop info_ptr;
	struct iw_image *img;
	int bit_depth;
	int color_type;
	int sbit_flag;
	png_color_8 sbit;
};

struct errstruct {
	struct iw_context *ctx;
	int write_flag; // So we can tell if we're reading, or writing.
	jmp_buf jbuf;
};

// TODO?: Combine iwpngrcontext and pr_rsrc_struct into one struct (or something).
struct pr_rsrc_struct {
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	png_structp png_ptr;
	png_infop  info_ptr;
	iw_byte **row_pointers;
	struct iwpngrcontext rctx;
	struct iw_image img;
	struct errstruct errinfo;
};

#if PNG_LIBPNG_VER < 10400
static png_voidp my_png_malloc_fn(png_structp png_ptr, png_size_t n)
#else
static png_voidp my_png_malloc_fn(png_structp png_ptr, png_alloc_size_t n)
#endif
{
	struct iw_context *ctx = (struct iw_context*)png_get_mem_ptr(png_ptr);
	return iw_malloc(ctx,(size_t)n);
}

static void my_png_free_fn(png_structp png_ptr, png_voidp mem)
{
	struct iw_context *ctx = (struct iw_context*)png_get_mem_ptr(png_ptr);
	iw_free(ctx,mem);
}

static void my_png_error_fn(png_structp png_ptr, const char *err_msg)
{
	struct errstruct *errinfop;
	struct iw_context *ctx;

	errinfop = (struct errstruct *)png_get_error_ptr(png_ptr);
	ctx = errinfop->ctx;

	if(errinfop->write_flag) {
		iw_set_errorf(ctx,"libpng reports write error: %s",err_msg);
	}
	else {
		iw_set_errorf(ctx,"libpng reports read error: %s",err_msg);
	}

	longjmp(errinfop->jbuf, 1);
}

static void my_png_warning_fn(png_structp png_ptr, const char *warn_msg)
{
	return;
}

static void my_png_read_fn(png_structp png_ptr,
      png_bytep buf, png_size_t length)
{
	struct iwpngrcontext *rctx;
	struct iw_context *ctx;
	int ret;
	size_t bytesread = 0;

	rctx = (struct iwpngrcontext*)png_get_io_ptr(png_ptr);
	ctx = rctx->ctx;

	ret = (*rctx->iodescr->read_fn)(ctx,rctx->iodescr,buf,(size_t)length,&bytesread);
	if(!ret) {
		png_error(png_ptr,"Read error");
		return;
	}
	if(bytesread != (size_t)length) {
		png_error(png_ptr,"Unexpected end of file");
		return;
	}
}

// In PNG files, gamma values are stored as fixed-precision decimals, with
// 5 decimal places of precision.
// Libpng (as of this writing) gives us the nearest double to the nearest
// float to the value in the file.
// This function fixes that up so that we get the nearest double
// to the value in the file, as if the precision hadn't been reduced by
// being passed through a 32-bit float.
// This is pretty much pointless, but it's in the spirit of this
// application to be as accurate as possible.
static double fixup_png_gamma(double g)
{
	return ((double)((int)(0.5+100000.0*g)))/100000.0;
}

static int lpng_intent_to_iw_intent(int x)
{
	switch(x) {
	case PNG_sRGB_INTENT_PERCEPTUAL: return IW_INTENT_PERCEPTUAL;
	case PNG_sRGB_INTENT_RELATIVE:   return IW_INTENT_RELATIVE;
	case PNG_sRGB_INTENT_SATURATION: return IW_INTENT_SATURATION;
	case PNG_sRGB_INTENT_ABSOLUTE:   return IW_INTENT_ABSOLUTE;
	}
	return IW_INTENT_UNKNOWN;
}

static void iwpng_read_colorspace(struct iwpngrcontext *rctx)
{
	int tmp;
	double file_gamma;
	struct iw_csdescr csdescr;

	if(png_get_sRGB(rctx->png_ptr, rctx->info_ptr, &tmp)) {
		iw_make_srgb_csdescr_2(&csdescr);
		rctx->img->rendering_intent = lpng_intent_to_iw_intent(tmp);
	}
	else if(png_get_gAMA(rctx->png_ptr, rctx->info_ptr, &file_gamma)) {
		file_gamma = fixup_png_gamma(file_gamma);
		iw_make_gamma_csdescr(&csdescr,1.0/file_gamma);
	}
	else {
		// default:
		iw_make_srgb_csdescr_2(&csdescr);
	}

	iw_set_input_colorspace(rctx->ctx,&csdescr);
}

static void iwpng_read_density(struct iwpngrcontext *rctx)
{
	png_uint_32 pngdensity_x,pngdensity_y;
	int pngdensity_units;
	int density_code;

	if(!png_get_pHYs(rctx->png_ptr,rctx->info_ptr,&pngdensity_x,&pngdensity_y,&pngdensity_units)) {
		return;
	}
	if(pngdensity_units==PNG_RESOLUTION_UNKNOWN) {
		density_code = IW_DENSITY_UNITS_UNKNOWN;
	}
	else if(pngdensity_units==PNG_RESOLUTION_METER) {
		density_code = IW_DENSITY_UNITS_PER_METER;
	}
	else {
		return;
	}

	rctx->img->density_x = (double)pngdensity_x;
	rctx->img->density_y = (double)pngdensity_y;
	if(!iw_is_valid_density(rctx->img->density_x,rctx->img->density_y,density_code)) return;
	rctx->img->density_code = density_code;
}

static void iwpng_read_sbit(struct iwpngrcontext *rctx)
{
	png_uint_32 ret;
	png_color_8p sbit;
	struct iw_context *ctx = rctx->ctx;

	ret = png_get_sBIT(rctx->png_ptr, rctx->info_ptr, &sbit);
	if(!ret) return;

	// Tell libpng to reduce the image to the depth(s) specified by
	// the sBIT chunk.
	if(rctx->color_type==PNG_COLOR_TYPE_PALETTE) {
		// We'd like to call png_set_shift now, since we know that our sbit
		// struct is in the right format.
		// However, there seems to be a bug in libpng that prevents it from
		// working with paletted images in some cases -- if we call
		// png_set_shift now, it may get shifted multiple times.
		// So, for palette images, call png_set_shift as late as possible
		// (after png_set_palette_to_rgb and png_read_update_info).
		rctx->sbit_flag = 1;
		rctx->sbit = *sbit;
		// Apparently, it's not possible for a PNG file to label the significant
		// bits of the alpha values of a palette image.
		rctx->sbit.alpha = 8;
	}
	else {
		png_set_shift(rctx->png_ptr, sbit);
	}

	// Tell IW the true depth(s) of the input image.
	if(rctx->color_type & PNG_COLOR_MASK_COLOR) {
		iw_set_input_max_color_code(ctx,0, (1<<sbit->red  )-1 );
		iw_set_input_max_color_code(ctx,1, (1<<sbit->green)-1 );
		iw_set_input_max_color_code(ctx,2, (1<<sbit->blue )-1 );
		if(rctx->color_type & PNG_COLOR_MASK_ALPHA) {
			iw_set_input_max_color_code(ctx,3, (1<<sbit->alpha)-1 );
		}
	}
	else {
		iw_set_input_max_color_code(ctx,0, (1<<sbit->gray )-1 );
		if(rctx->color_type & PNG_COLOR_MASK_ALPHA) {
			iw_set_input_max_color_code(ctx,1, (1<<sbit->alpha)-1 );
		}
	}
}

static void iwpng_read_bkgd(struct iwpngrcontext *rctx)
{
	png_color_16p bg_colorp;
	double maxcolor;

	if(!png_get_bKGD(rctx->png_ptr, rctx->info_ptr, &bg_colorp)) return;

	maxcolor = (double)((1<<rctx->bit_depth)-1);
	switch(rctx->color_type) {
	case PNG_COLOR_TYPE_GRAY:
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		iw_set_input_bkgd_label(rctx->ctx,
			((double)bg_colorp->gray)/maxcolor,
			((double)bg_colorp->gray)/maxcolor,
			((double)bg_colorp->gray)/maxcolor);
		break;
	case PNG_COLOR_TYPE_RGB:
	case PNG_COLOR_TYPE_RGB_ALPHA:
		iw_set_input_bkgd_label(rctx->ctx,
			((double)bg_colorp->red)/maxcolor,
			((double)bg_colorp->green)/maxcolor,
			((double)bg_colorp->blue)/maxcolor);
		break;
	}
}

static void iw_read_ancillary_data1(struct iwpngrcontext *rctx)
{
	iwpng_read_sbit(rctx);
}

static void iw_read_ancillary_data(struct iwpngrcontext *rctx)
{
	iwpng_read_colorspace(rctx);
	iwpng_read_density(rctx);
	iwpng_read_bkgd(rctx);
}

static int iw_read_png_file3(struct pr_rsrc_struct *pr)
{
	struct iw_context *ctx = pr->ctx;
	struct iw_iodescr *iodescr = pr->iodescr;
	png_uint_32 width, height;
	int interlace_type;
	int i;
	int is_supported;
	int has_trns;
	int need_update_info;
	int numchannels=0;
	int retval=0;

	pr->errinfo.ctx = ctx;
	pr->errinfo.write_flag=0;

	pr->png_ptr = png_create_read_struct_2(PNG_LIBPNG_VER_STRING,
		(void*)(&pr->errinfo), my_png_error_fn, my_png_warning_fn,
		(void*)ctx, my_png_malloc_fn, my_png_free_fn);
	if(!pr->png_ptr) goto done;

	pr->info_ptr = png_create_info_struct(pr->png_ptr);
	if(!pr->info_ptr) goto done;

	pr->rctx.ctx = ctx;
	pr->rctx.iodescr = iodescr;
	pr->rctx.png_ptr = pr->png_ptr;
	pr->rctx.info_ptr = pr->info_ptr;
	pr->rctx.img = &pr->img;
	png_set_read_fn(pr->png_ptr, (void*)&pr->rctx, my_png_read_fn);

	png_read_info(pr->png_ptr, pr->info_ptr);

	png_get_IHDR(pr->png_ptr, pr->info_ptr, &width, &height, &pr->rctx.bit_depth, &pr->rctx.color_type,
		&interlace_type, NULL, NULL);

	if(!iw_check_image_dimensions(ctx,width,height)) {
		goto done;
	}

	// I'm not sure I know everything that png_read_update_info() does,
	// so to be safe, read some things before calling it.
	iw_read_ancillary_data1(&pr->rctx);

	if(!(pr->rctx.color_type&PNG_COLOR_MASK_COLOR)) {
		// Remember whether the image was originally encoded as grayscale.
		pr->img.native_grayscale = 1;
	}

	// TODO: Currently, we promote binary transparency to a full alpha channel.
	// (If necessary to do that, we also promote the image to 8bits/sample.)
	// It would be better to create and support 1-bpp alpha channels.

	has_trns=png_get_valid(pr->png_ptr,pr->info_ptr,PNG_INFO_tRNS);
	need_update_info=0;

	if(pr->rctx.color_type==PNG_COLOR_TYPE_PALETTE) {
		// Expand all palette images to full RGB or RGBA.
		png_set_palette_to_rgb(pr->png_ptr);
		need_update_info=1;
	}
	else if(has_trns && !(pr->rctx.color_type&PNG_COLOR_MASK_ALPHA)) {
		// Expand binary transparency to a full alpha channel.
		// For (grayscale) images with <8bpp, this will also
		// expand them to 8bpp.
		png_set_tRNS_to_alpha(pr->png_ptr);
		need_update_info=1;
	}

	if(need_update_info) {
		// Update things to reflect any transformations done above.
		png_read_update_info(pr->png_ptr, pr->info_ptr);
		pr->rctx.color_type = png_get_color_type(pr->png_ptr, pr->info_ptr);
		pr->rctx.bit_depth = png_get_bit_depth(pr->png_ptr, pr->info_ptr);
	}

	pr->img.bit_depth = pr->rctx.bit_depth;

	is_supported=0;

	switch(pr->rctx.color_type) {
	case PNG_COLOR_TYPE_GRAY:
		pr->img.imgtype = IW_IMGTYPE_GRAY;
		numchannels = 1;
		is_supported=1;
		break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		pr->img.imgtype = IW_IMGTYPE_GRAYA;
		numchannels = 2;
		is_supported=1;
		break;
	case PNG_COLOR_TYPE_RGB:
		pr->img.imgtype = IW_IMGTYPE_RGB;
		numchannels = 3;
		is_supported=1;
		break;
	case PNG_COLOR_TYPE_RGB_ALPHA:
		pr->img.imgtype = IW_IMGTYPE_RGBA;
		numchannels = 4;
		is_supported=1;
		break;
	}

	if(!is_supported) {
		iw_set_errorf(ctx,"This PNG image type (color type=%d, bit depth=%d) is not supported",
			(int)pr->rctx.color_type,(int)pr->rctx.bit_depth);
		goto done;
	}

	iw_read_ancillary_data(&pr->rctx);

	if(pr->rctx.sbit_flag) {
		// See comment in iwpng_read_sbit().
		png_set_shift(pr->png_ptr, &pr->rctx.sbit);
	}

	pr->img.width = width;
	pr->img.height = height;
	pr->img.bpr = iw_calc_bytesperrow(pr->img.width,pr->img.bit_depth*numchannels);

	pr->img.pixels = (iw_byte*)iw_malloc_large(ctx, pr->img.bpr,pr->img.height);
	if(!pr->img.pixels) {
		goto done;
	}
	pr->row_pointers = (iw_byte**)iw_malloc(ctx, pr->img.height * sizeof(iw_byte*));
	if(!pr->row_pointers) goto done;

	for(i=0;i<pr->img.height;i++) {
		pr->row_pointers[i] = &pr->img.pixels[pr->img.bpr*i];
	}

	png_read_image(pr->png_ptr, pr->row_pointers);

	png_read_end(pr->png_ptr, pr->info_ptr);

	iw_set_input_image(ctx, &pr->img);
	// The contents of img no longer belong to us.
	pr->img.pixels = NULL;

	retval = 1;

done:
	if(!retval) {
		iw_set_error(ctx,"Read failed");
	}
	// Don't free memory here; do it in iw_read_png_file().
	return retval;
}

// This function serves as a target for longjmp(). It shouldn't do much of
// anything else.
static int iw_read_png_file2(struct pr_rsrc_struct *pr)
{
	if(setjmp(pr->errinfo.jbuf)) {
		return 0;
	}

	return iw_read_png_file3(pr);
}

IW_IMPL(int) iw_read_png_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct pr_rsrc_struct *pr = NULL;
	int retval = 0;

	pr = iw_mallocz(ctx, sizeof(struct pr_rsrc_struct));
	if(!pr) goto done;
	pr->ctx = ctx;
	pr->iodescr = iodescr;

	retval = iw_read_png_file2(pr);
done:
	if(pr) {
		if(pr->png_ptr) {
			png_destroy_read_struct(&pr->png_ptr, &pr->info_ptr, (png_infopp)NULL);
		}
		if(pr->img.pixels) iw_free(ctx, pr->img.pixels);
		if(pr->row_pointers) iw_free(ctx, pr->row_pointers);
		iw_free(ctx, pr);
	}
	return retval;
}

///////////////////////////////////////////////////////////////////////

struct iwpngwcontext {
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	png_structp png_ptr;
	png_infop info_ptr;
	struct iw_image *img;

	int bkgd_pal_entry_valid;
	int bkgd_pal_entry; // Write the background color to this palette entry.
};

struct pw_rsrc_struct {
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	png_structp png_ptr;
	png_infop info_ptr;
	iw_byte **row_pointers;
	struct iwpngwcontext wctx;
	struct iw_image img;
	struct errstruct errinfo;
};

static void iwpng_set_phys(struct iwpngwcontext *wctx)
{
	png_uint_32 pngres_x, pngres_y;

	if(wctx->img->density_code==IW_DENSITY_UNITS_UNKNOWN) {
		pngres_x = (png_uint_32)(0.5+wctx->img->density_x);
		pngres_y = (png_uint_32)(0.5+wctx->img->density_y);
		png_set_pHYs(wctx->png_ptr, wctx->info_ptr, pngres_x, pngres_y, PNG_RESOLUTION_UNKNOWN);
	}
	else if(wctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
		pngres_x = (png_uint_32)(0.5+wctx->img->density_x);
		pngres_y = (png_uint_32)(0.5+wctx->img->density_y);
		png_set_pHYs(wctx->png_ptr, wctx->info_ptr, pngres_x, pngres_y, PNG_RESOLUTION_METER);
	}
}

static void iwpng_set_binary_trns(struct iwpngwcontext *wctx, int lpng_color_type)
{
	png_color_16 newtrns;

	iw_zeromem(&newtrns,sizeof(png_color_16));

	if(wctx->img->has_colorkey_trns) {
		if(lpng_color_type==PNG_COLOR_TYPE_GRAY) {
			// The R, G, and B components of the colorkey should all be the same,
			// so we only need to look at one of them.
			newtrns.gray = (png_uint_16)wctx->img->colorkey[IW_CHANNELTYPE_RED];
			png_set_tRNS(wctx->png_ptr, wctx->info_ptr, NULL, 1, &newtrns);
		}
		else if(lpng_color_type==PNG_COLOR_TYPE_RGB) {
			newtrns.red   = (png_uint_16)wctx->img->colorkey[IW_CHANNELTYPE_RED];
			newtrns.green = (png_uint_16)wctx->img->colorkey[IW_CHANNELTYPE_GREEN];
			newtrns.blue  = (png_uint_16)wctx->img->colorkey[IW_CHANNELTYPE_BLUE];
			png_set_tRNS(wctx->png_ptr, wctx->info_ptr, NULL, 1, &newtrns);
		}
	}
}

static void iwpng_set_bkgd_label(struct iwpngwcontext *wctx, int color_type, int bit_depth,
	const struct iw_palette *iwpal)
{
	png_color_16 clr;
	int i;
	int idx;
	int ok = 0;
	unsigned int bkgdlabel[3];
	int k;

	if(!wctx->img->has_bkgdlabel) return;

	iw_zeromem(&clr,sizeof(png_color_16));

	if(color_type & PNG_COLOR_MASK_PALETTE) {
		if(!iwpal) return;

		for(k=0;k<3;k++) {
			bkgdlabel[k] = iw_color_get_int_sample(&wctx->img->bkgdlabel,k,255);
		}

		// There should be at least one palette entry that is suitable for using
		// as the background color. Find it.
		idx = -1;
		for(i=0;i<iwpal->num_entries;i++) {
			if(iwpal->entry[i].a==0) {
				idx = i;
				// Any fully transparent color will do, but we'll likely have to
				// modify its underlying RGB values when we write the palette.
				// Make a note to store the background color in this palette entry.
				wctx->bkgd_pal_entry_valid = 1;
				wctx->bkgd_pal_entry = idx;
				break;
			}
			if((unsigned int)iwpal->entry[i].r == bkgdlabel[0] &&
				(unsigned int)iwpal->entry[i].g == bkgdlabel[1] &&
				(unsigned int)iwpal->entry[i].b == bkgdlabel[2])
			{
				idx = i;
				break;
			}
		}
		if(idx<0) {
			return; // This shouldn't happen. The optimizer did something wrong.
		}
		clr.index = (png_byte)idx;
		ok = 1;
	}
	else if(color_type & PNG_COLOR_MASK_COLOR) {
		if((bit_depth==wctx->img->bit_depth) && (bit_depth==8 || bit_depth==16)) {
			for(k=0;k<3;k++) {
				bkgdlabel[k] = iw_color_get_int_sample(&wctx->img->bkgdlabel,k,bit_depth==8?255:65535);
			}
			clr.red   = (png_uint_16)bkgdlabel[0];
			clr.green = (png_uint_16)bkgdlabel[1];
			clr.blue  = (png_uint_16)bkgdlabel[2];
			ok = 1;
		}
	}
	else { // Grayscale
		if(bit_depth==1) {
			clr.gray = (png_uint_16)iw_color_get_int_sample(&wctx->img->bkgdlabel,0,1);
			ok = 1;
		}
		else if(bit_depth==2) {
			clr.gray = (png_uint_16)iw_color_get_int_sample(&wctx->img->bkgdlabel,0,3);
			ok = 1;
		}
		else if(bit_depth==4) {
			clr.gray = (png_uint_16)iw_color_get_int_sample(&wctx->img->bkgdlabel,0,15);
			ok = 1;
		}
		else if((bit_depth==8 || bit_depth==16) && (bit_depth==wctx->img->bit_depth)) {
			clr.gray = (png_uint_16)iw_color_get_int_sample(&wctx->img->bkgdlabel,0,bit_depth==8?255:65535);
			ok = 1;
		}
	}

	if(ok)
		png_set_bKGD(wctx->png_ptr,wctx->info_ptr,&clr);
}

static void iwpng_set_palette(struct iwpngwcontext *wctx,
	const struct iw_palette *iwpal)
{
	int i;
	int num_trans;
	png_color pngpal[256];
	png_byte pngtrans[256];

	if(!iwpal) return;

	num_trans=0;
	for(i=0;i<iwpal->num_entries;i++) {
		pngpal[i].red   = iwpal->entry[i].r;
		pngpal[i].green = iwpal->entry[i].g;
		pngpal[i].blue  = iwpal->entry[i].b;
		pngtrans[i]     = iwpal->entry[i].a;
		if(iwpal->entry[i].a<255) num_trans = i+1;
	}

	if(wctx->bkgd_pal_entry_valid) {
		// Palette color #bkgd_pal_entry is presumably fully-transparent, and we
		// want to use its RGB values for the background color label.
		pngpal[wctx->bkgd_pal_entry].red   = (png_byte)iw_color_get_int_sample(&wctx->img->bkgdlabel,0,255);
		pngpal[wctx->bkgd_pal_entry].green = (png_byte)iw_color_get_int_sample(&wctx->img->bkgdlabel,1,255);
		pngpal[wctx->bkgd_pal_entry].blue  = (png_byte)iw_color_get_int_sample(&wctx->img->bkgdlabel,2,255);
	}

	png_set_PLTE(wctx->png_ptr, wctx->info_ptr, pngpal, iwpal->num_entries);

	if(num_trans>0) {
		png_set_tRNS(wctx->png_ptr, wctx->info_ptr, pngtrans, num_trans, 0);
	}
}

static int iw_intent_to_lpng_intent(int x)
{
	switch(x) {
	case IW_INTENT_RELATIVE:   return PNG_sRGB_INTENT_RELATIVE;
	case IW_INTENT_SATURATION: return PNG_sRGB_INTENT_SATURATION;
	case IW_INTENT_ABSOLUTE:   return PNG_sRGB_INTENT_ABSOLUTE;
	}
	return PNG_sRGB_INTENT_PERCEPTUAL;
}

static void my_png_write_fn(png_structp png_ptr, png_bytep data, png_size_t length)
{
	struct iwpngwcontext *wctx;

	wctx = (struct iwpngwcontext*)png_get_io_ptr(png_ptr);
	(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,(void*)data,(size_t)length);
}

static void my_png_flush_fn(png_structp png_ptr)
{
}

static int iw_write_png_file3(struct pw_rsrc_struct *pw)
{
	struct iw_context *ctx = pw->ctx;
	struct iw_iodescr *iodescr = pw->iodescr;
	int i;
	int lpng_color_type;
	int lpng_bit_depth;
	int lpng_interlace_type;
	int retval=0;
	struct iw_csdescr csdescr;
	const struct iw_palette *iwpal = NULL;
	int no_cslabel;
	int palette_is_gray;
	int cmprlevel;
	const char *optv;

	iw_get_output_image(ctx,&pw->img);
	iw_get_output_colorspace(ctx,&csdescr);

	pw->errinfo.ctx = ctx;
	pw->errinfo.write_flag = 1;

	pw->png_ptr = png_create_write_struct_2(PNG_LIBPNG_VER_STRING,
		(void*)(&pw->errinfo), my_png_error_fn, my_png_warning_fn,
		(void*)ctx, my_png_malloc_fn, my_png_free_fn);
	if(!pw->png_ptr) goto done;

	cmprlevel = 9;
	optv = iw_get_option(ctx, "deflate:cmprlevel");
	if(optv) {
		cmprlevel = iw_parse_int(optv);
	}
	if(cmprlevel >= 0) {
		png_set_compression_level(pw->png_ptr, cmprlevel);
	}

	png_set_compression_buffer_size(pw->png_ptr, 1048576);

	pw->info_ptr = png_create_info_struct(pw->png_ptr);
	if(!pw->info_ptr) goto done;

	pw->wctx.ctx = ctx;
	pw->wctx.iodescr = iodescr;
	pw->wctx.png_ptr = pw->png_ptr;
	pw->wctx.info_ptr = pw->info_ptr;
	pw->wctx.img = &pw->img;
	png_set_write_fn(pw->png_ptr, (void*)&pw->wctx, my_png_write_fn, my_png_flush_fn);

	lpng_color_type = -1;

	switch(pw->img.imgtype) {
	case IW_IMGTYPE_RGBA:  lpng_color_type=PNG_COLOR_TYPE_RGB_ALPHA;  break;
	case IW_IMGTYPE_RGB:   lpng_color_type=PNG_COLOR_TYPE_RGB;        break;
	case IW_IMGTYPE_GRAYA: lpng_color_type=PNG_COLOR_TYPE_GRAY_ALPHA; break;
	case IW_IMGTYPE_GRAY:  lpng_color_type=PNG_COLOR_TYPE_GRAY;       break;
	case IW_IMGTYPE_PALETTE: lpng_color_type=PNG_COLOR_TYPE_PALETTE;  break;
	}

	if(lpng_color_type == -1) {
		iw_set_error(ctx,"Internal: Don\xe2\x80\x99t know how to write this image");
		goto done;
	}

	lpng_bit_depth = pw->img.bit_depth;

	if(lpng_color_type==PNG_COLOR_TYPE_PALETTE) {
		iwpal = iw_get_output_palette(ctx);
		if(!iwpal) goto done;
		if(iwpal->num_entries <= 2) lpng_bit_depth=1;
		else if(iwpal->num_entries <= 4) lpng_bit_depth=2;
		else if(iwpal->num_entries <= 16) lpng_bit_depth=4;

		palette_is_gray = iw_get_value(ctx,IW_VAL_OUTPUT_PALETTE_GRAYSCALE);
		if(palette_is_gray) {
			lpng_color_type = PNG_COLOR_TYPE_GRAY;
		}
	}

	if(pw->img.reduced_maxcolors) {
		if(lpng_bit_depth<8 || lpng_color_type==PNG_COLOR_TYPE_PALETTE) {
			iw_set_error(ctx,"Internal: Can\xe2\x80\x99t support reduced bit depth");
			goto done;
		}
	}

	lpng_interlace_type = PNG_INTERLACE_NONE;
	if(iw_get_value(ctx,IW_VAL_OUTPUT_INTERLACED)) {
		lpng_interlace_type = PNG_INTERLACE_ADAM7;
	}

	png_set_IHDR(pw->png_ptr, pw->info_ptr, pw->img.width, pw->img.height,
		lpng_bit_depth, lpng_color_type, lpng_interlace_type,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	no_cslabel = iw_get_value(ctx,IW_VAL_NO_CSLABEL);

	if(no_cslabel) {
		;
	}
	else if(csdescr.cstype==IW_CSTYPE_GAMMA) {
		png_set_gAMA(pw->png_ptr, pw->info_ptr, 1.0/csdescr.gamma);
	}
	else if(csdescr.cstype==IW_CSTYPE_LINEAR) {
		png_set_gAMA(pw->png_ptr, pw->info_ptr, 1.0);
	}
	else if(csdescr.cstype==IW_CSTYPE_SRGB) {
		png_set_sRGB(pw->png_ptr, pw->info_ptr,
			iw_intent_to_lpng_intent(pw->img.rendering_intent));
	}

	iwpng_set_phys(&pw->wctx);

	iwpng_set_bkgd_label(&pw->wctx, lpng_color_type, lpng_bit_depth, iwpal);

	if(lpng_color_type==PNG_COLOR_TYPE_PALETTE) {
		iwpng_set_palette(&pw->wctx, iwpal);
	}

	iwpng_set_binary_trns(&pw->wctx, lpng_color_type);

	if(pw->img.reduced_maxcolors) {
		png_color_8 sbit;
		sbit.red   = iw_max_color_to_bitdepth(pw->img.maxcolorcode[IW_CHANNELTYPE_RED]);
		sbit.green = iw_max_color_to_bitdepth(pw->img.maxcolorcode[IW_CHANNELTYPE_GREEN]);
		sbit.blue  = iw_max_color_to_bitdepth(pw->img.maxcolorcode[IW_CHANNELTYPE_BLUE]);
		sbit.gray  = iw_max_color_to_bitdepth(pw->img.maxcolorcode[IW_CHANNELTYPE_GRAY]);
		sbit.alpha = iw_max_color_to_bitdepth(pw->img.maxcolorcode[IW_CHANNELTYPE_ALPHA]);
		png_set_sBIT(pw->png_ptr,pw->info_ptr,&sbit);
		png_set_shift(pw->png_ptr,&sbit);
	}

	png_write_info(pw->png_ptr, pw->info_ptr);

	pw->row_pointers = (iw_byte**)iw_malloc(ctx, pw->img.height * sizeof(iw_byte*));
	if(!pw->row_pointers) goto done;

	for(i=0;i<pw->img.height;i++) {
		pw->row_pointers[i] = &pw->img.pixels[pw->img.bpr*i];
	}

	if(lpng_bit_depth<8) {
		png_set_packing(pw->png_ptr);
	}

	png_write_image(pw->png_ptr, pw->row_pointers);

	png_write_end(pw->png_ptr, pw->info_ptr);

	retval = 1;

done:
	if(!retval) {
		iw_set_error(ctx,"Write failed");
	}
	// Don't free memory here; do it in iw_write_png_file().
	return retval;
}

// This function serves as a target for longjmp(). It shouldn't do much of
// anything else.
static int iw_write_png_file2(struct pw_rsrc_struct *pw)
{
	if(setjmp(pw->errinfo.jbuf)) {
		return 0;
	}

	return iw_write_png_file3(pw);
}

IW_IMPL(int) iw_write_png_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct pw_rsrc_struct *pw = NULL;
	int retval = 0;

	pw = iw_mallocz(ctx, sizeof(struct pw_rsrc_struct));
	if(!pw) goto done;
	pw->ctx = ctx;
	pw->iodescr = iodescr;

	retval = iw_write_png_file2(pw);

done:
	if(pw) {
		if(pw->png_ptr) {
			png_destroy_write_struct(&pw->png_ptr, &pw->info_ptr);
		}
		if(pw->row_pointers) iw_free(ctx, pw->row_pointers);
		iw_free(ctx, pw);
	}
	return retval;
}

IW_IMPL(char*) iw_get_libpng_version_string(char *s, int s_len)
{
	const char *pv;
	pv = png_get_libpng_ver(NULL);
	iw_snprintf(s,s_len,"%s",pv);
	return s;
}

#endif // IW_SUPPORT_PNG
