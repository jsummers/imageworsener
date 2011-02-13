// imagew-png.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#define _CRT_SECURE_NO_WARNINGS
#ifdef IW_WINDOWS
#include <tchar.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include <png.h>
#include <zlib.h>

#include "imagew.h"

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

#ifdef _UNICODE
	iw_seterror(ctx,_T("libpng reports %s error: %S"),errinfop->write_flag?_T("write"):_T("read"),err_msg);
#else
	iw_seterror(ctx,"libpng reports %s error: %s",errinfop->write_flag?_T("write"):_T("read"),err_msg);
#endif

	longjmp(*j, -1);
}

static void my_png_warning_fn(png_structp png_ptr, const char *warn_msg)
{
	return;
}

// In PNG files, gamma values are stored as fixed-precision decimals, with
// 5 decimal places of precision.
// Libpng (as of this writing) gives us the nearest double to the nearest
// float to the value in the file.
// This function fixes that up so that up to get the nearest double
// to the value in the files, as if the precision hadn't been reduced by
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

	memset(&csdescr,0,sizeof(struct iw_csdescr));

	// default:
	csdescr.cstype = IW_CSTYPE_SRGB;
	csdescr.sRGB_intent = IW_sRGB_INTENT_PERCEPTUAL;

	if(png_get_sRGB(png_ptr, info_ptr, &tmp)) {
		csdescr.cstype = IW_CSTYPE_SRGB;
		csdescr.sRGB_intent = tmp;
	}
	else if(png_get_gAMA(png_ptr, info_ptr, &file_gamma)) {
		file_gamma = fixup_png_gamma(file_gamma);
		csdescr.cstype = IW_CSTYPE_GAMMA;
		csdescr.gamma = 1.0/file_gamma;
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

static void iw_read_ancillary_data(struct iw_context *ctx,
   struct iw_image *img, png_structp png_ptr, png_infop info_ptr)
{
	iwpng_read_colorspace(ctx,png_ptr,info_ptr);
	iwpng_read_density(ctx,img,png_ptr,info_ptr);
}

int iw_read_png_file(struct iw_context *ctx, const TCHAR *fn)
{
	png_uint_32 width, height;
	int bit_depth, color_type, interlace_type;
	unsigned char **row_pointers = NULL;
	int i;
	jmp_buf jbuf;
	struct errstruct errinfo;
	int is_supported;
	int has_trns;
	int need_update_info;
	int numchannels=0;
	struct iw_image img;
	FILE *infp = NULL;
	int retval=0;
	png_structp png_ptr = NULL;
	png_infop  info_ptr = NULL;

	memset(&img,0,sizeof(struct iw_image));
	errinfo.jbufp = &jbuf;
	errinfo.ctx = ctx;
	errinfo.write_flag=0;

	infp = _tfopen(fn,_T("rb"));
	if(!infp) {
		iw_seterror(ctx,_T("Failed to open for reading (error code=%d)"),(int)errno);
		goto done;
	}

	if(setjmp(jbuf)) {
		goto done;
	}

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,(void*)(&errinfo),
		my_png_error_fn, my_png_warning_fn);

	if(!png_ptr) goto done;
	info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) goto done;

	png_init_io(png_ptr, infp);
	png_read_info(png_ptr, info_ptr);

	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
		&interlace_type, NULL, NULL);

	if(!iw_check_image_dimensons(ctx,width,height)) {
		goto done;
	}

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
		iw_seterror(ctx,_T("This PNG image type (color type=%d, bit depth=%d) is not supported"),(int)color_type,(int)bit_depth);
		goto done;
	}

	iw_read_ancillary_data(ctx, &img, png_ptr, info_ptr);

	img.width = width;
	img.height = height;
	img.bpr = iw_calc_bytesperrow(img.width,img.bit_depth*numchannels);

	img.pixels = (unsigned char*)iw_malloc_large(ctx, img.bpr,img.height);
	if(!img.pixels) {
		goto done;
	}
	row_pointers = (unsigned char**)iw_malloc(ctx, img.height * sizeof(unsigned char*));
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
		iw_seterror(ctx,_T("Read failed (png)"));
	}
	if(png_ptr) {
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
	}
	if(infp) fclose(infp);
	if(row_pointers) iw_free(row_pointers);
	return retval;
}

///////////////////////////////////////////////////////////////////////
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

static void iwpng_set_palette(struct iw_context *ctx,
	png_structp png_ptr, png_infop info_ptr,
	const struct iw_palette *iwpal)
{
	int i;
	int num_trans;
	png_color pngpal[256];
	unsigned char pngtrans[256];

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

int iw_write_png_file(struct iw_context *ctx, const TCHAR *fn)
{
	unsigned char **row_pointers = NULL;
	int i;
	jmp_buf jbuf;
	struct errstruct errinfo;
	int png_color_type;
	int png_bit_depth;
	FILE *outfp = NULL;
	int retval=0;
	png_structp png_ptr = NULL;
	png_infop  info_ptr = NULL;
	struct iw_image img;
	struct iw_csdescr csdescr;
	const struct iw_palette *iwpal = NULL;
	int no_cslabel;
	int palette_is_gray;

	iw_get_output_image(ctx,&img);
	iw_get_output_colorspace(ctx,&csdescr);

	errinfo.jbufp = &jbuf;
	errinfo.ctx = ctx;
	errinfo.write_flag = 1;

	outfp = _tfopen(fn,_T("wb"));
	if(!outfp) {
		iw_seterror(ctx,_T("Failed to open for writing (error code=%d)"),(int)errno);
		goto done;
	}

	if(setjmp(jbuf)) {
		goto done;
	}

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,(void*)(&errinfo),
		my_png_error_fn, my_png_warning_fn);
	if(!png_ptr) goto done;

	png_set_compression_buffer_size(png_ptr, 1048576);

	info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) goto done;

	png_init_io(png_ptr, outfp);

	png_color_type = -1;

	switch(img.imgtype) {
	case IW_IMGTYPE_RGBA:  png_color_type=PNG_COLOR_TYPE_RGB_ALPHA;  break;
	case IW_IMGTYPE_RGB:   png_color_type=PNG_COLOR_TYPE_RGB;        break;
	case IW_IMGTYPE_GRAYA: png_color_type=PNG_COLOR_TYPE_GRAY_ALPHA; break;
	case IW_IMGTYPE_GRAY:  png_color_type=PNG_COLOR_TYPE_GRAY;       break;
	case IW_IMGTYPE_PALETTE: png_color_type=PNG_COLOR_TYPE_PALETTE;  break;
	case IW_IMGTYPE_GRAY1: png_color_type=PNG_COLOR_TYPE_GRAY;       break;
	}

	if(png_color_type == -1) {
		iw_seterror(ctx,_T("Internal: Don't know how to write this image"));
		goto done;
	}

	png_bit_depth = img.bit_depth;

	if(png_color_type==PNG_COLOR_TYPE_PALETTE) {
		iwpal = iw_get_output_palette(ctx);
		if(!iwpal) goto done;
		if(iwpal->num_entries <= 2) png_bit_depth=1;
		else if(iwpal->num_entries <= 4) png_bit_depth=2;
		else if(iwpal->num_entries <= 16) png_bit_depth=4;

		palette_is_gray = iw_get_value(ctx,IW_VAL_OUTPUT_PALETTE_GRAYSCALE);
		if(palette_is_gray) {
			png_color_type = PNG_COLOR_TYPE_GRAY;
		}
	}

	png_set_IHDR(png_ptr, info_ptr, img.width, img.height,
		png_bit_depth, png_color_type, PNG_INTERLACE_NONE,
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
		png_set_sRGB(png_ptr, info_ptr, csdescr.sRGB_intent);
	}

	iwpng_set_phys(ctx, png_ptr, info_ptr, &img);

	if(png_color_type==PNG_COLOR_TYPE_PALETTE) {
		iwpng_set_palette(ctx, png_ptr, info_ptr, iwpal);
	}

	png_write_info(png_ptr, info_ptr);

	row_pointers = (unsigned char**)iw_malloc(ctx, img.height * sizeof(unsigned char*));
	if(!row_pointers) goto done;

	for(i=0;i<img.height;i++) {
		row_pointers[i] = &img.pixels[img.bpr*i];
	}

	if(png_bit_depth<8) {
		png_set_packing(png_ptr);
	}

	png_write_image(png_ptr, row_pointers);

	png_write_end(png_ptr, info_ptr);

	retval = 1;

done:
	if(!retval) {
		iw_seterror(ctx,_T("Write failed"));
	}
	if(png_ptr) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
	}
	if(outfp) fclose(outfp);
	if(row_pointers) iw_free(row_pointers);
	return retval;
}

TCHAR *iw_get_libpng_version_string(TCHAR *s, int s_len, int cset)
{
	const char *pv;
	pv = png_get_libpng_ver(NULL);
#ifdef _UNICODE
	iw_snprintf(s,s_len,_T("%S"),pv);
#else
	iw_snprintf(s,s_len,_T("%s"),pv);
#endif
	return s;
}

TCHAR *iw_get_zlib_version_string(TCHAR *s, int s_len, int cset)
{
	const char *zv;
	zv = zlibVersion();
#ifdef _UNICODE
	iw_snprintf(s,s_len,_T("%S"),zv);
#else
	iw_snprintf(s,s_len,_T("%s"),zv);
#endif
	return s;
}
