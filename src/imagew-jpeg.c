// imagew-jpeg.c
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
#include <errno.h>
#include <setjmp.h>

#include <jpeglib.h>
#include <jerror.h>

#include "imagew.h"

#if BITS_IN_JSAMPLE != 8
#error "Wrong JSAMPLE size"
#endif

struct my_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr * my_error_ptr;


static void my_error_exit(j_common_ptr cinfo)
{
	my_error_ptr myerr = (my_error_ptr)cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}

// If we don't make our own output_message function, libjpeg will
// print warnings to stderr.
static void my_output_message(j_common_ptr cinfo)
{
	return;
}

static void iwjpeg_read_density(struct iw_context *ctx, struct iw_image *img,
				struct jpeg_decompress_struct *cinfo)
{
	switch(cinfo->density_unit) {
	case 1: // pixels/inch
		img->density_x = ((double)cinfo->X_density)/0.0254;
		img->density_y = ((double)cinfo->Y_density)/0.0254;
		img->density_code = IW_DENSITY_UNITS_PER_METER;
		break;
	case 2: // pixels/cm
		img->density_x = ((double)cinfo->X_density)*100.0;
		img->density_y = ((double)cinfo->Y_density)*100.0;
		img->density_code = IW_DENSITY_UNITS_PER_METER;
		break;
	default: // unknown units
		img->density_x = (double)cinfo->X_density;
		img->density_y = (double)cinfo->Y_density;
		img->density_code = IW_DENSITY_UNITS_UNKNOWN;
	}
}

int iw_read_jpeg_file(struct iw_context *ctx, const TCHAR *fn)
{
	int retval=0;
	FILE *f = NULL;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	int cinfo_valid=0;
	int colorspace;
	int j;
	JSAMPLE *jsamprow;
	int numchannels=0;
	struct iw_image img;

	memset(&img,0,sizeof(struct iw_image));
	memset(&cinfo,0,sizeof(struct jpeg_decompress_struct));
	memset(&jerr,0,sizeof(struct my_error_mgr));

	f=_tfopen(fn,_T("rb"));
	if(!f) {
		iw_seterror(ctx,_T("Can't open jpeg file"));
		goto done;
	}

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	jerr.pub.output_message = my_output_message;

	if (setjmp(jerr.setjmp_buffer)) {
		char buffer[JMSG_LENGTH_MAX];

		(*cinfo.err->format_message) ((j_common_ptr)&cinfo, buffer);

#ifdef _UNICODE
		iw_seterror(ctx,_T("libjpeg reports error: %S"),buffer);
#else
		iw_seterror(ctx,"libjpeg reports error: %s",buffer);
#endif

		goto done;
	}

	jpeg_create_decompress(&cinfo);
	cinfo_valid=1;

	jpeg_stdio_src(&cinfo, f);
	jpeg_read_header(&cinfo, TRUE);

	iwjpeg_read_density(ctx,&img,&cinfo);

	jpeg_start_decompress(&cinfo);

	colorspace=cinfo.jpeg_color_space;
	numchannels=cinfo.output_components;

	if(colorspace==JCS_GRAYSCALE && numchannels==1) {
		img.imgtype = IW_IMGTYPE_GRAY;
		img.native_grayscale = 1;
	}
	else if((colorspace==JCS_YCbCr || JCS_RGB) && numchannels==3) {
		img.imgtype = IW_IMGTYPE_RGB;
	}
	else {
		iw_seterror(ctx,_T("Unsupported type of JPEG"));
		goto done;
	}

	img.width = cinfo.output_width;
	img.height = cinfo.output_height;
	if(!iw_check_image_dimensons(ctx,img.width,img.height)) {
		goto done;
	}

	img.bit_depth = 8;
	img.bpr = iw_calc_bytesperrow(img.width,img.bit_depth*numchannels);

	img.pixels = (unsigned char*)iw_malloc_large(ctx, img.bpr, img.height);
	if(!img.pixels) {
		goto done;
	}

	while(cinfo.output_scanline < cinfo.output_height) {
		j=cinfo.output_scanline;
		jsamprow = &img.pixels[j*img.bpr];
		jpeg_read_scanlines(&cinfo, &jsamprow, 1);
	}
	jpeg_finish_decompress(&cinfo);

	iw_set_input_image(ctx, &img);
	retval=1;

done:
	if(cinfo_valid) jpeg_destroy_decompress(&cinfo);
	if(f) fclose(f);
	return retval;
}

int iw_write_jpeg_file(struct iw_context *ctx, const TCHAR *fn)
{
	int retval=0;
	struct jpeg_compress_struct cinfo;
	struct my_error_mgr jerr;
	J_COLOR_SPACE jpeg_colortype;
	int jpeg_cmpts;
	int compress_created = 0;
	int compress_started = 0;
	JSAMPROW *row_pointers = NULL;
	FILE *f = NULL;
	int is_grayscale;
	int j;
	struct iw_image img;
	int jpeg_quality;

	memset(&cinfo,0,sizeof(struct jpeg_compress_struct));
	memset(&jerr,0,sizeof(struct my_error_mgr));

	iw_get_output_image(ctx,&img);

	if(IW_IMGTYPE_HAS_ALPHA(img.imgtype)) {
		iw_seterror(ctx,_T("Internal: Transparency not supported with JPEG output"));
		goto done;
	}

	if(img.bit_depth!=8) {
		iw_seterror(ctx,_T("Internal: Precision %d not supported with JPEG output"),img.bit_depth);
		goto done;
	}

	is_grayscale = IW_IMGTYPE_IS_GRAY(img.imgtype);

	if(is_grayscale) {
		jpeg_colortype=JCS_GRAYSCALE;
		jpeg_cmpts=1;
	}
	else {
		jpeg_colortype=JCS_RGB;
		jpeg_cmpts=3;
	}


	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;

	if (setjmp(jerr.setjmp_buffer)) {
		char buffer[JMSG_LENGTH_MAX];

		(*cinfo.err->format_message) ((j_common_ptr)&cinfo, buffer);

#ifdef _UNICODE
		iw_seterror(ctx,_T("libjpeg reports error: %S"),buffer);
#else
		iw_seterror(ctx,"libjpeg reports error: %s",buffer);
#endif

		goto done;
	}

	jpeg_create_compress(&cinfo);
	compress_created=1;

	f=_tfopen(fn,_T("wb"));
	if(!f) {
		iw_seterror(ctx,_T("Failed to open for writing (error code=%d)"),(int)errno);
		goto done;
	}
	jpeg_stdio_dest(&cinfo, f);

	cinfo.image_width = img.width;
	cinfo.image_height = img.height;
	cinfo.input_components = jpeg_cmpts;
	cinfo.in_color_space = jpeg_colortype;

	jpeg_set_defaults(&cinfo);

	jpeg_quality = iw_get_value(ctx,IW_VAL_JPEG_QUALITY);
	if(jpeg_quality>0) {
		jpeg_set_quality(&cinfo,jpeg_quality,0);
	}
	//if(ctx->output_interlaced) jpeg_simple_progression(&cinfo);

	row_pointers = (JSAMPROW*)iw_malloc(ctx, img.height * sizeof(JSAMPROW));
	if(!row_pointers) goto done;

	for(j=0;j<img.height;j++) {
		row_pointers[j] = &img.pixels[j*img.bpr];
	}

	jpeg_start_compress(&cinfo, TRUE);
	compress_started=1;

	jpeg_write_scanlines(&cinfo, row_pointers, img.height);

	retval=1;

done:
	if(compress_started)
		jpeg_finish_compress(&cinfo);

	if(compress_created)
		jpeg_destroy_compress(&cinfo);

	if(f) fclose(f);
	if(row_pointers) iw_free(row_pointers);

	return retval;
}

TCHAR *iw_get_libjpeg_version_string(TCHAR *s, int s_len, int cset)
{
	struct jpeg_error_mgr jerr;
	const char *jv;

	jpeg_std_error(&jerr);
	jv = jerr.jpeg_message_table[JMSG_VERSION];
#ifdef UNICODE
	iw_snprintf(s,s_len,_T("%S"),jv);
#else
	iw_snprintf(s,s_len,_T("%s"),jv);
#endif
	return s;
}
