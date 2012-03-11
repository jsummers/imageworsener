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

static int iwbmp_read_bitfields(struct iwbmpreadcontext *rctx)
{
	iw_byte buf[12];
	int low_bit[3];
	int k;

	if(!iwbmp_read(rctx,buf,12)) return 0;

	for(k=0;k<3;k++) {
		rctx->bf_mask[k] = iw_get_ui32le(&buf[k*4]);
		if(rctx->bf_mask[k]==0) return 0;

		// The bits representing the mask for each channel are required to be
		// contiguous, so all we need to do is find the highest and lowest bit.
		rctx->bf_high_bit[k] = find_high_bit(rctx->bf_mask[k]);

		// Check if the mask specifies an invalid bit
		if(rctx->bf_high_bit[k] > (int)(rctx->bitcount-1)) return 0;

		low_bit[k] = find_low_bit(rctx->bf_mask[k]);
		rctx->bf_bits_count[k] = 1+rctx->bf_high_bit[k]-low_bit[k];

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
	if(rctx->bitcount==16) {
		rctx->bf_mask[0]=0x007c00; rctx->bf_bits_count[0]=5; rctx->bf_high_bit[0]=14;
		rctx->bf_mask[1]=0x0003e0; rctx->bf_bits_count[1]=5; rctx->bf_high_bit[1]=9;
		rctx->bf_mask[2]=0x00001f; rctx->bf_bits_count[2]=5; rctx->bf_high_bit[2]=4;
	}
	else if(rctx->bitcount==32) {
		rctx->bf_mask[0]=0xff0000; rctx->bf_bits_count[0]=8; rctx->bf_high_bit[0]=23;
		rctx->bf_mask[1]=0x00ff00; rctx->bf_bits_count[1]=8; rctx->bf_high_bit[1]=15;
		rctx->bf_mask[2]=0x0000ff; rctx->bf_bits_count[2]=8; rctx->bf_high_bit[2]=7;
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
			if(rctx->bf_high_bit[k]>7)
				v >>= (rctx->bf_high_bit[k]-7);
			else if(rctx->bf_high_bit[k]<7)
				v <<= (7-rctx->bf_high_bit[k]);
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
			iw_set_input_sbit(ctx,IW_CHANNELTYPE_RED  ,rctx->bf_bits_count[0]);
		if(rctx->bf_bits_count[1]!=8)
			iw_set_input_sbit(ctx,IW_CHANNELTYPE_GREEN,rctx->bf_bits_count[1]);
		if(rctx->bf_bits_count[2]!=8)
			iw_set_input_sbit(ctx,IW_CHANNELTYPE_BLUE ,rctx->bf_bits_count[2]);
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
	size_t palsize;
	size_t unc_dst_bpr;
	size_t unc_bitssize;
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
	const struct iw_palette *pal;
	size_t total_written;
};

static void iwbmp_write(struct iwbmpwritecontext *bmpctx, const void *buf, size_t n)
{
	(*bmpctx->iodescr->write_fn)(bmpctx->ctx,bmpctx->iodescr,buf,n);
	bmpctx->total_written+=n;
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

static void iwbmp_convert_row24(const iw_byte *srcrow, iw_byte *dstrow, int width)
{
	int i;

	for(i=0;i<width;i++) {
		dstrow[i*3+0] = srcrow[i*3+2];
		dstrow[i*3+1] = srcrow[i*3+1];
		dstrow[i*3+2] = srcrow[i*3+0];
	}
}

static void iwbmp_write_file_header(struct iwbmpwritecontext *bmpctx)
{
	iw_byte fileheader[14];

	if(!bmpctx->include_file_header) return;

	iw_zeromem(fileheader,sizeof(fileheader));
	fileheader[0] = 66; // 'B'
	fileheader[1] = 77; // 'M'

	// This will be overwritten later, if the bitmap was compressed.
	iw_set_ui32le(&fileheader[ 2],14+40+(unsigned int)bmpctx->palsize+
		(unsigned int)bmpctx->unc_bitssize); // bfSize

	iw_set_ui32le(&fileheader[10],14+40+(unsigned int)bmpctx->palsize); // bfOffBits
	iwbmp_write(bmpctx,fileheader,14);
}

static void iwbmp_write_bmp_header(struct iwbmpwritecontext *bmpctx)
{
	unsigned int dens_x, dens_y;
	unsigned int cmpr;
	iw_byte header[40];

	iw_zeromem(header,sizeof(header));

	iw_set_ui32le(&header[ 0],40);      // biSize
	iw_set_ui32le(&header[ 4],bmpctx->img->width);  // biWidth
	iw_set_ui32le(&header[ 8],bmpctx->img->height); // biHeight
	iw_set_ui16le(&header[12],1);    // biPlanes
	iw_set_ui16le(&header[14],bmpctx->bitcount);   // biBitCount

	cmpr = IWBMP_BI_RGB;
	if(bmpctx->compressed) {
		if(bmpctx->bitcount==8) cmpr = IWBMP_BI_RLE8;
		else if(bmpctx->bitcount==4) cmpr = IWBMP_BI_RLE4;
	}
	iw_set_ui32le(&header[16],cmpr); // biCompression

	iw_set_ui32le(&header[20],(unsigned int)bmpctx->unc_bitssize); // biSizeImage

	if(bmpctx->img->density_code==IW_DENSITY_UNITS_PER_METER) {
		dens_x = (unsigned int)(0.5+bmpctx->img->density_x);
		dens_y = (unsigned int)(0.5+bmpctx->img->density_y);
	}
	else {
		dens_x = dens_y = 2835;
	}
	iw_set_ui32le(&header[24],dens_x); // biXPelsPerMeter
	iw_set_ui32le(&header[28],dens_y); // biYPelsPerMeter

	iw_set_ui32le(&header[32],bmpctx->palentries);    // biClrUsed
	//iw_set_ui32le(&header[36],0);    // biClrImportant
	iwbmp_write(bmpctx,header,40);
}

static void iwbmp_write_palette(struct iwbmpwritecontext *bmpctx)
{
	int i;
	iw_byte buf[4];

	if(bmpctx->palentries<1) return;

	buf[3] = 0; // Reserved field; always 0.

	for(i=0;i<bmpctx->palentries;i++) {
		if(i<bmpctx->pal->num_entries) {
			if(bmpctx->pal->entry[i].a == 0) {
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
				buf[0] = bmpctx->pal->entry[i].b;
				buf[1] = bmpctx->pal->entry[i].g;
				buf[2] = bmpctx->pal->entry[i].r;
			}
		}
		else {
			buf[0] = buf[1] = buf[2] = 0;
		}
		iwbmp_write(bmpctx,buf,4);
	}
}

struct rle_context {
	struct iw_context *ctx;
	struct iwbmpwritecontext *bmpctx;
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

static void rle8_write_unc(struct rle_context *rctx)
{
	size_t i;
	iw_byte dstbuf[2];

	if(rctx->unc_len<1) return;
	if(rctx->unc_len>=3 && (rctx->unc_len&1)) {
		iw_set_error(rctx->ctx,"Internal: RLE encode error 4");
		return;
	}
	if(rctx->unc_len>254) {
		iw_set_error(rctx->ctx,"Internal: RLE encode error 5");
		return;
	}

	if(rctx->unc_len<3) {
		// The minimum length for a noncompressed run is 3. For shorter runs
		// write them "compressed".
		for(i=0;i<rctx->unc_len;i++) {
			dstbuf[0] = 0x01;  // count
			dstbuf[1] = rctx->srcrow[i+rctx->pending_data_start]; // value
			iwbmp_write(rctx->bmpctx,dstbuf,2);
			rctx->total_bytes_written+=2;
		}
	}
	else {
		dstbuf[0] = 0x00;
		dstbuf[1] = (iw_byte)rctx->unc_len;
		iwbmp_write(rctx->bmpctx,dstbuf,2);
		rctx->total_bytes_written+=2;
		iwbmp_write(rctx->bmpctx,&rctx->srcrow[rctx->pending_data_start],rctx->unc_len);
		rctx->total_bytes_written+=rctx->unc_len;
		if(rctx->unc_len&0x1) {
			// Need a padding byte if the length was odd. (This shouldn't
			// happen, because we never write odd-length UNC segments.)
			dstbuf[0] = 0x00;
			iwbmp_write(rctx->bmpctx,dstbuf,1);
			rctx->total_bytes_written+=1;
		}
	}

	rctx->pending_data_start+=rctx->unc_len;
	rctx->unc_len=0;
}

static void rle8_write_unc_and_run(struct rle_context *rctx)
{
	iw_byte dstbuf[2];

	rle8_write_unc(rctx);

	if(rctx->run_len<1) {
		return;
	}
	if(rctx->run_len>255) {
		iw_set_error(rctx->ctx,"Internal: RLE encode error 6");
		return;
	}

	dstbuf[0] = (iw_byte)rctx->run_len;
	dstbuf[1] = rctx->run_byte;
	iwbmp_write(rctx->bmpctx,dstbuf,2);
	rctx->total_bytes_written+=2;

	rctx->pending_data_start+=rctx->run_len;
	rctx->run_len=0;
}

static void rle_write_trns(struct rle_context *rctx, int num_trns)
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
		iwbmp_write(rctx->bmpctx,dstbuf,4);
		rctx->total_bytes_written+=4;
		num_remaining -= num_to_write;
	}
	rctx->pending_data_start += num_trns;
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
static int rle8_compress_row(struct rle_context *rctx)
{
	size_t i;
	iw_byte dstbuf[2];
	iw_byte next_byte;
	int next_pix_is_trns;
	int num_trns = 0; // number of consecutive transparent pixels seen
	int retval = 0;

	rctx->pending_data_start=0;
	rctx->unc_len=0;
	rctx->run_len=0;

	for(i=0;i<rctx->img_width;i++) {

		// Read the next byte.
		next_byte = rctx->srcrow[i];

		next_pix_is_trns = (rctx->bmpctx->pal->entry[next_byte].a==0);

		if(num_trns>0 && !next_pix_is_trns) {
			rle_write_trns(rctx,num_trns);
			num_trns=0;
		}
		else if(next_pix_is_trns) {
			if (rctx->unc_len>0 || rctx->run_len>0) {
				rle8_write_unc_and_run(rctx);
			}
			num_trns++;
			continue;
		}

		// --------------------------------------------------------------
		// Add the byte we just read to either the UNC or the RUN data.

		if(rctx->run_len>0 && next_byte==rctx->run_byte) {
			// Byte fits in the current run; add it.
			rctx->run_len++;
		}
		else if(rctx->run_len==0) {
			// We don't have a RUN, so we can put this byte there.
			rctx->run_len = 1;
			rctx->run_byte = next_byte;
		}
		else if(rctx->unc_len==0 && rctx->run_len==1) {
			// We have one previous byte, and it's different from this one.
			// Move it to UNC, and make this one the RUN.
			rctx->unc_len++;
			rctx->run_byte = next_byte;
		}
		else if(rctx->unc_len>0 && rctx->run_len<(rctx->unc_len==1 ? 3U : 4U)) {
			// We have a run, but it's not long enough to be beneficial.
			// Convert it to uncompressed bytes.
			// A good rule is that a run length of 4 or more (3 or more if
			// unc_len=1) should always be run-legth encoded.
			rctx->unc_len += rctx->run_len;
			rctx->run_len = 0;
			// If UNC is now odd and >1, add the next byte to it to make it even.
			// Otherwise, add it to RUN.
			if(rctx->unc_len>=3 && (rctx->unc_len&0x1)) {
				rctx->unc_len++;
			}
			else {
				rctx->run_len = 1;
				rctx->run_byte = next_byte;
			}
		}
		else {
			// Nowhere to put the byte: write out everything, and start fresh.
			rle8_write_unc_and_run(rctx);
			rctx->run_len = 1;
			rctx->run_byte = next_byte;
		}

		// --------------------------------------------------------------
		// If we hit certain high water marks, write out the current data.

		if(rctx->unc_len>=254) {
			// Our maximum size for an UNC segment.
			rle8_write_unc(rctx);
		}
		else if(rctx->unc_len>0 && (rctx->unc_len+rctx->run_len)>254) {
			// It will not be possible to coalesce the RUN into the UNC (it
			// would be too big) so write out the UNC.
			rle8_write_unc(rctx);
		}
		else if(rctx->run_len>=255) {
			// The maximum size for an RLE segment.
			rle8_write_unc_and_run(rctx);
		}

		// --------------------------------------------------------------
		// Sanity checks. These can be removed if we're sure the algorithm
		// is bug-free.

		// We don't allow unc_len to be odd (except temporarily), except
		// that it can be 1.
		// What's special about 1 is that if we add another byte to it, it
		// increases the cost. For 3,5,...,253, we can add another byte for
		// free, so we should never fail to do that.
		if((rctx->unc_len&0x1) && rctx->unc_len!=1) {
			iw_set_errorf(rctx->ctx,"Internal: BMP RLE encode error 1");
			goto done;
		}

		// unc_len can be at most 252 at this point.
		// If it were 254, it should have been written out already.
		if(rctx->unc_len>252) {
			iw_set_error(rctx->ctx,"Internal: BMP RLE encode error 2");
			goto done;
		}

		// run_len can be at most 254 at this point.
		// If it were 255, it should have been written out already.
		if(rctx->run_len>254) {
			iw_set_error(rctx->ctx,"Internal: BMP RLE encode error 3");
			goto done;
		}
	}

	// End of row. Write out anything left over.
	rle8_write_unc_and_run(rctx);

	// Write an end-of-line marker (0 0), or if this is the last row,
	// an end-of-bitmap marker (0 1).
	dstbuf[0]=0x00;
	dstbuf[1]= (rctx->cur_row==0)? 0x01 : 0x00;
	iwbmp_write(rctx->bmpctx,dstbuf,2);
	rctx->total_bytes_written+=2;

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
static int rle4_get_incr_unc_cost(struct rle_context *rctx)
{
	int n;
	int m;

	n = (int)rctx->unc_len;

	if(n==2 || n==255 || n==257 || n==507 || n==510) return 2;
	if(n==256 || n==508) return 0;

	if(n>=759) {
		m = n%252;
		if(m==3 || m==6 || m==9) return 2;
		if(m==4 || m==8) return 0;
	}

	return (n%4)?0:2;
}

static void rle4_write_unc(struct rle_context *rctx)
{
	iw_byte dstbuf[128];
	size_t pixels_to_write;
	size_t bytes_to_write;

	if(rctx->unc_len<1) return;

	// Note that, unlike the RLE8 encoder, we allow this function to be called
	// with uncompressed runs of arbitrary length.

	while(rctx->unc_len>0) {
		pixels_to_write = rle4_get_best_unc_split(rctx->unc_len);

		if(pixels_to_write<3) {
			// The minimum length for an uncompressed run is 3. For shorter runs
			// write them "compressed".
			dstbuf[0] = (iw_byte)pixels_to_write;
			dstbuf[1] = (rctx->srcrow[rctx->pending_data_start]<<4);
			if(pixels_to_write>1)
				dstbuf[1] |= (rctx->srcrow[rctx->pending_data_start+1]);

			// The actual writing will occur below. Just indicate how many bytes
			// of dstbuf[] to write.
			bytes_to_write = 2;
		}
		else {
			size_t i;

			// Write the length of the uncompressed run.
			dstbuf[0] = 0x00;
			dstbuf[1] = (iw_byte)pixels_to_write;
			iwbmp_write(rctx->bmpctx,dstbuf,2);
			rctx->total_bytes_written+=2;

			// Put the data to write in dstbuf[].
			bytes_to_write = 2*((pixels_to_write+3)/4);
			iw_zeromem(dstbuf,bytes_to_write);

			for(i=0;i<pixels_to_write;i++) {
				if(i&0x1) dstbuf[i/2] |= rctx->srcrow[rctx->pending_data_start+i];
				else dstbuf[i/2] = rctx->srcrow[rctx->pending_data_start+i]<<4;
			}
		}

		iwbmp_write(rctx->bmpctx,dstbuf,bytes_to_write);
		rctx->total_bytes_written += bytes_to_write;
		rctx->unc_len -= pixels_to_write;
		rctx->pending_data_start += pixels_to_write;
	}
}

static void rle4_write_unc_and_run(struct rle_context *rctx)
{
	iw_byte dstbuf[2];

	rle4_write_unc(rctx);

	if(rctx->run_len<1) {
		return;
	}
	if(rctx->run_len>255) {
		iw_set_error(rctx->ctx,"Internal: RLE encode error 6");
		return;
	}

	dstbuf[0] = (iw_byte)rctx->run_len;
	dstbuf[1] = rctx->run_byte;
	iwbmp_write(rctx->bmpctx,dstbuf,2);
	rctx->total_bytes_written+=2;

	rctx->pending_data_start+=rctx->run_len;
	rctx->run_len=0;
}

// Should we move the pending compressible data to the "uncompressed"
// segment (return 1), or should we write it to disk as a compressed run of
// pixels (0)?
static int ok_to_move_to_unc(struct rle_context *rctx)
{
	// This logic is probably not optimal in every case.
	// One possible improvement might be to adjust the thresholds when
	// unc_len+run_len is around 255 or higher.
	// Other improvements might require looking ahead at pixels we haven't
	// read yet.

	if(rctx->unc_len==0) {
		return (rctx->run_len<4);
	}
	else if(rctx->unc_len<=2) {
		return (rctx->run_len<6);
	}
	else {
		return (rctx->run_len<8);
	}
	return 0;
}

static int rle4_compress_row(struct rle_context *rctx)
{
	size_t i;
	iw_byte dstbuf[2];
	iw_byte next_pix;
	int next_pix_is_trns;
	int num_trns = 0; // number of consecutive transparent pixels seen
	int retval = 0;

	rctx->pending_data_start=0;
	rctx->unc_len=0;
	rctx->run_len=0;

	for(i=0;i<rctx->img_width;i++) {

		// Read the next pixel
		next_pix = rctx->srcrow[i];

		next_pix_is_trns = (rctx->bmpctx->pal->entry[next_pix].a==0);
		if(num_trns>0 && !next_pix_is_trns) {
			rle_write_trns(rctx,num_trns);
			num_trns=0;
		}
		else if(next_pix_is_trns) {
			if (rctx->unc_len>0 || rctx->run_len>0) {
				rle4_write_unc_and_run(rctx);
			}
			num_trns++;
			continue;
		}

		// --------------------------------------------------------------
		// Add the pixel we just read to either the UNC or the RUN data.

		if(rctx->run_len==0) {
			// We don't have a RUN, so we can put this pixel there.
			rctx->run_len = 1;
			rctx->run_byte = next_pix<<4;
		}
		else if(rctx->run_len==1) {
			// If the run is 1, we can always add a 2nd pixel
			rctx->run_byte |= next_pix;
			rctx->run_len++;
		}
		else if(rctx->run_len>=2 && (rctx->run_len&1)==0 && next_pix==(rctx->run_byte>>4)) {
			// pixel fits in the current run; add it.
			rctx->run_len++;
		}
		else if(rctx->run_len>=3 && (rctx->run_len&1) && next_pix==(rctx->run_byte&0x0f)) {
			// pixel fits in the current run; add it.
			rctx->run_len++;
		}
		else if(rctx->unc_len==0 && rctx->run_len==2) {
			// We have one previous byte, and it's different from this one.
			// Move it to UNC, and make this one the RUN.
			rctx->unc_len+=rctx->run_len;
			rctx->run_byte = next_pix<<4;
			rctx->run_len = 1;
		}
		else if(ok_to_move_to_unc(rctx)) {
			// We have a compressible run, but we think it's not long enough to be
			// beneficial. Convert it to uncompressed bytes.
			rctx->unc_len += rctx->run_len;

			// Put the next byte in RLE. (It might get moved to UNC, below.)
			rctx->run_len = 1;
			rctx->run_byte = next_pix<<4;
		}
		else {
			// Nowhere to put the byte: write out everything, and start fresh.
			rle4_write_unc_and_run(rctx);
			rctx->run_len = 1;
			rctx->run_byte = next_pix<<4;
		}

		// --------------------------------------------------------------
		// If any RUN bytes that can be added to UNC for free, do so.
		while(rctx->unc_len>0 && rctx->run_len>0 && rle4_get_incr_unc_cost(rctx)==0) {
			rctx->unc_len++;
			rctx->run_len--;
		}

		// --------------------------------------------------------------
		// If we hit certain high water marks, write out the current data.

		if(rctx->run_len>=255) {
			// The maximum size for an RLE segment.
			rle4_write_unc_and_run(rctx);
		}

		// --------------------------------------------------------------
		// Sanity check(s). This can be removed if we're sure the algorithm
		// is bug-free.

		// run_len can be at most 254 at this point.
		// If it were 255, it should have been written out already.
		if(rctx->run_len>255) {
			iw_set_error(rctx->ctx,"Internal: BMP RLE encode error 3");
			goto done;
		}
	}

	// End of row. Write out anything left over.
	rle4_write_unc_and_run(rctx);

	// Write an end-of-line marker (0 0), or if this is the last row,
	// an end-of-bitmap marker (0 1).
	dstbuf[0]=0x00;
	dstbuf[1]= (rctx->cur_row==0)? 0x01 : 0x00;
	iwbmp_write(rctx->bmpctx,dstbuf,2);
	rctx->total_bytes_written+=2;

	retval = 1;

done:
	return retval;
}

//======================================================================

// Seek back and write the "file size" and "bits size" fields.
static int rle_patch_file_size(struct iwbmpwritecontext *bmpctx,size_t rlesize)
{
	iw_byte buf[4];
	size_t fileheader_size;

	if(!bmpctx->iodescr->seek_fn) {
		iw_set_error(bmpctx->ctx,"Writing compressed BMP requires a seek function");
		return 0;
	}

	if(bmpctx->include_file_header) {
		// Patch the file size in the file header
		(*bmpctx->iodescr->seek_fn)(bmpctx->ctx,bmpctx->iodescr,2,SEEK_SET);
		iw_set_ui32le(buf,(unsigned int)(14+40+bmpctx->palsize+rlesize));
		iwbmp_write(bmpctx,buf,4);
		fileheader_size = 14;
	}
	else {
		fileheader_size = 0;
	}

	// Patch the "bits" size
	(*bmpctx->iodescr->seek_fn)(bmpctx->ctx,bmpctx->iodescr,fileheader_size+20,SEEK_SET);
	iw_set_ui32le(buf,(unsigned int)rlesize);
	iwbmp_write(bmpctx,buf,4);

	(*bmpctx->iodescr->seek_fn)(bmpctx->ctx,bmpctx->iodescr,0,SEEK_END);
	return 1;
}

static int iwbmp_write_pixels_compressed(struct iwbmpwritecontext *bmpctx,
	struct iw_image *img)
{
	struct rle_context rctx;
	int j;
	int retval = 0;

	iw_zeromem(&rctx,sizeof(struct rle_context));

	rctx.ctx = bmpctx->ctx;
	rctx.bmpctx = bmpctx;
	rctx.total_bytes_written = 0;
	rctx.img_width = img->width;

	for(j=img->height-1;j>=0;j--) {
		// Compress and write a row of pixels
		rctx.srcrow = &img->pixels[j*img->bpr];
		rctx.cur_row = j;

		if(bmpctx->bitcount==4) {
			if(!rle4_compress_row(&rctx)) goto done;
		}
		else if(bmpctx->bitcount==8) {
			if(!rle8_compress_row(&rctx)) goto done;
		}
		else {
			goto done;
		}
	}

	// Back-patch the 'file size' and 'bits size' fields
	rle_patch_file_size(bmpctx,rctx.total_bytes_written);

	retval = 1;
done:
	return retval;
}

static void iwbmp_write_pixels_uncompressed(struct iwbmpwritecontext *bmpctx,
	struct iw_image *img)
{
	int j;
	iw_byte *dstrow = NULL;
	const iw_byte *srcrow;

	dstrow = iw_mallocz(bmpctx->ctx,bmpctx->unc_dst_bpr);
	if(!dstrow) goto done;

	for(j=img->height-1;j>=0;j--) {
		srcrow = &img->pixels[j*img->bpr];
		switch(bmpctx->bitcount) {
		case 24: iwbmp_convert_row24(srcrow,dstrow,img->width); break;
		case 8: iwbmp_convert_row8(srcrow,dstrow,img->width); break;
		case 4: iwbmp_convert_row4(srcrow,dstrow,img->width); break;
		case 1: iwbmp_convert_row1(srcrow,dstrow,img->width); break;
		}
		iwbmp_write(bmpctx,dstrow,bmpctx->unc_dst_bpr);
	}

done:
	if(dstrow) iw_free(bmpctx->ctx,dstrow);
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

static int iwbmp_write_main(struct iwbmpwritecontext *bmpctx)
{
	struct iw_image *img;
	int cmpr_req;
	int retval = 0;
	int x;

	img = bmpctx->img;

	cmpr_req = iw_get_value(bmpctx->ctx,IW_VAL_COMPRESSION);

	// If any kind of compression was requested, use RLE if possible.
	if(cmpr_req==IW_COMPRESSION_AUTO || cmpr_req==IW_COMPRESSION_NONE)
		cmpr_req = IW_COMPRESSION_NONE;
	else
		cmpr_req = IW_COMPRESSION_RLE;

	if(img->imgtype==IW_IMGTYPE_RGB) {
		bmpctx->bitcount=24;
	}
	else if(img->imgtype==IW_IMGTYPE_PALETTE) {
		if(!bmpctx->pal) goto done;

		x = check_palette_transparency(bmpctx->pal);

		if(x==2) {
			iw_set_error(bmpctx->ctx,"Cannot save this image as a transparent BMP: Has partial transparency");
			goto done;
		}
		else if(x!=0 && cmpr_req!=IW_COMPRESSION_RLE) {
			iw_set_error(bmpctx->ctx,"Cannot save as a transparent BMP: RLE compression required");
			goto done;
		}

		if(bmpctx->pal->num_entries<=2 && cmpr_req!=IW_COMPRESSION_RLE)
			bmpctx->bitcount=1;
		else if(bmpctx->pal->num_entries<=16)
			bmpctx->bitcount=4;
		else
			bmpctx->bitcount=8;
	}
	else if(img->imgtype==IW_IMGTYPE_RGBA) {
		// It should only be possible to get here if the user enabled the transparent-BMP hack.
		iw_set_error(bmpctx->ctx,"Cannot save this image as a transparent BMP: Too many colors");
		goto done;
	}
	else {
		iw_set_error(bmpctx->ctx,"Internal: Bad image type for BMP");
		goto done;
	}

	if(cmpr_req==IW_COMPRESSION_RLE && (bmpctx->bitcount==4 || bmpctx->bitcount==8)) {
		bmpctx->compressed = 1;
	}

	bmpctx->unc_dst_bpr = iwbmp_calc_bpr(bmpctx->bitcount,img->width);
	bmpctx->unc_bitssize = bmpctx->unc_dst_bpr * img->height;
	bmpctx->palentries = 0;
	if(bmpctx->pal) {
		bmpctx->palentries = bmpctx->pal->num_entries;
		if(bmpctx->bitcount==1) {
			// The documentation says that if the bitdepth is 1, the palette
			// must contain exactly two entries.
			bmpctx->palentries=2;
		}
	}
	bmpctx->palsize = bmpctx->palentries*4;

	// File header
	iwbmp_write_file_header(bmpctx);

	// Bitmap header ("BITMAPINFOHEADER")
	iwbmp_write_bmp_header(bmpctx);

	// Palette
	iwbmp_write_palette(bmpctx);

	// Pixels
	if(bmpctx->compressed) {
		if(!iwbmp_write_pixels_compressed(bmpctx,img)) goto done;
	}
	else {
		iwbmp_write_pixels_uncompressed(bmpctx,img);
	}

	retval = 1;
done:
	//if(dstrow) iw_free(bmpctx->ctx,dstrow);
	return retval;
}

IW_IMPL(int) iw_write_bmp_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	struct iwbmpwritecontext bmpctx;
	int retval=0;
	struct iw_image img1;

	iw_zeromem(&img1,sizeof(struct iw_image));

	iw_zeromem(&bmpctx,sizeof(struct iwbmpwritecontext));

	bmpctx.ctx = ctx;
	bmpctx.include_file_header = 1;

	bmpctx.iodescr=iodescr;

	iw_get_output_image(ctx,&img1);
	bmpctx.img = &img1;

	if(bmpctx.img->imgtype==IW_IMGTYPE_PALETTE) {
		bmpctx.pal = iw_get_output_palette(ctx);
		if(!bmpctx.pal) goto done;
	}

	iwbmp_write_main(&bmpctx);

	retval=1;

done:
	return retval;
}
