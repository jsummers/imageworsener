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

struct iw_pngrctx {
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
};

struct errstruct {
	jmp_buf *jbufp;
	struct iw_context *ctx;
	int write_flag; // So we can tell if we're reading, or writing.
};

static void my_png_error_fn(png_structp png_ptr, const char *err_msg)
{
	struct errstruct *errinfop;
	jmp_buf *j;
	struct iw_context *ctx;

	errinfop = (struct errstruct *)png_get_error_ptr(png_ptr);
	j = errinfop->jbufp;
	ctx = errinfop->ctx;

	if(errinfop->write_flag) {
		iw_set_errorf(ctx,"libpng reports write error: %s",err_msg);
	}
	else {
		iw_set_errorf(ctx,"libpng reports read error: %s",err_msg);
	}

	longjmp(*j, -1);
}

static void my_png_warning_fn(png_structp png_ptr, const char *warn_msg)
{
	return;
}

static void my_png_read_fn(png_structp png_ptr,
      png_bytep buf, png_size_t length)
{
	struct iw_pngrctx *pngrctx;
	struct iw_context *ctx;
	int ret;
	size_t bytesread = 0;

	pngrctx = (struct iw_pngrctx*)png_get_io_ptr(png_ptr);
	ctx = pngrctx->ctx;

	ret = (*pngrctx->iodescr->read_fn)(ctx,pngrctx->iodescr,buf,(size_t)length,&bytesread);
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

static void iwpng_read_colorspace(struct iw_context *ctx,
	png_structp png_ptr, png_infop info_ptr)
{
	int tmp;
	double file_gamma;
	struct iw_csdescr csdescr;

	if(png_get_sRGB(png_ptr, info_ptr, &tmp)) {
		iw_make_srgb_csdescr(&csdescr,tmp);
	}
	else if(png_get_gAMA(png_ptr, info_ptr, &file_gamma)) {
		file_gamma = fixup_png_gamma(file_gamma);
		iw_make_gamma_csdescr(&csdescr,1.0/file_gamma);
	}
	else {
		// default:
		iw_make_srgb_csdescr(&csdescr,IW_SRGB_INTENT_PERCEPTUAL);
	}

	iw_set_input_colorspace(ctx,&csdescr);
}

static int iw_is_valid_density(double density_x, double density_y, int density_code)
{
	if(density_x<0.0001 || density_y<0.0001) return 0;
	if(density_x>10000000.0 || density_y>10000000.0) return 0;
	if(density_x/10.0>density_y) return 0;
	if(density_y/10.0>density_x) return 0;
	if(density_code!=IW_DENSITY_UNITS_UNKNOWN && density_code!=IW_DENSITY_UNITS_PER_METER)
		return 0;
	return 1;
}

static void iwpng_read_density(struct iw_context *ctx,
   struct iw_image *img, png_structp png_ptr, png_infop info_ptr)
{
	png_uint_32 pngdensity_x,pngdensity_y;
	int pngdensity_units;
	int density_code;

	if(!png_get_pHYs(png_ptr,info_ptr,&pngdensity_x,&pngdensity_y,&pngdensity_units)) {
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

	img->density_x = (double)pngdensity_x;
	img->density_y = (double)pngdensity_y;
	if(!iw_is_valid_density(img->density_x,img->density_y,density_code)) return;
	img->density_code = density_code;
}

static void iwpng_read_sbit(struct iw_context *ctx,
	png_structp png_ptr, png_infop info_ptr, int color_type)
{
	png_uint_32 ret;
	png_color_8p sbit;

	ret = png_get_sBIT(png_ptr, info_ptr, &sbit);
	if(!ret) return;

	if(color_type & PNG_COLOR_MASK_COLOR) {
		iw_set_input_sbit(ctx,IW_CHANNELTYPE_RED  ,sbit->red);
		iw_set_input_sbit(ctx,IW_CHANNELTYPE_GREEN,sbit->green);
		iw_set_input_sbit(ctx,IW_CHANNELTYPE_BLUE ,sbit->blue);
	}
	else {
		iw_set_input_sbit(ctx,IW_CHANNELTYPE_GRAY ,sbit->gray);
	}
	
	if(color_type & PNG_COLOR_MASK_ALPHA) {
		iw_set_input_sbit(ctx,IW_CHANNELTYPE_ALPHA,sbit->alpha);
		// Apparently, it's not possible for a PNG file to indicate
		// the significant bits of the alpha values of a palette image.
	}
}

static void iwpng_read_bkgd(struct iw_context *ctx,
   png_structp png_ptr, png_infop info_ptr, int color_type,
   int bit_depth)
{
	png_color_16p bg_colorp;
	double maxcolor;

	if(!png_get_bKGD(png_ptr, info_ptr, &bg_colorp)) return;

	maxcolor = (double)((1<<bit_depth)-1);
	switch(color_type) {
	case PNG_COLOR_TYPE_GRAY:
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		iw_set_input_bkgd_label(ctx,
			((double)bg_colorp->gray)/maxcolor,
			((double)bg_colorp->gray)/maxcolor,
			((double)bg_colorp->gray)/maxcolor);
		break;
	case PNG_COLOR_TYPE_RGB:
	case PNG_COLOR_TYPE_RGB_ALPHA:
		iw_set_input_bkgd_label(ctx,
			((double)bg_colorp->red)/maxcolor,
			((double)bg_colorp->green)/maxcolor,
			((double)bg_colorp->blue)/maxcolor);
		break;
	}
}

static void iw_read_ancillary_data1(struct iw_context *ctx,
   struct iw_image *img, png_structp png_ptr, png_infop info_ptr,
   int color_type)
{
	iwpng_read_sbit(ctx,png_ptr,info_ptr,color_type);
}

static void iw_read_ancillary_data(struct iw_context *ctx,
   struct iw_image *img, png_structp png_ptr, png_infop info_ptr,
   int color_type, int bit_depth)
{
	iwpng_read_colorspace(ctx,png_ptr,info_ptr);
	iwpng_read_density(ctx,img,png_ptr,info_ptr);
	iwpng_read_bkgd(ctx,png_ptr,info_ptr,color_type,bit_depth);
}

IW_IMPL(int) iw_read_png_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	png_uint_32 width, height;
	int bit_depth, color_type, interlace_type;
	iw_byte **row_pointers = NULL;
	int i;
	jmp_buf jbuf;
	struct errstruct errinfo;
	int is_supported;
	int has_trns;
	int need_update_info;
	int numchannels=0;
	struct iw_image img;
	int retval=0;
	png_structp png_ptr = NULL;
	png_infop  info_ptr = NULL;
	struct iw_pngrctx pngrctx;

	memset(&pngrctx,0,sizeof(struct iw_pngrctx));
	memset(&img,0,sizeof(struct iw_image));

	errinfo.jbufp = &jbuf;
	errinfo.ctx = ctx;
	errinfo.write_flag=0;

	if(setjmp(jbuf)) {
		goto done;
	}

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,(void*)(&errinfo),
		my_png_error_fn, my_png_warning_fn);

	if(!png_ptr) goto done;
	info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) goto done;

	pngrctx.ctx = ctx;
	pngrctx.iodescr = iodescr;
	png_set_read_fn(png_ptr, (void*)&pngrctx, my_png_read_fn);

	png_read_info(png_ptr, info_ptr);

	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
		&interlace_type, NULL, NULL);

	if(!iw_check_image_dimensions(ctx,width,height)) {
		goto done;
	}

	// I'm not sure I know everything that png_read_update_info() does,
	// so to be safe, read some things before calling it.
	iw_read_ancillary_data1(ctx, &img, png_ptr, info_ptr, color_type);

	if(!(color_type&PNG_COLOR_MASK_COLOR)) {
		// Remember whether the image was originally encoded as grayscale.
		img.native_grayscale = 1;
	}

	// TODO: Currently, we promote binary transparency to a full alpha channel.
	// (If necessary to do that, we also promote the image to 8bits/sample.)
	// It would be better to create and support 1-bpp alpha channels.

	has_trns=png_get_valid(png_ptr,info_ptr,PNG_INFO_tRNS);
	need_update_info=0;

	if(color_type==PNG_COLOR_TYPE_PALETTE) {
		// Expand all palette images to full RGB or RGBA.
		png_set_palette_to_rgb(png_ptr);
		need_update_info=1;
	}
	else if(has_trns && !(color_type&PNG_COLOR_MASK_ALPHA)) {
		// Expand binary transparency to a full alpha channel.
		// For (grayscale) images with <8bpp, this will also
		// expand them to 8bpp.
		png_set_tRNS_to_alpha(png_ptr);
		need_update_info=1;
	}

	if(need_update_info) {
		// Update things to reflect any transformations done above.
		png_read_update_info(png_ptr, info_ptr);
		color_type = png_get_color_type(png_ptr, info_ptr);
		bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	}

	img.bit_depth = bit_depth;

	is_supported=0;

	switch(color_type) {
	case PNG_COLOR_TYPE_GRAY:
		img.imgtype = IW_IMGTYPE_GRAY;
		numchannels = 1;
		is_supported=1;
		break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		img.imgtype = IW_IMGTYPE_GRAYA;
		numchannels = 2;
		is_supported=1;
		break;
	case PNG_COLOR_TYPE_RGB:
		img.imgtype = IW_IMGTYPE_RGB;
		numchannels = 3;
		is_supported=1;
		break;
	case PNG_COLOR_TYPE_RGB_ALPHA:
		img.imgtype = IW_IMGTYPE_RGBA;
		numchannels = 4;
		is_supported=1;
		break;
	}

	if(!is_supported) {
		iw_set_errorf(ctx,"This PNG image type (color type=%d, bit depth=%d) is not supported",(int)color_type,(int)bit_depth);
		goto done;
	}

	iw_read_ancillary_data(ctx, &img, png_ptr, info_ptr, color_type, bit_depth);

	img.width = width;
	img.height = height;
	img.bpr = iw_calc_bytesperrow(img.width,img.bit_depth*numchannels);

	img.pixels = (iw_byte*)iw_malloc_large(ctx, img.bpr,img.height);
	if(!img.pixels) {
		goto done;
	}
	row_pointers = (iw_byte**)iw_malloc(ctx, img.height * sizeof(iw_byte*));
	if(!row_pointers) goto done;

	for(i=0;i<img.height;i++) {
		row_pointers[i] = &img.pixels[img.bpr*i];
	}

	png_read_image(png_ptr, row_pointers);

	png_read_end(png_ptr, info_ptr);

	iw_set_input_image(ctx, &img);

	retval = 1;

done:
	if(!retval) {
		iw_set_error(ctx,"Read failed");
	}
	if(png_ptr) {
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
	}
	if(iodescr->close_fn)
		(*iodescr->close_fn)(ctx,iodescr);
	if(row_pointers) iw_free(row_pointers);
	return retval;
}

///////////////////////////////////////////////////////////////////////

struct iw_pngwctx {
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
};

static void iwpng_set_phys(struct iw_context *ctx,
	png_structp png_ptr, png_infop info_ptr, const struct iw_image *img)
{
	png_uint_32 pngres_x, pngres_y;

	if(img->density_code==IW_DENSITY_UNITS_UNKNOWN) {
		pngres_x = (png_uint_32)(0.5+img->density_x);
		pngres_y = (png_uint_32)(0.5+img->density_y);
		png_set_pHYs(png_ptr, info_ptr, pngres_x, pngres_y, PNG_RESOLUTION_UNKNOWN);
	}
	else if(img->density_code==IW_DENSITY_UNITS_PER_METER) {
		pngres_x = (png_uint_32)(0.5+img->density_x);
		pngres_y = (png_uint_32)(0.5+img->density_y);
		png_set_pHYs(png_ptr, info_ptr, pngres_x, pngres_y, PNG_RESOLUTION_METER);
	}
}

static void iwpng_set_binary_trns(struct iw_context *ctx,
	png_structp png_ptr, png_infop info_ptr, const struct iw_image *img, int lpng_color_type)
{
	png_color_16 newtrns;

	memset(&newtrns,0,sizeof(png_color_16));

	if(img->has_colorkey_trns) {
		if(lpng_color_type==PNG_COLOR_TYPE_GRAY) {
			newtrns.gray = (png_uint_16)img->colorkey_r;
			png_set_tRNS(png_ptr, info_ptr, NULL, 1, &newtrns);
		}
		else if(lpng_color_type==PNG_COLOR_TYPE_RGB) {
			newtrns.red   = (png_uint_16)img->colorkey_r;
			newtrns.green = (png_uint_16)img->colorkey_g;
			newtrns.blue  = (png_uint_16)img->colorkey_b;
			png_set_tRNS(png_ptr, info_ptr, NULL, 1, &newtrns);
		}
	}
}

static void iwpng_set_palette(struct iw_context *ctx,
	png_structp png_ptr, png_infop info_ptr,
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

	png_set_PLTE(png_ptr, info_ptr, pngpal, iwpal->num_entries);

	if(num_trans>0) {
		png_set_tRNS(png_ptr, info_ptr, pngtrans, num_trans, 0);
	}
}


static void my_png_write_fn(png_structp png_ptr, png_bytep data, png_size_t length)
{
	struct iw_pngwctx *pngwctx;

	pngwctx = (struct iw_pngwctx*)png_get_io_ptr(png_ptr);
	(*pngwctx->iodescr->write_fn)(pngwctx->ctx,pngwctx->iodescr,(void*)data,(size_t)length);
}

static void my_png_flush_fn(png_structp png_ptr)
{
}

IW_IMPL(int) iw_write_png_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	iw_byte **row_pointers = NULL;
	int i;
	jmp_buf jbuf;
	struct errstruct errinfo;
	int lpng_color_type;
	int lpng_bit_depth;
	int lpng_interlace_type;
	int retval=0;
	png_structp png_ptr = NULL;
	png_infop  info_ptr = NULL;
	struct iw_image img;
	struct iw_csdescr csdescr;
	const struct iw_palette *iwpal = NULL;
	int no_cslabel;
	int palette_is_gray;
	int cmprlevel;
	struct iw_pngwctx pngwctx;

	memset(&pngwctx,0,sizeof(struct iw_pngwctx));

	iw_get_output_image(ctx,&img);
	iw_get_output_colorspace(ctx,&csdescr);

	errinfo.jbufp = &jbuf;
	errinfo.ctx = ctx;
	errinfo.write_flag = 1;

	if(setjmp(jbuf)) {
		goto done;
	}

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,(void*)(&errinfo),
		my_png_error_fn, my_png_warning_fn);
	if(!png_ptr) goto done;

	cmprlevel = iw_get_value(ctx,IW_VAL_PNG_CMPR_LEVEL);
	if(cmprlevel >= 0) {
		png_set_compression_level(png_ptr, cmprlevel);
	}
	png_set_compression_buffer_size(png_ptr, 1048576);

	info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) goto done;

	pngwctx.ctx = ctx;
	pngwctx.iodescr = iodescr;
	png_set_write_fn(png_ptr, (void*)&pngwctx, my_png_write_fn, my_png_flush_fn);

	lpng_color_type = -1;

	switch(img.imgtype) {
	case IW_IMGTYPE_RGBA:  lpng_color_type=PNG_COLOR_TYPE_RGB_ALPHA;  break;
	case IW_IMGTYPE_RGB:   lpng_color_type=PNG_COLOR_TYPE_RGB;        break;
	case IW_IMGTYPE_GRAYA: lpng_color_type=PNG_COLOR_TYPE_GRAY_ALPHA; break;
	case IW_IMGTYPE_GRAY:  lpng_color_type=PNG_COLOR_TYPE_GRAY;       break;
	case IW_IMGTYPE_PALETTE: lpng_color_type=PNG_COLOR_TYPE_PALETTE;  break;
	case IW_IMGTYPE_GRAY1: lpng_color_type=PNG_COLOR_TYPE_GRAY;       break;
	}

	if(lpng_color_type == -1) {
		iw_set_error(ctx,"Internal: Don\xe2\x80\x99t know how to write this image");
		goto done;
	}

	lpng_bit_depth = img.bit_depth;

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

	lpng_interlace_type = PNG_INTERLACE_NONE;
	if(iw_get_value(ctx,IW_VAL_OUTPUT_INTERLACED)) {
		lpng_interlace_type = PNG_INTERLACE_ADAM7;
	}

	png_set_IHDR(png_ptr, info_ptr, img.width, img.height,
		lpng_bit_depth, lpng_color_type, lpng_interlace_type,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	no_cslabel = iw_get_value(ctx,IW_VAL_NO_CSLABEL);

	if(no_cslabel) {
		;
	}
	else if(csdescr.cstype==IW_CSTYPE_GAMMA) {
		png_set_gAMA(png_ptr, info_ptr, 1.0/csdescr.gamma);
	}
	else if(csdescr.cstype==IW_CSTYPE_LINEAR) {
		png_set_gAMA(png_ptr, info_ptr, 1.0);
	}
	else { // Assume IW_CSTYPE_SRGB
		png_set_sRGB(png_ptr, info_ptr, csdescr.srgb_intent);
	}

	iwpng_set_phys(ctx, png_ptr, info_ptr, &img);

	if(lpng_color_type==PNG_COLOR_TYPE_PALETTE) {
		iwpng_set_palette(ctx, png_ptr, info_ptr, iwpal);
	}

	iwpng_set_binary_trns(ctx, png_ptr, info_ptr, &img, lpng_color_type);

	png_write_info(png_ptr, info_ptr);

	row_pointers = (iw_byte**)iw_malloc(ctx, img.height * sizeof(iw_byte*));
	if(!row_pointers) goto done;

	for(i=0;i<img.height;i++) {
		row_pointers[i] = &img.pixels[img.bpr*i];
	}

	if(lpng_bit_depth<8) {
		png_set_packing(png_ptr);
	}

	png_write_image(png_ptr, row_pointers);

	png_write_end(png_ptr, info_ptr);

	retval = 1;

done:
	if(!retval) {
		iw_set_error(ctx,"Write failed");
	}
	if(png_ptr) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
	}
	if(iodescr->close_fn)
		(*iodescr->close_fn)(ctx,iodescr);
	if(row_pointers) iw_free(row_pointers);
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
