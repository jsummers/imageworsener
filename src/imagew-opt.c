// imagew-opt.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// Routines for optimizing the output file.

#include "imagew-config.h"

#ifdef IW_WINDOWS
#include <tchar.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef IW_WINDOWS
#include <search.h> // for qsort
#endif

#include "imagew-internals.h"

static void iw_opt_scanpixels_rgb8(struct iw_opt_ctx *optctx)
{
	int i,j;
	unsigned char r,g,b;
	const unsigned char *ptr;

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
	unsigned char gray,a;
	const unsigned char *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+i*2];
			gray=ptr[0]; a=ptr[1];

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
	unsigned char r,g,b,a;
	const unsigned char *ptr;

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
	unsigned int gray;
	const unsigned char *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+(i*2)];

			// Check if 16-bit output is necessary
			if(ptr[0]!=ptr[1])
			{
				optctx->has_16bit_precision=1;
			}

			gray=(((unsigned int)ptr[0])<<8) | ptr[1];

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
	unsigned int gray, a;
	const unsigned char *ptr;

	for(j=0;j<optctx->height;j++) {
		for(i=0;i<optctx->width;i++) {
			ptr = &optctx->pixelsptr[j*optctx->bpr+(i*2)*2];

			// Check if 16-bit output is necessary
			if(ptr[0]!=ptr[1] || ptr[2]!=ptr[3])
			{
				optctx->has_16bit_precision=1;
			}

			gray=(((unsigned int)ptr[0])<<8) | ptr[1];
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
	const unsigned char *ptr;

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
	const unsigned char *ptr;

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
	unsigned char *newpixels;
	size_t newbpr;
	int i,j;

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
	if(optctx->tmp_pixels) iw_free(optctx->tmp_pixels);

	// Attach our new image
	optctx->tmp_pixels = newpixels;
	optctx->pixelsptr = optctx->tmp_pixels;
	optctx->bpr = newbpr;
	optctx->bit_depth = 8;
}

// Create a new (8-bit) image by copying up to 3 channels from the old image.
static void iw_opt_copychannels_8(struct iw_context *ctx, struct iw_opt_ctx *optctx,
			int new_imgtype, int c0, int c1, int c2)
{
	unsigned char *newpixels;
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
	if(optctx->tmp_pixels) iw_free(optctx->tmp_pixels);

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
	unsigned char *newpixels;
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
	if(optctx->tmp_pixels) iw_free(optctx->tmp_pixels);

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

// Returns 1 if a palette can be used.
static int optctx_collect_palette_colors(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	int x,y;
	struct iw_rgba8color c;
	const unsigned char *ptr;
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
				if(c.a==0) { c.r = c.g = c.b = 0; } // all invisible colors are the same
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
	return 1;
}

static void iwopt_convert_to_palette_image(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	unsigned char *newpixels;
	size_t newbpr;
	int x,y;
	struct iw_rgba8color c;
	const unsigned char *ptr;
	int spp;
	int e;

	spp = iw_imgtype_num_channels(optctx->imgtype);

	newbpr = iw_calc_bytesperrow(optctx->width,8*spp);
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

			e = iwopt_find_color(optctx->palette,&c);
			if(e<0) e=0; // shouldn't happen

			newpixels[y*newbpr + x] = e;
		}
	}

	// Remove previous image if it was allocated by the optimization code.
	if(optctx->tmp_pixels) iw_free(optctx->tmp_pixels);

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

static int iwopt_palette_is_valid_gray(struct iw_context *ctx, struct iw_opt_ctx *optctx, int bpp)
{
	int factor;
	int i;

	if(optctx->has_color) return 0;
	if(optctx->has_transparency) return 0;

	switch(bpp) {
	case 1: factor=255; break;
	case 2: factor=85; break;
	case 4: factor=17; break;
	case 8: factor=1; break;
	default: return 0;
	}

	for(i=0;i<optctx->palette->num_entries;i++) {
		if(optctx->palette->entry[i].r % factor) return 0;
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

static void iwopt_try_palette_optimization(struct iw_context *ctx, struct iw_opt_ctx *optctx)
{
	int ret;
	int to_grayscale=0;

	if(!(ctx->output_profile&IW_PROFILE_PALETTE)) {
		// Output format doesn't support palette images.
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

	if(optctx->palette->num_entries>16 && optctx->imgtype==IW_IMGTYPE_GRAY)
	{
		// Image is already encoded as 8bpp grayscale; encoding it as
		// 8bpp palette wouldn't be an improvement.
		goto done;
	}

	if((ctx->output_profile&IW_PROFILE_1BPP) && iwopt_palette_is_valid_gray(ctx,optctx,1)) {
		// Replace the palette with a fully-populated grayscale palette.
		// The palette might already be correct, but it might not be.
		// It will be missing any gray shade that wasn't in the image.
		iwopt_make_gray_palette(ctx,optctx,1);
	}
	else if((ctx->output_profile&IW_PROFILE_2BPP) && iwopt_palette_is_valid_gray(ctx,optctx,2)) {
		iwopt_make_gray_palette(ctx,optctx,2);
	}
	else if((ctx->output_profile&IW_PROFILE_4BPP) && iwopt_palette_is_valid_gray(ctx,optctx,4)) {
		iwopt_make_gray_palette(ctx,optctx,4);
	}

	// Sort the palette
	if(!optctx->palette_is_grayscale) {
		qsort((void*)optctx->palette->entry,optctx->palette->num_entries,
			sizeof(struct iw_rgba8color),iwopt_palsortfunc);
	}

	iwopt_convert_to_palette_image(ctx,optctx);

done:
	if(optctx->imgtype!=IW_IMGTYPE_PALETTE) {
		iw_free(optctx->palette);
		optctx->palette = NULL;
	}
}

////////////////////

// Strip alpha channel if there are no actual transparent pixels, etc.
void iw_optimize_image(struct iw_context *ctx)
{
	struct iw_opt_ctx *optctx;

	optctx = &ctx->optctx;

	memset(optctx,0,sizeof(struct iw_opt_ctx));
	optctx->width = ctx->img2.width;
	optctx->height = ctx->img2.height;
	optctx->imgtype = ctx->img2.imgtype;
	optctx->bit_depth = ctx->img2.bit_depth;
	optctx->bpr = ctx->img2.bpr;
	optctx->pixelsptr = ctx->img2.pixels;
	optctx->has_transparency=0;
	optctx->has_partial_transparency=0;
	optctx->has_16bit_precision=0;
	optctx->has_color=0;

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

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==8 && !optctx->has_transparency) {
		iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_RGB,0,1,2); // RGBA -> RGB
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==16 && !optctx->has_transparency) {
		iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_RGB,0,1,2); // RGBA -> RGB (16)
	}

	if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==16 && !optctx->has_transparency) {
		iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_GRAY,0, 0,0); // GA -> G (16)
	}

	if(optctx->imgtype==IW_IMGTYPE_GRAYA && optctx->bit_depth==8 && !optctx->has_transparency) {
		iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_GRAY,0, 0,0); // GA -> G
	}

	if(optctx->imgtype==IW_IMGTYPE_RGB && optctx->bit_depth==8 && !optctx->has_color) {
		iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_GRAY,0, 0,0); // RGB -> G
	}

	if(optctx->imgtype==IW_IMGTYPE_RGB && optctx->bit_depth==16 && !optctx->has_color) {
		iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_GRAY,0, 0,0); // RGB -> G (16)
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==8 && !optctx->has_color) {
		iw_opt_copychannels_8(ctx,optctx,IW_IMGTYPE_GRAYA,0,3, 0); // RGBA -> GA
	}

	if(optctx->imgtype==IW_IMGTYPE_RGBA && optctx->bit_depth==16 && !optctx->has_color) {
		iw_opt_copychannels_16(ctx,optctx,IW_IMGTYPE_GRAYA,0,3, 0); // RGBA -> GA (16)
	}

noscan:

	iwopt_try_palette_optimization(ctx,optctx);
}
