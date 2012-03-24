// imagew-bmp.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdio.h> // for SEEK_SET
#include <stdlib.h>
#include <string.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

#define IWBMP_BI_RGB       0 // = uncompressed
#define IWBMP_BI_RLE8      1
#define IWBMP_BI_RLE4      2
#define IWBMP_BI_BITFIELDS 3
#define IWBMP_BI_JPEG      4
#define IWBMP_BI_PNG       5

static size_t iwbmp_calc_bpr(int bpp, size_t width)
{
	return ((bpp*width+31)/32)*4;
}

struct iwbmpreadcontext {
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	int bmpversion;
	int width, height;
	int topdown;
	unsigned int bitcount; // bits per pixel
	unsigned int compression; // IWBMP_BI_*
	unsigned int palette_entries;
	size_t fileheader_size;
	size_t infoheader_size;
	size_t bitfields_nbytes;
	size_t palette_nbytes;
	size_t bfOffBits;
	struct iw_palette palette;

	// For 16- & 32-bit images:
	unsigned int bf_mask[3];
	int bf_high_bit[3];
	int bf_low_bit[3];
	int bf_bits_count[3]; // number of bits in each channel
};

static int iwbmp_read(struct iwbmpreadcontext *rctx,
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

static int iwbmp_skip_bytes(struct iwbmpreadcontext *rctx, size_t n)
{
	iw_byte buf[1024];
	size_t still_to_read;
	size_t num_to_read;

	still_to_read = n;
	while(still_to_read>0) {
		num_to_read = still_to_read;
		if(num_to_read>1024) num_to_read=1024;
		if(!iwbmp_read(rctx,buf,num_to_read)) {
			return 0;
		}
		still_to_read -= num_to_read;
	}
	return 1;
}

static int iwbmp_read_file_header(struct iwbmpreadcontext *rctx)
{
	iw_byte buf[14];

	if(!iwbmp_read(rctx,buf,14)) return 0;
	if(buf[0]!=66 || buf[1]!=77) {
		iw_set_error(rctx->ctx,"Not a BMP file");
		return 0;
	}

	rctx->fileheader_size = 14;
	rctx->bfOffBits = iw_get_ui32le(&buf[10]);
	return 1;
}

static int iwbmp_read_info_header(struct iwbmpreadcontext *rctx)
{
	iw_byte buf[40];
	int retval = 0;
	unsigned int nplanes;
	unsigned int biSizeImage;
	int biXPelsPerMeter, biYPelsPerMeter;
	unsigned int biClrUsed;

	// First, read just the "size" field. It tells the size of the header
	// structure, and identifies the BMP version.
	if(!iwbmp_read(rctx,buf,4)) goto done;
	rctx->infoheader_size = iw_get_ui32le(&buf[0]);

	if(rctx->infoheader_size==40) {
		rctx->bmpversion=3;

		// Read the rest of the header.
		if(!iwbmp_read(rctx,&buf[4],rctx->infoheader_size-4)) goto done;
		rctx->width = iw_get_i32le(&buf[4]);
		rctx->height = iw_get_i32le(&buf[8]);
		if(rctx->height<0) {
			rctx->height = -rctx->height;
			rctx->topdown = 1;
		}

		nplanes = iw_get_ui16le(&buf[12]);

		rctx->bitcount = iw_get_ui16le(&buf[14]);
		if(rctx->bitcount!=1 && rctx->bitcount!=4 && rctx->bitcount!=8 &&
			rctx->bitcount!=16 && rctx->bitcount!=24 && rctx->bitcount!=32)
		{
			goto done;
		}
		rctx->compression = iw_get_ui32le(&buf[16]);
		if(rctx->compression==IWBMP_BI_BITFIELDS) {
			// The compression field is overloaded: BITFIELDS is not a type of
			// compression. Un-overload it.
			rctx->bitfields_nbytes=12;
			rctx->compression=IWBMP_BI_RGB;
		}

		biSizeImage = iw_get_ui32le(&buf[20]);
		biXPelsPerMeter = iw_get_i32le(&buf[24]);
		biYPelsPerMeter = iw_get_i32le(&buf[28]);

		biClrUsed = iw_get_ui32le(&buf[32]);
		// The documentation of the biClrUsed field is not very clear.
		// I'm going to assume that if biClrUsed is 0 and bitcount<=8, then
		// the number of palette colors is the maximum that would be useful
		// for that bitcount. In all other cases, the number of palette colors
		// equals biClrUsed.
		if(biClrUsed==0 && rctx->bitcount<=8) {
			rctx->palette_entries = 1<<rctx->bitcount;
		}
		else {
			rctx->palette_entries = biClrUsed;
		}
	}
	else if(rctx->infoheader_size==12) {
		// This is a rare old-style "OS/2" bitmap.
		rctx->bmpversion=2;
		if(!iwbmp_read(rctx,&buf[4],rctx->infoheader_size-4)) goto done;

		rctx->width = iw_get_ui16le(&buf[4]);
		rctx->height = iw_get_ui16le(&buf[6]);
		nplanes = iw_get_ui16le(&buf[8]);
		rctx->bitcount = iw_get_ui16le(&buf[10]);
		if(rctx->bitcount!=1 && rctx->bitcount!=4 &&
			rctx->bitcount!=8 && rctx->bitcount!=24)
		{
			goto done;
		}
		if(rctx->bitcount<=8) {
			rctx->palette_entries = 1<<rctx->bitcount;
		}
	}
	else {
		iw_set_error(rctx->ctx,"Unsupported BMP version");
		goto done;
	}

	if(!iw_check_image_dimensions(rctx->ctx,rctx->width,rctx->height)) {
		goto done;
	}

	if(nplanes!=1) {
		iw_set_error(rctx->ctx,"Unsupported type of BMP image");
		goto done;
	}

	if(rctx->bmpversion==2) {
		rctx->palette_nbytes = 3*rctx->palette_entries;
	}
	else {
		rctx->palette_nbytes = 4*rctx->palette_entries;

		rctx->img->density_code = IW_DENSITY_UNITS_PER_METER;
		rctx->img->density_x = (double)biXPelsPerMeter;
		rctx->img->density_y = (double)biYPelsPerMeter;
		if(!iw_is_valid_density(rctx->img->density_x,rctx->img->density_y,rctx->img->density_code)) {
			rctx->img->density_code=IW_DENSITY_UNKNOWN;
		}
	}

	retval = 1;

done:
	return retval;
}

// Find the highest/lowest bit that is set.
static int find_high_bit(unsigned int x)
{
	int i;
	for(i=31;i>=0;i--) {
		if(x&(1<<i)) return i;
	}
	return 0;
}
static int find_low_bit(unsigned int x)
{
	int i;
	for(i=0;i<=31;i++) {
		if(x&(1<<i)) return i;
	}
	return 0;
}

// Given .bf_mask[k], set high_bit[k], low_bit[k], etc.
static void process_bf_mask(struct iwbmpreadcontext *rctx, int k)
{
	// The bits representing the mask for each channel are required to be
	// contiguous, so all we need to do is find the highest and lowest bit.
	rctx->bf_high_bit[k] = find_high_bit(rctx->bf_mask[k]);
	rctx->bf_low_bit[k] = find_low_bit(rctx->bf_mask[k]);
	rctx->bf_bits_count[k] = 1+rctx->bf_high_bit[k]-rctx->bf_low_bit[k];
}

static int iwbmp_read_bitfields(struct iwbmpreadcontext *rctx)
{
	iw_byte buf[12];
	int k;

	if(!iwbmp_read(rctx,buf,12)) return 0;

	for(k=0;k<3;k++) {
		rctx->bf_mask[k] = iw_get_ui32le(&buf[k*4]);
		if(rctx->bf_mask[k]==0) return 0;

		// Find the high bit, low bit, etc.
		process_bf_mask(rctx,k);

		// Check if the mask specifies an invalid bit
		if(rctx->bf_high_bit[k] > (int)(rctx->bitcount-1)) return 0;

		if(rctx->bf_bits_count[k]>8) {
			// We could support larger bit counts with a little effort, but such BMP
			// files are, as far as I know, nonexistent.
			iw_set_errorf(rctx->ctx,"BMP bits per channel >8 (%d) not supported",
				rctx->bf_bits_count[k]);
			return 0;
		}
	}

	return 1;
}

static void iwbmp_set_default_bitfields(struct iwbmpreadcontext *rctx)
{
	int k;

	if(rctx->bitcount==16) {
		// Default is 5 bits for each channel.
		rctx->bf_mask[0]=0x7c00; // 01111100 00000000 (red)
		rctx->bf_mask[1]=0x03e0; // 00000011 11100000 (green)
		rctx->bf_mask[2]=0x001f; // 00000000 00011111 (blue)
	}
	else if(rctx->bitcount==32) {
		rctx->bf_mask[0]=0x00ff0000;
		rctx->bf_mask[1]=0x0000ff00;
		rctx->bf_mask[2]=0x000000ff;
	}
	else {
		return;
	}

	for(k=0;k<3;k++) {
		process_bf_mask(rctx,k);
	}
}

static int iwbmp_read_palette(struct iwbmpreadcontext *rctx)
{
	size_t i;
	iw_byte buf[1024];
	size_t b;

	// TODO: Maybe we should allow palettes with >256 colors.
	if(rctx->palette_entries>256) return 0;

	b = (rctx->bmpversion==2) ? 3 : 4; // bytes per palette entry

	if(!iwbmp_read(rctx,buf,rctx->palette_nbytes)) return 0;
	rctx->palette.num_entries = rctx->palette_entries;
	for(i=0;i<rctx->palette_entries;i++) {
		rctx->palette.entry[i].b = buf[i*b+0];
		rctx->palette.entry[i].g = buf[i*b+1];
		rctx->palette.entry[i].r = buf[i*b+2];
		rctx->palette.entry[i].a = 255;
	}
	return 1;
}

static void bmpr_convert_row_32_16(struct iwbmpreadcontext *rctx, const iw_byte *src, size_t row)
{
	int i,k;
	unsigned int v,x;

	for(i=0;i<rctx->width;i++) {
		if(rctx->bitcount==32) {
			x = ((unsigned int)src[i*4+0]) | ((unsigned int)src[i*4+1])<<8 |
				((unsigned int)src[i*4+2])<<16 | ((unsigned int)src[i*4+3])<<24;
		}
		else { // 16
			x = ((unsigned int)src[i*2+0]) | ((unsigned int)src[i*2+1])<<8;
		}
		v = 0;
		for(k=0;k<3;k++) { // For red, green, blue:
			v = x & rctx->bf_mask[k];
			if(rctx->bf_low_bit[k]>0)
				v >>= rctx->bf_low_bit[k];
			rctx->img->pixels[row*rctx->img->bpr + i*3 + k] = (iw_byte)v;
		}
	}
}

static void bmpr_convert_row_24(struct iwbmpreadcontext *rctx,const iw_byte *src, size_t row)
{
	int i;
	for(i=0;i<rctx->width;i++) {
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 0] = src[i*3+2];
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 1] = src[i*3+1];
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 2] = src[i*3+0];
	}
}

static void bmpr_convert_row_8(struct iwbmpreadcontext *rctx,const iw_byte *src, size_t row)
{
	int i;
	for(i=0;i<rctx->width;i++) {
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 0] = rctx->palette.entry[src[i]].r;
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 1] = rctx->palette.entry[src[i]].g;
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 2] = rctx->palette.entry[src[i]].b;
	}
}

static void bmpr_convert_row_4(struct iwbmpreadcontext *rctx,const iw_byte *src, size_t row)
{
	int i;
	int pal_index;

	for(i=0;i<rctx->width;i++) {
		pal_index = (i&0x1) ? src[i/2]&0x0f : src[i/2]>>4;
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 0] = rctx->palette.entry[pal_index].r;
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 1] = rctx->palette.entry[pal_index].g;
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 2] = rctx->palette.entry[pal_index].b;
	}
}

static void bmpr_convert_row_1(struct iwbmpreadcontext *rctx,const iw_byte *src, size_t row)
{
	int i;
	int pal_index;

	for(i=0;i<rctx->width;i++) {
		pal_index = (src[i/8] & (1<<(7-i%8))) ? 1 : 0;
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 0] = rctx->palette.entry[pal_index].r;
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 1] = rctx->palette.entry[pal_index].g;
		rctx->img->pixels[row*rctx->img->bpr + i*3 + 2] = rctx->palette.entry[pal_index].b;
	}
}

static int bmpr_read_uncompressed(struct iwbmpreadcontext *rctx)
{
	iw_byte *rowbuf = NULL;
	size_t bmp_bpr;
	int j;
	size_t targetrow;
	int retval = 0;

	rctx->img->imgtype = IW_IMGTYPE_RGB;
	rctx->img->bit_depth = 8;
	rctx->img->bpr = iw_calc_bytesperrow(rctx->width,24);

	bmp_bpr = iwbmp_calc_bpr(rctx->bitcount,rctx->width);

	rctx->img->pixels = (iw_byte*)iw_malloc_large(rctx->ctx,rctx->img->bpr,rctx->img->height);
	if(!rctx->img->pixels) goto done;

	rowbuf = iw_malloc(rctx->ctx,bmp_bpr);

	for(j=0;j<rctx->img->height;j++) {
		targetrow = rctx->topdown ? j : rctx->img->height-1-j;

		// Read a row of the BMP file.
		if(!iwbmp_read(rctx,rowbuf,bmp_bpr)) {
			goto done;
		}
		switch(rctx->bitcount) {
		case 32:
		case 16:
			bmpr_convert_row_32_16(rctx,rowbuf,targetrow);
			break;
		case 24:
			bmpr_convert_row_24(rctx,rowbuf,targetrow);
			break;
		case 8:
			bmpr_convert_row_8(rctx,rowbuf,targetrow);
			break;
		case 4:
			bmpr_convert_row_4(rctx,rowbuf,targetrow);
			break;
		case 1:
			bmpr_convert_row_1(rctx,rowbuf,targetrow);
			break;
		}
	}

	retval = 1;
done:
	if(rowbuf) iw_free(rctx->ctx,rowbuf);
	return retval;
}

// Read and decompress RLE8 or RLE4-compressed bits, and write pixels to
// rctx->img->pixels.
static int bmpr_read_rle_internal(struct iwbmpreadcontext *rctx)
{
	int retval = 0;
	int pos_x, pos_y;
	iw_byte buf[255];
	size_t n_pix;
	size_t n_bytes;
	size_t i;
	size_t pal_index;

	// The position of the next pixel to set.
	// pos_y is in IW coordinates (top=0), not BMP coordinates (bottom=0).
	pos_x = 0;
	pos_y = rctx->img->height-1;

	// Initially make all pixels transparent, so that any any pixels we
	// don't modify will be transparent.
	iw_zeromem(rctx->img->pixels,rctx->img->bpr*rctx->img->height);

	while(1) {
		// If we've reached the end of the bitmap, stop.
		if(pos_y<0) break;
		if(pos_y==0 && pos_x>=rctx->img->width) break;

		if(!iwbmp_read(rctx,buf,2)) goto done;
		if(buf[0]==0) {
			if(buf[1]==0) {
				// End of Line
				pos_y--;
				pos_x=0;
			}
			else if(buf[1]==1) {
				// (Premature) End of Bitmap
				break;
			}
			else if(buf[1]==2) {
				// DELTA: The next two bytes are unsigned values representing
				// the relative position of the next pixel from the "current
				// position".
				// I interpret "current position" to mean the position at which
				// the next pixel would normally have been.
				if(!iwbmp_read(rctx,buf,2)) goto done;

				if(pos_x<rctx->img->width) pos_x += buf[0];
				pos_y -= buf[1];
			}
			else {
				// A uncompressed segment
				n_pix = (size_t)buf[1]; // Number of uncompressed pixels which follow
				if(rctx->compression==IWBMP_BI_RLE4) {
					n_bytes = ((n_pix+3)/4)*2;
				}
				else {
					n_bytes = ((n_pix+1)/2)*2;
				}
				if(!iwbmp_read(rctx,buf,n_bytes)) goto done;
				for(i=0;i<n_pix;i++) {
					if(pos_x<rctx->img->width) {
						if(rctx->compression==IWBMP_BI_RLE4) {
							pal_index = (i%2) ? buf[i/2]&0x0f : buf[i/2]>>4;
						}
						else {
							pal_index = buf[i];
						}
						rctx->img->pixels[rctx->img->bpr*pos_y + pos_x*4 + 0] = rctx->palette.entry[pal_index].r;
						rctx->img->pixels[rctx->img->bpr*pos_y + pos_x*4 + 1] = rctx->palette.entry[pal_index].g;
						rctx->img->pixels[rctx->img->bpr*pos_y + pos_x*4 + 2] = rctx->palette.entry[pal_index].b;
						rctx->img->pixels[rctx->img->bpr*pos_y + pos_x*4 + 3] = 255;
						pos_x++;
					}
				}
			}
		}
		else {
			// An RLE-compressed segment
			n_pix = (size_t)buf[0];
			for(i=0;i<n_pix;i++) {
				if(pos_x<rctx->img->width) {
					if(rctx->compression==IWBMP_BI_RLE4) {
						pal_index = (i%2) ? buf[1]&0x0f : buf[1]>>4;
					}
					else {
						pal_index = buf[1];
					}
					rctx->img->pixels[rctx->img->bpr*pos_y + pos_x*4 + 0] = rctx->palette.entry[pal_index].r;
					rctx->img->pixels[rctx->img->bpr*pos_y + pos_x*4 + 1] = rctx->palette.entry[pal_index].g;
					rctx->img->pixels[rctx->img->bpr*pos_y + pos_x*4 + 2] = rctx->palette.entry[pal_index].b;
					rctx->img->pixels[rctx->img->bpr*pos_y + pos_x*4 + 3] = 255;
					pos_x++;
				}
			}
		}
	}

	retval = 1;
done:
	return retval;
}

static int bmpr_has_transparency(struct iw_image *img)
{
	int i,j;

	if(img->imgtype!=IW_IMGTYPE_RGBA) return 0;

	for(j=0;j<img->height;j++) {
		for(i=0;i<img->width;i++) {
			if(img->pixels[j*img->bpr + i*4 + 3] != 255)
				return 1;
		}
	}
	return 0;
}

// Remove the alpha channel.
// This doesn't free the extra memory used by the alpha channel, it just
// moves the pixels around in-place.
static void bmpr_strip_alpha(struct iw_image *img)
{
	int i,j;
	size_t oldbpr;

	img->imgtype = IW_IMGTYPE_RGB;
	oldbpr = img->bpr;
	img->bpr = iw_calc_bytesperrow(img->width,24);

	for(j=0;j<img->height;j++) {
		for(i=0;i<img->width;i++) {
			img->pixels[j*img->bpr + i*3 + 0] = img->pixels[j*oldbpr + i*4 + 0];
			img->pixels[j*img->bpr + i*3 + 1] = img->pixels[j*oldbpr + i*4 + 1];
			img->pixels[j*img->bpr + i*3 + 2] = img->pixels[j*oldbpr + i*4 + 2];
		}
	}
}

static int bmpr_read_rle(struct iwbmpreadcontext *rctx)
{
	int retval = 0;

	if(!(rctx->compression==IWBMP_BI_RLE8 && rctx->bitcount==8) &&
		!(rctx->compression==IWBMP_BI_RLE4 && rctx->bitcount==4))
	{
		iw_set_error(rctx->ctx,"Compression type incompatible with image type");
	}

	if(rctx->topdown) {
		// The documentation says that top-down images may not be compressed.
		iw_set_error(rctx->ctx,"Compression not allowed with top-down images");
	}

	// RLE-compressed BMP images don't have to assign a color to every pixel,
	// and it's reasonable to interpret undefined pixels as transparent.
	// I'm not going to worry about handling compressed BMP images as
	// efficiently as possible, so start with an RGBA image, and convert to
	// RGB format later if (as is almost always the case) there was no
	// transparency.
	rctx->img->imgtype = IW_IMGTYPE_RGBA;
	rctx->img->bit_depth = 8;
	rctx->img->bpr = iw_calc_bytesperrow(rctx->width,32);

	rctx->img->pixels = (iw_byte*)iw_malloc_large(rctx->ctx,rctx->img->bpr,rctx->img->height);
	if(!rctx->img->pixels) goto done;

	if(!bmpr_read_rle_internal(rctx)) goto done;

	if(!bmpr_has_transparency(rctx->img)) {
		bmpr_strip_alpha(rctx->img);
	}

	retval = 1;
done:
	return retval;
}

static int iwbmp_read_bits(struct iwbmpreadcontext *rctx)
{
	int retval = 0;

	rctx->img->width = rctx->width;
	rctx->img->height = rctx->height;

	// If applicable, use the fileheader's "bits offset" field to locate the
	// bitmap bits.
	if(rctx->fileheader_size>0) {
		size_t expected_offbits;

		expected_offbits = rctx->fileheader_size + rctx->infoheader_size +
			rctx->bitfields_nbytes + rctx->palette_nbytes;

		if(rctx->bfOffBits==expected_offbits) {
			;
		}
		else if(rctx->bfOffBits>expected_offbits && rctx->bfOffBits<1000000) {
			// Apparently, there's some extra space between the header data and
			// the bits. If it's not unreasonably large, skip over it.
			if(!iwbmp_skip_bytes(rctx, rctx->bfOffBits - expected_offbits)) goto done;
		}
		else {
			iw_set_error(rctx->ctx,"Invalid BMP bits offset");
			goto done;
		}
	}

	if(rctx->compression==IWBMP_BI_RGB) {
		if(!bmpr_read_uncompressed(rctx)) goto done;
	}
	else if(rctx->compression==IWBMP_BI_RLE8 || rctx->compression==IWBMP_BI_RLE4) {
		if(!bmpr_read_rle(rctx)) goto done;
	}
	else {
		iw_set_errorf(rctx->ctx,"Unsupported BMP compression type (%d)",(int)rctx->compression);
		goto done;
	}

	retval = 1;
done:
	return retval;
}

static void iwbmpr_misc_config(struct iw_context *ctx, struct iwbmpreadcontext *rctx)
{
	struct iw_csdescr csdescr;

	// Tell IW the colorspace.
	iw_make_srgb_csdescr(&csdescr,IW_SRGB_INTENT_PERCEPTUAL);
	iw_set_input_colorspace(ctx,&csdescr);

	// Tell IW the significant bits.
	if(rctx->bitcount==16 || rctx->bitcount==32) {
		if(rctx->bf_bits_count[0]!=8)
			iw_set_input_max_color_code(ctx,0, (1 << rctx->bf_bits_count[0])-1 );
		if(rctx->bf_bits_count[1]!=8)
			iw_set_input_max_color_code(ctx,1, (1 << rctx->bf_bits_count[1])-1 );
		if(rctx->bf_bits_count[2]!=8)
			iw_set_input_max_color_code(ctx,2, (1 << rctx->bf_bits_count[2])-1 );
	}
}

IW_IMPL(int) iw_read_bmp_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwbmpreadcontext rctx;
	struct iw_image img;
	int retval = 0;

	iw_zeromem(&rctx,sizeof(struct iwbmpreadcontext));
	iw_zeromem(&img,sizeof(struct iw_image));

	rctx.ctx = ctx;
	rctx.img = &img;
	rctx.iodescr = iodescr;

	if(!iwbmp_read_file_header(&rctx)) goto done;
	if(!iwbmp_read_info_header(&rctx)) goto done;

	iwbmp_set_default_bitfields(&rctx);
	if(rctx.bitfields_nbytes>0) {
		if(!iwbmp_read_bitfields(&rctx)) goto done;
	}

	if(rctx.palette_entries>0) {
		if(!iwbmp_read_palette(&rctx)) goto done;
	}
	if(!iwbmp_read_bits(&rctx)) goto done;

	iw_set_input_image(ctx, &img);

	iwbmpr_misc_config(ctx, &rctx);

	retval = 1;
done:
	if(!retval) {
		iw_set_error(ctx,"BMP read failed");
	}
	return retval;
}

struct iwbmpwritecontext {
	int include_file_header;
	int bitcount;
	int palentries;
	int compressed;
	size_t bitfields_size;
	size_t palsize;
	size_t unc_dst_bpr;
	size_t unc_bitssize;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	const struct iw_palette *pal;
	size_t total_written;
	int bf_amt_to_shift[3]; // For 16-bit images
};

static void iwbmp_write(struct iwbmpwritecontext *wctx, const void *buf, size_t n)
{
	(*wctx->iodescr->write_fn)(wctx->ctx,wctx->iodescr,buf,n);
	wctx->total_written+=n;
}

static void iwbmp_convert_row1(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	int i;
	int m;

	for(i=0;i<width;i++) {
		m = i%8;
		if(m==0)
			dstrow[i/8] = srcrow[i]<<7;
		else
			dstrow[i/8] |= srcrow[i]<<(7-m);
	}
}

static void iwbmp_convert_row4(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	int i;

	for(i=0;i<width;i++) {
		if(i%2==0)
			dstrow[i/2] = srcrow[i]<<4;
		else
			dstrow[i/2] |= srcrow[i];
	}
}

static void iwbmp_convert_row8(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	memcpy(dstrow,srcrow,width);
}

static void bmpw_convert_row_16(struct iwbmpwritecontext *wctx, const iw_byte *srcrow,
	iw_byte *dstrow, int width)
{
	int i;
	unsigned int v;

	for(i=0;i<width;i++) {
		v = srcrow[i*3+0] << wctx->bf_amt_to_shift[0];
		v |= srcrow[i*3+1] << wctx->bf_amt_to_shift[1];
		v |= srcrow[i*3+2] << wctx->bf_amt_to_shift[2];
		dstrow[i*2+0] = (iw_byte)(v&0xff);
		dstrow[i*2+1] = (iw_byte)(v>>8);
	}
}

static void iwbmp_convert_row24(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	int i;

	for(i=0;i<width;i++) {
		dstrow[i*3+0] = srcrow[i*3+2];
		dstrow[i*3+1] = srcrow[i*3+1];
		dstrow[i*3+2] = srcrow[i*3+0];
	}
}

static void iwbmp_write_file_header(struct iwbmpwritecontext *wctx)
{
	iw_byte fileheader[14];

	if(!wctx->include_file_header) return;

	iw_zeromem(fileheader,sizeof(fileheader));
	fileheader[0] = 66; // 'B'
	fileheader[1] = 77; // 'M'

	// This will be overwritten later, if the bitmap was compressed.
	iw_set_ui32le(&fileheader[ 2],14+40+(unsigned int)(wctx->bitfields_size+wctx->palsize)+
		(unsigned int)wctx->unc_bitssize); // bfSize

	iw_set_ui32le(&fileheader[10],14+40+(unsigned int)(wctx->bitfields_size+wctx->palsize)); // bfOffBits
	iwbmp_write(wctx,fileheader,14);
}

static void iwbmp_write_bmp_header(struct iwbmpwritecontext *wctx)
{
	unsigned int dens_x, dens_y;
	unsigned int cmpr;
	iw_byte header[40];

	iw_zeromem(header,sizeof(header));

	iw_set_ui32le(&header[ 0],40);      // biSize
	iw_set_ui32le(&header[ 4],wctx->img->width);  // biWidth
	iw_set_ui32le(&header[ 8],wctx->img->height); // biHeight
	iw_set_ui16le(&header[12],1);    // biPlanes
	iw_set_ui16le(&header[14],wctx->bitcount);   // biBitCount

	cmpr = IWBMP_BI_RGB;
	if(wctx->compressed) {
		if(wctx->bitcount==8) cmpr = IWBMP_BI_RLE8;
		else if(wctx->bitcount==4) cmpr = IWBMP_BI_RLE4;
	}
	else if(wctx->bitfields_size>0) {
		cmpr = IWBMP_BI_BITFIELDS;
	}
	iw_set_ui32le(&header[16],cmpr); // biCompression

	iw_set_ui32le(&header[20],(unsigned int)wctx->unc_bitssize); // biSizeImage

	if(wctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
		dens_x = (unsigned int)(0.5+wctx->img->density_x);
		dens_y = (unsigned int)(0.5+wctx->img->density_y);
	}
	else {
		dens_x = dens_y = 2835;
	}
	iw_set_ui32le(&header[24],dens_x); // biXPelsPerMeter
	iw_set_ui32le(&header[28],dens_y); // biYPelsPerMeter

	iw_set_ui32le(&header[32],wctx->palentries);    // biClrUsed
	//iw_set_ui32le(&header[36],0);    // biClrImportant
	iwbmp_write(wctx,header,40);
}

static int maxcc_to_bitdepth(unsigned int mcc)
{
	if(mcc<=1) return 1;
	if(mcc<=3) return 2;
	if(mcc<=7) return 3;
	if(mcc<=15) return 4;
	if(mcc<=31) return 5;
	if(mcc<=63) return 6;
	if(mcc<=127) return 7;
	return 8;
}

// Write the BITFIELDS segment, and set the wctx->bf_amt_to_shift[] values.
static int iwbmp_write_bitfields(struct iwbmpwritecontext *wctx)
{
	iw_byte buf[12];
	iw_uint32 bf_r, bf_g, bf_b;
	int bits_r, bits_g, bits_b; 

	if(wctx->bitcount != 16) return 0;

	bits_r = maxcc_to_bitdepth(wctx->img->maxcolor_r);
	bits_g = maxcc_to_bitdepth(wctx->img->maxcolor_g);
	bits_b = maxcc_to_bitdepth(wctx->img->maxcolor_b);

	if(bits_r + bits_g + bits_b > 16) {
		iw_set_error(wctx->ctx,"Cannot write a BMP image in this color format");
		return 0;
	}

	wctx->bf_amt_to_shift[0] = bits_g + bits_b;
	wctx->bf_amt_to_shift[1] = bits_b;
	wctx->bf_amt_to_shift[2] = 0;

	bf_r = wctx->img->maxcolor_r << wctx->bf_amt_to_shift[0];
	bf_g = wctx->img->maxcolor_g << wctx->bf_amt_to_shift[1];
	bf_b = wctx->img->maxcolor_b << wctx->bf_amt_to_shift[2];

	iw_set_ui32le(&buf[0],bf_r);
	iw_set_ui32le(&buf[4],bf_g);
	iw_set_ui32le(&buf[8],bf_b);
	iwbmp_write(wctx,buf,12);
	return 1;
}

static void iwbmp_write_palette(struct iwbmpwritecontext *wctx)
{
	int i;
	iw_byte buf[4];

	if(wctx->palentries<1) return;

	buf[3] = 0; // Reserved field; always 0.

	for(i=0;i<wctx->palentries;i++) {
		if(i<wctx->pal->num_entries) {
			if(wctx->pal->entry[i].a == 0) {
				// A transparent color. Because of the way we handle writing
				// transparent BMP images, the first palette entry may be a
				// fully transparent color, whose index will not be used when
				// we write the image. But many apps will interpret our
				// "transparent" pixels as having color #0. So, set it to an
				// arbitrary high-contrast color (magenta).
				// If and when we support writing background color labels,
				// that's probably what we should use here instead.
				buf[0] = 255;
				buf[1] = 0;
				buf[2] = 255;
			}
			else {
				buf[0] = wctx->pal->entry[i].b;
				buf[1] = wctx->pal->entry[i].g;
				buf[2] = wctx->pal->entry[i].r;
			}
		}
		else {
			buf[0] = buf[1] = buf[2] = 0;
		}
		iwbmp_write(wctx,buf,4);
	}
}

struct rle_context {
	struct iw_context *ctx;
	struct iwbmpwritecontext *wctx;
	const iw_byte *srcrow;

	size_t img_width;
	int cur_row; // current row; 0=top (last)

	// Position in srcrow of the first byte that hasn't been written to the
	// output file
	size_t pending_data_start;

	// Current number of uncompressible bytes that haven't been written yet
	// (starting at pending_data_start)
	size_t unc_len;

	// Current number of identical bytes that haven't been written yet
	// (starting at pending_data_start+unc_len)
	size_t run_len;

	// The value of the bytes referred to by run_len.
	// Valid if run_len>0.
	iw_byte run_byte;

	size_t total_bytes_written; // Bytes written, after compression
};

//============================ RLE8 encoder ============================

// TODO: The RLE8 and RLE4 encoders are more different than they should be.
// The RLE8 encoder could probably be made more similar to the (more
// complicated) RLE4 encoder.

static void rle8_write_unc(struct rle_context *rlectx)
{
	size_t i;
	iw_byte dstbuf[2];

	if(rlectx->unc_len<1) return;
	if(rlectx->unc_len>=3 && (rlectx->unc_len&1)) {
		iw_set_error(rlectx->ctx,"Internal: RLE encode error 4");
		return;
	}
	if(rlectx->unc_len>254) {
		iw_set_error(rlectx->ctx,"Internal: RLE encode error 5");
		return;
	}

	if(rlectx->unc_len<3) {
		// The minimum length for a noncompressed run is 3. For shorter runs
		// write them "compressed".
		for(i=0;i<rlectx->unc_len;i++) {
			dstbuf[0] = 0x01;  // count
			dstbuf[1] = rlectx->srcrow[i+rlectx->pending_data_start]; // value
			iwbmp_write(rlectx->wctx,dstbuf,2);
			rlectx->total_bytes_written+=2;
		}
	}
	else {
		dstbuf[0] = 0x00;
		dstbuf[1] = (iw_byte)rlectx->unc_len;
		iwbmp_write(rlectx->wctx,dstbuf,2);
		rlectx->total_bytes_written+=2;
		iwbmp_write(rlectx->wctx,&rlectx->srcrow[rlectx->pending_data_start],rlectx->unc_len);
		rlectx->total_bytes_written+=rlectx->unc_len;
		if(rlectx->unc_len&0x1) {
			// Need a padding byte if the length was odd. (This shouldn't
			// happen, because we never write odd-length UNC segments.)
			dstbuf[0] = 0x00;
			iwbmp_write(rlectx->wctx,dstbuf,1);
			rlectx->total_bytes_written+=1;
		}
	}

	rlectx->pending_data_start+=rlectx->unc_len;
	rlectx->unc_len=0;
}

static void rle8_write_unc_and_run(struct rle_context *rlectx)
{
	iw_byte dstbuf[2];

	rle8_write_unc(rlectx);

	if(rlectx->run_len<1) {
		return;
	}
	if(rlectx->run_len>255) {
		iw_set_error(rlectx->ctx,"Internal: RLE encode error 6");
		return;
	}

	dstbuf[0] = (iw_byte)rlectx->run_len;
	dstbuf[1] = rlectx->run_byte;
	iwbmp_write(rlectx->wctx,dstbuf,2);
	rlectx->total_bytes_written+=2;

	rlectx->pending_data_start+=rlectx->run_len;
	rlectx->run_len=0;
}

static void rle_write_trns(struct rle_context *rlectx, int num_trns)
{
	iw_byte dstbuf[4];
	int num_remaining = num_trns;
	int num_to_write;

	while(num_remaining>0) {
		num_to_write = num_remaining;
		if(num_to_write>255) num_to_write=255;
		dstbuf[0]=0x00; // 00 02 = Delta
		dstbuf[1]=0x02;
		dstbuf[2]=(iw_byte)num_to_write; // X offset
		dstbuf[3]=0x00; // Y offset
		iwbmp_write(rlectx->wctx,dstbuf,4);
		rlectx->total_bytes_written+=4;
		num_remaining -= num_to_write;
	}
	rlectx->pending_data_start += num_trns;
}

// The RLE format used by BMP files is pretty simple, but I've gone to some
// effort to optimize it for file size, which makes for a complicated
// algorithm.
// The overall idea:
// We defer writing data until certain conditions are met. In the meantime,
// we split the unwritten data into two segments:
//  "UNC": data classified as uncompressible
//  "RUN": data classified as compressible. All bytes in this segment must be
//    identical.
// The RUN segment always follows the UNC segment.
// For each byte in turn, we examine the current state, and do one of a number
// of things, such as:
//    - add it to RUN
//    - add it to UNC (if there is no RUN)
//    - move RUN into UNC, then add it to RUN (or to UNC)
//    - move UNC and RUN to the file, then make it the new RUN
// Then, we check to see if we've accumulated enough data that something needs
// to be written out.
static int rle8_compress_row(struct rle_context *rlectx)
{
	size_t i;
	iw_byte dstbuf[2];
	iw_byte next_byte;
	int next_pix_is_trns;
	int num_trns = 0; // number of consecutive transparent pixels seen
	int retval = 0;

	rlectx->pending_data_start=0;
	rlectx->unc_len=0;
	rlectx->run_len=0;

	for(i=0;i<rlectx->img_width;i++) {

		// Read the next byte.
		next_byte = rlectx->srcrow[i];

		next_pix_is_trns = (rlectx->wctx->pal->entry[next_byte].a==0);

		if(num_trns>0 && !next_pix_is_trns) {
			rle_write_trns(rlectx,num_trns);
			num_trns=0;
		}
		else if(next_pix_is_trns) {
			if (rlectx->unc_len>0 || rlectx->run_len>0) {
				rle8_write_unc_and_run(rlectx);
			}
			num_trns++;
			continue;
		}

		// --------------------------------------------------------------
		// Add the byte we just read to either the UNC or the RUN data.

		if(rlectx->run_len>0 && next_byte==rlectx->run_byte) {
			// Byte fits in the current run; add it.
			rlectx->run_len++;
		}
		else if(rlectx->run_len==0) {
			// We don't have a RUN, so we can put this byte there.
			rlectx->run_len = 1;
			rlectx->run_byte = next_byte;
		}
		else if(rlectx->unc_len==0 && rlectx->run_len==1) {
			// We have one previous byte, and it's different from this one.
			// Move it to UNC, and make this one the RUN.
			rlectx->unc_len++;
			rlectx->run_byte = next_byte;
		}
		else if(rlectx->unc_len>0 && rlectx->run_len<(rlectx->unc_len==1 ? 3U : 4U)) {
			// We have a run, but it's not long enough to be beneficial.
			// Convert it to uncompressed bytes.
			// A good rule is that a run length of 4 or more (3 or more if
			// unc_len=1) should always be run-legth encoded.
			rlectx->unc_len += rlectx->run_len;
			rlectx->run_len = 0;
			// If UNC is now odd and >1, add the next byte to it to make it even.
			// Otherwise, add it to RUN.
			if(rlectx->unc_len>=3 && (rlectx->unc_len&0x1)) {
				rlectx->unc_len++;
			}
			else {
				rlectx->run_len = 1;
				rlectx->run_byte = next_byte;
			}
		}
		else {
			// Nowhere to put the byte: write out everything, and start fresh.
			rle8_write_unc_and_run(rlectx);
			rlectx->run_len = 1;
			rlectx->run_byte = next_byte;
		}

		// --------------------------------------------------------------
		// If we hit certain high water marks, write out the current data.

		if(rlectx->unc_len>=254) {
			// Our maximum size for an UNC segment.
			rle8_write_unc(rlectx);
		}
		else if(rlectx->unc_len>0 && (rlectx->unc_len+rlectx->run_len)>254) {
			// It will not be possible to coalesce the RUN into the UNC (it
			// would be too big) so write out the UNC.
			rle8_write_unc(rlectx);
		}
		else if(rlectx->run_len>=255) {
			// The maximum size for an RLE segment.
			rle8_write_unc_and_run(rlectx);
		}

		// --------------------------------------------------------------
		// Sanity checks. These can be removed if we're sure the algorithm
		// is bug-free.

		// We don't allow unc_len to be odd (except temporarily), except
		// that it can be 1.
		// What's special about 1 is that if we add another byte to it, it
		// increases the cost. For 3,5,...,253, we can add another byte for
		// free, so we should never fail to do that.
		if((rlectx->unc_len&0x1) && rlectx->unc_len!=1) {
			iw_set_errorf(rlectx->ctx,"Internal: BMP RLE encode error 1");
			goto done;
		}

		// unc_len can be at most 252 at this point.
		// If it were 254, it should have been written out already.
		if(rlectx->unc_len>252) {
			iw_set_error(rlectx->ctx,"Internal: BMP RLE encode error 2");
			goto done;
		}

		// run_len can be at most 254 at this point.
		// If it were 255, it should have been written out already.
		if(rlectx->run_len>254) {
			iw_set_error(rlectx->ctx,"Internal: BMP RLE encode error 3");
			goto done;
		}
	}

	// End of row. Write out anything left over.
	rle8_write_unc_and_run(rlectx);

	// Write an end-of-line marker (0 0), or if this is the last row,
	// an end-of-bitmap marker (0 1).
	dstbuf[0]=0x00;
	dstbuf[1]= (rlectx->cur_row==0)? 0x01 : 0x00;
	iwbmp_write(rlectx->wctx,dstbuf,2);
	rlectx->total_bytes_written+=2;

	retval = 1;

done:
	return retval;
}

//============================ RLE4 encoder ============================

// Calculate the most efficient way to split a run of uncompressible pixels.
// This only finds the first place to split the run. If the run is still
// over 255 pixels, call it again to find the next split.
static size_t rle4_get_best_unc_split(size_t n)
{
	// For <=255 pixels, we can never do better than storing it as one run.
	if(n<=255) return n;

	// With runs of 252, we can store 252/128 = 1.96875 pixels/byte.
	// With runs of 255, we can store 255/130 = 1.96153 pixels/byte.
	// Hence, using runs of 252 is the most efficient way to store a large
	// number of uncompressible pixels.
	// (Lengths other than 252 or 255 are no help.)
	// However, there are three exceptional cases where, if we split at 252,
	// the most efficient encoding will no longer be possible:
	if(n==257 || n==510 || n==765) return 255;

	return 252;
}

// Returns the incremental cost of adding a pixel to the current UNC
// (which is always either 0 or 2).
// To derive this function, I calculated the optimal cost of every length,
// and enumerated the exceptions to the (n%4)?0:2 rule.
// The exceptions are mostly caused by the cases where
// rle4_get_best_unc_split() returns 255 instead of 252.
static int rle4_get_incr_unc_cost(struct rle_context *rlectx)
{
	int n;
	int m;

	n = (int)rlectx->unc_len;

	if(n==2 || n==255 || n==257 || n==507 || n==510) return 2;
	if(n==256 || n==508) return 0;

	if(n>=759) {
		m = n%252;
		if(m==3 || m==6 || m==9) return 2;
		if(m==4 || m==8) return 0;
	}

	return (n%4)?0:2;
}

static void rle4_write_unc(struct rle_context *rlectx)
{
	iw_byte dstbuf[128];
	size_t pixels_to_write;
	size_t bytes_to_write;

	if(rlectx->unc_len<1) return;

	// Note that, unlike the RLE8 encoder, we allow this function to be called
	// with uncompressed runs of arbitrary length.

	while(rlectx->unc_len>0) {
		pixels_to_write = rle4_get_best_unc_split(rlectx->unc_len);

		if(pixels_to_write<3) {
			// The minimum length for an uncompressed run is 3. For shorter runs
			// write them "compressed".
			dstbuf[0] = (iw_byte)pixels_to_write;
			dstbuf[1] = (rlectx->srcrow[rlectx->pending_data_start]<<4);
			if(pixels_to_write>1)
				dstbuf[1] |= (rlectx->srcrow[rlectx->pending_data_start+1]);

			// The actual writing will occur below. Just indicate how many bytes
			// of dstbuf[] to write.
			bytes_to_write = 2;
		}
		else {
			size_t i;

			// Write the length of the uncompressed run.
			dstbuf[0] = 0x00;
			dstbuf[1] = (iw_byte)pixels_to_write;
			iwbmp_write(rlectx->wctx,dstbuf,2);
			rlectx->total_bytes_written+=2;

			// Put the data to write in dstbuf[].
			bytes_to_write = 2*((pixels_to_write+3)/4);
			iw_zeromem(dstbuf,bytes_to_write);

			for(i=0;i<pixels_to_write;i++) {
				if(i&0x1) dstbuf[i/2] |= rlectx->srcrow[rlectx->pending_data_start+i];
				else dstbuf[i/2] = rlectx->srcrow[rlectx->pending_data_start+i]<<4;
			}
		}

		iwbmp_write(rlectx->wctx,dstbuf,bytes_to_write);
		rlectx->total_bytes_written += bytes_to_write;
		rlectx->unc_len -= pixels_to_write;
		rlectx->pending_data_start += pixels_to_write;
	}
}

static void rle4_write_unc_and_run(struct rle_context *rlectx)
{
	iw_byte dstbuf[2];

	rle4_write_unc(rlectx);

	if(rlectx->run_len<1) {
		return;
	}
	if(rlectx->run_len>255) {
		iw_set_error(rlectx->ctx,"Internal: RLE encode error 6");
		return;
	}

	dstbuf[0] = (iw_byte)rlectx->run_len;
	dstbuf[1] = rlectx->run_byte;
	iwbmp_write(rlectx->wctx,dstbuf,2);
	rlectx->total_bytes_written+=2;

	rlectx->pending_data_start+=rlectx->run_len;
	rlectx->run_len=0;
}

// Should we move the pending compressible data to the "uncompressed"
// segment (return 1), or should we write it to disk as a compressed run of
// pixels (0)?
static int ok_to_move_to_unc(struct rle_context *rlectx)
{
	// This logic is probably not optimal in every case.
	// One possible improvement might be to adjust the thresholds when
	// unc_len+run_len is around 255 or higher.
	// Other improvements might require looking ahead at pixels we haven't
	// read yet.

	if(rlectx->unc_len==0) {
		return (rlectx->run_len<4);
	}
	else if(rlectx->unc_len<=2) {
		return (rlectx->run_len<6);
	}
	else {
		return (rlectx->run_len<8);
	}
	return 0;
}

static int rle4_compress_row(struct rle_context *rlectx)
{
	size_t i;
	iw_byte dstbuf[2];
	iw_byte next_pix;
	int next_pix_is_trns;
	int num_trns = 0; // number of consecutive transparent pixels seen
	int retval = 0;

	rlectx->pending_data_start=0;
	rlectx->unc_len=0;
	rlectx->run_len=0;

	for(i=0;i<rlectx->img_width;i++) {

		// Read the next pixel
		next_pix = rlectx->srcrow[i];

		next_pix_is_trns = (rlectx->wctx->pal->entry[next_pix].a==0);
		if(num_trns>0 && !next_pix_is_trns) {
			rle_write_trns(rlectx,num_trns);
			num_trns=0;
		}
		else if(next_pix_is_trns) {
			if (rlectx->unc_len>0 || rlectx->run_len>0) {
				rle4_write_unc_and_run(rlectx);
			}
			num_trns++;
			continue;
		}

		// --------------------------------------------------------------
		// Add the pixel we just read to either the UNC or the RUN data.

		if(rlectx->run_len==0) {
			// We don't have a RUN, so we can put this pixel there.
			rlectx->run_len = 1;
			rlectx->run_byte = next_pix<<4;
		}
		else if(rlectx->run_len==1) {
			// If the run is 1, we can always add a 2nd pixel
			rlectx->run_byte |= next_pix;
			rlectx->run_len++;
		}
		else if(rlectx->run_len>=2 && (rlectx->run_len&1)==0 && next_pix==(rlectx->run_byte>>4)) {
			// pixel fits in the current run; add it.
			rlectx->run_len++;
		}
		else if(rlectx->run_len>=3 && (rlectx->run_len&1) && next_pix==(rlectx->run_byte&0x0f)) {
			// pixel fits in the current run; add it.
			rlectx->run_len++;
		}
		else if(rlectx->unc_len==0 && rlectx->run_len==2) {
			// We have one previous byte, and it's different from this one.
			// Move it to UNC, and make this one the RUN.
			rlectx->unc_len+=rlectx->run_len;
			rlectx->run_byte = next_pix<<4;
			rlectx->run_len = 1;
		}
		else if(ok_to_move_to_unc(rlectx)) {
			// We have a compressible run, but we think it's not long enough to be
			// beneficial. Convert it to uncompressed bytes.
			rlectx->unc_len += rlectx->run_len;

			// Put the next byte in RLE. (It might get moved to UNC, below.)
			rlectx->run_len = 1;
			rlectx->run_byte = next_pix<<4;
		}
		else {
			// Nowhere to put the byte: write out everything, and start fresh.
			rle4_write_unc_and_run(rlectx);
			rlectx->run_len = 1;
			rlectx->run_byte = next_pix<<4;
		}

		// --------------------------------------------------------------
		// If any RUN bytes that can be added to UNC for free, do so.
		while(rlectx->unc_len>0 && rlectx->run_len>0 && rle4_get_incr_unc_cost(rlectx)==0) {
			rlectx->unc_len++;
			rlectx->run_len--;
		}

		// --------------------------------------------------------------
		// If we hit certain high water marks, write out the current data.

		if(rlectx->run_len>=255) {
			// The maximum size for an RLE segment.
			rle4_write_unc_and_run(rlectx);
		}

		// --------------------------------------------------------------
		// Sanity check(s). This can be removed if we're sure the algorithm
		// is bug-free.

		// run_len can be at most 254 at this point.
		// If it were 255, it should have been written out already.
		if(rlectx->run_len>255) {
			iw_set_error(rlectx->ctx,"Internal: BMP RLE encode error 3");
			goto done;
		}
	}

	// End of row. Write out anything left over.
	rle4_write_unc_and_run(rlectx);

	// Write an end-of-line marker (0 0), or if this is the last row,
	// an end-of-bitmap marker (0 1).
	dstbuf[0]=0x00;
	dstbuf[1]= (rlectx->cur_row==0)? 0x01 : 0x00;
	iwbmp_write(rlectx->wctx,dstbuf,2);
	rlectx->total_bytes_written+=2;

	retval = 1;

done:
	return retval;
}

//======================================================================

// Seek back and write the "file size" and "bits size" fields.
static int rle_patch_file_size(struct iwbmpwritecontext *wctx,size_t rlesize)
{
	iw_byte buf[4];
	size_t fileheader_size;

	if(!wctx->iodescr->seek_fn) {
		iw_set_error(wctx->ctx,"Writing compressed BMP requires a seek function");
		return 0;
	}

	if(wctx->include_file_header) {
		// Patch the file size in the file header
		(*wctx->iodescr->seek_fn)(wctx->ctx,wctx->iodescr,2,SEEK_SET);
		iw_set_ui32le(buf,(unsigned int)(14+40+wctx->bitfields_size+wctx->palsize+rlesize));
		iwbmp_write(wctx,buf,4);
		fileheader_size = 14;
	}
	else {
		fileheader_size = 0;
	}

	// Patch the "bits" size
	(*wctx->iodescr->seek_fn)(wctx->ctx,wctx->iodescr,fileheader_size+20,SEEK_SET);
	iw_set_ui32le(buf,(unsigned int)rlesize);
	iwbmp_write(wctx,buf,4);

	(*wctx->iodescr->seek_fn)(wctx->ctx,wctx->iodescr,0,SEEK_END);
	return 1;
}

static int iwbmp_write_pixels_compressed(struct iwbmpwritecontext *wctx,
	struct iw_image *img)
{
	struct rle_context rlectx;
	int j;
	int retval = 0;

	iw_zeromem(&rlectx,sizeof(struct rle_context));

	rlectx.ctx = wctx->ctx;
	rlectx.wctx = wctx;
	rlectx.total_bytes_written = 0;
	rlectx.img_width = img->width;

	for(j=img->height-1;j>=0;j--) {
		// Compress and write a row of pixels
		rlectx.srcrow = &img->pixels[j*img->bpr];
		rlectx.cur_row = j;

		if(wctx->bitcount==4) {
			if(!rle4_compress_row(&rlectx)) goto done;
		}
		else if(wctx->bitcount==8) {
			if(!rle8_compress_row(&rlectx)) goto done;
		}
		else {
			goto done;
		}
	}

	// Back-patch the 'file size' and 'bits size' fields
	rle_patch_file_size(wctx,rlectx.total_bytes_written);

	retval = 1;
done:
	return retval;
}

static void iwbmp_write_pixels_uncompressed(struct iwbmpwritecontext *wctx,
	struct iw_image *img)
{
	int j;
	iw_byte *dstrow = NULL;
	const iw_byte *srcrow;

	dstrow = iw_mallocz(wctx->ctx,wctx->unc_dst_bpr);
	if(!dstrow) goto done;

	for(j=img->height-1;j>=0;j--) {
		srcrow = &img->pixels[j*img->bpr];
		switch(wctx->bitcount) {
		case 24: iwbmp_convert_row24(srcrow,dstrow,img->width); break;
		case 16: bmpw_convert_row_16(wctx,srcrow,dstrow,img->width); break;
		case 8: iwbmp_convert_row8(srcrow,dstrow,img->width); break;
		case 4: iwbmp_convert_row4(srcrow,dstrow,img->width); break;
		case 1: iwbmp_convert_row1(srcrow,dstrow,img->width); break;
		}
		iwbmp_write(wctx,dstrow,wctx->unc_dst_bpr);
	}

done:
	if(dstrow) iw_free(wctx->ctx,dstrow);
	return;
}

// 0 = no transparency
// 1 = binary transparency
// 2 = partial transparency
static int check_palette_transparency(const struct iw_palette *p)
{
	int i;
	int retval = 0;

	for(i=0;i<p->num_entries;i++) {
		if(p->entry[i].a!=255) retval=1;
		if(p->entry[i].a!=255 && p->entry[i].a!=0) return 2;
	}
	return retval;
}

static int iwbmp_write_main(struct iwbmpwritecontext *wctx)
{
	struct iw_image *img;
	int cmpr_req;
	int retval = 0;
	int x;

	img = wctx->img;

	cmpr_req = iw_get_value(wctx->ctx,IW_VAL_COMPRESSION);

	// If any kind of compression was requested, use RLE if possible.
	if(cmpr_req==IW_COMPRESSION_AUTO || cmpr_req==IW_COMPRESSION_NONE)
		cmpr_req = IW_COMPRESSION_NONE;
	else
		cmpr_req = IW_COMPRESSION_RLE;

	if(img->imgtype==IW_IMGTYPE_RGB) {
		if(img->reduced_maxcolors) {
			wctx->bitcount=16;
			if(img->maxcolor_r==31 && img->maxcolor_g==31 && img->maxcolor_b==31) {
				// For the default 5-5-5, we don't write a BITFIELDS segment.
				wctx->bf_amt_to_shift[0]=10;
				wctx->bf_amt_to_shift[1]=5;
				wctx->bf_amt_to_shift[2]=0;
				wctx->bitfields_size = 0;
			}
			else {
				wctx->bitfields_size = 12;
			}
		}
		else {
			wctx->bitcount=24;
		}
	}
	else if(img->imgtype==IW_IMGTYPE_PALETTE) {
		if(!wctx->pal) goto done;

		x = check_palette_transparency(wctx->pal);

		if(x==2) {
			iw_set_error(wctx->ctx,"Cannot save this image as a transparent BMP: Has partial transparency");
			goto done;
		}
		else if(x!=0 && cmpr_req!=IW_COMPRESSION_RLE) {
			iw_set_error(wctx->ctx,"Cannot save as a transparent BMP: RLE compression required");
			goto done;
		}

		if(wctx->pal->num_entries<=2 && cmpr_req!=IW_COMPRESSION_RLE)
			wctx->bitcount=1;
		else if(wctx->pal->num_entries<=16)
			wctx->bitcount=4;
		else
			wctx->bitcount=8;
	}
	else if(img->imgtype==IW_IMGTYPE_RGBA) {
		// It should only be possible to get here if the user enabled the transparent-BMP hack.
		iw_set_error(wctx->ctx,"Cannot save this image as a transparent BMP: Too many colors");
		goto done;
	}
	else {
		iw_set_error(wctx->ctx,"Internal: Bad image type for BMP");
		goto done;
	}

	if(cmpr_req==IW_COMPRESSION_RLE && (wctx->bitcount==4 || wctx->bitcount==8)) {
		wctx->compressed = 1;
	}

	wctx->unc_dst_bpr = iwbmp_calc_bpr(wctx->bitcount,img->width);
	wctx->unc_bitssize = wctx->unc_dst_bpr * img->height;
	wctx->palentries = 0;
	if(wctx->pal) {
		wctx->palentries = wctx->pal->num_entries;
		if(wctx->bitcount==1) {
			// The documentation says that if the bitdepth is 1, the palette
			// must contain exactly two entries.
			wctx->palentries=2;
		}
	}
	wctx->palsize = wctx->palentries*4;

	// File header
	iwbmp_write_file_header(wctx);

	// Bitmap header ("BITMAPINFOHEADER")
	iwbmp_write_bmp_header(wctx);

	if(wctx->bitfields_size>0) {
		if(!iwbmp_write_bitfields(wctx)) goto done;
	}

	// Palette
	iwbmp_write_palette(wctx);

	// Pixels
	if(wctx->compressed) {
		if(!iwbmp_write_pixels_compressed(wctx,img)) goto done;
	}
	else {
		iwbmp_write_pixels_uncompressed(wctx,img);
	}

	retval = 1;
done:
	return retval;
}

IW_IMPL(int) iw_write_bmp_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwbmpwritecontext wctx;
	int retval=0;
	struct iw_image img1;

	iw_zeromem(&img1,sizeof(struct iw_image));

	iw_zeromem(&wctx,sizeof(struct iwbmpwritecontext));

	wctx.ctx = ctx;
	wctx.include_file_header = 1;

	wctx.iodescr=iodescr;

	iw_get_output_image(ctx,&img1);
	wctx.img = &img1;

	if(wctx.img->imgtype==IW_IMGTYPE_PALETTE) {
		wctx.pal = iw_get_output_palette(ctx);
		if(!wctx.pal) goto done;
	}

	iwbmp_write_main(&wctx);

	retval=1;

done:
	return retval;
}
