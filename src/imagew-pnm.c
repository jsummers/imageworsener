// imagew-pnm.c
// Part of ImageWorsener, Copyright (c) 2013 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iwpnmcontext {
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
};

IW_IMPL(int) iw_read_pnm_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	iw_set_error(ctx,"PNM reading not implemented");
	return 0;
}

struct iwpnmwcontext {
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	const struct iw_palette *iwpal;
	iw_byte *rowbuf;
	int requested_output_format;
	int actual_output_format;
	int maxcolorcode;
};

static void iwpnm_write(struct iwpnmwcontext *wctx, const void *buf, size_t n)
{
	(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,buf,n);
}

static int iwpnm_write_ppm_main(struct iwpnmwcontext *wctx)
{
	struct iw_image *img;
	int retval = 0;
	int i,j;
	size_t outrowsize;
	char tmpstring[80];
	int bytes_per_ppm_pixel;

	img = wctx->img;

	if(img->bit_depth==8) {
		bytes_per_ppm_pixel=3;
	}
	else if(img->bit_depth==16) {
		bytes_per_ppm_pixel=6;
	}
	else {
		goto done;
	}

	outrowsize = bytes_per_ppm_pixel*img->width;
	wctx->rowbuf = iw_mallocz(wctx->ctx, outrowsize);
	if(!wctx->rowbuf) goto done;

	iw_snprintf(tmpstring, sizeof(tmpstring), "P6\n%d %d\n%d\n", img->width,
		img->height, wctx->maxcolorcode);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));

	for(j=0;j<img->height;j++) {
		for(i=0;i<img->width;i++) {
			if(img->imgtype==IW_IMGTYPE_RGB && img->bit_depth==8) {
				wctx->rowbuf[i*3+0] = img->pixels[j*img->bpr+i*3+0];
				wctx->rowbuf[i*3+1] = img->pixels[j*img->bpr+i*3+1];
				wctx->rowbuf[i*3+2] = img->pixels[j*img->bpr+i*3+2];
			}
			else if(img->imgtype==IW_IMGTYPE_GRAY && img->bit_depth==8) {
				wctx->rowbuf[i*3+0] = img->pixels[j*img->bpr+i];
				wctx->rowbuf[i*3+1] = img->pixels[j*img->bpr+i];
				wctx->rowbuf[i*3+2] = img->pixels[j*img->bpr+i];
			}
			else if(img->imgtype==IW_IMGTYPE_RGB && img->bit_depth==16) {
				wctx->rowbuf[i*6+0] = img->pixels[j*img->bpr+6*i+0];
				wctx->rowbuf[i*6+1] = img->pixels[j*img->bpr+6*i+1];
				wctx->rowbuf[i*6+2] = img->pixels[j*img->bpr+6*i+2];
				wctx->rowbuf[i*6+3] = img->pixels[j*img->bpr+6*i+3];
				wctx->rowbuf[i*6+4] = img->pixels[j*img->bpr+6*i+4];
				wctx->rowbuf[i*6+5] = img->pixels[j*img->bpr+6*i+5];
			}
			else if(img->imgtype==IW_IMGTYPE_GRAY && img->bit_depth==16) {
				wctx->rowbuf[i*6+0] = img->pixels[j*img->bpr+2*i+0];
				wctx->rowbuf[i*6+1] = img->pixels[j*img->bpr+2*i+1];
				wctx->rowbuf[i*6+2] = img->pixels[j*img->bpr+2*i+0];
				wctx->rowbuf[i*6+3] = img->pixels[j*img->bpr+2*i+1];
				wctx->rowbuf[i*6+4] = img->pixels[j*img->bpr+2*i+0];
				wctx->rowbuf[i*6+5] = img->pixels[j*img->bpr+2*i+1];
			}
		}
		iwpnm_write(wctx, wctx->rowbuf, outrowsize);
	}

	retval = 1;

done:
	return retval;
}

static int iwpnm_write_pgm_main(struct iwpnmwcontext *wctx)
{
	struct iw_image *img;
	int retval = 0;
	int i,j;
	size_t outrowsize;
	char tmpstring[80];
	int bytes_per_ppm_pixel;

	img = wctx->img;

	if(img->bit_depth==8) {
		bytes_per_ppm_pixel=1;
	}
	else if(img->bit_depth==16) {
		bytes_per_ppm_pixel=2;
	}
	else {
		goto done;
	}

	outrowsize = bytes_per_ppm_pixel*img->width;
	wctx->rowbuf = iw_mallocz(wctx->ctx, outrowsize);
	if(!wctx->rowbuf) goto done;

	iw_snprintf(tmpstring, sizeof(tmpstring), "P5\n%d %d\n%d\n", img->width,
		img->height, wctx->maxcolorcode);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));

	for(j=0;j<img->height;j++) {
		for(i=0;i<img->width;i++) {
			if(img->bit_depth==8) {
				wctx->rowbuf[i] = img->pixels[j*img->bpr+i];
			}
			else if(img->bit_depth==16) {
				wctx->rowbuf[i*2+0] = img->pixels[j*img->bpr+i*2+0];
				wctx->rowbuf[i*2+1] = img->pixels[j*img->bpr+i*2+1];
			}
		}
		iwpnm_write(wctx, wctx->rowbuf, outrowsize);
	}

	retval = 1;

done:
	return retval;
}

static int iwpnm_write_pbm_main(struct iwpnmwcontext *wctx)
{
	struct iw_image *img;
	int retval = 0;
	int i,j;
	size_t outrowsize;
	char tmpstring[80];

	img = wctx->img;


	outrowsize = (img->width+7)/8;
	wctx->rowbuf = iw_mallocz(wctx->ctx, outrowsize);
	if(!wctx->rowbuf) goto done;

	iw_snprintf(tmpstring, sizeof(tmpstring), "P4\n%d %d\n", img->width,
		img->height);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));

	for(j=0;j<img->height;j++) {
		memset(wctx->rowbuf, 0, outrowsize);
		for(i=0;i<img->width;i++) {
			if(img->pixels[j*img->bpr+i]==0) {
				wctx->rowbuf[i/8] |= 1<<(7-i%8);
			}
		}
		iwpnm_write(wctx, wctx->rowbuf, outrowsize);
	}

	retval = 1;

done:
	return retval;
}

IW_IMPL(int) iw_write_pnm_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwpnmwcontext *wctx = NULL;
	int retval=0;
	struct iw_image img1;
	int ret;

	iw_zeromem(&img1,sizeof(struct iw_image));

	wctx = iw_mallocz(ctx,sizeof(struct iwpnmwcontext));
	if(!wctx) goto done;

	wctx->ctx = ctx;
	wctx->iodescr=iodescr;

	iw_get_output_image(ctx,&img1);
	wctx->img = &img1;

	wctx->requested_output_format = iw_get_value(ctx,IW_VAL_OUTPUT_FORMAT);

	if(wctx->requested_output_format == IW_FORMAT_PNM) {
		if(wctx->img->imgtype==IW_IMGTYPE_PALETTE) {
			wctx->actual_output_format = IW_FORMAT_PBM;
		}
		else if(wctx->img->imgtype==IW_IMGTYPE_GRAY) {
			wctx->actual_output_format = IW_FORMAT_PGM;
		}
		else if(wctx->img->imgtype==IW_IMGTYPE_RGB) {
			wctx->actual_output_format = IW_FORMAT_PPM;
		}
		else {
			iw_set_error(wctx->ctx,"Internal: Bad image type for PNM");
			goto done;
		}
	}
	else {
		wctx->actual_output_format = wctx->requested_output_format;
	}

	if(wctx->actual_output_format==IW_FORMAT_PGM) {
		if(wctx->img->imgtype!=IW_IMGTYPE_GRAY) {
			iw_set_error(wctx->ctx,"Cannot write non-grayscale image to PGM file");
			goto done;
		}
	}

	if(wctx->actual_output_format==IW_FORMAT_PBM) {
		if(wctx->img->imgtype!=IW_IMGTYPE_PALETTE) {
			iw_set_error(ctx,"Cannot write this image type to PBM file");
			goto done;
		}
		if(!iw_get_value(ctx,IW_VAL_OUTPUT_PALETTE_GRAYSCALE)) {
			iw_set_error(ctx,"Cannot write this image type to PBM file");
			goto done;
		}
		wctx->iwpal = iw_get_output_palette(ctx);
		if(wctx->iwpal->num_entries != 2) {
			iw_set_error(ctx,"Cannot write this image type to PBM file");
			goto done;
		}
	}

	if(wctx->img->reduced_maxcolors) {
		if(wctx->img->imgtype==IW_IMGTYPE_RGB) {
			wctx->maxcolorcode = wctx->img->maxcolorcode[IW_CHANNELTYPE_RED];
			if(wctx->img->maxcolorcode[IW_CHANNELTYPE_GREEN] != wctx->maxcolorcode ||
				wctx->img->maxcolorcode[IW_CHANNELTYPE_BLUE] != wctx->maxcolorcode)
			{
				iw_set_error(wctx->ctx,"PNM format requires equal bit depths");
				goto done;
			}
		}
		else if(wctx->img->imgtype==IW_IMGTYPE_GRAY) {
			wctx->maxcolorcode = wctx->img->maxcolorcode[IW_CHANNELTYPE_GRAY];
		}
		else {
			iw_set_error(wctx->ctx,"Requested bit depth not supported with this file format");
			goto done;
		}
	}
	else {
		if(wctx->img->bit_depth==8) {
			wctx->maxcolorcode = 255;
		}
		else if(wctx->img->bit_depth==16) {
			wctx->maxcolorcode = 65535;
		}
		else {
			iw_set_error(wctx->ctx,"Internal: Bad bit depth for PNM");
			goto done;
		}
	}

	if(wctx->maxcolorcode<1 || wctx->maxcolorcode>65535) {
		iw_set_error(wctx->ctx,"Unsupported PNM bit depth");
		goto done;
	}

	ret=0;
	switch(wctx->actual_output_format) {
	case IW_FORMAT_PPM:
		ret = iwpnm_write_ppm_main(wctx);
		break;
	case IW_FORMAT_PGM:
		ret = iwpnm_write_pgm_main(wctx);
		break;
	case IW_FORMAT_PBM:
		ret = iwpnm_write_pbm_main(wctx);
		break;
	default:
		iw_set_error(wctx->ctx,"Internal: Bad image type for PNM");
		goto done;
	}

	if(!ret) {
		goto done;
	}

	retval = 1;

done:
	if(wctx) {
		iw_free(ctx,wctx->rowbuf);
		iw_free(ctx,wctx);
	}
	return retval;
}
