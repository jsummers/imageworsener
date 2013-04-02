// imagew-pnm.c
// Part of ImageWorsener, Copyright (c) 2013 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iwpnmrcontext {
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	int file_format_code;
	int file_format;
	int color_count;
};

static int iwpnm_read_byte(struct iwpnmrcontext *rctx, iw_byte *b)
{
	iw_byte buf[1];
	int ret;
	size_t bytesread = 0;

	ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
		buf,1,&bytesread);
	if(!ret || bytesread!=1) {
		*b = 0;
		return 0;
	}

	*b = buf[0];
	return 1;
}

static int iwpnm_read(struct iwpnmrcontext *rctx,
	iw_byte *buf, size_t buflen)
{
	int ret;
	size_t bytesread = 0;

	ret = (*rctx->iodescr->read_fn)(rctx->ctx,rctx->iodescr,
		buf,buflen,&bytesread);
	if(!ret || bytesread!=buflen) {
		return 0;
	}
	return 1;
}

static int iwpnm_is_whitespace(iw_byte b)
{
	return (b==9 || b==10 || b==13 || b==32);
}

static int iwpnm_read_next_token(struct iwpnmrcontext *rctx,
	char *tokenbuf, int tokenbuflen)
{
	iw_byte b;
	int ret;
	int token_len = 0;
	int in_comment = 0;

	token_len = 0;
	while(1) {
		if(token_len >= tokenbuflen) {
			// Token too long.
			return 0;
		}

		ret = iwpnm_read_byte(rctx, &b);
		if(!ret) return 0;

		if(in_comment) {
			if(b==10) {
				in_comment = 0;
			}
			continue;
		}
		else if(b=='#') {
			in_comment = 1;
			continue;
		}
		else if(iwpnm_is_whitespace(b)) {
			if(token_len>0) {
				tokenbuf[token_len] = '\0';
				return 1;
			}
			else {
				// Skip leading whitespace.
				continue;
			}
		}
		else {
			// Append the character to the token.
			tokenbuf[token_len] = b;
			token_len++;
		}
	}

	return 0;
}

static int iwpnm_read_pgm_bitmap(struct iwpnmrcontext *rctx)
{
	int j;
	int pnm_bytesperpix;
	int pnm_bpr;
	int retval = 0;

	iw_set_input_max_color_code(rctx->ctx, 0, rctx->color_count);

	pnm_bytesperpix = (rctx->color_count>=256) ? 2 : 1;
	pnm_bpr = pnm_bytesperpix * rctx->img->width;

	rctx->img->imgtype = IW_IMGTYPE_GRAY;
	rctx->img->native_grayscale = 1;
	rctx->img->bit_depth = 8 * pnm_bytesperpix;
	rctx->img->bpr = rctx->img->width * pnm_bytesperpix;

	rctx->img->pixels = (iw_byte*)iw_malloc_large(rctx->ctx,rctx->img->bpr,rctx->img->height);
	if(!rctx->img->pixels) goto done;

	if(pnm_bpr != rctx->img->bpr) goto done;

	for(j=0;j<rctx->img->height;j++) {
		// PGM bitmap is identical to our internal format, so we can read it
		// directly.
		if(!iwpnm_read(rctx, &rctx->img->pixels[j*rctx->img->bpr], pnm_bpr)) {
			goto done;
		}
	}

	retval = 1;
done:
	return retval;
}

static int iwpnm_read_header(struct iwpnmrcontext *rctx)
{
	char tokenbuf[100];
	int ret;
	int retval = 0;

	// Read file signature
	ret = iwpnm_read_next_token(rctx, tokenbuf, sizeof(tokenbuf));
	if(!ret) return 0;
	if(strlen(tokenbuf)!=2 || tokenbuf[0]!='P' || 
		(tokenbuf[1]<'1' || tokenbuf[1]>'7'))
	{
		iw_set_error(rctx->ctx,"Not a PNM file");
		goto done;
	}

	rctx->file_format_code = tokenbuf[1] - '0';

	if(rctx->file_format_code == 7) {
		iw_set_error(rctx->ctx,"PAM format is not supported");
		goto done;
	}

	if(rctx->file_format_code != 5) {
		iw_set_error(rctx->ctx,"Reading this PNM format is not supported");
		goto done;
	}

	// Read width
	ret = iwpnm_read_next_token(rctx, tokenbuf, sizeof(tokenbuf));
	if(!ret) goto done;
	rctx->img->width = atoi(tokenbuf);

	// Read height
	ret = iwpnm_read_next_token(rctx, tokenbuf, sizeof(tokenbuf));
	if(!ret) goto done;
	rctx->img->height = atoi(tokenbuf);

	if(rctx->file_format_code==1 || rctx->file_format_code==4) {
		// PBM files don't have a max-color-value token.
		retval = 1;
		goto done;
	}

	// Read bit depth (number of color shades)
	ret = iwpnm_read_next_token(rctx, tokenbuf, sizeof(tokenbuf));
	if(!ret) goto done;
	rctx->color_count = atoi(tokenbuf);
	if(rctx->color_count<1 || rctx->color_count>65535) {
		iw_set_errorf(rctx->ctx, "Invalid max color value (%d)\n", rctx->color_count);
		goto done;
	}

	retval = 1;
done:
	return retval;
}

IW_IMPL(int) iw_read_pnm_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwpnmrcontext *rctx = NULL;
	struct iw_image *img = NULL;
	int retval = 0;

	rctx = iw_mallocz(ctx, sizeof(struct iwpnmrcontext));
	if(!rctx) goto done;
	img = iw_mallocz(ctx, sizeof(struct iw_image));
	if(!img) goto done;

	rctx->ctx = ctx;
	rctx->img = img;
	rctx->iodescr = iodescr;

	if(!iwpnm_read_header(rctx)) goto done;

	if(!iw_check_image_dimensions(rctx->ctx,rctx->img->width,rctx->img->height))
		goto done;

	switch(rctx->file_format_code) {
	case 5:
		if(!iwpnm_read_pgm_bitmap(rctx)) goto done;
		break;
	default:
		goto done;
	}

	iw_set_input_image(ctx, img);
	retval = 1;

done:
	if(img) iw_free(ctx, img);
	if(rctx) iw_free(ctx, rctx);
	return retval;
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
		wctx->maxcolorcode = 255;
	}
	else if(img->bit_depth==16) {
		bytes_per_ppm_pixel=6;
		wctx->maxcolorcode = 65535;
	}
	else {
		goto done;
	}

	if(wctx->img->reduced_maxcolors) {
		if(img->imgtype==IW_IMGTYPE_GRAY) {
			wctx->maxcolorcode = wctx->img->maxcolorcode[IW_CHANNELTYPE_GRAY];
		}
		else {
			wctx->maxcolorcode = wctx->img->maxcolorcode[IW_CHANNELTYPE_RED];
			if(wctx->img->maxcolorcode[IW_CHANNELTYPE_GREEN] != wctx->maxcolorcode ||
				wctx->img->maxcolorcode[IW_CHANNELTYPE_BLUE] != wctx->maxcolorcode)
			{
				iw_set_error(wctx->ctx,"PNM/PPM format requires equal bit depths");
				goto done;
			}
		}
	}

	if(wctx->maxcolorcode<1 || wctx->maxcolorcode>65535) {
		iw_set_error(wctx->ctx,"Unsupported PPM bit depth");
		goto done;
	}

	outrowsize = bytes_per_ppm_pixel*img->width;
	wctx->rowbuf = iw_mallocz(wctx->ctx, outrowsize);
	if(!wctx->rowbuf) goto done;

	iw_snprintf(tmpstring, sizeof(tmpstring), "P6\n%d %d\n%d\n", img->width,
		img->height, wctx->maxcolorcode);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));

	for(j=0;j<img->height;j++) {
		if(img->imgtype==IW_IMGTYPE_RGB && img->bit_depth==8) {
			memcpy(wctx->rowbuf, &img->pixels[j*img->bpr], outrowsize);
		}
		else if(img->imgtype==IW_IMGTYPE_GRAY && img->bit_depth==8) {
			for(i=0;i<img->width;i++) {
				wctx->rowbuf[i*3+0] = img->pixels[j*img->bpr+i];
				wctx->rowbuf[i*3+1] = img->pixels[j*img->bpr+i];
				wctx->rowbuf[i*3+2] = img->pixels[j*img->bpr+i];
			}
		}
		else if(img->imgtype==IW_IMGTYPE_RGB && img->bit_depth==16) {
			memcpy(wctx->rowbuf, &img->pixels[j*img->bpr], outrowsize);
		}
		else if(img->imgtype==IW_IMGTYPE_GRAY && img->bit_depth==16) {
			for(i=0;i<img->width;i++) {
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
	int j;
	size_t outrowsize;
	char tmpstring[80];
	int bytes_per_ppm_pixel;

	img = wctx->img;

	if(img->imgtype!=IW_IMGTYPE_GRAY) {
		iw_set_error(wctx->ctx,"Cannot write non-grayscale image to PGM file");
		goto done;
	}

	if(img->bit_depth==8) {
		bytes_per_ppm_pixel=1;
		wctx->maxcolorcode = 255;
	}
	else if(img->bit_depth==16) {
		bytes_per_ppm_pixel=2;
		wctx->maxcolorcode = 65535;
	}
	else {
		goto done;
	}

	if(wctx->img->reduced_maxcolors) {
		wctx->maxcolorcode = wctx->img->maxcolorcode[IW_CHANNELTYPE_GRAY];
	}

	if(wctx->maxcolorcode<1 || wctx->maxcolorcode>65535) {
		iw_set_error(wctx->ctx,"Unsupported PGM bit depth");
		goto done;
	}

	outrowsize = bytes_per_ppm_pixel*img->width;
	wctx->rowbuf = iw_mallocz(wctx->ctx, outrowsize);
	if(!wctx->rowbuf) goto done;

	iw_snprintf(tmpstring, sizeof(tmpstring), "P5\n%d %d\n%d\n", img->width,
		img->height, wctx->maxcolorcode);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));

	for(j=0;j<img->height;j++) {
		iwpnm_write(wctx, &img->pixels[j*img->bpr], outrowsize);
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
	int ok;
	size_t outrowsize;
	char tmpstring[80];

	img = wctx->img;

	// Make sure the image is paletted, and has a grayscale palette with
	// exactly 2 entries.
	ok=0;
	if(img->imgtype==IW_IMGTYPE_PALETTE) {
		if(iw_get_value(wctx->ctx,IW_VAL_OUTPUT_PALETTE_GRAYSCALE)) {
			wctx->iwpal = iw_get_output_palette(wctx->ctx);
			if(wctx->iwpal->num_entries == 2) {
				ok=1;
			}
		}
	}
	if(!ok) {
		iw_set_error(wctx->ctx,"Cannot write this image type to a PBM file");
		goto done;
	}

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
			// PBM is the only one of these formats that allows palette optimization,
			// and it requires it, so this test is sufficient.
			wctx->actual_output_format = IW_FORMAT_PBM;
		}
		else if(wctx->img->imgtype==IW_IMGTYPE_GRAY) {
			wctx->actual_output_format = IW_FORMAT_PGM;
		}
		else if(wctx->img->imgtype==IW_IMGTYPE_RGB) {
			wctx->actual_output_format = IW_FORMAT_PPM;
		}
		else {
			iw_set_error(ctx,"Internal: Bad image type for PNM");
			goto done;
		}
	}
	else {
		wctx->actual_output_format = wctx->requested_output_format;
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
