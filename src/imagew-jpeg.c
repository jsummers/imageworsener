// imagew-jpeg.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#ifdef IW_WINDOWS
#include <tchar.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void my_error_exit(j_common_ptr cinfo)
{
	struct my_error_mgr* myerr = (struct my_error_mgr*)cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}

// If we don't make our own output_message function, libjpeg will
// print warnings to stderr.
static void my_output_message(j_common_ptr cinfo)
{
	return;
}

struct iw_jpegrctx {
	struct jpeg_source_mgr pub; // This field must be first.
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	JOCTET *buffer;
	size_t buffer_len;
};

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

static void my_init_source_fn(j_decompress_ptr cinfo)
{
	struct iw_jpegrctx *jpegrctx = (struct iw_jpegrctx*)cinfo->src;
	jpegrctx->pub.next_input_byte = jpegrctx->buffer;
	jpegrctx->pub.bytes_in_buffer = 0;
}

static boolean my_fill_input_buffer_fn(j_decompress_ptr cinfo)
{
	struct iw_jpegrctx *jpegrctx = (struct iw_jpegrctx*)cinfo->src;
	size_t bytesread = 0;
	int ret;

	ret = (*jpegrctx->iodescr->read_fn)(jpegrctx->ctx,jpegrctx->iodescr,
		jpegrctx->buffer,jpegrctx->buffer_len,&bytesread);
	if(!ret) return FALSE;

	jpegrctx->pub.next_input_byte = jpegrctx->buffer;
	jpegrctx->pub.bytes_in_buffer = bytesread;

	if(bytesread<1) return FALSE;
	return TRUE;
}

static void my_skip_input_data_fn(j_decompress_ptr cinfo, long num_bytes)
{
	struct iw_jpegrctx *jpegrctx = (struct iw_jpegrctx*)cinfo->src;
	size_t bytes_still_to_skip;
	size_t nbytes;
	int ret;
	size_t bytesread;

	if(num_bytes<=0) return;
	bytes_still_to_skip = (size_t)num_bytes;

	while(bytes_still_to_skip>0) {
		if(jpegrctx->pub.bytes_in_buffer>0) {
			// There are some bytes in the buffer. Skip up to
			// 'bytes_still_to_skip' of them.
			nbytes = jpegrctx->pub.bytes_in_buffer;
			if(nbytes>bytes_still_to_skip)
				nbytes = bytes_still_to_skip;

			jpegrctx->pub.bytes_in_buffer -= nbytes;
			jpegrctx->pub.next_input_byte += nbytes;
			bytes_still_to_skip -= nbytes;
		}

		if(bytes_still_to_skip<1) return;

		// Need to read from the file (or do a seek, but we currently don't
		// support seeking).
		ret = (*jpegrctx->iodescr->read_fn)(jpegrctx->ctx,jpegrctx->iodescr,
			jpegrctx->buffer,jpegrctx->buffer_len,&bytesread);
		if(!ret) bytesread=0;

		jpegrctx->pub.next_input_byte = jpegrctx->buffer;
		jpegrctx->pub.bytes_in_buffer = bytesread;
	}
}

static void my_term_source_fn(j_decompress_ptr cinfo)
{
}

int iw_read_jpeg_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	int retval=0;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	int cinfo_valid=0;
	int colorspace;
	int j;
	JSAMPLE *jsamprow;
	int numchannels=0;
	struct iw_image img;
	struct iw_jpegrctx jpegrctx;

	memset(&img,0,sizeof(struct iw_image));
	memset(&cinfo,0,sizeof(struct jpeg_decompress_struct));
	memset(&jerr,0,sizeof(struct my_error_mgr));
	memset(&jpegrctx,0,sizeof(struct iw_jpegrctx));

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

	// Set up our custom source manager.
	jpegrctx.pub.init_source = my_init_source_fn;
	jpegrctx.pub.fill_input_buffer = my_fill_input_buffer_fn;
	jpegrctx.pub.skip_input_data = my_skip_input_data_fn;
	jpegrctx.pub.resync_to_restart = jpeg_resync_to_restart; // libjpeg default
	jpegrctx.pub.term_source = my_term_source_fn;
	jpegrctx.ctx = ctx;
	jpegrctx.iodescr = iodescr;
	jpegrctx.buffer_len = 32768;
	jpegrctx.buffer = iw_malloc(ctx, jpegrctx.buffer_len);
	if(!jpegrctx.buffer) goto done;
	cinfo.src = (struct jpeg_source_mgr*)&jpegrctx;

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
	if(iodescr->close_fn)
		(*iodescr->close_fn)(ctx,iodescr);
	if(jpegrctx.buffer) iw_free(jpegrctx.buffer);
	return retval;
}

////////////////////////////////////

struct iw_jpegwctx {
	struct jpeg_destination_mgr pub; // This field must be first.
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	JOCTET *buffer;
	size_t buffer_len;
};

static void iwjpg_set_density(struct iw_context *ctx,struct jpeg_compress_struct *cinfo,
	const struct iw_image *img)
{
	if(img->density_code==IW_DENSITY_UNITS_UNKNOWN) {
		cinfo->density_unit=0; // unknown units
		cinfo->X_density = (UINT16)(0.5+img->density_x);
		cinfo->Y_density = (UINT16)(0.5+img->density_y);
	}
	else if(img->density_code==IW_DENSITY_UNITS_PER_METER) {
		cinfo->density_unit=1; // dots/inch
		cinfo->X_density = (UINT16)(0.5+ img->density_x*0.0254);
		cinfo->Y_density = (UINT16)(0.5+ img->density_y*0.0254);
	}
}

static void my_init_destination_fn(j_compress_ptr cinfo)
{
	struct iw_jpegwctx *jpegwctx = (struct iw_jpegwctx*)cinfo->dest;

	// Configure the destination manager to use our buffer.
	jpegwctx->pub.next_output_byte = jpegwctx->buffer;
	jpegwctx->pub.free_in_buffer = jpegwctx->buffer_len;
}

static boolean my_empty_output_buffer_fn(j_compress_ptr cinfo)
{
	struct iw_jpegwctx *jpegwctx = (struct iw_jpegwctx*)cinfo->dest;

	// Write out the entire buffer
	(*jpegwctx->iodescr->write_fn)(jpegwctx->ctx,jpegwctx->iodescr,
		jpegwctx->buffer,jpegwctx->buffer_len);
	// Change the data pointer and free-space indicator to reflect the
	// data we wrote.
	jpegwctx->pub.next_output_byte = jpegwctx->buffer;
	jpegwctx->pub.free_in_buffer = jpegwctx->buffer_len;
	return TRUE;
}

static void my_term_destination_fn(j_compress_ptr cinfo)
{
	struct iw_jpegwctx *jpegwctx = (struct iw_jpegwctx*)cinfo->dest;
	size_t bytesleft;

	bytesleft = jpegwctx->buffer_len - jpegwctx->pub.free_in_buffer;
	if(bytesleft>0) {
		(*jpegwctx->iodescr->write_fn)(jpegwctx->ctx,jpegwctx->iodescr,
			jpegwctx->buffer,bytesleft);
	}
}

int iw_write_jpeg_file(struct iw_context *ctx,  struct iw_iodescr *iodescr)
{
	int retval=0;
	struct jpeg_compress_struct cinfo;
	struct my_error_mgr jerr;
	J_COLOR_SPACE jpeg_colortype;
	int jpeg_cmpts;
	int compress_created = 0;
	int compress_started = 0;
	JSAMPROW *row_pointers = NULL;
	int is_grayscale;
	int j;
	struct iw_image img;
	int jpeg_quality;
	int samp_factor_h, samp_factor_v;
	struct iw_jpegwctx jpegwctx;

	memset(&cinfo,0,sizeof(struct jpeg_compress_struct));
	memset(&jerr,0,sizeof(struct my_error_mgr));
	memset(&jpegwctx,0,sizeof(struct iw_jpegwctx));

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

	// Set up our custom destination manager.
	jpegwctx.pub.init_destination = my_init_destination_fn;
	jpegwctx.pub.empty_output_buffer = my_empty_output_buffer_fn;
	jpegwctx.pub.term_destination = my_term_destination_fn;
	jpegwctx.ctx = ctx;
	jpegwctx.iodescr = iodescr;
	jpegwctx.buffer_len = 32768;
	jpegwctx.buffer = iw_malloc(ctx,jpegwctx.buffer_len);
	if(!jpegwctx.buffer) goto done;
	// Our jpegwctx is organized so it can double as a
	// 'struct jpeg_destination_mgr'.
	cinfo.dest = (struct jpeg_destination_mgr*)&jpegwctx;

	cinfo.image_width = img.width;
	cinfo.image_height = img.height;
	cinfo.input_components = jpeg_cmpts;
	cinfo.in_color_space = jpeg_colortype;

	jpeg_set_defaults(&cinfo);

	iwjpg_set_density(ctx,&cinfo,&img);

	jpeg_quality = iw_get_value(ctx,IW_VAL_JPEG_QUALITY);
	if(jpeg_quality>0) {
		jpeg_set_quality(&cinfo,jpeg_quality,0);
	}

	if(jpeg_cmpts>1) {
		samp_factor_h = iw_get_value(ctx,IW_VAL_JPEG_SAMP_FACTOR_H);
		samp_factor_v = iw_get_value(ctx,IW_VAL_JPEG_SAMP_FACTOR_V);

		if(samp_factor_h>0) {
			if(samp_factor_h>4) samp_factor_h=4;
			cinfo.comp_info[0].h_samp_factor = samp_factor_h;
		}
		if(samp_factor_v>0) {
			if(samp_factor_v>4) samp_factor_v=4;
			cinfo.comp_info[0].v_samp_factor = samp_factor_v;
		}
	}

	if(iw_get_value(ctx,IW_VAL_OUTPUT_INTERLACED)) {
		jpeg_simple_progression(&cinfo);
	}

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

	if(iodescr->close_fn)
		(*iodescr->close_fn)(ctx,iodescr);
	if(row_pointers) iw_free(row_pointers);

	if(jpegwctx.buffer) iw_free(jpegwctx.buffer);

	return retval;
}

TCHAR *iw_get_libjpeg_version_string(TCHAR *s, int s_len, int cset)
{
	struct jpeg_error_mgr jerr;
	const char *jv;
	TCHAR *space_ptr;

	jpeg_std_error(&jerr);
	jv = jerr.jpeg_message_table[JMSG_VERSION];
#ifdef UNICODE
	iw_snprintf(s,s_len,_T("%S"),jv);
#else
	iw_snprintf(s,s_len,_T("%s"),jv);
#endif

	// The version is probably a string like "8c  16-Jan-2011", containing
	// both the version number and the release date. We only need the version
	// number, so chop it off at the first space.
	space_ptr = _tcschr(s,' ');
	if(space_ptr) *space_ptr = '\0';
	return s;
}
