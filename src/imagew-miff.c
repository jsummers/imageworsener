// imagew-miff.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

enum iwmiff_string {
	iws_miff_internal_bad_type=1,
	iws_miff_unsupp_class,
	iws_miff_unsupp_colorspace,
	iws_miff_unsupp_depth,
	iws_miff_unsupp_compression,
	iws_miff_unsupp_sampleformat
};

struct iw_stringtableentry iwmiff_stringtable[] = {
	{ iws_miff_internal_bad_type, "Internal: Bad image type for MIFF" },
	{ iws_miff_unsupp_class, "MIFF: Unsupported image class" },
	{ iws_miff_unsupp_colorspace, "MIFF: Unsupported colorspace" },
	{ iws_miff_unsupp_depth, "MIFF: Unsupported bit depth" },
	{ iws_miff_unsupp_compression, "MIFF: Unsupported compression" },
	{ iws_miff_unsupp_sampleformat, "MIFF: Unsupported sample format" },
	{ 0, NULL }
};

static const char *iwmiff_get_string(struct iw_context *ctx, int n)
{
	return iw_get_string(ctx,IW_STRINGTABLENUM_MIFF,n);
}

struct iwmiffreadcontext {
	int host_little_endian;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	int read_error_flag;
	int error_flag;
	int has_alpha;
	int is_grayscale;
	struct iw_csdescr csdescr;
};

static int iwmiff_read(struct iwmiffreadcontext *rctx,
		unsigned char *buf, size_t buflen)
{
	int ret;
	size_t bytesread = 0;

	ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
		buf,buflen,&bytesread);
	if(!ret || bytesread!=buflen) {
		rctx->read_error_flag=1;
		return 0;
	}
	return 1;
}

static unsigned char iwmiff_read_byte(struct iwmiffreadcontext *rctx)
{
	unsigned char buf[1];
	int ret;
	size_t bytesread = 0;

	// TODO: buffering

	ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
		buf,1,&bytesread);
	if(!ret || bytesread!=1) {
		rctx->read_error_flag=1;
		return '\0';
	}
	return buf[0];
}

// Called for each attribute in the header of a MIFF file.
static void iwmiff_found_attribute(struct iwmiffreadcontext *rctx,
  const char *name, const char *val)
{
	double tmpd;

	if(rctx->error_flag) return;

	if(!strcmp(name,"matte")) {
		if(val[0]=='T') rctx->has_alpha = 1;
	}
	else if(!strcmp(name,"class")) {
		if(strcmp(val,"DirectClass")) {
			iw_seterror(rctx->ctx,iwmiff_get_string(rctx->ctx,iws_miff_unsupp_class));
			rctx->error_flag=1;
		}
	}
	else if(!strcmp(name,"columns")) {
		rctx->img->width = atoi(val);
	}
	else if(!strcmp(name,"rows")) {
		rctx->img->height = atoi(val);
	}
	else if(!strcmp(name,"colorspace")) {
		if(!strcmp(val,"RGB")) {
			;
		}
		else if(!strcmp(val,"Gray")) {
			rctx->is_grayscale = 1;
		}
		else {
			iw_seterror(rctx->ctx,iwmiff_get_string(rctx->ctx,iws_miff_unsupp_colorspace));
			rctx->error_flag=1;
		}
	}
	else if(!strcmp(name,"depth")) {
		rctx->img->bit_depth = atoi(val);
		if(rctx->img->bit_depth!=32 && rctx->img->bit_depth!=64) {
			iw_seterror(rctx->ctx,iwmiff_get_string(rctx->ctx,iws_miff_unsupp_depth));
			rctx->error_flag=1;
		}
	}
	else if(!strcmp(name,"compression")) {
		if(strcmp(val,"None")) {
			iw_seterror(rctx->ctx,iwmiff_get_string(rctx->ctx,iws_miff_unsupp_compression));
			rctx->error_flag=1;
		}
	}
	else if(!strcmp(name,"gamma")) {
		tmpd =  atof(val);
		if(tmpd>=0.00001 && tmpd<=10.0) {
			rctx->csdescr.cstype = IW_CSTYPE_GAMMA;
			rctx->csdescr.gamma = 1.0/tmpd;
		}
	}
	else if(!strcmp(name,"quantum")) {
		if(strcmp(val,"floating-point")) {
			iw_seterror(rctx->ctx,iwmiff_get_string(rctx->ctx,iws_miff_unsupp_sampleformat));
			rctx->error_flag=1;
		}
	}
}

static int iwmiff_read_header(struct iwmiffreadcontext *rctx)
{
	char name[101];
	char val[101];
	int namelen;
	int vallen;
	char b;
	int is_whitespace;

#define STATE_NEUTRAL 0
#define STATE_READING_NAME 1
#define STATE_READING_VALUE 2
#define STATE_READING_VALUE_INQUOTE 3
	int st;

	name[0]='\0'; namelen=0;
	val[0]='\0'; vallen=0;

	st=STATE_NEUTRAL;

	while(1) {
		if(rctx->error_flag) return 0;

		b=(char)iwmiff_read_byte(rctx);
		if(rctx->read_error_flag) {
			return 0;
		}

		is_whitespace = (b>=0 && b<=32);

		if(st==STATE_NEUTRAL) {
			if(is_whitespace) continue;
			st=STATE_READING_NAME;
		}

		if(st==STATE_READING_NAME) {
			if(namelen==0 && b==':') {
				// End of header section
				st=STATE_NEUTRAL;
				break;
			}

			if(b=='=') {
				st=STATE_READING_VALUE;
				continue;
			}

			// Append char to name
			if(namelen<100) {
				name[namelen]=b;
				namelen++;
				name[namelen]='\0';
			}
		}

		if(st==STATE_READING_VALUE) {
			if(vallen==0 && b=='{') {
				st=STATE_READING_VALUE_INQUOTE;
				continue;
			}
		}

		if( (st==STATE_READING_VALUE && is_whitespace) ||
			(st==STATE_READING_VALUE_INQUOTE && b=='}') )
		{
			// Unquoted value terminated by whitespace, or
			// quoted value terminated by '}'.
			iwmiff_found_attribute(rctx,name,val);
			name[0]='\0'; namelen=0;
			val[0]='\0'; vallen=0;
			st=STATE_NEUTRAL;
			continue;
		}

		if(st==STATE_READING_VALUE || st==STATE_READING_VALUE_INQUOTE) {
			// Append char to val
			if(vallen<100) {
				val[vallen]=b;
				vallen++;
				val[vallen]='\0';
			}
			continue;
		}
	}

	// Skip the byte after the ":", which is usually Ctrl-Z.
	(void)iwmiff_read_byte(rctx);
	if(rctx->read_error_flag || rctx->error_flag) {
		return 0;
	}
	return 1;
}

static void iwmiffr_convert_row32(struct iwmiffreadcontext *rctx,
  const unsigned char *src, unsigned char *dst, int nsamples)
{
	int i;
	int k;

	if(rctx->host_little_endian) {
		for(i=0;i<nsamples;i++) {
			for(k=0;k<4;k++) {
				dst[i*4+k] = src[i*4+3-k];
			}
		}
	}
	else {
		memcpy(dst,src,4*nsamples);
	}
}

static void iwmiffr_convert_row64(struct iwmiffreadcontext *rctx,
  const unsigned char *src, unsigned char *dst, int nsamples)
{
	int i;
	int k;

	if(rctx->host_little_endian) {
		for(i=0;i<nsamples;i++) {
			for(k=0;k<8;k++) {
				dst[i*8+k] = src[i*8+7-k];
			}
		}
	}
	else {
		memcpy(dst,src,8*nsamples);
	}
}

static int iwmiff_read_pixels(struct iwmiffreadcontext *rctx)
{
	int samples_per_pixel_used;
	int samples_per_pixel_alloc;
	int samples_per_row_used;
	int samples_per_row_alloc;
	size_t tmprowsize;
	unsigned char *tmprow = NULL;
	int retval=0;
	int j;
	struct iw_image *img;

	img = rctx->img;

	samples_per_pixel_used = iw_imgtype_num_channels(img->imgtype);

	samples_per_pixel_alloc = rctx->has_alpha ? 4 : 3;

	samples_per_row_used = samples_per_pixel_used * img->width;
	samples_per_row_alloc = samples_per_pixel_alloc * img->width;

	tmprowsize = (img->bit_depth/8)*samples_per_row_alloc;
	tmprow = iw_malloc(rctx->ctx,tmprowsize);
	if(!tmprow) goto done;
	memset(tmprow,0,tmprowsize);

	img->bpr = tmprowsize;

	img->pixels = (unsigned char*)iw_malloc_large(rctx->ctx, img->bpr, img->height);
	if(!img->pixels) goto done;

	for(j=0;j<img->height;j++) {
		if(!iwmiff_read(rctx,tmprow,tmprowsize))
			goto done;

		if(img->bit_depth==32) {
			iwmiffr_convert_row32(rctx,tmprow,&img->pixels[j*img->bpr],samples_per_row_used);
		}
		else if(img->bit_depth==64) {
			iwmiffr_convert_row64(rctx,tmprow,&img->pixels[j*img->bpr],samples_per_row_used);
		}
	}

	retval=1;

done:
	if(tmprow) free(tmprow);
	return retval;
}

int iw_read_miff_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iw_image img;
	struct iwmiffreadcontext rctx;
	int retval=0;

	memset(&rctx,0,sizeof(struct iwmiffreadcontext));
	memset(&img,0,sizeof(struct iw_image));

	rctx.ctx = ctx;
	rctx.host_little_endian = iw_get_host_endianness();
	rctx.iodescr = iodescr;
	rctx.img = &img;

	// Assume unlabeled images are sRGB
	rctx.csdescr.cstype = IW_CSTYPE_SRGB;
	rctx.csdescr.sRGB_intent = IW_sRGB_INTENT_PERCEPTUAL;

	iw_set_string_table(ctx,IW_STRINGTABLENUM_MIFF,iwmiff_stringtable);

	img.sampletype = IW_SAMPLETYPE_FLOATINGPOINT;

	if(!iwmiff_read_header(&rctx)) {
		goto done;
	}

	if(rctx.is_grayscale) {
		if(rctx.has_alpha)
			img.imgtype = IW_IMGTYPE_GRAYA;
		else
			img.imgtype = IW_IMGTYPE_GRAY;
	}
	else {
		if(rctx.has_alpha)
			img.imgtype = IW_IMGTYPE_RGBA;
		else
			img.imgtype = IW_IMGTYPE_RGB;
	}

	if(!iwmiff_read_pixels(&rctx)) {
		goto done;
	}

	iw_set_input_image(ctx, &img);

	iw_set_input_colorspace(ctx,&rctx.csdescr);

	retval = 1;

done:
	if(!retval) {
		iw_seterror(ctx,"Failed to read MIFF file");
	}

	if(iodescr->close_fn)
		(*iodescr->close_fn)(ctx,iodescr);
	return retval;
}

struct iwmiffwritecontext {
	int has_alpha;
	int host_little_endian;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
};

static void iwmiff_write(struct iwmiffwritecontext *wctx, const void *buf, size_t n)
{
	(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,buf,n);
}

static void iwmiff_write_sz(struct iwmiffwritecontext *wctx, const char *s)
{
	iwmiff_write(wctx,s,strlen(s));
}

static void iwmiff_writef(struct iwmiffwritecontext *wctx, const char *fmt, ...)
{
	char buf[500];
	va_list ap;

	va_start(ap, fmt);
	iw_vsnprintf(buf,sizeof(buf),fmt,ap);
	va_end(ap);

	iwmiff_write_sz(wctx,buf);
}

static void iwmiff_write_header(struct iwmiffwritecontext *wctx)
{
	iwmiff_write_sz(wctx,"id=ImageMagick  version=1.0\n");
	iwmiff_writef(wctx,"class=DirectClass  colors=0  matte=%s\n",wctx->has_alpha?"True":"False");
	iwmiff_writef(wctx,"columns=%d  rows=%d  depth=%d\n",wctx->img->width,
		wctx->img->height,wctx->img->bit_depth);

	if(wctx->img->imgtype==IW_IMGTYPE_GRAY) {
		iwmiff_write_sz(wctx,"type=Grayscale\ncolorspace=Gray\n");
	}
	else if(wctx->img->imgtype==IW_IMGTYPE_GRAYA) {
		iwmiff_write_sz(wctx,"type=GrayscaleMatte\ncolorspace=Gray\n");
	}
	else {
		iwmiff_write_sz(wctx,"colorspace=RGB\n");
	}

	iwmiff_write_sz(wctx,"compression=None  quality=0\n");
	//units=PixelsPerCentimeter
	//resolution=28.35x28.35
	//page=1x1+0+0
	//rendering-intent=Perceptual
	iwmiff_write_sz(wctx,"gamma=1.0\n");
	iwmiff_write_sz(wctx,"quantum:format={floating-point}\n");

	iwmiff_write(wctx,"\x0c\x0a\x3a\x1a",4);
}

static void iwmiffw_convert_row32(struct iwmiffwritecontext *wctx,
	const unsigned char *srcrow, unsigned char *dstrow, int numsamples)
{
	int i,j;

	// The MIFF format is barely documented, but apparently it uses
	// big-endian byte order.
	if(wctx->host_little_endian) {
		for(i=0;i<numsamples;i++) {
			for(j=0;j<4;j++) {
				dstrow[i*4+j] = srcrow[i*4+3-j];
			}
		}
	}
	else {
		memcpy(dstrow,srcrow,4*numsamples);
	}
}

static void iwmiffw_convert_row64(struct iwmiffwritecontext *wctx,
	const unsigned char *srcrow, unsigned char *dstrow, int numsamples)
{
	int i,j;

	if(wctx->host_little_endian) {
		for(i=0;i<numsamples;i++) {
			for(j=0;j<8;j++) {
				dstrow[i*8+j] = srcrow[i*8+7-j];
			}
		}
	}
	else {
		memcpy(dstrow,srcrow,8*numsamples);
	}
}

static int iwmiff_write_main(struct iwmiffwritecontext *wctx)
{
	struct iw_image *img;
	unsigned char *dstrow = NULL;
	size_t dstbpr;
	int j;
	const unsigned char *srcrow;
	int bytes_per_sample;
	int num_channels_alloc;
	int num_channels_used;

	img = wctx->img;

	if(img->sampletype!=IW_SAMPLETYPE_FLOATINGPOINT) {
		iw_seterror(wctx->ctx,iwmiff_get_string(wctx->ctx,iws_miff_internal_bad_type));
		goto done;
	}

	switch(img->imgtype) {
	case IW_IMGTYPE_GRAY:
		num_channels_used=1;
		break;
	case IW_IMGTYPE_GRAYA:
		wctx->has_alpha=1;
		num_channels_used=2;
		break;
	case IW_IMGTYPE_RGB:
		num_channels_used=3;
		break;
	case IW_IMGTYPE_RGBA:
		wctx->has_alpha=1;
		num_channels_used=4;
		break;
	default:
		goto done;
	}

	num_channels_alloc = wctx->has_alpha ? 4 : 3;

	bytes_per_sample = img->bit_depth / 8;

	// dstbpr = number of bytes per row stored in the file.
	// For grayscale images, not all bytes are used. Extra bytes will be stored
	// in the file with the value 0.
	dstbpr = bytes_per_sample * num_channels_alloc * img->width;

	iwmiff_write_header(wctx);


	dstrow = iw_malloc(wctx->ctx,dstbpr);
	if(!dstrow) goto done;
	memset(dstrow,0,dstbpr);

	for(j=0;j<img->height;j++) {
		srcrow = &img->pixels[j*img->bpr];
		switch(img->bit_depth) {
		case 32: iwmiffw_convert_row32(wctx,srcrow,dstrow,img->width*num_channels_used); break;
		case 64: iwmiffw_convert_row64(wctx,srcrow,dstrow,img->width*num_channels_used); break;
		}
		iwmiff_write(wctx,dstrow,dstbpr);
	}

done:
	if(dstrow) iw_free(dstrow);
	return 1;
}

int iw_write_miff_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwmiffwritecontext wctx;
	int retval=0;
	struct iw_image img1;

	iw_set_string_table(ctx,IW_STRINGTABLENUM_MIFF,iwmiff_stringtable);

	memset(&img1,0,sizeof(struct iw_image));

	memset(&wctx,0,sizeof(struct iwmiffwritecontext));

	wctx.ctx = ctx;

	wctx.iodescr=iodescr;

	wctx.host_little_endian=iw_get_host_endianness();

	iw_get_output_image(ctx,&img1);
	wctx.img = &img1;

	iwmiff_write_main(&wctx);

	retval=1;

//done:
	if(wctx.iodescr->close_fn)
		(*wctx.iodescr->close_fn)(ctx,wctx.iodescr);
	return retval;
}
