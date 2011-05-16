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
	iws_miff_internal_bad_type=1
};

struct iw_stringtableentry iwmiff_stringtable[] = {
	{ iws_miff_internal_bad_type, "Internal: Bad image type for MIFF" },
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
	int has_alpha;
	int is_grayscale;
};

static int iwmiff_read(struct iwmiffreadcontext *miffreadctx,
		unsigned char *buf, size_t buflen)
{
	int ret;
	size_t bytesread = 0;

	ret = (*miffreadctx->iodescr->read_fn)(miffreadctx->ctx,miffreadctx->iodescr,
		buf,buflen,&bytesread);
	if(!ret || bytesread!=buflen) {
		miffreadctx->read_error_flag=1;
		return 0;
	}
	return 1;
}

static unsigned char iwmiff_read_byte(struct iwmiffreadcontext *miffreadctx)
{
	unsigned char buf[1];
	int ret;
	size_t bytesread = 0;

	// TODO: buffering

	ret = (*miffreadctx->iodescr->read_fn)(miffreadctx->ctx,miffreadctx->iodescr,
		buf,1,&bytesread);
	if(!ret || bytesread!=1) {
		miffreadctx->read_error_flag=1;
		return '\0';
	}
	return buf[0];
}

// Called for each attribute in the header of a MIFF file.
static void iwmiff_found_attribute(struct iwmiffreadcontext *miffreadctx,
  const char *name, const char *val)
{
	if(!strcmp(name,"matte")) {
		if(val[0]=='T') miffreadctx->has_alpha = 1;
	}
	//else if(!strcmp(name,"class")) {
	//}
	else if(!strcmp(name,"columns")) {
		miffreadctx->img->width = atoi(val);
	}
	else if(!strcmp(name,"rows")) {
		miffreadctx->img->height = atoi(val);
	}
	else if(!strcmp(name,"colorspace")) {
		if(val[0]=='G') miffreadctx->is_grayscale = 1;
	}
	else if(!strcmp(name,"depth")) {
		miffreadctx->img->bit_depth = atoi(val);
	}
	//else if(!strcmp(name,"compression")) {
	//}
	//else if(!strcmp(name,"gamma")) {
	//}
	//else if(!strcmp(name,"quantum")) {
	//}
}

//static void iwmiff_append_char(
static int read_miff_header(struct iwmiffreadcontext *miffreadctx)
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
		b=(char)iwmiff_read_byte(miffreadctx);
		if(miffreadctx->read_error_flag) {
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
			iwmiff_found_attribute(miffreadctx,name,val);
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
	(void)iwmiff_read_byte(miffreadctx);
	if(miffreadctx->read_error_flag) {
		return 0;
	}
	return 1;
}

static void iwmiffread_convert_row32(struct iwmiffreadcontext *miffreadctx,
  const unsigned char *src, unsigned char *dst, int nsamples)
{
	int i;
	int k;

	if(miffreadctx->host_little_endian) {
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

static void iwmiffread_convert_row64(struct iwmiffreadcontext *miffreadctx,
  const unsigned char *src, unsigned char *dst, int nsamples)
{
	int i;
	int k;

	if(miffreadctx->host_little_endian) {
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

static int read_miff_bits(struct iwmiffreadcontext *miffreadctx)
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

	img = miffreadctx->img;

	samples_per_pixel_used = iw_imgtype_num_channels(img->imgtype);

	samples_per_pixel_alloc = miffreadctx->has_alpha ? 4 : 3;

	samples_per_row_used = samples_per_pixel_used * img->width;
	samples_per_row_alloc = samples_per_pixel_alloc * img->width;

	tmprowsize = (img->bit_depth/8)*samples_per_row_alloc;
	tmprow = iw_malloc(miffreadctx->ctx,tmprowsize);
	if(!tmprow) goto done;
	memset(tmprow,0,tmprowsize);

	img->bpr = tmprowsize;

	img->pixels = (unsigned char*)iw_malloc_large(miffreadctx->ctx, img->bpr, img->height);
	if(!img->pixels) goto done;

	for(j=0;j<img->height;j++) {
		if(!iwmiff_read(miffreadctx,tmprow,tmprowsize))
			goto done;

		if(img->bit_depth==32) {
			iwmiffread_convert_row32(miffreadctx,tmprow,&img->pixels[j*img->bpr],samples_per_row_used);
		}
		else if(img->bit_depth==64) {
			iwmiffread_convert_row64(miffreadctx,tmprow,&img->pixels[j*img->bpr],samples_per_row_used);
		}
	}

	retval=1;

done:
	if(tmprow) free(tmprow);
	return retval;
}

// TODO: This MIFF decoder is very quick and dirty.
// By design, it only supports a small subset of MIFF files, but it needs
// a lot of error checking added, so that it will fail gracefully when it
// encounters a file that it can't read.
int iw_read_miff_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iw_image img;
	struct iwmiffreadcontext miffreadctx;
	int retval=0;
	struct iw_csdescr csdescr;

	memset(&miffreadctx,0,sizeof(struct iwmiffreadcontext));
	memset(&img,0,sizeof(struct iw_image));

	miffreadctx.ctx = ctx;
	miffreadctx.host_little_endian = iw_get_host_endianness();
	miffreadctx.iodescr = iodescr;
	miffreadctx.img = &img;

	iw_set_string_table(ctx,IW_STRINGTABLENUM_MIFF,iwmiff_stringtable);

	img.sampletype = IW_SAMPLETYPE_FLOATINGPOINT;

	if(!read_miff_header(&miffreadctx)) {
		goto done;
	}

	if(miffreadctx.is_grayscale) {
		if(miffreadctx.has_alpha)
			img.imgtype = IW_IMGTYPE_GRAYA;
		else
			img.imgtype = IW_IMGTYPE_GRAY;
	}
	else {
		if(miffreadctx.has_alpha)
			img.imgtype = IW_IMGTYPE_RGBA;
		else
			img.imgtype = IW_IMGTYPE_RGB;
	}

	if(!read_miff_bits(&miffreadctx)) {
		goto done;
	}

	iw_set_input_image(ctx, &img);

	memset(&csdescr,0,sizeof(struct iw_csdescr));
	csdescr.cstype = IW_CSTYPE_LINEAR;
	iw_set_input_colorspace(ctx,&csdescr);

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

static void iwmiff_write(struct iwmiffwritecontext *miffctx, const void *buf, size_t n)
{
	(*miffctx->iodescr->write_fn)(miffctx->ctx,miffctx->iodescr,buf,n);
}

static void iwmiff_write_sz(struct iwmiffwritecontext *miffctx, const char *s)
{
	iwmiff_write(miffctx,s,strlen(s));
}

static void iwmiff_writef(struct iwmiffwritecontext *miffctx, const char *fmt, ...)
{
	char buf[500];
	va_list ap;

	va_start(ap, fmt);
	iw_vsnprintf(buf,sizeof(buf),fmt,ap);
	va_end(ap);

	iwmiff_write_sz(miffctx,buf);
}

static void iwmiff_write_miff_header(struct iwmiffwritecontext *miffctx)
{
	iwmiff_write_sz(miffctx,"id=ImageMagick  version=1.0\n");
	iwmiff_writef(miffctx,"class=DirectClass  colors=0  matte=%s\n",miffctx->has_alpha?"True":"False");
	iwmiff_writef(miffctx,"columns=%d  rows=%d  depth=%d\n",miffctx->img->width,
		miffctx->img->height,miffctx->img->bit_depth);

	if(miffctx->img->imgtype==IW_IMGTYPE_GRAY) {
		iwmiff_write_sz(miffctx,"type=Grayscale\ncolorspace=Gray\n");
	}
	else if(miffctx->img->imgtype==IW_IMGTYPE_GRAYA) {
		iwmiff_write_sz(miffctx,"type=GrayscaleMatte\ncolorspace=Gray\n");
	}
	else {
		iwmiff_write_sz(miffctx,"colorspace=RGB\n");
	}

	iwmiff_write_sz(miffctx,"compression=None  quality=0\n");
	//units=PixelsPerCentimeter
	//resolution=28.35x28.35
	//page=1x1+0+0
	//rendering-intent=Perceptual
	iwmiff_write_sz(miffctx,"gamma=1.0\n");
	iwmiff_write_sz(miffctx,"quantum:format={floating-point}\n");

	iwmiff_write(miffctx,"\x0c\x0a\x3a\x1a",4);
}

static void iwmiffwrite_convert_row32(struct iwmiffwritecontext *miffctx,
	const unsigned char *srcrow, unsigned char *dstrow, int numsamples)
{
	int i,j;

	// The MIFF format is barely documented, but apparently it uses
	// big-endian byte order.
	if(miffctx->host_little_endian) {
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

static void iwmiffwrite_convert_row64(struct iwmiffwritecontext *miffctx,
	const unsigned char *srcrow, unsigned char *dstrow, int numsamples)
{
	int i,j;

	if(miffctx->host_little_endian) {
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

static int iwmiff_write_main(struct iwmiffwritecontext *miffctx)
{
	struct iw_image *img;
	unsigned char *dstrow = NULL;
	size_t dstbpr;
	int j;
	const unsigned char *srcrow;
	int bytes_per_sample;
	int num_channels_alloc;
	int num_channels_used;

	img = miffctx->img;

	if(img->sampletype!=IW_SAMPLETYPE_FLOATINGPOINT) {
		iw_seterror(miffctx->ctx,iwmiff_get_string(miffctx->ctx,iws_miff_internal_bad_type));
		goto done;
	}

	switch(img->imgtype) {
	case IW_IMGTYPE_GRAY:
		num_channels_used=1;
		break;
	case IW_IMGTYPE_GRAYA:
		miffctx->has_alpha=1;
		num_channels_used=2;
		break;
	case IW_IMGTYPE_RGB:
		num_channels_used=3;
		break;
	case IW_IMGTYPE_RGBA:
		miffctx->has_alpha=1;
		num_channels_used=4;
		break;
	default:
		goto done;
	}

	num_channels_alloc = miffctx->has_alpha ? 4 : 3;

	bytes_per_sample = img->bit_depth / 8;

	// dstbpr = number of bytes per row stored in the file.
	// For grayscale images, not all bytes are used. Extra bytes will be stored
	// in the file with the value 0.
	dstbpr = bytes_per_sample * num_channels_alloc * img->width;

	iwmiff_write_miff_header(miffctx);


	dstrow = iw_malloc(miffctx->ctx,dstbpr);
	if(!dstrow) goto done;
	memset(dstrow,0,dstbpr);

	for(j=0;j<img->height;j++) {
		srcrow = &img->pixels[j*img->bpr];
		switch(img->bit_depth) {
		case 32: iwmiffwrite_convert_row32(miffctx,srcrow,dstrow,img->width*num_channels_used); break;
		case 64: iwmiffwrite_convert_row64(miffctx,srcrow,dstrow,img->width*num_channels_used); break;
		}
		iwmiff_write(miffctx,dstrow,dstbpr);
	}

done:
	if(dstrow) iw_free(dstrow);
	return 1;
}

int iw_write_miff_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwmiffwritecontext miffctx;
	int retval=0;
	struct iw_image img1;

	iw_set_string_table(ctx,IW_STRINGTABLENUM_MIFF,iwmiff_stringtable);

	memset(&img1,0,sizeof(struct iw_image));

	memset(&miffctx,0,sizeof(struct iwmiffwritecontext));

	miffctx.ctx = ctx;

	miffctx.iodescr=iodescr;

	miffctx.host_little_endian=iw_get_host_endianness();

	iw_get_output_image(ctx,&img1);
	miffctx.img = &img1;

	iwmiff_write_main(&miffctx);

	retval=1;

//done:
	if(miffctx.iodescr->close_fn)
		(*miffctx.iodescr->close_fn)(ctx,miffctx.iodescr);
	return retval;
}
