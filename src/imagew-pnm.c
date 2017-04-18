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
	int num_channels_pam;
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

// Read a binary PBM/PGM/PPM/PAM bitmap.
static int iwpnm_read_pnm_bitmap(struct iwpnmrcontext *rctx)
{
	int i,j;
	int pnm_bytesperpix;
	int pnm_bpr;
	int retval = 0;

	if(rctx->file_format_code==4) { // PBM
		rctx->img->imgtype = IW_IMGTYPE_GRAY;
		rctx->img->native_grayscale = 1;
		rctx->img->bit_depth = 1;
		pnm_bpr = (rctx->img->width+7)/8;
	}
	else if(rctx->file_format_code==5 ||
		(rctx->file_format_code==7 && rctx->num_channels_pam==1))  // PGM or PAM-GRAYSCALE
	{
		rctx->img->imgtype = IW_IMGTYPE_GRAY;
		rctx->img->native_grayscale = 1;
		if(rctx->color_count>=256) {
			rctx->img->bit_depth = 16;
			pnm_bytesperpix = 2;
		}
		else {
			rctx->img->bit_depth = 8;
			pnm_bytesperpix = 1;
		}
		pnm_bpr = pnm_bytesperpix * rctx->img->width;
		if(rctx->color_count!=255 && rctx->color_count!=65535) {
			iw_set_input_max_color_code(rctx->ctx, 0, rctx->color_count);
		}
	}
	else if(rctx->file_format_code==6 ||
		(rctx->file_format_code==7 && rctx->num_channels_pam==3)) // PPM or PAM-RGB
	{
		rctx->img->imgtype = IW_IMGTYPE_RGB;
		if(rctx->color_count>=256) {
			rctx->img->bit_depth = 16;
			pnm_bytesperpix = 6;
		}
		else {
			rctx->img->bit_depth = 8;
			pnm_bytesperpix = 3;
		}
		pnm_bpr = pnm_bytesperpix * rctx->img->width;
		if(rctx->color_count!=255 && rctx->color_count!=65535) {
			iw_set_input_max_color_code(rctx->ctx, 0, rctx->color_count);
			iw_set_input_max_color_code(rctx->ctx, 1, rctx->color_count);
			iw_set_input_max_color_code(rctx->ctx, 2, rctx->color_count);
		}
	}
	else if(rctx->file_format_code==7 && rctx->num_channels_pam==2) { // PAM-GRAYSCALE_ALPHA
		rctx->img->imgtype = IW_IMGTYPE_GRAYA;
		if(rctx->color_count>=256) {
			rctx->img->bit_depth = 16;
			pnm_bytesperpix = 4;
		}
		else {
			rctx->img->bit_depth = 8;
			pnm_bytesperpix = 2;
		}
		pnm_bpr = pnm_bytesperpix * rctx->img->width;
		if(rctx->color_count!=255 && rctx->color_count!=65535) {
			iw_set_input_max_color_code(rctx->ctx, 0, rctx->color_count);
			iw_set_input_max_color_code(rctx->ctx, 1, rctx->color_count);
		}
	}
	else if(rctx->file_format_code==7 && rctx->num_channels_pam==4) { // PAM-RGB_ALPHA
		rctx->img->imgtype = IW_IMGTYPE_RGBA;
		if(rctx->color_count>=256) {
			rctx->img->bit_depth = 16;
			pnm_bytesperpix = 8;
		}
		else {
			rctx->img->bit_depth = 8;
			pnm_bytesperpix = 4;
		}
		pnm_bpr = pnm_bytesperpix * rctx->img->width;
		if(rctx->color_count!=255 && rctx->color_count!=65535) {
			iw_set_input_max_color_code(rctx->ctx, 0, rctx->color_count);
			iw_set_input_max_color_code(rctx->ctx, 1, rctx->color_count);
			iw_set_input_max_color_code(rctx->ctx, 2, rctx->color_count);
			iw_set_input_max_color_code(rctx->ctx, 3, rctx->color_count);
		}
	}

	else {
		iw_set_error(rctx->ctx,"Unsupported PNM/PAM image type");
		goto done;
	}

	rctx->img->bpr = pnm_bpr;

	rctx->img->pixels = (iw_byte*)iw_malloc_large(rctx->ctx,rctx->img->bpr,rctx->img->height);
	if(!rctx->img->pixels) goto done;

	for(j=0;j<rctx->img->height;j++) {
		// Binary PNM files are identical or very similar to our internal format,
		// so we can read them directly.
		if(!iwpnm_read(rctx, &rctx->img->pixels[j*rctx->img->bpr], pnm_bpr)) {
			goto done;
		}
		if(rctx->file_format_code==4) {
			// PBM images need to be inverted.
			for(i=0;i<pnm_bpr;i++) {
				rctx->img->pixels[j*rctx->img->bpr+i] = 255-rctx->img->pixels[j*rctx->img->bpr+i];
			}
		}
	}

	retval = 1;
done:
	return retval;
}

// Read the header (following the first 3 bytes) of a PNM (not PAM) file.
static int iwpnm_read_pnm_header(struct iwpnmrcontext *rctx)
{
	char tokenbuf[100];
	int ret;
	int retval = 0;

	if(rctx->file_format_code!=4 && rctx->file_format_code!=5 && rctx->file_format_code!=6) {
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

// Read a token from a NUL-terminated string.
static int read_next_pam_token(struct iwpnmrcontext *rctx,
	const char *linebuf, int linebuflen,
	char *tokenbuf, int tokenbuflen, int *curpos)
{
	iw_byte b;
	int token_len = 0;
	int linepos;

	token_len = 0;

	linepos = *curpos;
	while(1) {
		if(token_len >= tokenbuflen) {
			// Token too long.
			return 0;
		}

		if(linepos>=linebuflen) {
			return 0;
		}
		b = linebuf[linepos++];
		if(b==0) break; // End of line

		if(iwpnm_is_whitespace(b)) {
			if(token_len>0) {
				break;
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

	tokenbuf[token_len] = '\0';
	*curpos = linepos;
	return 1;
}

static int read_pam_header_line(struct iwpnmrcontext *rctx, char *line, int linesize)
{
	iw_byte b;
	int retval = 0;
	int linepos = 0;

	while(1) {
		if(!iwpnm_read_byte(rctx, &b)) goto done;

		if(b==0x0a) { // end of line
			break;
		}

		if(linepos < linesize-1) {
			line[linepos++] = (char)b;
		}

	}
	retval = 1;
done:
	line[linepos] = '\0';
	return retval;
}

// Read the header (following the first 3 bytes) PAM file.
static int iwpnm_read_pam_header(struct iwpnmrcontext *rctx)
{
	char linebuf[100];
	char tokenbuf[100];
	char token2buf[100];
	int retval = 0;
	int curpos = 0;

	while(1) {
		// Read a header line
		if(!read_pam_header_line(rctx, linebuf, sizeof(linebuf))) goto done;
		if(linebuf[0]=='#') {
			// Comment line.
			continue;
		}

		// Read first token in that header line
		curpos = 0;
		if(!read_next_pam_token(rctx, linebuf, (int)sizeof(linebuf),
			tokenbuf, (int)sizeof(tokenbuf), &curpos))
		{
			goto done;
		}

		if(!strcmp(tokenbuf,"ENDHDR")) {
			break;
		}

		if(!strcmp(tokenbuf,"")) {
			// Blank or whitespace-only lines are allowed.
			continue;
		}

		// Read second token
		if(!read_next_pam_token(rctx, linebuf, (int)sizeof(linebuf),
			token2buf, (int)sizeof(token2buf), &curpos))
		{
			goto done;
		}
		if(!strcmp(tokenbuf,"WIDTH")) {
			rctx->img->width = atoi(token2buf);
		}
		else if(!strcmp(tokenbuf,"HEIGHT")) {
			rctx->img->height = atoi(token2buf);
		}
		else if(!strcmp(tokenbuf,"DEPTH")) {
			rctx->num_channels_pam = atoi(token2buf);
		}
		else if(!strcmp(tokenbuf,"MAXVAL")) {
			rctx->color_count = atoi(token2buf);
		}
		else if(!strcmp(tokenbuf,"TUPLTYPE")) {
			// TODO
		}
	}

	if(rctx->color_count<1 || rctx->color_count>65535) {
		iw_set_errorf(rctx->ctx, "Invalid max color value (%d)\n", rctx->color_count);
		goto done;
	}

	retval = 1;
done:
	return retval;
}

// Read the header of a PNM or PAM file.
static int iwpnm_read_header(struct iwpnmrcontext *rctx)
{
	int ret;
	int retval = 0;
	char sig[3];
	int isvalid;

	// Read the file signature.
	ret = iwpnm_read(rctx, (iw_byte*)sig, 3);
	if(!ret) goto done;

	isvalid = 0;
	if(sig[0]!='P') {
		isvalid=0;
	}
	else if(sig[1]=='7' && sig[2]==0x0a) {
		isvalid=1;
	}
	else if(sig[1]>='1' && sig[1]<='6' && iwpnm_is_whitespace(sig[2])) {
		isvalid=1;
	}
	else {
		isvalid=0;
	}

	if(!isvalid) {
		iw_set_error(rctx->ctx,"Not a PNM/PAM file");
		goto done;
	}

	rctx->file_format_code = sig[1] - '0';

	if(rctx->file_format_code == 7)
		retval = iwpnm_read_pam_header(rctx);
	else
		retval = iwpnm_read_pnm_header(rctx);

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

	if(!iwpnm_read_header(rctx)) {
		iw_set_error(ctx, "Error parsing header");
		goto done;
	}

	if(!iw_check_image_dimensions(rctx->ctx,rctx->img->width,rctx->img->height))
		goto done;

	if(!iwpnm_read_pnm_bitmap(rctx)) goto done;

	iw_set_input_image(ctx, img);
	// The contents of img no longer belong to us.
	img->pixels = NULL;

	retval = 1;

done:
	if(img) {
		iw_free(ctx, img->pixels);
		iw_free(ctx, img);
	}
	if(rctx) iw_free(ctx, rctx);
	return retval;
}

IW_IMPL(int) iw_read_pam_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	return iw_read_pnm_file(ctx, iodescr);
}

struct iwpnmwcontext {
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	iw_byte *rowbuf;
	int requested_output_format;
	int actual_output_format;
	int maxcolorcode;
};

static void iwpnm_write(struct iwpnmwcontext *wctx, const void *buf, size_t n)
{
	(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,buf,n);
}

static int write_pam_header(struct iwpnmwcontext *wctx, int numchannels,
	int maxcolorcode, const char *tupltype)
{
	char tmpstring[80];

	iw_snprintf(tmpstring, sizeof(tmpstring), "P7\n");
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));
	iw_snprintf(tmpstring, sizeof(tmpstring), "WIDTH %d\n", wctx->img->width);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));
	iw_snprintf(tmpstring, sizeof(tmpstring), "HEIGHT %d\n", wctx->img->height);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));
	iw_snprintf(tmpstring, sizeof(tmpstring), "DEPTH %d\n", numchannels);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));
	iw_snprintf(tmpstring, sizeof(tmpstring), "MAXVAL %d\n", maxcolorcode);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));
	iw_snprintf(tmpstring, sizeof(tmpstring), "TUPLTYPE %s\n", tupltype);
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));
	iw_snprintf(tmpstring, sizeof(tmpstring), "ENDHDR\n");
	iwpnm_write(wctx, tmpstring, strlen(tmpstring));

	return 1;
}

// Write a PPM or PAM-RGB file.
static int iwpnm_write_rgb_main(struct iwpnmwcontext *wctx)
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
				iw_set_error(wctx->ctx,"PNM/PPM/PAM format requires equal bit depths");
				goto done;
			}
		}
	}

	if(wctx->maxcolorcode<1 || wctx->maxcolorcode>65535) {
		iw_set_error(wctx->ctx,"Unsupported PPM/PAM bit depth");
		goto done;
	}

	outrowsize = bytes_per_ppm_pixel*img->width;
	wctx->rowbuf = iw_mallocz(wctx->ctx, outrowsize);
	if(!wctx->rowbuf) goto done;

	if(wctx->requested_output_format==IW_FORMAT_PAM) {
		write_pam_header(wctx, 3, wctx->maxcolorcode, "RGB");
	}
	else {
		iw_snprintf(tmpstring, sizeof(tmpstring), "P6\n%d %d\n%d\n", img->width,
			img->height, wctx->maxcolorcode);
		iwpnm_write(wctx, tmpstring, strlen(tmpstring));
	}

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

// Write a PAM-RGB_ALPHA file.
static int iwpnm_write_rgba_main(struct iwpnmwcontext *wctx)
{
	struct iw_image *img;
	int retval = 0;
	int j;
	size_t outrowsize;
	int bytes_per_ppm_pixel;

	img = wctx->img;

	if(img->imgtype!=IW_IMGTYPE_RGBA) goto done;

	if(img->bit_depth==8) {
		bytes_per_ppm_pixel=4;
		wctx->maxcolorcode = 255;
	}
	else if(img->bit_depth==16) {
		bytes_per_ppm_pixel=8;
		wctx->maxcolorcode = 65535;
	}
	else {
		goto done;
	}

	if(wctx->img->reduced_maxcolors) {
		wctx->maxcolorcode = wctx->img->maxcolorcode[IW_CHANNELTYPE_RED];
		if(wctx->img->maxcolorcode[IW_CHANNELTYPE_GREEN] != wctx->maxcolorcode ||
			wctx->img->maxcolorcode[IW_CHANNELTYPE_BLUE] != wctx->maxcolorcode ||
			wctx->img->maxcolorcode[IW_CHANNELTYPE_ALPHA] != wctx->maxcolorcode)
		{
			iw_set_error(wctx->ctx,"PAM format requires equal bit depths");
			goto done;
		}
	}

	if(wctx->maxcolorcode<1 || wctx->maxcolorcode>65535) {
		iw_set_error(wctx->ctx,"Unsupported PAM bit depth");
		goto done;
	}

	outrowsize = bytes_per_ppm_pixel*img->width;
	wctx->rowbuf = iw_mallocz(wctx->ctx, outrowsize);
	if(!wctx->rowbuf) goto done;

	write_pam_header(wctx, 4, wctx->maxcolorcode, "RGB_ALPHA");

	for(j=0;j<img->height;j++) {
		if(img->bit_depth==8) {
			memcpy(wctx->rowbuf, &img->pixels[j*img->bpr], outrowsize);
		}
		else if(img->bit_depth==16) {
			memcpy(wctx->rowbuf, &img->pixels[j*img->bpr], outrowsize);
		}

		iwpnm_write(wctx, wctx->rowbuf, outrowsize);
	}

	retval = 1;

done:
	return retval;
}

// Returns 1 if the image is paletted, and has a grayscale palette with
// exactly 2 entries.
static int has_bw_palette(struct iwpnmwcontext *wctx, struct iw_image *img)
{
	const struct iw_palette *iwpal;

	if(img->imgtype!=IW_IMGTYPE_PALETTE) return 0;
	if(!iw_get_value(wctx->ctx,IW_VAL_OUTPUT_PALETTE_GRAYSCALE)) return 0;

	iwpal = iw_get_output_palette(wctx->ctx);
	if(iwpal->num_entries != 2) return 0;

	return 1;
}

// Write a PGM or PAM-GRAYSCALE or PAM-BLACKANDWHITE file.
static int iwpnm_write_gray_main(struct iwpnmwcontext *wctx)
{
	struct iw_image *img;
	int retval = 0;
	int j;
	size_t outrowsize;
	char tmpstring[80];
	int bytes_per_ppm_pixel;
	int is_bilevel = 0;

	img = wctx->img;

	if(has_bw_palette(wctx, img)) {
		is_bilevel = 1;
	}
	else if(img->imgtype!=IW_IMGTYPE_GRAY) {
		iw_set_error(wctx->ctx,"Cannot write non-grayscale image to PGM file");
		goto done;
	}

	if(is_bilevel) {
		bytes_per_ppm_pixel=1;
		wctx->maxcolorcode = 1;
	}
	else if(img->bit_depth==8) {
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

	if(wctx->requested_output_format==IW_FORMAT_PAM) {
		if(wctx->maxcolorcode==1) {
			write_pam_header(wctx, 1, 1, "BLACKANDWHITE");
		}
		else {
			write_pam_header(wctx, 1, wctx->maxcolorcode, "GRAYSCALE");
		}
	}
	else {
		iw_snprintf(tmpstring, sizeof(tmpstring), "P5\n%d %d\n%d\n", img->width,
			img->height, wctx->maxcolorcode);
		iwpnm_write(wctx, tmpstring, strlen(tmpstring));
	}

	for(j=0;j<img->height;j++) {
		iwpnm_write(wctx, &img->pixels[j*img->bpr], outrowsize);
	}

	retval = 1;

done:
	return retval;
}

// Write a PAM-GRAYSCALE_ALPHA file.
static int iwpnm_write_graya_main(struct iwpnmwcontext *wctx)
{
	struct iw_image *img;
	int retval = 0;
	int j;
	size_t outrowsize;
	int bytes_per_pam_pixel;

	img = wctx->img;

	if(img->imgtype!=IW_IMGTYPE_GRAYA) goto done;

	if(img->bit_depth==8) {
		bytes_per_pam_pixel=2;
		wctx->maxcolorcode = 255;
	}
	else if(img->bit_depth==16) {
		bytes_per_pam_pixel=4;
		wctx->maxcolorcode = 65535;
	}
	else {
		goto done;
	}

	if(wctx->img->reduced_maxcolors) {
		wctx->maxcolorcode = wctx->img->maxcolorcode[IW_CHANNELTYPE_GRAY];
		if(wctx->img->maxcolorcode[IW_CHANNELTYPE_ALPHA] != wctx->maxcolorcode)
		{
			iw_set_error(wctx->ctx,"PAM format requires equal bit depths");
			goto done;
		}
	}

	if(wctx->maxcolorcode<1 || wctx->maxcolorcode>65535) {
		iw_set_error(wctx->ctx,"Unsupported PAM bit depth");
		goto done;
	}

	outrowsize = bytes_per_pam_pixel*img->width;
	wctx->rowbuf = iw_mallocz(wctx->ctx, outrowsize);
	if(!wctx->rowbuf) goto done;

	// GRAYSCALE and BLACKANDWHITE seem to be identical, except that for
	// BLACKANDWHITE, MAXVAL can only be 1.
	if(wctx->maxcolorcode==1) {
		write_pam_header(wctx, 2, wctx->maxcolorcode, "BLACKANDWHITE_ALPHA");
	}
	else {
		write_pam_header(wctx, 2, wctx->maxcolorcode, "GRAYSCALE_ALPHA");
	}

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
	size_t outrowsize;
	char tmpstring[80];

	img = wctx->img;

	if(!has_bw_palette(wctx, img)) {
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
		ret = iwpnm_write_rgb_main(wctx);
		break;
	case IW_FORMAT_PGM:
		ret = iwpnm_write_gray_main(wctx);
		break;
	case IW_FORMAT_PBM:
		ret = iwpnm_write_pbm_main(wctx);
		break;
	case IW_FORMAT_PAM:
		if(wctx->img->imgtype==IW_IMGTYPE_RGBA) {
			ret = iwpnm_write_rgba_main(wctx);
		}
		else if(wctx->img->imgtype==IW_IMGTYPE_GRAYA) {
			ret = iwpnm_write_graya_main(wctx);
		}
		else if(wctx->img->imgtype==IW_IMGTYPE_RGB) {
			ret = iwpnm_write_rgb_main(wctx);
		}
		else if(wctx->img->imgtype==IW_IMGTYPE_GRAY || wctx->img->imgtype==IW_IMGTYPE_PALETTE) {
			ret = iwpnm_write_gray_main(wctx);
		}
		else {
			iw_set_error(wctx->ctx,"Unsupported image type for PAM");
			goto done;
		}
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

IW_IMPL(int) iw_write_pam_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	return iw_write_pnm_file(ctx, iodescr);
}
