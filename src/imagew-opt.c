// imagew-opt.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// Routines for optimizing the output file.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>

#ifdef IW_WINDOWS
#include <search.h> // for qsort
#endif

#include "imagew-internals.h"

static void iw_opt_scanpixels_rgb8(struct iw_opt_ctx *optctx)
{
	int i,j;
	iw_byte r,g,b;
	const iw_byte *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+i*3];
			r=ptr[0]; g=ptr[1]; b=ptr[2];

			// Check grayscale
			if(r!=g || r!=b)
				optctx->has_color=1;

			if(optctx->has_color) {
				// No optimizations possible. Stop the scan
				goto done;
			}
		}
	}
done:
	;
}

static void iw_opt_scanpixels_ga8(struct iw_opt_ctx *optctx)
{
	int i,j;
	iw_byte a;
	const iw_byte *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+i*2];
			a=ptr[1];

			// Check transparency
			if(a<255) {
				optctx->has_transparency=1;
				if(a>0) {
					optctx->has_partial_transparency=1;
				}
			}

			if(optctx->has_partial_transparency) {
				// No optimizations possible. Stop the scan
				goto done;
			}
		}
	}
done:
	;
}

static void iw_opt_scanpixels_rgba8(struct iw_opt_ctx *optctx)
{
	int i,j;
	iw_byte r,g,b,a;
	const iw_byte *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+i*4];
			r=ptr[0]; g=ptr[1]; b=ptr[2]; a=ptr[3];

			// Check transparency
			if(a<255) {
				optctx->has_transparency=1;
				if(a>0) {
					optctx->has_partial_transparency=1;
				}
			}

			// Check grayscale
			if(r!=g || r!=b)
				optctx->has_color=1;

			if(optctx->has_partial_transparency && optctx->has_color) {
				// No optimizations possible. Stop the scan
				goto done;
			}
		}
	}
done:
	;
}

static void iw_opt_scanpixels_g16(struct iw_opt_ctx *optctx)
{
	int i,j;
	const iw_byte *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+(i*2)];

			// Check if 16-bit output is necessary
			if(ptr[0]!=ptr[1])
			{
				optctx->has_16bit_precision=1;
			}

			if(optctx->has_16bit_precision) {
				// No optimizations possible. Stop the scan
				goto done;
			}
		}
	}
done:
	;
}

static void iw_opt_scanpixels_ga16(struct iw_opt_ctx *optctx)
{
	int i,j;
	unsigned int a;
	const iw_byte *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+(i*2)*2];

			// Check if 16-bit output is necessary
			if(ptr[0]!=ptr[1] || ptr[2]!=ptr[3])
			{
				optctx->has_16bit_precision=1;
			}

			a=(((unsigned int)ptr[2])<<8) | ptr[3];

			// Check transparency
			if(a<65535) {
				optctx->has_transparency=1;
				if(a>0) {
					optctx->has_partial_transparency=1;
				}
			}

			if(optctx->has_partial_transparency && optctx->has_16bit_precision) {
				// No optimizations possible. Stop the scan
				goto done;
			}
		}
	}
done:
	;
}

static void iw_opt_scanpixels_rgb16(struct iw_opt_ctx *optctx)
{
	int i,j;
	unsigned int r,g,b;
	const iw_byte *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+(i*2)*3];

			// Check if 16-bit output is necessary
			if(ptr[0]!=ptr[1] || ptr[2]!=ptr[3] || ptr[4]!=ptr[5])
			{
				optctx->has_16bit_precision=1;
			}

			r=(((unsigned int)ptr[0])<<8) | ptr[1];
			g=(((unsigned int)ptr[2])<<8) | ptr[3];
			b=(((unsigned int)ptr[4])<<8) | ptr[5];

			// Check grayscale
			if(r!=g || r!=b)
				optctx->has_color=1;

			if(optctx->has_color && optctx->has_16bit_precision) {
				// No optimizations possible. Stop the scan
				goto done;
			}
		}
	}
done:
	;
}

static void iw_opt_scanpixels_rgba16(struct iw_opt_ctx *optctx)
{
	int i,j;
	unsigned int r,g,b,a;
	const iw_byte *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+(i*2)*4];

			// Check if 16-bit output is necessary
			if(ptr[0]!=ptr[1] || ptr[2]!=ptr[3] || ptr[4]!=ptr[5] || ptr[6]!=ptr[7])
			{
				optctx->has_16bit_precision=1;
			}

			r=(((unsigned int)ptr[0])<<8) | ptr[1];
			g=(((unsigned int)ptr[2])<<8) | ptr[3];
			b=(((unsigned int)ptr[4])<<8) | ptr[5];
			a=(((unsigned int)ptr[6])<<8) | ptr[7];

			// Check transparency
			if(a<65535) {
				optctx->has_transparency=1;
				if(a>0) {
					optctx->has_partial_transparency=1;
				}
			}

			// Check grayscale
			if(r!=g || r!=b)
				optctx->has_color=1;

			if(optctx->has_partial_transparency && optctx->has_color && optctx->has_16bit_precision) {
				// No optimizations possible. Stop the scan
				goto done;
			}
		}
	}
done:
	;
}

static void iw_opt_16_to_8(struct iw_context *ctx, struct iw_opt_ctx *optctx, int spp)
{
	iw_byte *newpixels;
	size_t newbpr;
	int i,j,k;

	if(!ctx->opt_16_to_8) return;

	newbpr = iw_calc_bytesperrow(optctx->width,8*spp);
	newpixels = iw_malloc_large(ctx, newbpr, optctx->height);
	if(!newpixels) return;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<spp*optctx->width;i++) {
			// i is a sample number, not a pixel number.
			newpixels[j*newbpr + i] = optctx->pixelsptr[j*optctx->bpr + i*2];
		}
	}

	// Remove previous image if it was allocated by the optimization code.
	if(optctx->tmp_pixels) iw_free(ctx,optctx->tmp_pixels);

	// Attach our new image
	optctx->tmp_pixels = newpixels;
	optctx->pixelsptr = optctx->tmp_pixels;
	optctx->bpr = newbpr;
	optctx->bit_depth = 8;

	// If there's a background color label, also reduce its precision.
	if(optctx->has_bkgdlabel) {
		for(k=0;k<4;k++) {
			optctx->bkgdlabel[k] >>= 8;
		}
	}
}

// Create a new (8-bit) image by copying up to 3 channels from the old image.
static void iw_opt_copychannels_8(struct iw_context *ctx, struct iw_opt_ctx *optctx,
			int new_imgtype, int c0, int c1, int c2)
{
	iw_byte *newpixels;
	int oldnc, newnc; // num_channels
	size_t newbpr;
	int i,j;

	oldnc = iw_imgtype_num_channels(optctx->imgtype);
	newnc = iw_imgtype_num_channels(new_imgtype);

	newbpr = iw_calc_bytesperrow(optctx->width,8*newnc);
	newpixels = iw_malloc_large(ctx, newbpr, optctx->height);
	if(!newpixels) return;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			newpixels[j*newbpr + i*newnc +0] = optctx->pixelsptr[j*optctx->bpr + i*oldnc +c0];
			if(newnc>1)
				newpixels[j*newbpr + i*newnc +1] = optctx->pixelsptr[j*optctx->bpr + i*oldnc +c1];
			if(newnc>2)
				newpixels[j*newbpr + i*newnc +2] = optctx->pixelsptr[j*optctx->bpr + i*oldnc +c2];
		}
	}

	// Remove previous image if it was allocated by the optimization code.
	if(optctx->tmp_pixels) iw_free(ctx,optctx->tmp_pixels);

	// Attach our new image
	optctx->tmp_pixels = newpixels;
	optctx->pixelsptr = optctx->tmp_pixels;
	optctx->bpr = newbpr;
	optctx->imgtype = new_imgtype;

}

// Create a new (16-bit) image by copying up to 3 channels from the old image.
static void iw_opt_copychannels_16(struct iw_context *ctx, struct iw_opt_ctx *optctx,
			int new_imgtype, int c0, int c1, int c2)
{
	iw_byte *newpixels;
	int oldnc, newnc; // num_channels
	size_t newbpr;
	int i,j;

	oldnc = iw_imgtype_num_channels(optctx->imgtype);
	newnc = iw_imgtype_num_channels(new_imgtype);

	newbpr = iw_calc_bytesperrow(optctx->width,16*newnc);
	newpixels = iw_malloc_large(ctx, newbpr, optctx->height);
	if(!newpixels) return;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			newpixels[j*newbpr + (i*newnc+0)*2+0] = optctx->pixelsptr[j*optctx->bpr + (i*oldnc+c0)*2+0];
			newpixels[j*newbpr + (i*newnc+0)*2+1] = optctx->pixelsptr[j*optctx->bpr + (i*oldnc+c0)*2+1];
			if(newnc>1) {
				newpixels[j*newbpr + (i*newnc+1)*2+0] = optctx->pixelsptr[j*optctx->bpr + (i*oldnc+c1)*2+0];
				newpixels[j*newbpr + (i*newnc+1)*2+1] = optctx->pixelsptr[j*optctx->bpr + (i*oldnc+c1)*2+1];
			}
			if(newnc>2) {
				newpixels[j*newbpr + (i*newnc+2)*2+0] = optctx->pixelsptr[j*optctx->bpr + (i*oldnc+c2)*2+0];
				newpixels[j*newbpr + (i*newnc+2)*2+1] = optctx->pixelsptr[j*optctx->bpr + (i*oldnc+c2)*2+1];
			}
		}
	}

	// Remove previous image if it was allocated by the optimization code.
	if(optctx->tmp_pixels) iw_free(ctx,optctx->tmp_pixels);

	// Attach our new image
	optctx->tmp_pixels = newpixels;
	optctx->pixelsptr = optctx->tmp_pixels;
	optctx->bpr = newbpr;
	optctx->imgtype = new_imgtype;
}

// Returns 0 if no scanning was done.
static int iw_opt_scanpixels(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==16) {
		iw_opt_scanpixels_rgba16(optctx);
	}
	else if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==8) {
		iw_opt_scanpixels_rgba8(optctx);
	}
	else if(optctx->imgtype==IW_IMGTYPE_RGB && optctx->bit_depth==16) {
		iw_opt_scanpixels_rgb16(optctx);
	}
	else if(optctx->imgtype==IW_IMGTYPE_RGB && optctx->bit_depth==8) {
		iw_opt_scanpixels_rgb8(optctx);
	}
	else if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==16) {
		iw_opt_scanpixels_ga16(optctx);
	}
	else if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==8) {
		iw_opt_scanpixels_ga8(optctx);
	}
	else if(optctx->imgtype==IW_IMGTYPE_GRAY && optctx->bit_depth==16) {
		iw_opt_scanpixels_g16(optctx);
	}
	else {
		return 0;
	}

	return 1;
}

////////////////////

// Returns 0 if nothing found.
static int iwopt_find_unused(const iw_byte *flags, int count, iw_byte *unused_clr)
{
	int i;
	int found=0;
	int found_dist=0; // Distance from our preferred value (192)
	int d;

	for(i=0;i<count;i++) {
		if(flags[i]==0) {
			d=abs(i-192);
			if(!found || d<found_dist) {
				*unused_clr = (iw_byte)i;
				found = 1;
				found_dist = d;
			}
		}
	}

	if(found) {
		return 1;
	}
	*unused_clr = 0;
	return 0;
}

// Try to convert from RGBA to RGB+binary trns.
// Assumes we already know there is transparency, but no partial transparency.
static void iwopt_try_rgb8_binary_trns(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	int i,j;
	const iw_byte *ptr;
	iw_byte *ptr2;
	iw_byte clr_used[256];
	iw_byte key_clr=0; // Red component of the key color
	iw_byte *trns_mask = NULL;

	if(!(ctx->output_profile&IW_PROFILE_BINARYTRNS)) return;
	if(!ctx->opt_binary_trns) return;

	// Try to find a color that's not used in the image.
	// Looking for all 2^24 possible colors is too much work.
	// We will just look for 256 predefined colors: R={0-255},G=192,B=192
	iw_zeromem(clr_used,256);

	// Hard to decide how to do this. I don't want the optimization phase
	// to modify img2.pixels, though that would be the easiest method.
	// Another option would be to make a version of iw_opt_copychannels_8()
	// that sets the transparent pixels to a certain value, but that would
	// get messy.
	// Instead, I'll make a transparency mask, then strip the alpha
	// channel, then use the mask to patch up the new image.
	trns_mask = iw_malloc_large(ctx, optctx->width, optctx->height);
	if(!trns_mask) goto done;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+i*4];
			if(ptr[3]==0) {
				// transparent pixel
				trns_mask[j*optctx->width+i] = 0; // Remember which pixels are transparent.
				continue;
			}
			else {
				trns_mask[j*optctx->width+i] = 1;
			}
			if(ptr[1]!=192 || ptr[2]!=192) continue;
			clr_used[(int)ptr[0]] = 1;
		}
	}

	if(!iwopt_find_unused(clr_used,256,&key_clr)) {
		goto done;
	}

	// Strip the alpha channel:
	iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_RGB,0,1,2);
	if(!optctx->tmp_pixels) goto done;

	// Change the color of all transparent pixels to the key color
	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr2 = &optctx->tmp_pixels[j*optctx->bpr+i*3];
			if(trns_mask[j*optctx->width+i]==0) {
				ptr2[0] = key_clr;
				ptr2[1] = 192;
				ptr2[2] = 192;
			}
		}
	}

	optctx->has_colorkey_trns = 1;
	optctx->colorkey[IW_CHANNELTYPE_RED] = key_clr;
	optctx->colorkey[IW_CHANNELTYPE_GREEN] = 192;
	optctx->colorkey[IW_CHANNELTYPE_BLUE] = 192;

done:
	if(trns_mask) iw_free(ctx,trns_mask);
}

static void iwopt_try_rgb16_binary_trns(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	int i,j;
	const iw_byte *ptr;
	iw_byte *ptr2;
	iw_byte clr_used[256];
	iw_byte key_clr=0; // low 8-bits of red component of the key color
	iw_byte *trns_mask = NULL;

	if(!(ctx->output_profile&IW_PROFILE_BINARYTRNS)) return;
	if(!ctx->opt_binary_trns) return;

	iw_zeromem(clr_used,256);

	trns_mask = iw_malloc_large(ctx, optctx->width, optctx->height);
	if(!trns_mask) goto done;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+(i*2)*4];
			if(ptr[6]==0 && ptr[7]==0) {
				// Transparent pixel
				trns_mask[j*optctx->width+i] = 0;
				continue;
			}
			else {
				// Nontransparent pixel
				trns_mask[j*optctx->width+i] = 1;
			}
			// For the colors we look for, all bytes are 192 except possibly the low-red byte.
			if(ptr[0]!=192 || ptr[2]!=192 || ptr[3]!=192 || ptr[4]!=192 || ptr[5]!=192)
				continue;
			clr_used[(int)ptr[1]] = 1;
		}
	}

	if(!iwopt_find_unused(clr_used,256,&key_clr)) {
		goto done;
	}

	// Strip the alpha channel:
	iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_RGB,0,1,2);
	if(!optctx->tmp_pixels) goto done;

	// Change the color of all transparent pixels to the key color
	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr2 = &optctx->tmp_pixels[j*optctx->bpr+(i*2)*3];
			if(trns_mask[j*optctx->width+i]==0) {
				ptr2[0] = 192;
				ptr2[1] = key_clr;
				ptr2[2] = 192;
				ptr2[3] = 192;
				ptr2[4] = 192;
				ptr2[5] = 192;
			}
		}
	}

	optctx->has_colorkey_trns = 1;
	optctx->colorkey[IW_CHANNELTYPE_RED] = 192*256+key_clr;
	optctx->colorkey[IW_CHANNELTYPE_GREEN] = 192*256+192;
	optctx->colorkey[IW_CHANNELTYPE_BLUE] = 192*256+192;

done:
	if(trns_mask) iw_free(ctx,trns_mask);
}

static void iwopt_try_gray8_binary_trns(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	int i,j;
	const iw_byte *ptr;
	iw_byte *ptr2;
	iw_byte clr_used[256];
	iw_byte key_clr=0;
	iw_byte *trns_mask = NULL;

	if(!(ctx->output_profile&IW_PROFILE_BINARYTRNS)) return;
	if(!ctx->opt_binary_trns) return;

	iw_zeromem(clr_used,256);

	trns_mask = iw_malloc_large(ctx, optctx->width, optctx->height);
	if(!trns_mask) goto done;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+i*2];
			if(ptr[1]==0) {
				// Transparent pixel
				trns_mask[j*optctx->width+i] = 0;
				continue;
			}
			else {
				// Nontransparent pixel
				trns_mask[j*optctx->width+i] = 1;
			}
			clr_used[(int)ptr[0]] = 1;
		}
	}

	if(!iwopt_find_unused(clr_used,256,&key_clr)) {
		goto done;
	}

	// Strip the alpha channel:
	iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_GRAY,0,0,0);
	if(!optctx->tmp_pixels) goto done;

	// Change the color of all transparent pixels to the key color
	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr2 = &optctx->tmp_pixels[j*optctx->bpr+i];
			if(trns_mask[j*optctx->width+i]==0) {
				ptr2[0] = key_clr;
			}
		}
	}

	optctx->has_colorkey_trns = 1;
	optctx->colorkey[IW_CHANNELTYPE_RED] = key_clr;
	optctx->colorkey[IW_CHANNELTYPE_GREEN] = key_clr;
	optctx->colorkey[IW_CHANNELTYPE_BLUE] = key_clr;

done:
	if(trns_mask) iw_free(ctx,trns_mask);
}

static void iwopt_try_gray16_binary_trns(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	int i,j;
	const iw_byte *ptr;
	iw_byte *ptr2;
	iw_byte clr_used[256];
	iw_byte key_clr=0; // low 8-bits of the key color
	iw_byte *trns_mask = NULL;

	if(!(ctx->output_profile&IW_PROFILE_BINARYTRNS)) return;
	if(!ctx->opt_binary_trns) return;

	iw_zeromem(clr_used,256);

	trns_mask = iw_malloc_large(ctx, optctx->width, optctx->height);
	if(!trns_mask) goto done;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+(i*2)*2];
			if(ptr[2]==0 && ptr[3]==0) {
				// Transparent pixel
				trns_mask[j*optctx->width+i] = 0;
				continue;
			}
			else {
				// Nontransparent pixel
				trns_mask[j*optctx->width+i] = 1;
			}
			// For the colors we look for, the high byte is always 192.
			if(ptr[0]!=192)
				continue;
			clr_used[(int)ptr[1]] = 1;
		}
	}

	if(!iwopt_find_unused(clr_used,256,&key_clr)) {
		goto done;
	}

	// Strip the alpha channel:
	iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_GRAY,0,0,0);
	if(!optctx->tmp_pixels) goto done;

	// Change the color of all transparent pixels to the key color
	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr2 = &optctx->tmp_pixels[j*optctx->bpr+(i*2)];
			if(trns_mask[j*optctx->width+i]==0) {
				ptr2[0] = 192;
				ptr2[1] = key_clr;
			}
		}
	}

	optctx->has_colorkey_trns = 1;
	optctx->colorkey[IW_CHANNELTYPE_RED] = 192*256+key_clr;
	optctx->colorkey[IW_CHANNELTYPE_GREEN] = 192*256+key_clr;
	optctx->colorkey[IW_CHANNELTYPE_BLUE] = 192*256+key_clr;

done:
	if(trns_mask) iw_free(ctx,trns_mask);
}

////////////////////

// returns palette entry, or -1 if not found
static int iwopt_find_color(const struct iw_palette *pal, const struct iw_rgba8color *c)
{
	int i;
	for(i=0;i<pal->num_entries;i++) {
		if(pal->entry[i].r==c->r && pal->entry[i].g==c->g &&
			pal->entry[i].b==c->b && pal->entry[i].a==c->a)
		{
			return i;
		}
	}
	return -1;
}

// Returns palette index to use for the background color, or -1 if not found.
static int iwopt_find_bkgd_color(const struct iw_palette *pal, const struct iw_rgba8color *c)
{
	int i;
	for(i=0;i<pal->num_entries;i++) {
		if(pal->entry[i].a==0) {
			// A fully transparent palette color can always be used for the background
			// (assuming PNG-style background colors).
			return i;
		}
		if(pal->entry[i].r==c->r && pal->entry[i].g==c->g &&
			pal->entry[i].b==c->b)
		{
			return i;
		}
	}
	return -1;
}

// Writes to optctx->palette.
// Returns 1 if a palette can be used.
static int optctx_collect_palette_colors(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	int x,y;
	struct iw_rgba8color c;
	const iw_byte *ptr;
	int spp;
	int e;

	spp = iw_imgtype_num_channels(optctx->imgtype);

	for(y=0;y<optctx->height;y++) {
		for(x=0;x<optctx->width;x++) {
			ptr = &optctx->pixelsptr[y*optctx->bpr + x*spp];

			if(optctx->imgtype==IW_IMGTYPE_RGB) {
				c.r = ptr[0];
				c.g = ptr[1];
				c.b = ptr[2];
				c.a = 255;
			}
			else if(optctx->imgtype==IW_IMGTYPE_RGBA) {
				c.r = ptr[0];
				c.g = ptr[1];
				c.b = ptr[2];
				c.a = ptr[3];
				// TODO: This check is probably no longer necessary.
				if(c.a==0) { c.r = c.g = c.b = 0; } // all invisible colors are the same
			}
			else if(optctx->imgtype==IW_IMGTYPE_GRAYA) {
				c.r = c.g = c.b = ptr[0];
				c.a = ptr[1];
				// TODO: This check is probably no longer necessary.
				if(c.a==0) { c.r = c.g = c.b = 0; }
			}
			else { // optctx->imgtype==IW_IMGTYPE_GRAY(?)
				c.r = c.g = c.b = ptr[0];
				c.a = 255;
			}

			e = iwopt_find_color(optctx->palette,&c);
			if(e<0) {
				// not in palette
				if(optctx->palette->num_entries<256) {
					optctx->palette->entry[optctx->palette->num_entries] = c; // struct copy
					optctx->palette->num_entries++;
				}
				else {
					// Image has more than 256 colors.
					return 0;
				}
			}
		}
	}

	if(optctx->palette->num_entries<1) return 0; // Shouldn't happen.

	if(optctx->has_bkgdlabel) {
		c.r = optctx->bkgdlabel[0];
		c.g = optctx->bkgdlabel[1];
		c.b = optctx->bkgdlabel[2];
		c.a = 255;
		e = iwopt_find_bkgd_color(optctx->palette,&c);
		if(e<0) {
			// Did not find a suiteable palette entry for the background color.
			// Is there room to add one?
			if(optctx->palette->num_entries<256) {
				// Yes.
				optctx->palette->entry[optctx->palette->num_entries] = c;
				optctx->palette->num_entries++;
			}
			else {
				// No.
				return 0;
			}
		}
	}

	return 1;
}

static void iwopt_convert_to_palette_image(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	iw_byte *newpixels;
	size_t newbpr;
	int x,y;
	struct iw_rgba8color c;
	const iw_byte *ptr;
	int spp;
	int e;

	spp = iw_imgtype_num_channels(optctx->imgtype);

	newbpr = optctx->width;
	newpixels = iw_malloc_large(ctx, newbpr, optctx->height);
	if(!newpixels) return;

	for(y=0;y<optctx->height;y++) {
		for(x=0;x<optctx->width;x++) {
			ptr = &optctx->pixelsptr[y*optctx->bpr + x*spp];

			if(optctx->imgtype==IW_IMGTYPE_RGB) {
				c.r = ptr[0];
				c.g = ptr[1];
				c.b = ptr[2];
				c.a = 255;
			}
			else if(optctx->imgtype==IW_IMGTYPE_RGBA) {
				c.r = ptr[0];
				c.g = ptr[1];
				c.b = ptr[2];
				c.a = ptr[3];
				if(c.a==0) { c.r = c.g = c.b = 0; }
			}
			else if(optctx->imgtype==IW_IMGTYPE_GRAYA) {
				c.r = c.g = c.b = ptr[0];
				c.a = ptr[1];
				if(c.a==0) { c.r = c.g = c.b = 0; }
			}
			else { // optctx->imgtype==IW_IMGTYPE_GRAY(?)
				c.r = c.g = c.b = ptr[0];
				c.a = 255;
			}

			if(optctx->has_colorkey_trns && c.a==0) {
				// We'll only get here if the image is really grayscale.
				e = optctx->colorkey[IW_CHANNELTYPE_RED];
			}
			else {
				e = iwopt_find_color(optctx->palette,&c);
				if(e<0) e=0; // shouldn't happen
			}

			newpixels[y*newbpr + x] = e;
		}
	}

	// Remove previous image if it was allocated by the optimization code.
	if(optctx->tmp_pixels) iw_free(ctx,optctx->tmp_pixels);

	// Attach our new image
	optctx->tmp_pixels = newpixels;
	optctx->pixelsptr = optctx->tmp_pixels;
	optctx->bpr = newbpr;
	optctx->bit_depth = 8;
	optctx->imgtype = IW_IMGTYPE_PALETTE;
}

static int iwopt_palsortfunc(const void* p1, const void* p2)
{
	const struct iw_rgba8color *e1,*e2;
	int s1,s2;

	e1=(const struct iw_rgba8color*)p1;
	e2=(const struct iw_rgba8color*)p2;

	// First, sort by whether the color is fully opaque.
	if(e1->a==255 && e2->a<255) return 1;
	if(e1->a<255 && e2->a==255) return -1;

	// Then by the approximate brightness.
	s1=e1->r+e1->g+e1->b;
	s2=e2->r+e2->g+e2->b;
	if(s1>s2) return 1;
	if(s1<s2) return -1;

	// This is to make the order fully deterministic.
	if(e1->g > e2->g) return 1;
	if(e1->g < e2->g) return -1;
	if(e1->r > e2->r) return 1;
	if(e1->r < e2->r) return -1;
	if(e1->b > e2->b) return 1;
	if(e1->b < e2->b) return -1;
	if(e1->a > e2->a) return 1;
	if(e1->a < e2->a) return -1;

	return 0; // Should be unreachable.
}

// Figure out if we can do palette optimization at the given bitdepth.
static int iwopt_palette_opt_ok(struct iw_context *ctx, struct iw_opt_ctx *optctx, int bpp)
{
	if(!ctx->opt_palette) return 0;

	if(optctx->has_transparency && !(ctx->output_profile&IW_PROFILE_PALETTETRNS)) return 0;

	switch(bpp) {
	case 1:
		if(!(ctx->output_profile&IW_PROFILE_PAL1)) return 0;
		if(optctx->palette->num_entries>2) return 0;
		break;
	case 2:
		if(!(ctx->output_profile&IW_PROFILE_PAL2)) return 0;
		if(optctx->palette->num_entries>4) return 0;
		break;
	case 4:
		if(!(ctx->output_profile&IW_PROFILE_PAL4)) return 0;
		if(optctx->palette->num_entries>16) return 0;
		break;
	case 8:
		if(!(ctx->output_profile&IW_PROFILE_PAL8)) return 0;
		if(optctx->palette->num_entries>256) return 0;
		break;
	default:
		return 0;
	}

	return 1;
}

static int iwopt_palette_is_valid_gray(struct iw_context *ctx, struct iw_opt_ctx *optctx, int bpp,
	int *pbinary_trns, unsigned int *ptrns_shade)
{
	int factor;
	int i;
	int max_entries;
	iw_byte clr_used[256];
	iw_byte key_clr=0;

	*pbinary_trns = 0;
	*ptrns_shade = 0;

	if(optctx->has_color) return 0;
	if(optctx->has_partial_transparency) return 0;
	if(optctx->has_transparency && !ctx->opt_binary_trns) return 0;
	if(optctx->has_transparency && !(ctx->output_profile&IW_PROFILE_BINARYTRNS)) return 0;

	switch(bpp) {
	case 1: factor=255; max_entries=2; break;
	case 2: factor=85; max_entries=4; break;
	case 4: factor=17; max_entries=16; break;
	case 8: factor=1; max_entries=256; break;
	default: return 0;
	}

	if(optctx->palette->num_entries > max_entries)
		return 0;

	// If there is a background color label, it must be one of the available gray shades.
	if(optctx->has_bkgdlabel && bpp<8) {
		// We already know the bkgd label is gray (because has_color is false), so we
		// only have to look at one of the components.
		if(optctx->bkgdlabel[0] % factor != 0) {
			return 0;
		}
	}

	iw_zeromem(clr_used,256);

	for(i=0;i<optctx->palette->num_entries;i++) {
		if(optctx->palette->entry[i].a>0) { // Look at all the nontransparent entries.
			if(optctx->palette->entry[i].r % factor) return 0;

			// Keep track of which shades were used.
			clr_used[optctx->palette->entry[i].r / factor] = 1;
		}
	}

	// In order for binary transparency to be usable, there must be at least
	// one unused gray shade.
	if(optctx->has_transparency) {
		if(!iwopt_find_unused(clr_used,max_entries,&key_clr))
			return 0;

		*pbinary_trns = 1;
		*ptrns_shade = (unsigned int)key_clr;
	}

	return 1;
}

static void iwopt_make_gray_palette(struct iw_context *ctx, struct iw_opt_ctx *optctx, int bpp)
{
	int factor;
	int i;

	switch(bpp) {
	case 1: factor=255; optctx->palette->num_entries=2; break;
	case 2: factor=85; optctx->palette->num_entries=4; break;
	case 4: factor=17; optctx->palette->num_entries=16; break;
	default: factor=1; optctx->palette->num_entries=256; break;
	}

	for(i=0;i<optctx->palette->num_entries;i++) {
		optctx->palette->entry[i].r = optctx->palette->entry[i].g =
			optctx->palette->entry[i].b = factor*i;
		optctx->palette->entry[i].a = 255;
	}

	optctx->palette_is_grayscale=1;
}

// Optimize to palette, or 1-, 2-, or 4-bpp grayscale.
static void iwopt_try_pal_lowgray_optimization(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	int ret;
	int binary_trns;
	unsigned int trns_shade;

	if(!(ctx->output_profile&IW_PROFILE_PAL1) &&
	   !(ctx->output_profile&IW_PROFILE_PAL2) &&
	   !(ctx->output_profile&IW_PROFILE_PAL4) &&
	   !(ctx->output_profile&IW_PROFILE_PAL8) &&
	   !(ctx->output_profile&IW_PROFILE_GRAY1) &&
	   !(ctx->output_profile&IW_PROFILE_GRAY2) &&
	   !(ctx->output_profile&IW_PROFILE_GRAY4) )
	{
		// Output format doesn't support anything that this optimization can provide.
		return;
	}

	if(optctx->bit_depth!=8) {
		// Palettes aren't supported with bitdepth>8.
		return;
	}

	optctx->palette = iw_malloc(ctx,sizeof(struct iw_palette));
	if(!optctx->palette) return;

	optctx->palette->num_entries=0;

	ret = optctx_collect_palette_colors(ctx,optctx);
	if(!ret) {
		// Image can't be converted to a palette image.
		goto done;
	}

	// optctx->palette now contains a palette that can be used.

	// For images that have at most 256 (8-bit-compatible) colors, the order
	// of preference is gray1, pal1, gray2, pal2, gray4, pal4, gray8, pal8.

	if(ctx->opt_grayscale && (ctx->output_profile&IW_PROFILE_GRAY1) && iwopt_palette_is_valid_gray(ctx,optctx,1,&binary_trns,&trns_shade)) {
		// Replace the palette with a fully-populated grayscale palette.
		// The palette might already be correct, but it might not be.
		// It will be missing any gray shade that wasn't in the image.
		iwopt_make_gray_palette(ctx,optctx,1);
		if(binary_trns) {
			optctx->has_colorkey_trns = 1;
			optctx->colorkey[IW_CHANNELTYPE_RED] = optctx->colorkey[IW_CHANNELTYPE_BLUE] = optctx->colorkey[IW_CHANNELTYPE_GREEN] = trns_shade;
		}
	}
	else if(iwopt_palette_opt_ok(ctx,optctx,1)) {
		;
	}
	else if(ctx->opt_grayscale && (ctx->output_profile&IW_PROFILE_GRAY2) && iwopt_palette_is_valid_gray(ctx,optctx,2,&binary_trns,&trns_shade)) {
		iwopt_make_gray_palette(ctx,optctx,2);
		if(binary_trns) {
			optctx->has_colorkey_trns = 1;
			optctx->colorkey[IW_CHANNELTYPE_RED] = optctx->colorkey[IW_CHANNELTYPE_BLUE] = optctx->colorkey[IW_CHANNELTYPE_GREEN] = trns_shade;
		}
	}
	else if(iwopt_palette_opt_ok(ctx,optctx,2)) {
		;
	}
	else if(ctx->opt_grayscale && (ctx->output_profile&IW_PROFILE_GRAY4) && iwopt_palette_is_valid_gray(ctx,optctx,4,&binary_trns,&trns_shade)) {
		iwopt_make_gray_palette(ctx,optctx,4);
		if(binary_trns) {
			optctx->has_colorkey_trns = 1;
			optctx->colorkey[IW_CHANNELTYPE_RED] = optctx->colorkey[IW_CHANNELTYPE_BLUE] = optctx->colorkey[IW_CHANNELTYPE_GREEN] = trns_shade;
		}
	}
	else if(iwopt_palette_opt_ok(ctx,optctx,4)) {
		;
	}
	else if(ctx->opt_grayscale && (ctx->output_profile&IW_PROFILE_GRAYSCALE) && iwopt_palette_is_valid_gray(ctx,optctx,8,&binary_trns,&trns_shade)) {
		// This image can best be encoded as 8-bit grayscale. We don't handle that here.
		goto done;
	}
	else if(iwopt_palette_opt_ok(ctx,optctx,8)) {
		;
	}
	else {
		// Found no optimizations that we can perform.
		goto done;
	}

	if(!optctx->palette_is_grayscale) {
		// Sort the palette
		qsort((void*)optctx->palette->entry,optctx->palette->num_entries,
			sizeof(struct iw_rgba8color),iwopt_palsortfunc);
	}

	iwopt_convert_to_palette_image(ctx,optctx);

done:
	if(optctx->imgtype!=IW_IMGTYPE_PALETTE) {
		iw_free(ctx,optctx->palette);
		optctx->palette = NULL;
	}
}

////////////////////

static void make_transparent_pixels_black8(struct iw_context *ctx, struct iw_image *img, int nc)
{
	int i,j,k;
	iw_byte *p;

	for(j=0;j<img->height;j++) {
		for(i=0;i<img->width;i++) {
			p = &img->pixels[j*img->bpr + i*nc];
			if(p[nc-1]==0) {
				for(k=0;k<nc-1;k++) {
					p[k]=0;
				}
			}
		}
	}
}

static void make_transparent_pixels_black16(struct iw_context *ctx, struct iw_image *img, int nc)
{
	int i,j,k;
	iw_byte *p;

	for(j=0;j<img->height;j++) {
		for(i=0;i<img->width;i++) {
			p = &img->pixels[j*img->bpr + i*nc*2];
			if(p[(nc-1)*2]==0 && p[(nc-1)*2+1]==0) {
				for(k=0;k<nc-1;k++) {
					p[k*2  ]=0;
					p[k*2+1]=0;
				}
			}
		}
	}
}

// Make all fully transparent pixels "black". This makes the other optimization
// routines simpler, makes the output image more deterministic, and can make
// the image look better in viewers that ignore the alpha channel.
// Doing this in a separate pass is not the most efficient way to do it, but
// it's easiest and safest.
static void make_transparent_pixels_black(struct iw_context *ctx, struct iw_image *img)
{
	int nc;
	if(!IW_IMGTYPE_HAS_ALPHA(img->imgtype)) return;

	nc = iw_imgtype_num_channels(img->imgtype);

	if(img->bit_depth>8)
		make_transparent_pixels_black16(ctx,img,nc);
	else
		make_transparent_pixels_black8(ctx,img,nc);
}

// Strip alpha channel if there are no actual transparent pixels, etc.
void iwpvt_optimize_image(struct iw_context *ctx)
{
	struct iw_opt_ctx *optctx;
	int k;

	optctx = &ctx->optctx;

	//iw_zeromem(optctx,sizeof(struct iw_opt_ctx));
	optctx->width = ctx->img2.width;
	optctx->height = ctx->img2.height;
	optctx->imgtype = ctx->img2.imgtype;
	optctx->bit_depth = ctx->img2.bit_depth;
	optctx->bpr = ctx->img2.bpr;
	optctx->pixelsptr = ctx->img2.pixels;
	//optctx->has_transparency=0;
	//optctx->has_partial_transparency=0;
	//optctx->has_16bit_precision=0;
	//optctx->has_color=0;
	if(ctx->img2.has_bkgdlabel) {
		optctx->has_bkgdlabel = ctx->img2.has_bkgdlabel;
		for(k=0;k<4;k++) {
			optctx->bkgdlabel[k] = iw_color_get_int_sample(&ctx->img2.bkgdlabel, k,
				ctx->img2.bit_depth==8?255:65535);
		}
	}

	if(ctx->img2.sampletype!=IW_SAMPLETYPE_UINT) {
		return;
	}

	if(ctx->reduced_output_maxcolor_flag) {
		return;
	}

	make_transparent_pixels_black(ctx,&ctx->img2);

	if(optctx->has_bkgdlabel) {
		// The optimization routines are responsible for ensuring that the
		// background color label can easily be written to the optimized image.
		// For example, they may have to add a color to the palette just for
		// the background color.
		// They are NOT responsible for telling the image encoder module
		// precisely how to write the background color. The encoder will be
		// given the background color in RGB format, and it will have to figure
		// out what to do with it. For example, it may have to search for that
		// color in the palette.

		// If the background color label exists, and is non-gray,
		// make sure we don't write a grayscale image
		// (assuming we're writing to a PNG-like format).
		if(optctx->bkgdlabel[0] != optctx->bkgdlabel[1] ||
			optctx->bkgdlabel[0] != optctx->bkgdlabel[2])
		{
			optctx->has_color = 1;
		}

		// If 16-bit precision is desired, and the background color cannot be
		// losslessly reduced to 8-bit precision, use 16-bit precision.
		if(optctx->bit_depth==16) {
			if(optctx->bkgdlabel[0]%257!=0 || optctx->bkgdlabel[1]%257!=0 ||
				optctx->bkgdlabel[2]%257!=0)
			{
				optctx->has_16bit_precision=1;
			}
		}
	}

	if(!iw_opt_scanpixels(ctx,optctx)) {
		goto noscan;
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==16 && !optctx->has_16bit_precision) {
		iw_opt_16_to_8(ctx,optctx,4);
	}

	if(optctx->imgtype==IW_IMGTYPE_RGB && optctx->bit_depth==16 && !optctx->has_16bit_precision) {
		iw_opt_16_to_8(ctx,optctx,3);
	}

	if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==16 && !optctx->has_16bit_precision) {
		iw_opt_16_to_8(ctx,optctx,2);
	}

	if(optctx->imgtype==IW_IMGTYPE_GRAY && optctx->bit_depth==16 && !optctx->has_16bit_precision) {
		iw_opt_16_to_8(ctx,optctx,1);
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==8 && !optctx->has_transparency && ctx->opt_strip_alpha) {
		iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_RGB,0,1,2); // RGBA -> RGB
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==16 && !optctx->has_transparency && ctx->opt_strip_alpha) {
		iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_RGB,0,1,2); // RGBA -> RGB (16)
	}

	if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==16 && !optctx->has_transparency && ctx->opt_strip_alpha) {
		iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_GRAY,0, 0,0); // GA -> G (16)
	}

	if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==8 && !optctx->has_transparency && ctx->opt_strip_alpha) {
		iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_GRAY,0, 0,0); // GA -> G
	}

	if(optctx->imgtype==IW_IMGTYPE_RGB && optctx->bit_depth==8 && !optctx->has_color &&
	   (ctx->output_profile&IW_PROFILE_GRAYSCALE) && ctx->opt_grayscale)
	{
		iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_GRAY,0, 0,0); // RGB -> G
	}

	if(optctx->imgtype==IW_IMGTYPE_RGB && optctx->bit_depth==16 && !optctx->has_color &&
	   (ctx->output_profile&IW_PROFILE_GRAYSCALE) && ctx->opt_grayscale)
	{
		iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_GRAY,0, 0,0); // RGB -> G (16)
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==8 && !optctx->has_color &&
	   (ctx->output_profile&IW_PROFILE_GRAYSCALE) && ctx->opt_grayscale)
	{
		iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_GRAYA,0,3, 0); // RGBA -> GA
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==16 && !optctx->has_color &&
	   (ctx->output_profile&IW_PROFILE_GRAYSCALE) && ctx->opt_grayscale)
	{
		iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_GRAYA,0,3, 0); // RGBA -> GA (16)
	}

noscan:

	iwopt_try_pal_lowgray_optimization(ctx,optctx);

	// Try to convert an alpha channel to binary transparency.

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==8 && !optctx->has_partial_transparency) {
		iwopt_try_rgb8_binary_trns(ctx,optctx);
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==16 && !optctx->has_partial_transparency) {
		iwopt_try_rgb16_binary_trns(ctx,optctx);
	}

	if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==8 && !optctx->has_partial_transparency) {
		iwopt_try_gray8_binary_trns(ctx,optctx);
	}

	if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==16 && !optctx->has_partial_transparency) {
		iwopt_try_gray16_binary_trns(ctx,optctx);
	}
}
