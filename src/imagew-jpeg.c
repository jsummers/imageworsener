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

struct iw_exif_state {
	int endian;
	const iw_byte *d;
	size_t d_len;
};

// Try to read an Exif tag into an integer.
// Returns zero on failure.
static int get_exif_tag_int_value(struct iw_exif_state *e, unsigned int tag_pos,
	unsigned int *pv)
{
	unsigned int field_type;
	unsigned int value_count;

	field_type = iw_get_ui16_e(&e->d[tag_pos+2],e->endian);
	value_count = iw_get_ui32_e(&e->d[tag_pos+4],e->endian);

	if(value_count!=1) return 0;

	if(field_type==3) { // SHORT (uint16)
		*pv = iw_get_ui16_e(&e->d[tag_pos+8],e->endian);
		return 1;
	}
	else if(field_type==4) { // LONG (uint32)
		*pv = iw_get_ui32_e(&e->d[tag_pos+8],e->endian);
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

	field_type = iw_get_ui16_e(&e->d[tag_pos+2],e->endian);
	value_count = iw_get_ui32_e(&e->d[tag_pos+4],e->endian);

	if(value_count!=1) return 0;

	if(field_type!=5) return 0; // 5=Rational (two uint32's)

	// A rational is 8 bytes. Since 8>4, it is stored indirectly. First, read
	// the location where it is stored.

	value_pos = iw_get_ui32_e(&e->d[tag_pos+8],e->endian);
	if(value_pos > e->d_len-8) return 0;

	// Read the actual value.
	numer = iw_get_ui32_e(&e->d[value_pos  ],e->endian);
	denom = iw_get_ui32_e(&e->d[value_pos+4],e->endian);
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

	if(ifd<8 || ifd>e->d_len-18) return;

	tag_count = iw_get_ui16_e(&e->d[ifd],e->endian);
	if(tag_count>1000) return; // Sanity check.

	for(i=0;i<tag_count;i++) {
		tag_pos = ifd+2+i*12;
		if(tag_pos+12 > e->d_len) return; // Avoid overruns.
		tag_id = iw_get_ui16_e(&e->d[tag_pos],e->endian);

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

	ifd = iw_get_ui32_e(&d[4],e.endian);

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
	if(!ret) return FALSE;

	rctx->pub.next_input_byte = rctx->buffer;
	rctx->pub.bytes_in_buffer = bytesread;

	if(bytesread<1) return FALSE;
	return TRUE;
}

static void my_skip_input_data_fn(j_decompress_ptr cinfo, long num_bytes)
{
	struct iwjpegrcontext *rctx = (struct iwjpegrcontext*)cinfo->src;
	size_t bytes_still_to_skip;
	size_t nbytes;
	int ret;
	size_t bytesread;

	if(num_bytes<=0) return;
	bytes_still_to_skip = (size_t)num_bytes;

	while(bytes_still_to_skip>0) {
		if(rctx->pub.bytes_in_buffer>0) {
			// There are some bytes in the buffer. Skip up to
			// 'bytes_still_to_skip' of them.
			nbytes = rctx->pub.bytes_in_buffer;
			if(nbytes>bytes_still_to_skip)
				nbytes = bytes_still_to_skip;

			rctx->pub.bytes_in_buffer -= nbytes;
			rctx->pub.next_input_byte += nbytes;
			bytes_still_to_skip -= nbytes;
		}

		if(bytes_still_to_skip<1) return;

		// Need to read from the file (or do a seek, but we currently don't
		// support seeking).
		ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
			rctx->buffer,rctx->buffer_len,&bytesread);
		if(!ret) bytesread=0;

		rctx->pub.next_input_byte = rctx->buffer;
		rctx->pub.bytes_in_buffer = bytesread;
	}
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
		if(r<0.0) r=0.0; if(r>1.0) r=1.0;
		if(g<0.0) g=0.0; if(g>1.0) g=1.0;
		if(b<0.0) b=0.0; if(b>1.0) b=1.0;
		dst[3*i+0] = (JSAMPLE)(0.5+255.0*r);
		dst[3*i+1] = (JSAMPLE)(0.5+255.0*g);
		dst[3*i+2] = (JSAMPLE)(0.5+255.0*b);
	}
}

IW_IMPL(int) iw_read_jpeg_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	int retval=0;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	int cinfo_valid=0;
	int colorspace;
	JDIMENSION rownum;
	JSAMPLE *jsamprow;
	int numchannels=0;
	struct iw_image img;
	struct iwjpegrcontext rctx;
	JSAMPLE *tmprow = NULL;
	int cmyk_flag = 0;

	iw_zeromem(&img,sizeof(struct iw_image));
	iw_zeromem(&cinfo,sizeof(struct jpeg_decompress_struct));
	iw_zeromem(&jerr,sizeof(struct my_error_mgr));
	iw_zeromem(&rctx,sizeof(struct iwjpegrcontext));

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	jerr.pub.output_message = my_output_message;

	if (setjmp(jerr.setjmp_buffer)) {
		char buffer[JMSG_LENGTH_MAX];

		(*cinfo.err->format_message) ((j_common_ptr)&cinfo, buffer);

		iw_set_errorf(ctx,"libjpeg reports read error: %s",buffer);

		goto done;
	}

	jpeg_create_decompress(&cinfo);
	cinfo_valid=1;

	// Set up our custom source manager.
	rctx.pub.init_source = my_init_source_fn;
	rctx.pub.fill_input_buffer = my_fill_input_buffer_fn;
	rctx.pub.skip_input_data = my_skip_input_data_fn;
	rctx.pub.resync_to_restart = jpeg_resync_to_restart; // libjpeg default
	rctx.pub.term_source = my_term_source_fn;
	rctx.ctx = ctx;
	rctx.iodescr = iodescr;
	rctx.buffer_len = 32768;
	rctx.buffer = iw_malloc(ctx, rctx.buffer_len);
	if(!rctx.buffer) goto done;
	rctx.exif_density_x = -1.0;
	rctx.exif_density_y = -1.0;
	cinfo.src = (struct jpeg_source_mgr*)&rctx;

	// The lazy way. It would be more efficient to use
	// jpeg_set_marker_processor(), instead of saving everything to memory.
	// But libjpeg's marker processing functions have fairly complex
	// requirements.
	jpeg_save_markers(&cinfo, 0xe1, 65535);

	jpeg_read_header(&cinfo, TRUE);

	rctx.is_jfif = cinfo.saw_JFIF_marker;

	iwjpeg_read_density(ctx,&img,&cinfo);

	iwjpeg_read_saved_markers(&rctx,&cinfo);

	jpeg_start_decompress(&cinfo);

	colorspace=cinfo.out_color_space;
	numchannels=cinfo.output_components;

	// libjpeg will automatically convert YCbCr images to RGB, and YCCK images
	// to CMYK. That leaves GRAYSCALE, RGB, and CMYK for us to handle.
	// Note: cinfo.jpeg_color_space is the colorspace before conversion, and
	// cinfo.out_color_space is the colorspace after conversion.

	if(colorspace==JCS_GRAYSCALE && numchannels==1) {
		img.imgtype = IW_IMGTYPE_GRAY;
		img.native_grayscale = 1;
	}
	else if((colorspace==JCS_RGB) && numchannels==3) {
		img.imgtype = IW_IMGTYPE_RGB;
	}
	else if((colorspace==JCS_CMYK) && numchannels==4) {
		img.imgtype = IW_IMGTYPE_RGB;
		cmyk_flag = 1;
	}
	else {
		iw_set_error(ctx,"Unsupported type of JPEG");
		goto done;
	}

	img.width = cinfo.output_width;
	img.height = cinfo.output_height;
	if(!iw_check_image_dimensions(ctx,img.width,img.height)) {
		goto done;
	}

	img.bit_depth = 8;
	img.bpr = iw_calc_bytesperrow(img.width,img.bit_depth*numchannels);

	img.pixels = (iw_byte*)iw_malloc_large(ctx, img.bpr, img.height);
	if(!img.pixels) {
		goto done;
	}

	if(cmyk_flag) {
		tmprow = iw_malloc(ctx,4*img.width);
		if(!tmprow) goto done;
	}

	while(cinfo.output_scanline < cinfo.output_height) {
		rownum=cinfo.output_scanline;
		jsamprow = &img.pixels[img.bpr * rownum];
		if(cmyk_flag) {
			// read into tmprow, then convert and copy to img.pixels
			jpeg_read_scanlines(&cinfo, &tmprow, 1);
			convert_cmyk_to_rbg(ctx,tmprow,jsamprow,img.width);
		}
		else {
			// read directly into img.pixels
			jpeg_read_scanlines(&cinfo, &jsamprow, 1);
		}
		if(cinfo.output_scanline<=rownum) {
			iw_set_error(ctx,"Error reading JPEG file");
			goto done;
		}
	}
	jpeg_finish_decompress(&cinfo);

	handle_exif_density(&rctx, &img);

	iw_set_input_image(ctx, &img);
	// The contents of img no longer belong to us.
	img.pixels = NULL;

	if(rctx.exif_orientation>=2 && rctx.exif_orientation<=8) {
		static const unsigned int exif_orient_to_transform[9] =
		   { 0,0, 1,3,2,4,5,7,6 };

		// An Exif marker indicated an unusual image orientation.

		if(rctx.is_jfif) {
			// The presence of a JFIF marker implies a particular orientation.
			// If there's also an Exif marker that says something different,
			// I'm not sure what we're supposed to do.
			iw_warning(ctx,"JPEG image has an ambiguous orientation");
		}
		iw_reorient_image(ctx,exif_orient_to_transform[rctx.exif_orientation]);
	}

	retval=1;

done:
	iw_free(ctx, img.pixels);
	if(cinfo_valid) jpeg_destroy_decompress(&cinfo);
	if(rctx.buffer) iw_free(ctx,rctx.buffer);
	if(tmprow) iw_free(ctx,tmprow);
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

IW_IMPL(int) iw_write_jpeg_file(struct iw_context *ctx,  struct iw_iodescr *iodescr)
{
	int retval=0;
	struct jpeg_compress_struct cinfo;
	struct my_error_mgr jerr;
	J_COLOR_SPACE in_colortype; // Color type of the data we give to libjpeg
	int jpeg_cmpts;
	int compress_created = 0;
	int compress_started = 0;
	JSAMPROW *row_pointers = NULL;
	int is_grayscale;
	int j;
	struct iw_image img;
	int jpeg_quality;
	int samp_factor_h, samp_factor_v;
	int disable_subsampling = 0;
	struct iwjpegwcontext wctx;
	const char *optv;
	int ret;

	iw_zeromem(&cinfo,sizeof(struct jpeg_compress_struct));
	iw_zeromem(&jerr,sizeof(struct my_error_mgr));
	iw_zeromem(&wctx,sizeof(struct iwjpegwcontext));

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

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;

	if (setjmp(jerr.setjmp_buffer)) {
		char buffer[JMSG_LENGTH_MAX];

		(*cinfo.err->format_message) ((j_common_ptr)&cinfo, buffer);

		iw_set_errorf(ctx,"libjpeg reports write error: %s",buffer);

		goto done;
	}

	jpeg_create_compress(&cinfo);
	compress_created=1;

	// Set up our custom destination manager.
	wctx.pub.init_destination = my_init_destination_fn;
	wctx.pub.empty_output_buffer = my_empty_output_buffer_fn;
	wctx.pub.term_destination = my_term_destination_fn;
	wctx.ctx = ctx;
	wctx.iodescr = iodescr;
	wctx.buffer_len = 32768;
	wctx.buffer = iw_malloc(ctx,wctx.buffer_len);
	if(!wctx.buffer) goto done;
	// Our wctx is organized so it can double as a
	// 'struct jpeg_destination_mgr'.
	cinfo.dest = (struct jpeg_destination_mgr*)&wctx;

	cinfo.image_width = img.width;
	cinfo.image_height = img.height;
	cinfo.input_components = jpeg_cmpts;
	cinfo.in_color_space = in_colortype;

	jpeg_set_defaults(&cinfo);

	optv = iw_get_option(ctx, "jpeg:block");
	if(optv) {
#if (JPEG_LIB_VERSION_MAJOR>=9 || \
	(JPEG_LIB_VERSION_MAJOR==8 && JPEG_LIB_VERSION_MINOR>=3))
		// Note: This might not work if DCT_SCALING_SUPPORTED was not defined when
		// libjpeg was compiled, but that symbol is not normally exposed to
		// applications.
		cinfo.block_size = iw_parse_int(optv);
#else
		iw_warning(ctx, "Setting block size is not supported by this version of libjpeg");
#endif
	}

	optv = iw_get_option(ctx, "jpeg:arith");
	if(optv)
		cinfo.arith_code = iw_parse_int(optv) ? TRUE : FALSE;
	else
		cinfo.arith_code = FALSE;

	optv = iw_get_option(ctx, "jpeg:colortype");
	if(optv) {
		if(!strcmp(optv, "rgb")) {
			if(in_colortype==JCS_RGB) {
				jpeg_set_colorspace(&cinfo,JCS_RGB);
				disable_subsampling = 1;
			}
		}
		else if(!strcmp(optv, "rgb1")) {
			if(in_colortype==JCS_RGB) {
#if JPEG_LIB_VERSION_MAJOR >= 9
				cinfo.color_transform = JCT_SUBTRACT_GREEN;
#else
				iw_warning(ctx, "Color type rgb1 is not supported by this version of libjpeg");
#endif
				jpeg_set_colorspace(&cinfo,JCS_RGB);
				disable_subsampling = 1;
			}
		}
	}

	optv = iw_get_option(ctx, "jpeg:bgycc");
	if(optv && iw_parse_int(optv)) {
#if (JPEG_LIB_VERSION_MAJOR>9 || \
	(JPEG_LIB_VERSION_MAJOR==9 && JPEG_LIB_VERSION_MINOR>=1))
		jpeg_set_colorspace(&cinfo, JCS_BG_YCC);
#else
		iw_warning(ctx, "Big gamut YCC is not supported by this version of libjpeg");
#endif
	}

	iwjpg_set_density(ctx,&cinfo,&img);

	optv = iw_get_option(ctx, "jpeg:quality");
	if(optv)
		jpeg_quality = iw_parse_int(optv);
	else
		jpeg_quality = 0;

	if(jpeg_quality>0) {
		jpeg_set_quality(&cinfo,jpeg_quality,0);
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

	if(row_pointers) iw_free(ctx,row_pointers);

	if(wctx.buffer) iw_free(ctx,wctx.buffer);

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
