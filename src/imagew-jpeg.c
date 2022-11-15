// imagew-jpeg.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#if IW_SUPPORT_JPEG == 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>

#include <jpeglib.h>
#include <jerror.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

#if BITS_IN_JSAMPLE != 8
#error "Wrong JSAMPLE size"
#endif

struct my_error_mgr {
	struct jpeg_error_mgr pub; // This field must be first.
	int have_libjpeg_error;
	jmp_buf setjmp_buffer;
};

static void my_error_exit(j_common_ptr cinfo)
{
	struct my_error_mgr* myerr = (struct my_error_mgr*)cinfo->err;
	myerr->have_libjpeg_error = 1;
	longjmp(myerr->setjmp_buffer, 1);
}

// If we don't make our own output_message function, libjpeg will
// print warnings to stderr.
static void my_output_message(j_common_ptr cinfo)
{
	return;
}

struct iwjpegrcontext {
	struct jpeg_source_mgr pub; // This field must be first.
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	JOCTET *buffer;
	size_t buffer_len;
	int is_jfif;
	unsigned int exif_orientation; // 0 means not set
	double exif_density_x, exif_density_y; // -1.0 means not set.
	unsigned int exif_density_unit; // 0 means not set
};

// TODO?: Combine iwjpegrcontext and jr_rsrc_struct into one struct (or something).
struct jr_rsrc_struct {
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	struct iwjpegrcontext rctx;
	JSAMPLE *tmprow;
	struct iw_image img;
	int cinfo_valid;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
};

struct iw_exif_state {
	int endian;
	const iw_byte *d;
	size_t d_len;
};

static unsigned int get_exif_ui16(struct iw_exif_state *e, unsigned int pos)
{
	if(e->d_len<2 || pos>e->d_len-2) return 0;
	return iw_get_ui16_e(&e->d[pos], e->endian);
}

static unsigned int get_exif_ui32(struct iw_exif_state *e, unsigned int pos)
{
	if(e->d_len<4 || pos>e->d_len-4) return 0;
	return iw_get_ui32_e(&e->d[pos], e->endian);
}

// Try to read an Exif tag into an integer.
// Returns zero on failure.
static int get_exif_tag_int_value(struct iw_exif_state *e, unsigned int tag_pos,
	unsigned int *pv)
{
	unsigned int field_type;
	unsigned int value_count;

	field_type = get_exif_ui16(e, tag_pos+2);
	value_count = get_exif_ui32(e, tag_pos+4);

	if(value_count!=1) return 0;

	if(field_type==3) { // SHORT (uint16)
		*pv = get_exif_ui16(e, tag_pos+8);
		return 1;
	}
	else if(field_type==4) { // LONG (uint32)
		*pv = get_exif_ui32(e, tag_pos+8);
		return 1;
	}

	return 0;
}

// Read an Exif tag into a double.
// This only supports the case where the tag contains exactly one Rational value.
static int get_exif_tag_dbl_value(struct iw_exif_state *e, unsigned int tag_pos,
	double *pv)
{
	unsigned int field_type;
	unsigned int value_count;
	unsigned int value_pos;
	unsigned int numer, denom;

	field_type = get_exif_ui16(e, tag_pos+2);
	value_count = get_exif_ui32(e, tag_pos+4);

	if(value_count!=1) return 0;

	if(field_type!=5) return 0; // 5=Rational (two uint32's)

	// A rational is 8 bytes. Since 8>4, it is stored indirectly. First, read
	// the location where it is stored.

	value_pos = get_exif_ui32(e, tag_pos+8);
	if(value_pos > e->d_len-8) return 0;

	// Read the actual value.
	numer = get_exif_ui32(e, value_pos);
	denom = get_exif_ui32(e, value_pos+4);
	if(denom==0) return 0;

	*pv = ((double)numer)/denom;
	return 1;
}

static void iwjpeg_scan_exif_ifd(struct iwjpegrcontext *rctx,
	struct iw_exif_state *e, iw_uint32 ifd)
{
	unsigned int tag_count;
	unsigned int i;
	unsigned int tag_pos;
	unsigned int tag_id;
	unsigned int v;
	double v_dbl;

	if(ifd<8 || e->d_len<18 || ifd>e->d_len-18) return;

	tag_count = get_exif_ui16(e, ifd);
	if(tag_count>1000) return; // Sanity check.

	for(i=0;i<tag_count;i++) {
		tag_pos = ifd+2+i*12;
		if(tag_pos+12 > e->d_len) return; // Avoid overruns.
		tag_id = get_exif_ui16(e, tag_pos);

		switch(tag_id) {
		case 274: // 274 = Orientation
			if(get_exif_tag_int_value(e,tag_pos,&v)) {
				rctx->exif_orientation = v;
			}
			break;

		case 296: // 296 = ResolutionUnit
			if(get_exif_tag_int_value(e,tag_pos,&v)) {
				rctx->exif_density_unit = v;
			}
			break;

		case 282: // 282 = XResolution
			if(get_exif_tag_dbl_value(e,tag_pos,&v_dbl)) {
				rctx->exif_density_x = v_dbl;
			}
			break;

		case 283: // 283 = YResolution
			if(get_exif_tag_dbl_value(e,tag_pos,&v_dbl)) {
				rctx->exif_density_y = v_dbl;
			}
			break;
		}
	}
}

static void iwjpeg_scan_exif(struct iwjpegrcontext *rctx,
		const iw_byte *d, size_t d_len)
{
	struct iw_exif_state e;
	iw_uint32 ifd;

	if(d_len<8) return;

	iw_zeromem(&e,sizeof(struct iw_exif_state));
	e.d = d;
	e.d_len = d_len;

	e.endian = d[0]=='I' ? IW_ENDIAN_LITTLE : IW_ENDIAN_BIG;

	ifd = get_exif_ui32(&e, 4);

	iwjpeg_scan_exif_ifd(rctx,&e,ifd);
}

// Look at the saved JPEG markers.
// The only one we care about is Exif.
static void iwjpeg_read_saved_markers(struct iwjpegrcontext *rctx,
	struct jpeg_decompress_struct *cinfo)
{
	struct jpeg_marker_struct *mk;
	const iw_byte *d;

	mk = cinfo->marker_list;

	// Walk the list of saved markers.
	while(mk) {
		d = (const iw_byte*)mk->data;

		if(mk->marker==0xe1) {
			if(mk->data_length>=6 && d[0]=='E' && d[1]=='x' &&
				d[2]=='i' && d[3]=='f' && d[4]==0)
			{
				// I don't know what the d[5] byte is for, but Exif
				// data always starts with d[6].
				iwjpeg_scan_exif(rctx, &d[6], mk->data_length-6);
			}
		}

		mk = mk->next;
	}
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
		// If we have square pixels with unknown units, we might be looking at
		// libjpeg's default (i.e. no JFIF segment), or the density might have
		// been read from the file. In either case, leave the density set to
		// "unknown", which allows it to be overridden later by Exif data.
		if(cinfo->X_density!=cinfo->Y_density) {
			img->density_x = (double)cinfo->X_density;
			img->density_y = (double)cinfo->Y_density;
			img->density_code = IW_DENSITY_UNITS_UNKNOWN;
		}
	}
}

// Look at the Exif density setting that we may have recorded, and copy
// it to the image, if appropriate.
static void handle_exif_density(struct iwjpegrcontext *rctx, struct iw_image *img)
{
	if(img->density_code!=IW_DENSITY_UNKNOWN) {
		// We already have a density, presumably from the JFIF segment.
		// TODO: In principle, Exif should not be allowed to overrule JFIF.
		// But Exif density can be more precise than JFIF density, so it might
		// be better to respect Exif.
		// (On the other other hand, files with Exif data are usually from
		// digital cameras, which means the density information is unlikely
		// to be meaningful anyway.)
		return;
	}

	if(rctx->exif_density_x<=0.0 || rctx->exif_density_y<=0.0) return;

	switch(rctx->exif_density_unit) {
	case 1: // No units
		if(fabs(rctx->exif_density_x-rctx->exif_density_y)<0.00001)
			return; // Square, unitless pixels = no meaningful information.
		img->density_x = rctx->exif_density_x;
		img->density_y = rctx->exif_density_y;
		img->density_code = IW_DENSITY_UNITS_UNKNOWN;
		break;
	case 2: // Inches
		img->density_x = rctx->exif_density_x/0.0254;
		img->density_y = rctx->exif_density_y/0.0254;
		img->density_code = IW_DENSITY_UNITS_PER_METER;
		break;
	case 3: // Centimeters
		img->density_x = rctx->exif_density_x*100.0;
		img->density_y = rctx->exif_density_y*100.0;
		img->density_code = IW_DENSITY_UNITS_PER_METER;
		break;
	}
}

static void my_init_source_fn(j_decompress_ptr cinfo)
{
	struct iwjpegrcontext *rctx = (struct iwjpegrcontext*)cinfo->src;
	rctx->pub.next_input_byte = rctx->buffer;
	rctx->pub.bytes_in_buffer = 0;
}

static boolean my_fill_input_buffer_fn(j_decompress_ptr cinfo)
{
	struct iwjpegrcontext *rctx = (struct iwjpegrcontext*)cinfo->src;
	size_t bytesread = 0;
	int ret;

	ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
		rctx->buffer,rctx->buffer_len,&bytesread);
	if((!ret) || (bytesread<1)) {
		iw_set_error(rctx->ctx, "Unexpected end of file");
		// Return a fake EOI marker.
		rctx->buffer[0] = 0xffU;
		rctx->buffer[1] = 0xd9U;
		rctx->pub.bytes_in_buffer = 2;
	}
	else {
		rctx->pub.bytes_in_buffer = bytesread;
	}
	rctx->pub.next_input_byte = rctx->buffer;

	return TRUE;
}

static void my_skip_input_data_fn(j_decompress_ptr cinfo, long num_bytes_to_skip)
{
	struct iwjpegrcontext *rctx = (struct iwjpegrcontext*)cinfo->src;
	size_t bytes_still_to_skip;
	int ret;
	size_t bytesread;

	// If the skip would leave some valid bytes in the buffer, ...
	if(num_bytes_to_skip < (long)rctx->pub.bytes_in_buffer) {
		// ... just move the pointer.
		rctx->pub.next_input_byte += num_bytes_to_skip;
		rctx->pub.bytes_in_buffer -= num_bytes_to_skip;
		return;
	}

	// Otherwise, read + throw away the necessary number of bytes ...
	bytes_still_to_skip = (size_t)num_bytes_to_skip - rctx->pub.bytes_in_buffer;
	while(bytes_still_to_skip>0) {
		size_t bytes_to_read;

		bytes_to_read = rctx->buffer_len;
		if(bytes_to_read > bytes_still_to_skip) {
			bytes_to_read = bytes_still_to_skip;
		}

		// Read from the file (could do a seek instead, but we currently don't
		// support seeking).
		ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
			rctx->buffer,bytes_to_read,&bytesread);
		if(!ret) bytesread=0;
		if(bytesread==0 || bytesread>bytes_still_to_skip) {
			// If we couldn't read any data, stop.
			// (We don't attempt to robustly support input streams for which
			// not all the data is available yet.)
			break;
		}

		bytes_still_to_skip -= bytesread;
	}

	// ... and mark the buffer as empty.
	rctx->pub.next_input_byte = rctx->buffer;
	rctx->pub.bytes_in_buffer = 0;
}

static void my_term_source_fn(j_decompress_ptr cinfo)
{
}

static void convert_cmyk_to_rbg(struct iw_context *ctx, const JSAMPLE *src,
	JSAMPLE *dst, int npixels)
{
	int i;
	double c, m, y, k, r, g, b;

	for(i=0;i<npixels;i++) {
		c = 1.0 - ((double)src[4*i+0])/255.0;
		m = 1.0 - ((double)src[4*i+1])/255.0;
		y = 1.0 - ((double)src[4*i+2])/255.0;
		k = 1.0 - ((double)src[4*i+3])/255.0;
		r = 1.0 - c*(1.0-k) - k;
		g = 1.0 - m*(1.0-k) - k;
		b = 1.0 - y*(1.0-k) - k;
		if(r<0.0) r=0.0;
		if(r>1.0) r=1.0;
		if(g<0.0) g=0.0;
		if(g>1.0) g=1.0;
		if(b<0.0) b=0.0;
		if(b>1.0) b=1.0;
		dst[3*i+0] = (JSAMPLE)(0.5+255.0*r);
		dst[3*i+1] = (JSAMPLE)(0.5+255.0*g);
		dst[3*i+2] = (JSAMPLE)(0.5+255.0*b);
	}
}

static int iw_read_jpeg_file3(struct jr_rsrc_struct *jr)
{
	struct iw_context *ctx = jr->ctx;
	struct iw_iodescr *iodescr = jr->iodescr;
	int retval=0;
	int colorspace;
	JDIMENSION rownum;
	JSAMPLE *jsamprow;
	int numchannels=0;
	int cmyk_flag = 0;
	int ret;

	jpeg_create_decompress(&jr->cinfo);
	jr->cinfo_valid=1;

	// Set up our custom source manager.
	jr->rctx.pub.init_source = my_init_source_fn;
	jr->rctx.pub.fill_input_buffer = my_fill_input_buffer_fn;
	jr->rctx.pub.skip_input_data = my_skip_input_data_fn;
	jr->rctx.pub.resync_to_restart = jpeg_resync_to_restart; // libjpeg default
	jr->rctx.pub.term_source = my_term_source_fn;
	jr->rctx.ctx = ctx;
	jr->rctx.iodescr = iodescr;
	jr->rctx.buffer_len = 32768;
	jr->rctx.buffer = iw_malloc(ctx, jr->rctx.buffer_len);
	if(!jr->rctx.buffer) goto done;
	jr->rctx.exif_density_x = -1.0;
	jr->rctx.exif_density_y = -1.0;
	jr->cinfo.src = (struct jpeg_source_mgr*)&jr->rctx;

	// The lazy way. It would be more efficient to use
	// jpeg_set_marker_processor(), instead of saving everything to memory.
	// But libjpeg's marker processing functions have fairly complex
	// requirements.
	jpeg_save_markers(&jr->cinfo, 0xe1, 65535);

	ret = jpeg_read_header(&jr->cinfo, TRUE);
	if(ret != JPEG_HEADER_OK) {
		// I don't think this is supposed to be possible, assuming the second
		// param to jpeg_read_header is TRUE, and our fill_input_buffer
		// function always returns TRUE.
		iw_set_error(ctx, "Unexpected libjpeg error");
		goto done;
	}

	jr->rctx.is_jfif = jr->cinfo.saw_JFIF_marker;

	iwjpeg_read_density(ctx,&jr->img,&jr->cinfo);

	iwjpeg_read_saved_markers(&jr->rctx,&jr->cinfo);

	jpeg_start_decompress(&jr->cinfo);

	colorspace=jr->cinfo.out_color_space;
	numchannels=jr->cinfo.output_components;

	// libjpeg will automatically convert YCbCr images to RGB, and YCCK images
	// to CMYK. That leaves GRAYSCALE, RGB, and CMYK for us to handle.
	// Note: cinfo.jpeg_color_space is the colorspace before conversion, and
	// cinfo.out_color_space is the colorspace after conversion.

	if(colorspace==JCS_GRAYSCALE && numchannels==1) {
		jr->img.imgtype = IW_IMGTYPE_GRAY;
		jr->img.native_grayscale = 1;
	}
	else if((colorspace==JCS_RGB) && numchannels==3) {
		jr->img.imgtype = IW_IMGTYPE_RGB;
	}
	else if((colorspace==JCS_CMYK) && numchannels==4) {
		jr->img.imgtype = IW_IMGTYPE_RGB;
		cmyk_flag = 1;
	}
	else {
		iw_set_error(ctx,"Unsupported type of JPEG");
		goto done;
	}

	jr->img.width = jr->cinfo.output_width;
	jr->img.height = jr->cinfo.output_height;
	if(!iw_check_image_dimensions(ctx,jr->img.width,jr->img.height)) {
		goto done;
	}

	jr->img.bit_depth = 8;
	jr->img.bpr = iw_calc_bytesperrow(jr->img.width,jr->img.bit_depth*numchannels);

	jr->img.pixels = (iw_byte*)iw_malloc_large(ctx, jr->img.bpr, jr->img.height);
	if(!jr->img.pixels) {
		goto done;
	}

	if(cmyk_flag) {
		jr->tmprow = iw_malloc(ctx,4*jr->img.width);
		if(!jr->tmprow) goto done;
	}

	while(jr->cinfo.output_scanline < jr->cinfo.output_height) {
		rownum=jr->cinfo.output_scanline;
		jsamprow = &jr->img.pixels[jr->img.bpr * rownum];
		if(cmyk_flag) {
			// read into tmprow, then convert and copy to img.pixels
			jpeg_read_scanlines(&jr->cinfo, &jr->tmprow, 1);
			convert_cmyk_to_rbg(ctx,jr->tmprow,jsamprow,jr->img.width);
		}
		else {
			// read directly into img.pixels
			jpeg_read_scanlines(&jr->cinfo, &jsamprow, 1);
		}
		if(jr->cinfo.output_scanline<=rownum) {
			iw_set_error(ctx,"Error reading JPEG file");
			goto done;
		}
	}
	jpeg_finish_decompress(&jr->cinfo);

	handle_exif_density(&jr->rctx, &jr->img);

	iw_set_input_image(ctx, &jr->img);
	// The contents of img no longer belong to us.
	jr->img.pixels = NULL;

	if(jr->rctx.exif_orientation>=2 && jr->rctx.exif_orientation<=8) {
		static const unsigned int exif_orient_to_transform[9] =
		   { 0,0, 1,3,2,4,5,7,6 };

		// An Exif marker indicated an unusual image orientation.

		// Note that if there is also a JFIF segment (jr->rctx.is_jfif), the
		// orientation is technically ambiguous. But, these days, the usual
		// practice is to allow Exif to overrule JFIF.

		iw_reorient_image(ctx,exif_orient_to_transform[jr->rctx.exif_orientation]);
	}

	retval=1;

done:
	// Don't free memory here; do it in iw_read_jpeg_file().
	return retval;
}

// This function serves as a target for longjmp(). It shouldn't do much of
// anything else.
static int iw_read_jpeg_file2(struct jr_rsrc_struct *jr)
{
	if (setjmp(jr->jerr.setjmp_buffer)) {
		return 0;
	}

	return iw_read_jpeg_file3(jr);
}

IW_IMPL(int) iw_read_jpeg_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	int retval = 0;
	struct jr_rsrc_struct *jr = NULL;

	jr = iw_mallocz(ctx, sizeof(struct jr_rsrc_struct));
	if(!jr) goto done;
	jr->ctx = ctx;
	jr->iodescr = iodescr;
	jr->cinfo.err = jpeg_std_error(&jr->jerr.pub);
	jr->jerr.pub.error_exit = my_error_exit;
	jr->jerr.pub.output_message = my_output_message;

	retval = iw_read_jpeg_file2(jr);

done:
	if(jr) {
		if(jr->jerr.have_libjpeg_error) {
			char buffer[JMSG_LENGTH_MAX];

			(*jr->cinfo.err->format_message) ((j_common_ptr)&jr->cinfo, buffer);
			iw_set_errorf(jr->ctx, "libjpeg reports read error: %s", buffer);
		}

		if(jr->cinfo_valid) jpeg_destroy_decompress(&jr->cinfo);
		if(jr->rctx.buffer) iw_free(ctx, jr->rctx.buffer);
		iw_free(ctx, jr->img.pixels);
		if(jr->tmprow) iw_free(ctx, jr->tmprow);
		iw_free(ctx, jr);
	}
	return retval;
}

////////////////////////////////////

struct iwjpegwcontext {
	struct jpeg_destination_mgr pub; // This field must be first.
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	JOCTET *buffer;
	size_t buffer_len;
};

struct jw_rsrc_struct {
	struct iw_context *ctx;
	struct iw_iodescr *iodescr;
	struct iwjpegwcontext wctx;
	JSAMPROW *row_pointers;
	int compress_created;
	struct jpeg_compress_struct cinfo;
	struct my_error_mgr jerr;
};

static void iwjpg_set_density(struct iw_context *ctx,struct jpeg_compress_struct *cinfo,
	const struct iw_image *img)
{
	int pref_units;

	if(img->density_code==IW_DENSITY_UNITS_UNKNOWN) {
		cinfo->density_unit=0; // unknown units
		cinfo->X_density = (UINT16)(0.5+img->density_x);
		cinfo->Y_density = (UINT16)(0.5+img->density_y);
	}
	else if(img->density_code==IW_DENSITY_UNITS_PER_METER) {
		pref_units = iw_get_value(ctx,IW_VAL_PREF_UNITS);

		if(pref_units==IW_PREF_UNITS_METRIC) {
			// If we think the caller prefers metric, use dots/cm.
			cinfo->density_unit=2; // dots/cm
			cinfo->X_density = (UINT16)(0.5+ img->density_x*0.01);
			cinfo->Y_density = (UINT16)(0.5+ img->density_y*0.01);
		}
		else {
			// Otherwise use dpi.
			cinfo->density_unit=1; // dots/inch
			cinfo->X_density = (UINT16)(0.5+ img->density_x*0.0254);
			cinfo->Y_density = (UINT16)(0.5+ img->density_y*0.0254);
		}
	}
}

static void my_init_destination_fn(j_compress_ptr cinfo)
{
	struct iwjpegwcontext *wctx = (struct iwjpegwcontext*)cinfo->dest;

	// Configure the destination manager to use our buffer.
	wctx->pub.next_output_byte = wctx->buffer;
	wctx->pub.free_in_buffer = wctx->buffer_len;
}

static boolean my_empty_output_buffer_fn(j_compress_ptr cinfo)
{
	struct iwjpegwcontext *wctx = (struct iwjpegwcontext*)cinfo->dest;

	// Write out the entire buffer
	(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,
		wctx->buffer,wctx->buffer_len);
	// Change the data pointer and free-space indicator to reflect the
	// data we wrote.
	wctx->pub.next_output_byte = wctx->buffer;
	wctx->pub.free_in_buffer = wctx->buffer_len;
	return TRUE;
}

static void my_term_destination_fn(j_compress_ptr cinfo)
{
	struct iwjpegwcontext *wctx = (struct iwjpegwcontext*)cinfo->dest;
	size_t bytesleft;

	bytesleft = wctx->buffer_len - wctx->pub.free_in_buffer;
	if(bytesleft>0) {
		(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,
			wctx->buffer,bytesleft);
	}
}

static int iw_write_jpeg_file3(struct jw_rsrc_struct *jw)
{
	struct iw_context *ctx = jw->ctx;
	struct iw_iodescr *iodescr = jw->iodescr;
	int retval=0;
	J_COLOR_SPACE in_colortype; // Color type of the data we give to libjpeg
	int jpeg_cmpts;
	int compress_started = 0;
	int is_grayscale;
	int j;
	struct iw_image img;
	int jpeg_quality;
	int samp_factor_h, samp_factor_v;
	int disable_subsampling = 0;
	const char *optv;
	int ret;

	iw_get_output_image(ctx,&img);

	if(IW_IMGTYPE_HAS_ALPHA(img.imgtype)) {
		iw_set_error(ctx,"Internal: Transparency not supported with JPEG output");
		goto done;
	}

	if(img.bit_depth!=8) {
		iw_set_errorf(ctx,"Internal: Precision %d not supported with JPEG output",img.bit_depth);
		goto done;
	}

	is_grayscale = IW_IMGTYPE_IS_GRAY(img.imgtype);

	if(is_grayscale) {
		in_colortype=JCS_GRAYSCALE;
		jpeg_cmpts=1;
	}
	else {
		in_colortype=JCS_RGB;
		jpeg_cmpts=3;
	}

	jpeg_create_compress(&jw->cinfo);
	jw->compress_created=1;

	// Set up our custom destination manager.
	jw->wctx.pub.init_destination = my_init_destination_fn;
	jw->wctx.pub.empty_output_buffer = my_empty_output_buffer_fn;
	jw->wctx.pub.term_destination = my_term_destination_fn;
	jw->wctx.ctx = ctx;
	jw->wctx.iodescr = iodescr;
	jw->wctx.buffer_len = 32768;
	jw->wctx.buffer = iw_malloc(ctx,jw->wctx.buffer_len);
	if(!jw->wctx.buffer) goto done;
	// Our wctx is organized so it can double as a
	// 'struct jpeg_destination_mgr'.
	jw->cinfo.dest = (struct jpeg_destination_mgr*)&jw->wctx;

	jw->cinfo.image_width = img.width;
	jw->cinfo.image_height = img.height;
	jw->cinfo.input_components = jpeg_cmpts;
	jw->cinfo.in_color_space = in_colortype;

	jpeg_set_defaults(&jw->cinfo);

	optv = iw_get_option(ctx, "jpeg:rstm");
	if(optv) {
		jw->cinfo.restart_interval = (unsigned int)iw_parse_int(optv);
	}
	optv = iw_get_option(ctx, "jpeg:rstr");
	if(optv) {
		jw->cinfo.restart_in_rows = (int)iw_parse_int(optv);
	}

	optv = iw_get_option(ctx, "jpeg:block");
	if(optv) {
#if (JPEG_LIB_VERSION_MAJOR>=9 || \
	(JPEG_LIB_VERSION_MAJOR==8 && JPEG_LIB_VERSION_MINOR>=3))
		// Note: This might not work if DCT_SCALING_SUPPORTED was not defined when
		// libjpeg was compiled, but that symbol is not normally exposed to
		// applications.
		jw->cinfo.block_size = iw_parse_int(optv);
#else
		iw_warning(ctx, "Setting block size is not supported by this version of libjpeg");
#endif
	}

	optv = iw_get_option(ctx, "jpeg:arith");
	if(optv)
		jw->cinfo.arith_code = iw_parse_int(optv) ? TRUE : FALSE;
	else
		jw->cinfo.arith_code = FALSE;

	optv = iw_get_option(ctx, "jpeg:colortype");
	if(optv) {
		if(!strcmp(optv, "rgb")) {
			if(in_colortype==JCS_RGB) {
				jpeg_set_colorspace(&jw->cinfo,JCS_RGB);
				disable_subsampling = 1;
			}
		}
		else if(!strcmp(optv, "rgb1")) {
			if(in_colortype==JCS_RGB) {
#if JPEG_LIB_VERSION_MAJOR >= 9
				jw->cinfo.color_transform = JCT_SUBTRACT_GREEN;
#else
				iw_warning(ctx, "Color type rgb1 is not supported by this version of libjpeg");
#endif
				jpeg_set_colorspace(&jw->cinfo,JCS_RGB);
				disable_subsampling = 1;
			}
		}
	}

	optv = iw_get_option(ctx, "jpeg:optcoding");
	if(optv && iw_parse_int(optv)) {
		jw->cinfo.optimize_coding = TRUE;
	}

	optv = iw_get_option(ctx, "jpeg:bgycc");
	if(optv && iw_parse_int(optv)) {
#if (JPEG_LIB_VERSION_MAJOR>9 || \
	(JPEG_LIB_VERSION_MAJOR==9 && JPEG_LIB_VERSION_MINOR>=1))
		jpeg_set_colorspace(&jw->cinfo, JCS_BG_YCC);
#else
		iw_warning(ctx, "Big gamut YCC is not supported by this version of libjpeg");
#endif
	}

	iwjpg_set_density(ctx,&jw->cinfo,&img);

	optv = iw_get_option(ctx, "jpeg:quality");
	if(optv)
		jpeg_quality = iw_parse_int(optv);
	else
		jpeg_quality = 0;

	if(jpeg_quality>0) {
		jpeg_set_quality(&jw->cinfo,jpeg_quality,0);
	}

	if(jpeg_cmpts>1 && !disable_subsampling) {
		samp_factor_h = 0;
		samp_factor_v = 0;

		// sampling-x and sampling-y are for backward compatibility, and should
		// not be used.
		optv = iw_get_option(ctx, "jpeg:sampling-x");
		if(optv)
			samp_factor_h = iw_parse_int(optv);
		optv = iw_get_option(ctx, "jpeg:sampling-y");
		if(optv)
			samp_factor_v = iw_parse_int(optv);

		optv = iw_get_option(ctx, "jpeg:sampling");
		if(optv) {
			double tmpsamp[2];
			tmpsamp[0] = 1.0;
			tmpsamp[1] = 1.0;
			ret = iw_parse_number_list(optv, 2, tmpsamp);
			samp_factor_h = iw_round_to_int(tmpsamp[0]);
			if(ret==1) {
				// If only one value was given, use it for both factors.
				samp_factor_v = samp_factor_h;
			}
			else {
				samp_factor_v = iw_round_to_int(tmpsamp[1]);
			}
		}

		if(samp_factor_h>0) {
			if(samp_factor_h>4) samp_factor_h=4;
			jw->cinfo.comp_info[0].h_samp_factor = samp_factor_h;
		}
		if(samp_factor_v>0) {
			if(samp_factor_v>4) samp_factor_v=4;
			jw->cinfo.comp_info[0].v_samp_factor = samp_factor_v;
		}
	}

	if(iw_get_value(ctx,IW_VAL_OUTPUT_INTERLACED)) {
		jpeg_simple_progression(&jw->cinfo);
	}

	jw->row_pointers = (JSAMPROW*)iw_malloc(ctx, img.height * sizeof(JSAMPROW));
	if(!jw->row_pointers) goto done;

	for(j=0;j<img.height;j++) {
		jw->row_pointers[j] = &img.pixels[j*img.bpr];
	}

	jpeg_start_compress(&jw->cinfo, TRUE);
	compress_started=1;

	jpeg_write_scanlines(&jw->cinfo, jw->row_pointers, img.height);

	retval=1;

done:
	if(compress_started)
		jpeg_finish_compress(&jw->cinfo);

	// Don't free memory here; do it in iw_write_jpeg_file().
	return retval;
}

// This function serves as a target for longjmp(). It shouldn't do much of
// anything else.
static int iw_write_jpeg_file2(struct jw_rsrc_struct *jw)
{
	if (setjmp(jw->jerr.setjmp_buffer)) {
		return 0;
	}

	return iw_write_jpeg_file3(jw);
}

IW_IMPL(int) iw_write_jpeg_file(struct iw_context *ctx,  struct iw_iodescr *iodescr)
{
	struct jw_rsrc_struct *jw = NULL;
	int retval = 0;

	jw = iw_mallocz(ctx, sizeof(struct jw_rsrc_struct));
	if(!jw) goto done;
	jw->ctx = ctx;
	jw->iodescr = iodescr;

	jw->cinfo.err = jpeg_std_error(&jw->jerr.pub);
	jw->jerr.pub.error_exit = my_error_exit;
	jw->jerr.pub.output_message = my_output_message;

	retval = iw_write_jpeg_file2(jw);

done:
	if(jw) {
		if(jw->jerr.have_libjpeg_error) {
			char buffer[JMSG_LENGTH_MAX];

			(*jw->cinfo.err->format_message) ((j_common_ptr)&jw->cinfo, buffer);
			iw_set_errorf(jw->ctx, "libjpeg reports write error: %s", buffer);
		}

		if(jw->compress_created) jpeg_destroy_compress(&jw->cinfo);
		if(jw->row_pointers) iw_free(ctx, jw->row_pointers);
		if(jw->wctx.buffer) iw_free(ctx, jw->wctx.buffer);
		iw_free(ctx, jw);
	}
	return retval;
}

IW_IMPL(char*) iw_get_libjpeg_version_string(char *s, int s_len)
{
	struct jpeg_error_mgr jerr;
	const char *jv;
	char *space_ptr;

	jpeg_std_error(&jerr);
	jv = jerr.jpeg_message_table[JMSG_VERSION];
	iw_snprintf(s,s_len,"%s",jv);

	// The version is probably a string like "8c  16-Jan-2011", containing
	// both the version number and the release date. We only need the version
	// number, so chop it off at the first space.
	space_ptr = strchr(s,' ');
	if(space_ptr) *space_ptr = '\0';
	return s;
}

#endif // IW_SUPPORT_JPEG
