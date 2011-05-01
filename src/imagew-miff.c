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

struct iwmiffwritecontext {
	int has_alpha;
	int host_little_endian;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
};

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
	iwmiff_write_sz(miffctx,"colorspace=RGB\n");
	iwmiff_write_sz(miffctx,"compression=None  quality=0\n");
	//units=PixelsPerCentimeter
	//resolution=28.35x28.35
	//page=1x1+0+0
	//rendering-intent=Perceptual
	iwmiff_write_sz(miffctx,"gamma=1.0\n");
	iwmiff_write_sz(miffctx,"quantum:format={floating-point}\n");

	iwmiff_write(miffctx,"\x0c\x0a\x3a\x1a",4);
}

static void iwmiff_convert_row32(struct iwmiffwritecontext *miffctx,
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

static void iwmiff_convert_row64(struct iwmiffwritecontext *miffctx,
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
	int bytes_per_pixel;
	int num_channels;

	img = miffctx->img;

	if(img->sampletype!=IW_SAMPLETYPE_FLOATINGPOINT) {
		iw_seterror(miffctx->ctx,iwmiff_get_string(miffctx->ctx,iws_miff_internal_bad_type));
		goto done;
	}

	switch(img->imgtype) {
	case IW_IMGTYPE_RGB:
		num_channels=3;
		break;
	case IW_IMGTYPE_RGBA:
		miffctx->has_alpha=1;
		num_channels=4;
		break;
	default:
		goto done;
	}

	bytes_per_sample = img->bit_depth / 8;
	bytes_per_pixel = bytes_per_sample * num_channels;

	dstbpr = bytes_per_pixel*img->width;

	iwmiff_write_miff_header(miffctx);


	dstrow = iw_malloc(miffctx->ctx,dstbpr);
	if(!dstrow) goto done;
	memset(dstrow,0,dstbpr);

	for(j=0;j<img->height;j++) {
		srcrow = &img->pixels[j*img->bpr];
		switch(img->bit_depth) {
		case 32: iwmiff_convert_row32(miffctx,srcrow,dstrow,img->width*num_channels); break;
		case 64: iwmiff_convert_row64(miffctx,srcrow,dstrow,img->width*num_channels); break;
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
	union en_union {
		unsigned char c[4];
		int ii;
	} en;

	iw_set_string_table(ctx,IW_STRINGTABLENUM_MIFF,iwmiff_stringtable);

	memset(&img1,0,sizeof(struct iw_image));

	memset(&miffctx,0,sizeof(struct iwmiffwritecontext));

	miffctx.ctx = ctx;

	miffctx.iodescr=iodescr;

	// Test the host's endianness.
	en.c[0]=0;
	en.ii = 1;
	if(en.c[0]!=0) {
		miffctx.host_little_endian=1;
	}

	iw_get_output_image(ctx,&img1);
	miffctx.img = &img1;

	iwmiff_write_main(&miffctx);

	retval=1;

//done:
	if(miffctx.iodescr->close_fn)
		(*miffctx.iodescr->close_fn)(ctx,miffctx.iodescr);
	return retval;
}
