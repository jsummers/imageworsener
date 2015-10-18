// imagew-main.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "imagew-internals.h"


// Given a color type having an alpha channel, returns the index of the
// alpha channel.
// Return value is not meaningful if type does not have an alpha channel.
static int iw_imgtype_alpha_channel_index(int t)
{
	switch(t) {
	case IW_IMGTYPE_RGBA:
		return 3;
	case IW_IMGTYPE_GRAYA:
		return 1;
	}
	return 0;
}

static IW_INLINE iw_tmpsample srgb_to_linear_sample(iw_tmpsample v_srgb)
{
	if(v_srgb<=0.04045) {
		return v_srgb/12.92;
	}
	else {
		return pow( (v_srgb+0.055)/(1.055) , 2.4);
	}
}

static IW_INLINE iw_tmpsample rec709_to_linear_sample(iw_tmpsample v_rec709)
{
	if(v_rec709 < 4.5*0.020) {
		return v_rec709/4.5;
	}
	else {
		return pow( (v_rec709+0.099)/1.099 , 1.0/0.45);
	}
}

static IW_INLINE iw_tmpsample gamma_to_linear_sample(iw_tmpsample v, double gamma)
{
	return pow(v,gamma);
}

static iw_tmpsample x_to_linear_sample(iw_tmpsample v, const struct iw_csdescr *csdescr)
{
	switch(csdescr->cstype) {
	case IW_CSTYPE_SRGB:
		return srgb_to_linear_sample(v);
	case IW_CSTYPE_LINEAR:
		return v;
	case IW_CSTYPE_GAMMA:
		return gamma_to_linear_sample(v,csdescr->gamma);
	case IW_CSTYPE_REC709:
		return rec709_to_linear_sample(v);
	}
	return srgb_to_linear_sample(v);
}

// Public version of x_to_linear_sample().
IW_IMPL(double) iw_convert_sample_to_linear(double v, const struct iw_csdescr *csdescr)
{
	return (double)x_to_linear_sample(v,csdescr);
}

static IW_INLINE iw_tmpsample linear_to_srgb_sample(iw_tmpsample v_linear)
{
	if(v_linear <= 0.0031308) {
		return 12.92*v_linear;
	}
	return 1.055*pow(v_linear,1.0/2.4) - 0.055;
}

static IW_INLINE iw_tmpsample linear_to_rec709_sample(iw_tmpsample v_linear)
{
	// The cutoff point is supposed to be 0.018, but that doesn't make sense,
	// because the curves don't intersect there. They intersect at almost exactly
	// 0.020.
	if(v_linear < 0.020) {
		return 4.5*v_linear;
	}
	return 1.099*pow(v_linear,0.45) - 0.099;
}

static IW_INLINE iw_tmpsample linear_to_gamma_sample(iw_tmpsample v_linear, double gamma)
{
	return pow(v_linear,1.0/gamma);
}

static iw_float32 iw_get_float32(const iw_byte *m)
{
	int k;
	// !!! Portability warning: Using a union in this way may be nonportable.
	union su_union {
		iw_byte c[4];
		iw_float32 f;
	} volatile su;

	for(k=0;k<4;k++) {
		su.c[k] = m[k];
	}
	return su.f;
}

static void iw_put_float32(iw_byte *m, iw_float32 s)
{
	int k;
	// !!! Portability warning: Using a union in this way may be nonportable.
	union su_union {
		iw_byte c[4];
		iw_float32 f;
	} volatile su;

	su.f = s;

	for(k=0;k<4;k++) {
		m[k] = su.c[k];
	}
}

static iw_tmpsample get_raw_sample_flt32(struct iw_context *ctx,
	   int x, int y, int channel)
{
	size_t z;
	z = y*ctx->img1.bpr + (ctx->img1_numchannels_physical*x + channel)*4;
	return (iw_tmpsample)iw_get_float32(&ctx->img1.pixels[z]);
}

static IW_INLINE unsigned int get_raw_sample_16(struct iw_context *ctx,
	   int x, int y, int channel)
{
	size_t z;
	unsigned short tmpui16;
	z = y*ctx->img1.bpr + (ctx->img1_numchannels_physical*x + channel)*2;
	tmpui16 = ( ((unsigned short)(ctx->img1.pixels[z+0])) <<8) | ctx->img1.pixels[z+1];
	return tmpui16;
}

static IW_INLINE unsigned int get_raw_sample_8(struct iw_context *ctx,
	   int x, int y, int channel)
{
	unsigned short tmpui8;
	tmpui8 = ctx->img1.pixels[y*ctx->img1.bpr + ctx->img1_numchannels_physical*x + channel];
	return tmpui8;
}

// 4 bits/pixel
static IW_INLINE unsigned int get_raw_sample_4(struct iw_context *ctx,
	   int x, int y)
{
	unsigned short tmpui8;
	tmpui8 = ctx->img1.pixels[y*ctx->img1.bpr + x/2];
	if(x&0x1)
		tmpui8 = tmpui8&0x0f;
	else
		tmpui8 = tmpui8>>4;
	return tmpui8;
}

// 2 bits/pixel
static IW_INLINE unsigned int get_raw_sample_2(struct iw_context *ctx,
	   int x, int y)
{
	unsigned short tmpui8;
	tmpui8 = ctx->img1.pixels[y*ctx->img1.bpr + x/4];
	tmpui8 = ( tmpui8 >> ((3-x%4)*2) ) & 0x03;
	return tmpui8;
}

// 1 bit/pixel
static IW_INLINE unsigned int get_raw_sample_1(struct iw_context *ctx,
	   int x, int y)
{
	unsigned short tmpui8;
	tmpui8 = ctx->img1.pixels[y*ctx->img1.bpr + x/8];
	if(tmpui8 & (1<<(7-x%8))) return 1;
	return 0;
}

// Translate a pixel position from logical to physical coordinates.
static IW_INLINE void translate_coords(struct iw_context *ctx,
	int x, int y, int *prx, int *pry)
{
	if(ctx->img1.orient_transform==0) {
		// The fast path
		*prx = ctx->input_start_x+x;
		*pry = ctx->input_start_y+y;
		return;
	}

	switch(ctx->img1.orient_transform) {
	case 1: // mirror-x
		*prx = ctx->img1.width - 1 - (ctx->input_start_x+x);
		*pry = ctx->input_start_y+y;
		break;
	case 2: // mirror-y
		*prx = ctx->input_start_x+x;
		*pry = ctx->img1.height - 1 - (ctx->input_start_y+y);
		break;
	case 3: // mirror-x, mirror-y
		*prx = ctx->img1.width - 1 - (ctx->input_start_x+x);
		*pry = ctx->img1.height - 1 - (ctx->input_start_y+y);
		break;
	case 4:
		// transpose
		*prx = ctx->input_start_y+y;
		*pry = ctx->input_start_x+x;
		break;
	case 5:
		*prx = ctx->input_start_y+y;
		*pry = ctx->img1.width - 1 - (ctx->input_start_x+x);
		break;
	case 6:
		*prx = ctx->img1.height - 1 - (ctx->input_start_y+y);
		*pry = ctx->input_start_x+x;
		break;
	case 7:
		*prx = ctx->img1.height - 1 - (ctx->input_start_y+y);
		*pry = ctx->img1.width - 1 - (ctx->input_start_x+x);
		break;
	default:
		*prx = 0;
		*pry = 0;
		break;
	}
}

// Returns a value from 0 to 2^(ctx->img1.bit_depth)-1.
// x and y are logical coordinates.
static unsigned int get_raw_sample_int(struct iw_context *ctx,
	   int x, int y, int channel)
{
	int rx,ry; // physical coordinates

	translate_coords(ctx,x,y,&rx,&ry);

	switch(ctx->img1.bit_depth) {
	case 8: return get_raw_sample_8(ctx,rx,ry,channel);
	case 1: return get_raw_sample_1(ctx,rx,ry);
	case 16: return get_raw_sample_16(ctx,rx,ry,channel);
	case 4: return get_raw_sample_4(ctx,rx,ry);
	case 2: return get_raw_sample_2(ctx,rx,ry);
	}
	return 0;
}

// Channel is the input channel number.
// x and y are logical coordinates.
static iw_tmpsample get_raw_sample(struct iw_context *ctx,
	   int x, int y, int channel)
{
	unsigned int v;

	if(channel>=ctx->img1_numchannels_physical) {
		// This is a virtual alpha channel. Return "opaque".
		return 1.0;
	}

	if(ctx->img1.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
		int rx, ry;
		translate_coords(ctx,x,y,&rx,&ry);
		if(ctx->img1.bit_depth!=32) return 0.0;
		return get_raw_sample_flt32(ctx,rx,ry,channel);
	}

	v = get_raw_sample_int(ctx,x,y,channel);
	return ((double)v) / ctx->img1_ci[channel].maxcolorcode_dbl;
}

static iw_tmpsample iw_color_to_grayscale(struct iw_context *ctx,
	iw_tmpsample r, iw_tmpsample g, iw_tmpsample b)
{
	iw_tmpsample v0,v1,v2;

	switch(ctx->grayscale_formula) {
	case IW_GSF_WEIGHTED:
		return ctx->grayscale_weight[0]*r +
			ctx->grayscale_weight[1]*g +
			ctx->grayscale_weight[2]*b;
	case IW_GSF_ORDERBYVALUE:
		// Sort the R, G, and B values, then use the corresponding weights.
		if(g<=r) { v0=r; v1=g; }
		else { v0=g; v1=r; }
		if(b<=v1) {
			v2=b;
		}
		else {
			v2=v1;
			if(b<=v0) { v1=b; }
			else { v1=v0; v0=b; }
		}
		return ctx->grayscale_weight[0]*v0 +
			ctx->grayscale_weight[1]*v1 +
			ctx->grayscale_weight[2]*v2;
	}
	return 0.0;
}

// Based on color depth of the input image.
// Assumes this channel's maxcolorcode == ctx->input_maxcolorcode
static iw_tmpsample cvt_int_sample_to_linear(struct iw_context *ctx,
	unsigned int v, const struct iw_csdescr *csdescr)
{
	iw_tmpsample s;

	if(csdescr->cstype==IW_CSTYPE_LINEAR) {
		// Sort of a hack: This is not just an optimization for linear colorspaces,
		// but is necessary to handle alpha channels correctly.
		// The lookup table is not correct for alpha channels.
		return ((double)v) / ctx->input_maxcolorcode;
	}
	else if(ctx->input_color_corr_table) {
		// If the colorspace is not linear, assume we can use the lookup table.
		return ctx->input_color_corr_table[v];
	}

	s = ((double)v) / ctx->input_maxcolorcode;
	return x_to_linear_sample(s,csdescr);
}

// Based on color depth of the output image.
static iw_tmpsample cvt_int_sample_to_linear_output(struct iw_context *ctx,
	unsigned int v, const struct iw_csdescr *csdescr, double overall_maxcolorcode)
{
	iw_tmpsample s;

	if(csdescr->cstype==IW_CSTYPE_LINEAR) {
		return ((double)v) / overall_maxcolorcode;
	}
	else if(ctx->output_rev_color_corr_table) {
		return ctx->output_rev_color_corr_table[v];
	}

	s = ((double)v) / overall_maxcolorcode;
	return x_to_linear_sample(s,csdescr);
}

// Return a sample, converted to a linear colorspace if it isn't already in one.
// Channel is the output channel number.
static iw_tmpsample get_sample_cvt_to_linear(struct iw_context *ctx,
	   int x, int y, int channel, const struct iw_csdescr *csdescr)
{
	unsigned int v1,v2,v3;
	iw_tmpsample r,g,b;
	int ch;

	ch = ctx->intermed_ci[channel].corresponding_input_channel;

	if(ctx->img1_ci[ch].disable_fast_get_sample) {
		// The slow way...
		if(ctx->intermed_ci[channel].cvt_to_grayscale) {
			r = x_to_linear_sample(get_raw_sample(ctx,x,y,ch+0),csdescr);
			g = x_to_linear_sample(get_raw_sample(ctx,x,y,ch+1),csdescr);
			b = x_to_linear_sample(get_raw_sample(ctx,x,y,ch+2),csdescr);
			return iw_color_to_grayscale(ctx,r,g,b);
		}
		return x_to_linear_sample(get_raw_sample(ctx,x,y,ch),csdescr);
	}

	// This method is faster, because it may use a gamma lookup table.
	// But all channels have to have the nominal input bitdepth, and it doesn't
	// support floating point samples, or a virtual alpha channel.
	if(ctx->intermed_ci[channel].cvt_to_grayscale) {
		v1 = get_raw_sample_int(ctx,x,y,ch+0);
		v2 = get_raw_sample_int(ctx,x,y,ch+1);
		v3 = get_raw_sample_int(ctx,x,y,ch+2);
		r = cvt_int_sample_to_linear(ctx,v1,csdescr);
		g = cvt_int_sample_to_linear(ctx,v2,csdescr);
		b = cvt_int_sample_to_linear(ctx,v3,csdescr);
		return iw_color_to_grayscale(ctx,r,g,b);
	}

	v1 = get_raw_sample_int(ctx,x,y,ch);
	return cvt_int_sample_to_linear(ctx,v1,csdescr);
}

// s is from 0.0 to 65535.0
static IW_INLINE void put_raw_sample_16(struct iw_context *ctx, double s,
	   int x, int y, int channel)
{
	size_t z;
	unsigned short tmpui16;

	tmpui16 = (unsigned short)(0.5+s);
	z = y*ctx->img2.bpr + (ctx->img2_numchannels*x + channel)*2;
	ctx->img2.pixels[z+0] = (iw_byte)(tmpui16>>8);
	ctx->img2.pixels[z+1] = (iw_byte)(tmpui16&0xff);
}

// s is from 0.0 to 255.0
static IW_INLINE void put_raw_sample_8(struct iw_context *ctx, double s,
	   int x, int y, int channel)
{
	iw_byte tmpui8;

	tmpui8 = (iw_byte)(0.5+s);
	ctx->img2.pixels[y*ctx->img2.bpr + ctx->img2_numchannels*x + channel] = tmpui8;
}

// Sample must already be scaled and in the target colorspace. E.g. 255.0 might be white.
static void put_raw_sample(struct iw_context *ctx, double s,
	   int x, int y, int channel)
{
	switch(ctx->img2.bit_depth) {
	case 8:  put_raw_sample_8(ctx,s,x,y,channel); break;
	case 16: put_raw_sample_16(ctx,s,x,y,channel); break;
	}
}

// s is from 0.0 to 1.0
static void put_raw_sample_flt32(struct iw_context *ctx, double s,
	   int x, int y, int channel)
{
	size_t pos;
	pos = y*ctx->img2.bpr + (ctx->img2_numchannels*x + channel)*4;
	iw_put_float32(&ctx->img2.pixels[pos], (iw_float32)s);
}

static iw_tmpsample linear_to_x_sample(iw_tmpsample samp_lin, const struct iw_csdescr *csdescr)
{
	if(samp_lin > 0.999999999) {
		// This check is done mostly because glibc's pow() function may be
		// very slow for some arguments near 1.
		return 1.0;
	}

	switch(csdescr->cstype) {
	case IW_CSTYPE_SRGB:
		return linear_to_srgb_sample(samp_lin);
	case IW_CSTYPE_LINEAR:
		return samp_lin;
	case IW_CSTYPE_GAMMA:
		return linear_to_gamma_sample(samp_lin,csdescr->gamma);
	case IW_CSTYPE_REC709:
		return linear_to_rec709_sample(samp_lin);
	}
	return linear_to_srgb_sample(samp_lin);
}

// Public version of linear_to_x_sample().
IW_IMPL(double) iw_convert_sample_from_linear(double v, const struct iw_csdescr *csdescr)
{
	return (double)linear_to_x_sample(v,csdescr);
}

// Returns 0 if we should round down, 1 if we should round up.
// TODO: It might be good to use a different-sized matrix for alpha channels
// (e.g. 9x7), but I don't know how to make a good one.
static int iw_ordered_dither(int dithersubtype, double fraction, int x, int y)
{
	double threshold;
	static const float pattern[2][64] = {
	 { // Dispersed ordered dither
		 0.5/64,48.5/64,12.5/64,60.5/64, 3.5/64,51.5/64,15.5/64,63.5/64,
		32.5/64,16.5/64,44.5/64,28.5/64,35.5/64,19.5/64,47.5/64,31.5/64,
		 8.5/64,56.5/64, 4.5/64,52.5/64,11.5/64,59.5/64, 7.5/64,55.5/64,
		40.5/64,24.5/64,36.5/64,20.5/64,43.5/64,27.5/64,39.5/64,23.5/64,
		 2.5/64,50.5/64,14.5/64,62.5/64, 1.5/64,49.5/64,13.5/64,61.5/64,
		34.5/64,18.5/64,46.5/64,30.5/64,33.5/64,17.5/64,45.5/64,29.5/64,
		10.5/64,58.5/64, 6.5/64,54.5/64, 9.5/64,57.5/64, 5.5/64,53.5/64,
		42.5/64,26.5/64,38.5/64,22.5/64,41.5/64,25.5/64,37.5/64,21.5/64
	 },
	 { // Halftone ordered dither
		 3.5/64, 9.5/64,17.5/64,27.5/64,25.5/64,15.5/64, 7.5/64, 1.5/64,
		11.5/64,29.5/64,37.5/64,45.5/64,43.5/64,35.5/64,23.5/64, 5.5/64,
		19.5/64,39.5/64,51.5/64,57.5/64,55.5/64,49.5/64,33.5/64,13.5/64,
		31.5/64,47.5/64,59.5/64,63.5/64,61.5/64,53.5/64,41.5/64,21.5/64,
		30.5/64,46.5/64,58.5/64,62.5/64,60.5/64,52.5/64,40.5/64,20.5/64,
		18.5/64,38.5/64,50.5/64,56.5/64,54.5/64,48.5/64,32.5/64,12.5/64,
		10.5/64,28.5/64,36.5/64,44.5/64,42.5/64,34.5/64,22.5/64, 4.5/64,
		 2.5/64, 8.5/64,16.5/64,26.5/64,24.5/64,14.5/64, 6.5/64, 0.5/64
	 }};

	threshold = pattern[dithersubtype][(x%8) + 8*(y%8)];
	return (fraction >= threshold);
}

// Returns 0 if we should round down, 1 if we should round up.
static int iw_random_dither(struct iw_context *ctx, double fraction, int x, int y,
	int dithersubtype, int channel)
{
	double threshold;

	threshold = ((double)iwpvt_prng_rand(ctx->prng)) / (double)0xffffffff;
	if(fraction>=threshold) return 1;
	return 0;
}

static void iw_errdiff_dither(struct iw_context *ctx,int dithersubtype,
	double err,int x,int y)
{
	int fwd;
	const double *m;

	//        x  0  1
	//  2  3  4  5  6
	//  7  8  9 10 11

	static const double matrix_list[][12] = {
	{                          7.0/16, 0.0,     // 0 = Floyd-Steinberg
	   0.0   , 3.0/16, 5.0/16, 1.0/16, 0.0,
	   0.0   ,    0.0,    0.0, 0.0   , 0.0    },
	{                          7.0/48, 5.0/48,  // 1 = JJN
	   3.0/48, 5.0/48, 7.0/48, 5.0/48, 3.0/48,
	   1.0/48, 3.0/48, 5.0/48, 3.0/48, 1.0/48 },
	{                          8.0/42, 4.0/42,  // 2 = Stucki
	   2.0/42, 4.0/42, 8.0/42, 4.0/42, 2.0/42,
	   1.0/42, 2.0/42, 4.0/42, 2.0/42, 1.0/42 },
	{                          8.0/32, 4.0/32,  // 3 = Burkes
	   2.0/32, 4.0/32, 8.0/32, 4.0/32, 2.0/32,
	   0.0   , 0.0   , 0.0   , 0.0   , 0.0    },
	{                          5.0/32, 3.0/32,  // 4 = Sierra3
	   2.0/32, 4.0/32, 5.0/32, 4.0/32, 2.0/32,
	      0.0, 2.0/32, 3.0/32, 2.0/32, 0.0    },
	{                          4.0/16, 3.0/16,  // 5 = Sierra2
	   1.0/16, 2.0/16, 3.0/16, 2.0/16, 1.0/16,
	   0.0   , 0.0   , 0.0   , 0.0   , 0.0    },
	{                          2.0/4 , 0.0,     // 6 = Sierra42a
	   0.0   , 1.0/4 , 1.0/4 , 0.0   , 0.0,
	   0.0   , 0.0   , 0.0   , 0.0   , 0.0    },
	{                          1.0/8 , 1.0/8,   // 7 = Atkinson
	   0.0   , 1.0/8 , 1.0/8 , 1.0/8 , 0.0,
	   0.0   , 0.0   , 1.0/8 , 0.0   , 0.0    }
	};

	if(dithersubtype<=7)
		m = matrix_list[dithersubtype];
	else
		m = matrix_list[0];

	fwd = (y%2)?(-1):1;

	if((x-fwd)>=0 && (x-fwd)<ctx->img2.width) {
		if((x-2*fwd)>=0 && (x-2*fwd)<ctx->img2.width) {
			ctx->dither_errors[1][x-2*fwd] += err*(m[2]);
			ctx->dither_errors[2][x-2*fwd] += err*(m[7]);
		}
		ctx->dither_errors[1][x-fwd] += err*(m[3]);
		ctx->dither_errors[2][x-fwd] += err*(m[8]);
	}

	ctx->dither_errors[1][x] += err*(m[4]);
	ctx->dither_errors[2][x] += err*(m[9]);

	if((x+fwd)>=0 && (x+fwd)<ctx->img2.width) {
		ctx->dither_errors[0][x+fwd] += err*(m[0]);
		ctx->dither_errors[1][x+fwd] += err*(m[5]);
		ctx->dither_errors[2][x+fwd] += err*(m[10]);
		if((x+2*fwd)>=0 && (x+2*fwd)<ctx->img2.width) {
			ctx->dither_errors[0][x+2*fwd] += err*(m[1]);
			ctx->dither_errors[1][x+2*fwd] += err*(m[6]);
			ctx->dither_errors[2][x+2*fwd] += err*(m[11]);
		}
	}
}

// 'channel' is the output channel.
static int get_nearest_valid_colors(struct iw_context *ctx, iw_tmpsample samp_lin,
		const struct iw_csdescr *csdescr,
		double *s_lin_floor_1, double *s_lin_ceil_1,
		double *s_cvt_floor_full, double *s_cvt_ceil_full,
		double overall_maxcolorcode, int color_count)
{
	iw_tmpsample samp_cvt;
	double samp_cvt_expanded;
	unsigned int floor_int, ceil_int;

	// A prelimary conversion to the target color space.
	samp_cvt = linear_to_x_sample(samp_lin,csdescr);

	if(color_count==0) {
		// The normal case: we want to use this channel's full available depth.
		samp_cvt_expanded = samp_cvt * overall_maxcolorcode;
		if(samp_cvt_expanded>overall_maxcolorcode) samp_cvt_expanded=overall_maxcolorcode;
		if(samp_cvt_expanded<0.0) samp_cvt_expanded=0.0;

		// Find the next-smallest and next-largest valid values that
		// can be stored in this image.
		// We will use one of them, but in order to figure out *which* one,
		// we have to compare their distances in the *linear* color space.
		*s_cvt_floor_full = floor(samp_cvt_expanded);
		*s_cvt_ceil_full  = ceil(samp_cvt_expanded);
	}
	else {
		// We're "posterizing": restricting to a certain number of color shades.
		double posterized_maxcolorcode;
		// Example: color_count = 4, bit_depth = 8;
		// Colors are from 0.0 to 3.0, mapped to 0.0 to 255.0.
		// Reduction factor is 255.0/3.0 = 85.0

		posterized_maxcolorcode = (double)(color_count-1);

		samp_cvt_expanded = samp_cvt * posterized_maxcolorcode;
		if(samp_cvt_expanded>posterized_maxcolorcode) samp_cvt_expanded=posterized_maxcolorcode;
		if(samp_cvt_expanded<0.0) samp_cvt_expanded=0.0;

		// If the number of shades is not 2, 4, 6, 16, 18, 52, 86, or 256 (assuming 8-bit depth),
		// then the shades will not be exactly evenly spaced. For example, if there are 3 shades,
		// they will be 0, 128, and 255. It will often be the case that the shade we want is exactly
		// halfway between the nearest two available shades, and the "0.5000000001" fudge factor is my
		// attempt to make sure it rounds consistently in the same direction.
		*s_cvt_floor_full = floor(0.5000000001 + floor(samp_cvt_expanded) * (overall_maxcolorcode/posterized_maxcolorcode));
		*s_cvt_ceil_full  = floor(0.5000000001 + ceil (samp_cvt_expanded) * (overall_maxcolorcode/posterized_maxcolorcode));
	}

	floor_int = (unsigned int)(*s_cvt_floor_full);
	ceil_int  = (unsigned int)(*s_cvt_ceil_full);
	if(floor_int == ceil_int) {
		return 1;
	}

	// Convert the candidates to our linear color space
	*s_lin_floor_1 = cvt_int_sample_to_linear_output(ctx,floor_int,csdescr,overall_maxcolorcode);
	*s_lin_ceil_1 =  cvt_int_sample_to_linear_output(ctx,ceil_int ,csdescr,overall_maxcolorcode);

	return 0;
}

// channel is the output channel
static void put_sample_convert_from_linear_flt(struct iw_context *ctx, iw_tmpsample samp_lin,
	   int x, int y, int channel, const struct iw_csdescr *csdescr)
{
	put_raw_sample_flt32(ctx,(double)samp_lin,x,y,channel);
}

static double get_final_sample_using_nc_tbl(struct iw_context *ctx, iw_tmpsample samp_lin)
{
	unsigned int x;
	unsigned int d;

	// For numbers 0 through 254, find the smallest one for which the
	// corresponding table value is larger than samp_lin.

	// Do a binary search.

	x = 127;
	d = 64;

	while(1) {
		if(x>254 || ctx->nearest_color_table[x] > samp_lin)
			x -= d;
		else
			x += d;

		if(d==1) {
			if(x>254 || ctx->nearest_color_table[x] > samp_lin)
				return (double)(x);
			else
				return (double)(x+1);
		}

		d = d/2;
	}
}

// channel is the output channel
static void put_sample_convert_from_linear(struct iw_context *ctx, iw_tmpsample samp_lin,
	   int x, int y, int channel, const struct iw_csdescr *csdescr)
{
	double s_lin_floor_1, s_lin_ceil_1;
	double s_cvt_floor_full, s_cvt_ceil_full;
	double d_floor, d_ceil;
	int is_exact;
	double s_full;
	int ditherfamily;
	int dd; // Dither decision: 0 to use floor, 1 to use ceil.

	// Clamp to the [0.0,1.0] range.
	// The sample type is UINT, so out-of-range samples can't be represented.
	// TODO: I think that out-of-range samples could still have a meaningful
	// effect if we are dithering. More investigation is needed here.
	if(samp_lin<0.0) samp_lin=0.0;
	if(samp_lin>1.0) samp_lin=1.0;

	// TODO: This is getting messy. The conditions under which we use lookup
	// tables are too complicated, and we still don't use them as often as we
	// should. For example, if we are not dithering, we can use a table optimized
	// for telling us the single nearest color. But if we are dithering, then we
	// instead need to know both the next-highest and next-lowest colors, which
	// would require a different table. The same table could be used for both,
	// but not quite as efficiently. Currently, we don't use use a lookup table
	// when dithering, except that we may still use one to do some of the
	// intermediate computations. Etc.
	if(ctx->img2_ci[channel].use_nearest_color_table) {
		s_full = get_final_sample_using_nc_tbl(ctx,samp_lin);
		goto okay;
	}

	ditherfamily=ctx->img2_ci[channel].ditherfamily;

	if(ditherfamily==IW_DITHERFAMILY_ERRDIFF) {
		samp_lin += ctx->dither_errors[0][x];
		// If the prior error makes the ideal brightness out of the available range,
		// just throw away any extra.
		if(samp_lin>1.0) samp_lin=1.0;
		else if(samp_lin<0.0) samp_lin=0.0;
	}

	is_exact = get_nearest_valid_colors(ctx,samp_lin,csdescr,
		&s_lin_floor_1, &s_lin_ceil_1,
		&s_cvt_floor_full, &s_cvt_ceil_full,
		ctx->img2_ci[channel].maxcolorcode_dbl, ctx->img2_ci[channel].color_count);

	if(is_exact) {
		s_full = s_cvt_floor_full;

		// Hack to keep the PRNG in sync. We have to generate exactly one random
		// number per sample, regardless of whether we use it.
		if(ditherfamily==IW_DITHERFAMILY_RANDOM) {
			(void)iwpvt_prng_rand(ctx->prng);
		}
		goto okay;
	}

	// samp_lin should be between s_lin_floor_1 and s_lin_ceil_1. Figure out
	// which is closer, and use the final pixel value we figured out earlier
	// (either s_cvt_floor_full or s_cvt_ceil_full).
	d_floor = samp_lin-s_lin_floor_1;
	d_ceil  = s_lin_ceil_1-samp_lin;

	if(ditherfamily==IW_DITHERFAMILY_NONE) {
		// Not dithering. Just choose closest value.
		if(d_ceil<=d_floor) s_full=s_cvt_ceil_full;
		else s_full=s_cvt_floor_full;
	}
	else if(ditherfamily==IW_DITHERFAMILY_ERRDIFF) {
		if(d_ceil<=d_floor) {
			// Ceiling is closer. This pixel will be lighter than ideal.
			// so the error is negative, to make other pixels darker.
			iw_errdiff_dither(ctx,ctx->img2_ci[channel].dithersubtype,-d_ceil,x,y);
			s_full=s_cvt_ceil_full;
		}
		else {
			iw_errdiff_dither(ctx,ctx->img2_ci[channel].dithersubtype,d_floor,x,y);
			s_full=s_cvt_floor_full;
		}
	}
	else if(ditherfamily==IW_DITHERFAMILY_ORDERED) {
		dd=iw_ordered_dither(ctx->img2_ci[channel].dithersubtype, d_floor/(d_floor+d_ceil),x,y);
		s_full = dd ? s_cvt_ceil_full : s_cvt_floor_full;
	}
	else if(ditherfamily==IW_DITHERFAMILY_RANDOM) {
		dd=iw_random_dither(ctx,d_floor/(d_floor+d_ceil),x,y,ctx->img2_ci[channel].dithersubtype,channel);
		s_full = dd ? s_cvt_ceil_full : s_cvt_floor_full;
	}
	else {
		// Unsupported dither method.
		s_full = 0.0;
	}

okay:
	put_raw_sample(ctx,s_full,x,y,channel);
}

// A stripped-down version of put_sample_convert_from_linear(),
// intended for use with background colors.
static unsigned int calc_sample_convert_from_linear(struct iw_context *ctx, iw_tmpsample samp_lin,
	   const struct iw_csdescr *csdescr, double overall_maxcolorcode)
{
	double s_lin_floor_1, s_lin_ceil_1;
	double s_cvt_floor_full, s_cvt_ceil_full;
	double d_floor, d_ceil;
	int is_exact;
	double s_full;

	if(samp_lin<0.0) samp_lin=0.0;
	if(samp_lin>1.0) samp_lin=1.0;

	is_exact = get_nearest_valid_colors(ctx,samp_lin,csdescr,
		&s_lin_floor_1, &s_lin_ceil_1,
		&s_cvt_floor_full, &s_cvt_ceil_full,
		overall_maxcolorcode, 0);

	if(is_exact) {
		s_full = s_cvt_floor_full;
		goto okay;
	}

	d_floor = samp_lin-s_lin_floor_1;
	d_ceil  = s_lin_ceil_1-samp_lin;

	if(d_ceil<=d_floor) s_full=s_cvt_ceil_full;
	else s_full=s_cvt_floor_full;

okay:
	return (unsigned int)(0.5+s_full);
}

static void clamp_output_samples(struct iw_context *ctx, iw_tmpsample *out_pix, int num_out_pix)
{
	int i;

	for(i=0;i<num_out_pix;i++) {
		if(out_pix[i]<0.0) out_pix[i]=0.0;
		else if(out_pix[i]>1.0) out_pix[i]=1.0;
	}
}

// TODO: Maybe this should be a flag in ctx, instead of a function that is
// called repeatedly.
static int iw_bkgd_has_transparency(struct iw_context *ctx)
{
	if(!ctx->apply_bkgd) return 0;
	if(!(ctx->output_profile&IW_PROFILE_TRANSPARENCY)) return 0;
	if(ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY) return 0;
	if(ctx->bkgd_color_source==IW_BKGD_COLOR_SOURCE_FILE) {
		if(ctx->img1_bkgd_label_inputcs.c[3]<1.0) return 1;
	}
	else if(ctx->bkgd_color_source==IW_BKGD_COLOR_SOURCE_REQ) {
		if(ctx->bkgd_checkerboard) {
			if(ctx->req.bkgd2.c[3]<1.0) return 1;
		}
		if(ctx->req.bkgd.c[3]<1.0) return 1;
	}
	return 0;
}

// 'channel' is an intermediate channel number.
static int iw_process_cols_to_intermediate(struct iw_context *ctx, int channel,
	const struct iw_csdescr *in_csdescr)
{
	int i,j;
	int retval=0;
	iw_tmpsample tmp_alpha;
	iw_tmpsample *inpix_tofree = NULL;
	iw_tmpsample *outpix_tofree = NULL;
	int is_alpha_channel;
	struct iw_resize_settings *rs = NULL;
	struct iw_channelinfo_intermed *int_ci;

	iw_tmpsample *in_pix;
	iw_tmpsample *out_pix;
	int num_in_pix;
	int num_out_pix;

	int_ci = &ctx->intermed_ci[channel];
	is_alpha_channel = (int_ci->channeltype==IW_CHANNELTYPE_ALPHA);

	num_in_pix = ctx->input_h;
	inpix_tofree = (iw_tmpsample*)iw_malloc(ctx, num_in_pix * sizeof(iw_tmpsample));
	if(!inpix_tofree) goto done;
	in_pix = inpix_tofree;

	num_out_pix = ctx->intermed_canvas_height;
	outpix_tofree = (iw_tmpsample*)iw_malloc(ctx, num_out_pix * sizeof(iw_tmpsample));
	if(!outpix_tofree) goto done;
	out_pix = outpix_tofree;

	rs=&ctx->resize_settings[IW_DIMENSION_V];

	// If the resize context for this dimension already exists, we should be
	// able to reuse it. Otherwise, create a new one.
	if(!rs->rrctx) {
		// TODO: The use of the word "rows" here is misleading, because we are
		// actually resizing columns.
		rs->rrctx = iwpvt_resize_rows_init(ctx,rs,int_ci->channeltype,
			num_in_pix, num_out_pix);
		if(!rs->rrctx) goto done;
	}

	for(i=0;i<ctx->input_w;i++) {

		// Read a column of pixels into ctx->in_pix
		for(j=0;j<ctx->input_h;j++) {

			in_pix[j] = get_sample_cvt_to_linear(ctx,i,j,channel,in_csdescr);

			if(int_ci->need_unassoc_alpha_processing) { // We need opacity information also
				tmp_alpha = get_raw_sample(ctx,i,j,ctx->img1_alpha_channel_index);

				// Multiply color amount by opacity
				in_pix[j] *= tmp_alpha;
			}
			else if(ctx->apply_bkgd && ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY) {
				// We're doing "Early" background color application.
				// All intermediate channels will need the background color
				// applied to them.
				tmp_alpha = get_raw_sample(ctx,i,j,ctx->img1_alpha_channel_index);
				in_pix[j] = (tmp_alpha)*(in_pix[j]) +
					(1.0-tmp_alpha)*(int_ci->bkgd_color_lin);
			}
		}

		// Now we have a row in the right format.
		// Resize it and store it in the right place in the intermediate array.

		iwpvt_resize_row_main(rs->rrctx,in_pix,out_pix);

		if(ctx->intclamp)
			clamp_output_samples(ctx,out_pix,num_out_pix);

		// The intermediate pixels are in ctx->out_pix. Copy them to the intermediate array.
		for(j=0;j<ctx->intermed_canvas_height;j++) {
			if(is_alpha_channel) {
				ctx->intermediate_alpha32[((size_t)j)*ctx->intermed_canvas_width + i] = (iw_float32)out_pix[j];
			}
			else {
				ctx->intermediate32[((size_t)j)*ctx->intermed_canvas_width + i] = (iw_float32)out_pix[j];
			}
		}
	}

	retval=1;

done:
	if(rs && rs->disable_rrctx_cache && rs->rrctx) {
		// In some cases, the channels may need different resize contexts.
		// Delete the current context, so that it doesn't get reused.
		iwpvt_resize_rows_done(rs->rrctx);
		rs->rrctx = NULL;
	}
	if(inpix_tofree) iw_free(ctx,inpix_tofree);
	if(outpix_tofree) iw_free(ctx,outpix_tofree);
	return retval;
}

// 'handle_alpha_flag' must be set if an alpha channel exists and this is not
// the alpha channel.
static int iw_process_rows_intermediate_to_final(struct iw_context *ctx, int intermed_channel,
	const struct iw_csdescr *out_csdescr)
{
	int i,j;
	int z;
	int k;
	int retval=0;
	iw_tmpsample tmpsamp;
	iw_tmpsample alphasamp = 0.0;
	iw_tmpsample *inpix_tofree = NULL; // Used if we need a separate temp buffer for input samples
	iw_tmpsample *outpix_tofree = NULL; // Used if we need a separate temp buffer for output samples
	// Do any of the output channels use error-diffusion dithering?
	int using_errdiffdither = 0;
	int output_channel;
	int is_alpha_channel;
	int bkgd_has_transparency;
	double tmpbkgdalpha=0.0;
	int alt_bkgd = 0; // Nonzero if we should use bkgd2 for this sample
	struct iw_resize_settings *rs = NULL;
	int ditherfamily, dithersubtype;
	struct iw_channelinfo_intermed *int_ci;
	struct iw_channelinfo_out *out_ci;

	iw_tmpsample *in_pix = NULL;
	iw_tmpsample *out_pix = NULL;
	int num_in_pix;
	int num_out_pix;

	num_in_pix = ctx->intermed_canvas_width;
	num_out_pix = ctx->img2.width;

	int_ci = &ctx->intermed_ci[intermed_channel];
	output_channel = int_ci->corresponding_output_channel;
	out_ci = &ctx->img2_ci[output_channel];
	is_alpha_channel = (int_ci->channeltype==IW_CHANNELTYPE_ALPHA);
	bkgd_has_transparency = iw_bkgd_has_transparency(ctx);

	inpix_tofree = (iw_tmpsample*)iw_malloc(ctx, num_in_pix * sizeof(iw_tmpsample));
	in_pix = inpix_tofree;

	// We need an output buffer.
	outpix_tofree = (iw_tmpsample*)iw_malloc(ctx, num_out_pix * sizeof(iw_tmpsample));
	if(!outpix_tofree) goto done;
	out_pix = outpix_tofree;

	// Decide if the 'nearest color table' optimization can be used
	if(ctx->nearest_color_table && !is_alpha_channel &&
	   out_ci->ditherfamily==IW_DITHERFAMILY_NONE &&
	   out_ci->color_count==0)
	{
		out_ci->use_nearest_color_table = 1;
	}
	else {
		out_ci->use_nearest_color_table = 0;
	}

	// Seed the PRNG, if necessary.
	ditherfamily = out_ci->ditherfamily;
	dithersubtype = out_ci->dithersubtype;
	if(ditherfamily==IW_DITHERFAMILY_RANDOM) {
		// Decide what random seed to use. The alpha channel always has its own
		// seed. If using "r" (not "r2") dithering, every channel has its own seed.
		if(dithersubtype==IW_DITHERSUBTYPE_SAMEPATTERN && out_ci->channeltype!=IW_CHANNELTYPE_ALPHA)
		{
			iwpvt_prng_set_random_seed(ctx->prng,ctx->random_seed);
		}
		else {
			iwpvt_prng_set_random_seed(ctx->prng,ctx->random_seed+out_ci->channeltype);
		}
	}

	// Initialize Floyd-Steinberg dithering.
	if(output_channel>=0 && out_ci->ditherfamily==IW_DITHERFAMILY_ERRDIFF) {
		using_errdiffdither = 1;
		for(i=0;i<ctx->img2.width;i++) {
			for(k=0;k<IW_DITHER_MAXROWS;k++) {
				ctx->dither_errors[k][i] = 0.0;
			}
		}
	}

	rs=&ctx->resize_settings[IW_DIMENSION_H];

	// If the resize context for this dimension already exists, we should be
	// able to reuse it. Otherwise, create a new one.
	if(!rs->rrctx) {
		rs->rrctx = iwpvt_resize_rows_init(ctx,rs,int_ci->channeltype,
			num_in_pix, num_out_pix);
		if(!rs->rrctx) goto done;
	}

	for(j=0;j<ctx->intermed_canvas_height;j++) {

		// As needed, either copy the input pixels to a temp buffer (inpix, which
		// ctx->in_pix already points to), or point ctx->in_pix directly to the
		// intermediate data.
		if(is_alpha_channel) {
			for(i=0;i<num_in_pix;i++) {
				inpix_tofree[i] = ctx->intermediate_alpha32[((size_t)j)*ctx->intermed_canvas_width+i];
			}
		}
		else {
			for(i=0;i<num_in_pix;i++) {
				inpix_tofree[i] = ctx->intermediate32[((size_t)j)*ctx->intermed_canvas_width+i];
			}
		}

		// Resize ctx->in_pix to ctx->out_pix.
		iwpvt_resize_row_main(rs->rrctx,in_pix,out_pix);

		if(ctx->intclamp)
			clamp_output_samples(ctx,out_pix,num_out_pix);

		// If necessary, copy the resized samples to the final_alpha image
		if(is_alpha_channel && outpix_tofree && ctx->final_alpha32) {
			for(i=0;i<num_out_pix;i++) {
				ctx->final_alpha32[((size_t)j)*ctx->img2.width+i] = (iw_float32)outpix_tofree[i];
			}
		}

		// Now convert the out_pix and put them in the final image.

		if(output_channel == -1) {
			// No corresponding output channel.
			// (Presumably because this is an alpha channel that's being
			// removed because we're applying a background.)
			goto here;
		}

		for(z=0;z<ctx->img2.width;z++) {
			// For decent Floyd-Steinberg dithering, we need to process alternate
			// rows in reverse order.
			if(using_errdiffdither && (j%2))
				i=ctx->img2.width-1-z;
			else
				i=z;

			tmpsamp = out_pix[i];

			if(ctx->bkgd_checkerboard) {
				alt_bkgd = (((ctx->bkgd_check_origin[IW_DIMENSION_H]+i)/ctx->bkgd_check_size)%2) !=
					(((ctx->bkgd_check_origin[IW_DIMENSION_V]+j)/ctx->bkgd_check_size)%2);
			}

			if(bkgd_has_transparency) {
				tmpbkgdalpha = alt_bkgd ? ctx->bkgd2alpha : ctx->bkgd1alpha;
			}

			if(int_ci->need_unassoc_alpha_processing) {
				// Convert color samples back to unassociated alpha.
				alphasamp = ctx->final_alpha32[((size_t)j)*ctx->img2.width + i];

				if(alphasamp!=0.0) {
					tmpsamp /= alphasamp;
				}

				if(ctx->apply_bkgd && ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_LATE) {
					// Apply a background color (or checkerboard pattern).
					double bkcolor;
					bkcolor = alt_bkgd ? out_ci->bkgd2_color_lin : out_ci->bkgd1_color_lin;

					if(bkgd_has_transparency) {
						tmpsamp = tmpsamp*alphasamp + bkcolor*tmpbkgdalpha*(1.0-alphasamp);
					}
					else {
						tmpsamp = tmpsamp*alphasamp + bkcolor*(1.0-alphasamp);
					}
				}
			}
			else if(is_alpha_channel && bkgd_has_transparency) {
				// Composite the alpha of the foreground over the alpha of the background.
				tmpsamp = tmpsamp + tmpbkgdalpha*(1.0-tmpsamp);
			}

			if(ctx->img2.sampletype==IW_SAMPLETYPE_FLOATINGPOINT)
				put_sample_convert_from_linear_flt(ctx,tmpsamp,i,j,output_channel,out_csdescr);
			else
				put_sample_convert_from_linear(ctx,tmpsamp,i,j,output_channel,out_csdescr);

		}

		if(using_errdiffdither) {
			// Move "next row" error data to "this row", and clear the "next row".
			// TODO: Obviously, it would be more efficient to just swap pointers
			// to the rows.
			for(i=0;i<ctx->img2.width;i++) {
				// Move data in all rows but the first row up one row.
				for(k=0;k<IW_DITHER_MAXROWS-1;k++) {
					ctx->dither_errors[k][i] = ctx->dither_errors[k+1][i];
				}
				// Clear the last row.
				ctx->dither_errors[IW_DITHER_MAXROWS-1][i] = 0.0;
			}
		}

here:
		;
	}

	retval=1;

done:
	if(rs && rs->disable_rrctx_cache && rs->rrctx) {
		// In some cases, the channels may need different resize contexts.
		// Delete the current context, so that it doesn't get reused.
		iwpvt_resize_rows_done(rs->rrctx);
		rs->rrctx = NULL;
	}
	if(inpix_tofree) iw_free(ctx,inpix_tofree);
	if(outpix_tofree) iw_free(ctx,outpix_tofree);

	return retval;
}

static int iw_process_one_channel(struct iw_context *ctx, int intermed_channel,
  const struct iw_csdescr *in_csdescr, const struct iw_csdescr *out_csdescr)
{
	if(!iw_process_cols_to_intermediate(ctx,intermed_channel,in_csdescr)) {
		return 0;
	}

	if(!iw_process_rows_intermediate_to_final(ctx,intermed_channel,out_csdescr)) {
		return 0;
	}

	return 1;
}

// Potentially make a lookup table for color correction.
static void iw_make_x_to_linear_table(struct iw_context *ctx, double **ptable,
	const struct iw_image *img, const struct iw_csdescr *csdescr)
{
	int ncolors;
	int i;
	double *tbl;

	if(csdescr->cstype==IW_CSTYPE_LINEAR) return;

	ncolors = (1 << img->bit_depth);
	if(ncolors>256) return;

	// Don't make a table if the image is really small.
	if( ((size_t)img->width)*img->height <= 512 ) return;

	tbl = iw_malloc(ctx,ncolors*sizeof(double));
	if(!tbl) return;

	for(i=0;i<ncolors;i++) {
		tbl[i] = x_to_linear_sample(((double)i)/(ncolors-1), csdescr);
	}

	*ptable = tbl;
}

static void iw_make_nearest_color_table(struct iw_context *ctx, double **ptable,
	const struct iw_image *img, const struct iw_csdescr *csdescr)
{
	int ncolors;
	int nentries;
	int i;
	double *tbl;
	double prev;
	double curr;

	if(ctx->no_gamma) return;
	if(csdescr->cstype==IW_CSTYPE_LINEAR) return;
	if(img->sampletype==IW_SAMPLETYPE_FLOATINGPOINT) return;
	if(img->bit_depth != ctx->img2.bit_depth) return;

	ncolors = (1 << img->bit_depth);
	if(ncolors>256) return;
	nentries = ncolors-1;

	// Don't make a table if the image is really small.
	if( ((size_t)img->width)*img->height <= 512 ) return;

	tbl = iw_malloc(ctx,nentries*sizeof(double));
	if(!tbl) return;

	// Table stores the maximum value for the given entry.
	// The final entry is omitted, since there is no maximum value.
	prev = 0.0;
	for(i=0;i<nentries;i++) {
		// This conversion may appear to be going in the wrong direction
		// (we're coverting *from* linear), but it's correct because we will
		// search through its contents to find the corresponding index,
		// instead of vice versa.
		curr = x_to_linear_sample( ((double)(i+1))/(ncolors-1), csdescr);
		tbl[i] = (prev + curr)/2.0;
		prev = curr;
	}

	*ptable = tbl;
}

// Label is returned in linear colorspace.
// Returns 0 if no label available.
static int get_output_bkgd_label_lin(struct iw_context *ctx, struct iw_color *clr)
{
	clr->c[0] = 1.0; clr->c[1] = 0.0; clr->c[2] = 1.0; clr->c[3] = 1.0;

	if(ctx->req.suppress_output_bkgd_label) return 0;

	if(ctx->req.output_bkgd_label_valid) {
		*clr = ctx->req.output_bkgd_label;
		return 1;
	}

	// If the user didn't specify a label, but the input file had one, copy the
	// input file's label.
	if(ctx->img1_bkgd_label_set) {
		*clr = ctx->img1_bkgd_label_lin;
		return 1;
	}

	return 0;
}

static unsigned int iw_scale_to_int(double s, unsigned int maxcolor)
{
	if(s<=0.0) return 0;
	if(s>=1.0) return maxcolor;
	return (unsigned int)(0.5+s*maxcolor);
}

// Quantize the background color label, and store in ctx->img2.bkgdlabel.
// Also convert it to grayscale if needed.
static void iw_process_bkgd_label(struct iw_context *ctx)
{
	int ret;
	int k;
	struct iw_color clr;
	double maxcolor;
	unsigned int tmpu;

	if(!(ctx->output_profile&IW_PROFILE_PNG_BKGD) &&
		!(ctx->output_profile&IW_PROFILE_RGB8_BKGD) &&
		!(ctx->output_profile&IW_PROFILE_RGB16_BKGD))
	{
		return;
	}

	ret = get_output_bkgd_label_lin(ctx,&clr);
	if(!ret) return;

	if(ctx->to_grayscale) {
		iw_tmpsample g;
		g = iw_color_to_grayscale(ctx, clr.c[0], clr.c[1], clr.c[2]);
		clr.c[0] = clr.c[1] = clr.c[2] = g;
	}

	if(ctx->output_profile&IW_PROFILE_RGB8_BKGD) {
		maxcolor=255.0;
	}
	else if(ctx->output_profile&IW_PROFILE_RGB16_BKGD) {
		maxcolor=65535.0;
	}
	else if(ctx->img2.bit_depth==8) {
		maxcolor=255.0;
	}
	else if(ctx->img2.bit_depth==16) {
		maxcolor=65535.0;
	}
	else {
		return;
	}

	// Although the bkgd label is stored as floating point, we're responsible for
	// making sure that, when scaled and rounded to a format suitable for the output
	// format, it will be the correct color.
	for(k=0;k<3;k++) {
		tmpu = calc_sample_convert_from_linear(ctx, clr.c[k], &ctx->img2cs, maxcolor);
		ctx->img2.bkgdlabel.c[k] = ((double)tmpu)/maxcolor;
	}
	// Alpha sample
	tmpu = iw_scale_to_int(clr.c[3],(unsigned int)maxcolor);
	ctx->img2.bkgdlabel.c[3] = ((double)tmpu)/maxcolor;

	ctx->img2.has_bkgdlabel = 1;
}

static void negate_target_image(struct iw_context *ctx)
{
	int channel;
	struct iw_channelinfo_out *ci;
	int i,j;
	size_t pos;
	iw_float32 s;
	unsigned int n;

	for(channel=0; channel<ctx->img2_numchannels; channel++) {
		ci = &ctx->img2_ci[channel];
		if(ci->channeltype == IW_CHANNELTYPE_ALPHA) continue; // Don't negate alpha channels

		if(ctx->img2.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
			for(j=0; j<ctx->img2.height; j++) {
				for(i=0; i<ctx->img2.width; i++) {
					pos = j*ctx->img2.bpr + ctx->img2_numchannels*i*4 + channel*4;
					s = iw_get_float32(&ctx->img2.pixels[pos]);
					iw_put_float32(&ctx->img2.pixels[pos], ((iw_float32)1.0)-s);
				}
			}
		}
		else if(ctx->img2.bit_depth==8) {
			for(j=0; j<ctx->img2.height; j++) {
				for(i=0; i<ctx->img2.width; i++) {
					pos = j*ctx->img2.bpr + ctx->img2_numchannels*i + channel;
					ctx->img2.pixels[pos] = ci->maxcolorcode_int-ctx->img2.pixels[pos];
				}
			}
		}
		else if(ctx->img2.bit_depth==16) {
			for(j=0; j<ctx->img2.height; j++) {
				for(i=0; i<ctx->img2.width; i++) {
					pos = j*ctx->img2.bpr + ctx->img2_numchannels*i*2 + channel*2;
					n = ctx->img2.pixels[pos]*256 + ctx->img2.pixels[pos+1];
					n = ci->maxcolorcode_int - n;
					ctx->img2.pixels[pos] = (n&0xff00)>>8;
					ctx->img2.pixels[pos+1] = n&0x00ff;
				}
			}
		}
	}
}

static int iw_process_internal(struct iw_context *ctx)
{
	int channel;
	int retval=0;
	int i,k;
	int ret;
	// A linear color-correction descriptor to use with alpha channels.
	struct iw_csdescr csdescr_linear;

	ctx->intermediate32=NULL;
	ctx->intermediate_alpha32=NULL;
	ctx->final_alpha32=NULL;
	ctx->intermed_canvas_width = ctx->input_w;
	ctx->intermed_canvas_height = ctx->img2.height;

	iw_make_linear_csdescr(&csdescr_linear);

	ctx->img2.bpr = iw_calc_bytesperrow(ctx->img2.width,ctx->img2.bit_depth*ctx->img2_numchannels);

	ctx->img2.pixels = iw_malloc_large(ctx, ctx->img2.bpr, ctx->img2.height);
	if(!ctx->img2.pixels) {
		goto done;
	}

	ctx->intermediate32 = (iw_float32*)iw_malloc_large(ctx, ctx->intermed_canvas_width * ctx->intermed_canvas_height, sizeof(iw_float32));
	if(!ctx->intermediate32) {
		goto done;
	}

	if(ctx->uses_errdiffdither) {
		for(k=0;k<IW_DITHER_MAXROWS;k++) {
			ctx->dither_errors[k] = (double*)iw_malloc(ctx, ctx->img2.width * sizeof(double));
			if(!ctx->dither_errors[k]) goto done;
		}
	}

	if(!ctx->disable_output_lookup_tables) {
		iw_make_x_to_linear_table(ctx,&ctx->output_rev_color_corr_table,&ctx->img2,&ctx->img2cs);

		iw_make_nearest_color_table(ctx,&ctx->nearest_color_table,&ctx->img2,&ctx->img2cs);
	}

	// If an alpha channel is present, we have to process it first.
	if(IW_IMGTYPE_HAS_ALPHA(ctx->intermed_imgtype)) {
		ctx->intermediate_alpha32 = (iw_float32*)iw_malloc_large(ctx, ctx->intermed_canvas_width * ctx->intermed_canvas_height, sizeof(iw_float32));
		if(!ctx->intermediate_alpha32) {
			goto done;
		}
		ctx->final_alpha32 = (iw_float32*)iw_malloc_large(ctx, ctx->img2.width * ctx->img2.height, sizeof(iw_float32));
		if(!ctx->final_alpha32) {
			goto done;
		}

		if(!iw_process_one_channel(ctx,ctx->intermed_alpha_channel_index,&csdescr_linear,&csdescr_linear)) goto done;
	}

	// Process the non-alpha channels.

	for(channel=0;channel<ctx->intermed_numchannels;channel++) {
		if(ctx->intermed_ci[channel].channeltype!=IW_CHANNELTYPE_ALPHA) {
			if(ctx->no_gamma)
				ret=iw_process_one_channel(ctx,channel,&csdescr_linear,&csdescr_linear);
			else
				ret=iw_process_one_channel(ctx,channel,&ctx->img1cs,&ctx->img2cs);

			if(!ret) goto done;
		}
	}

	iw_process_bkgd_label(ctx);

	if(ctx->req.negate_target) {
		negate_target_image(ctx);
	}

	retval=1;

done:
	if(ctx->intermediate32) { iw_free(ctx,ctx->intermediate32); ctx->intermediate32=NULL; }
	if(ctx->intermediate_alpha32) { iw_free(ctx,ctx->intermediate_alpha32); ctx->intermediate_alpha32=NULL; }
	if(ctx->final_alpha32) { iw_free(ctx,ctx->final_alpha32); ctx->final_alpha32=NULL; }
	for(k=0;k<IW_DITHER_MAXROWS;k++) {
		if(ctx->dither_errors[k]) { iw_free(ctx,ctx->dither_errors[k]); ctx->dither_errors[k]=NULL; }
	}
	// The 'resize contexts' are usually kept around so that they can be reused.
	// Now that we're done with everything, free them.
	for(i=0;i<2;i++) { // horizontal, vertical
		if(ctx->resize_settings[i].rrctx) {
			iwpvt_resize_rows_done(ctx->resize_settings[i].rrctx);
			ctx->resize_settings[i].rrctx = NULL;
		}
	}
	return retval;
}

static int iw_get_channeltype(int imgtype, int channel)
{
	switch(imgtype) {
	case IW_IMGTYPE_GRAY:
		if(channel==0) return IW_CHANNELTYPE_GRAY;
		break;
	case IW_IMGTYPE_GRAYA:
		if(channel==0) return IW_CHANNELTYPE_GRAY;
		if(channel==1) return IW_CHANNELTYPE_ALPHA;
		break;
	case IW_IMGTYPE_RGB:
		if(channel==0) return IW_CHANNELTYPE_RED;
		if(channel==1) return IW_CHANNELTYPE_GREEN;
		if(channel==2) return IW_CHANNELTYPE_BLUE;
		break;
	case IW_IMGTYPE_RGBA:
		if(channel==0) return IW_CHANNELTYPE_RED;
		if(channel==1) return IW_CHANNELTYPE_GREEN;
		if(channel==2) return IW_CHANNELTYPE_BLUE;
		if(channel==3) return IW_CHANNELTYPE_ALPHA;
		break;
	}
	return 0;
}

static void iw_set_input_channeltypes(struct iw_context *ctx)
{
	int i;
	for(i=0;i<ctx->img1_numchannels_logical;i++) {
		ctx->img1_ci[i].channeltype = iw_get_channeltype(ctx->img1_imgtype_logical,i);
	}
}

static void iw_set_intermed_channeltypes(struct iw_context *ctx)
{
	int i;
	for(i=0;i<ctx->intermed_numchannels;i++) {
		ctx->intermed_ci[i].channeltype = iw_get_channeltype(ctx->intermed_imgtype,i);
	}
}

static void iw_set_out_channeltypes(struct iw_context *ctx)
{
	int i;
	for(i=0;i<ctx->img2_numchannels;i++) {
		ctx->img2_ci[i].channeltype = iw_get_channeltype(ctx->img2.imgtype,i);
	}
}

// Set img2.bit_depth based on output_depth_req, etc.
// Set img2.sampletype.
static void decide_output_bit_depth(struct iw_context *ctx)
{
	if(ctx->output_profile&IW_PROFILE_HDRI) {
		ctx->img2.sampletype=IW_SAMPLETYPE_FLOATINGPOINT;
	}
	else {
		ctx->img2.sampletype=IW_SAMPLETYPE_UINT;
	}

	if(ctx->img2.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
		// Floating point output.
		ctx->img2.bit_depth=32;
		return;
	}

	// Below this point, sample type is UINT.

	if(ctx->req.output_depth>8 && (ctx->output_profile&IW_PROFILE_16BPS)) {
		ctx->img2.bit_depth=16;
	}
	else {
		if(ctx->req.output_depth>8) {
			// Caller requested a depth higher than this format can handle.
			iw_warning(ctx,"Reducing depth to 8; required by the output format.");
		}
		ctx->img2.bit_depth=8;
	}
}

// Set the background color samples that will be used when processing the
// image. (All the logic about how to apply a background color is in
// decide_how_to_apply_bkgd(), not here.)
static void prepare_apply_bkgd(struct iw_context *ctx)
{
	struct iw_color bkgd1; // Main background color in linear colorspace
	struct iw_color bkgd2; // Secondary background color ...
	int i;

	if(!ctx->apply_bkgd) return;

	// Start with a default background color.
	bkgd1.c[0]=1.0; bkgd1.c[1]=0.0; bkgd1.c[2]=1.0; bkgd1.c[3]=1.0;
	bkgd2.c[0]=0.0; bkgd2.c[1]=0.0; bkgd2.c[2]=0.0; bkgd2.c[3]=1.0;

	// Possibly overwrite it with the background color from the appropriate
	// source.
	if(ctx->bkgd_color_source == IW_BKGD_COLOR_SOURCE_FILE) {
		bkgd1 = ctx->img1_bkgd_label_lin; // sructure copy
		ctx->bkgd_checkerboard = 0;
	}
	else if(ctx->bkgd_color_source == IW_BKGD_COLOR_SOURCE_REQ) {
		bkgd1 = ctx->req.bkgd;
		if(ctx->req.bkgd_checkerboard) {
			bkgd2 = ctx->req.bkgd2;
		}
	}

	// Set up the channelinfo (and ctx->bkgd*alpha) as needed according to the
	// target image type, and whether we are applying the background before or
	// after resizing.

	if(ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY) {
		ctx->bkgd1alpha = 1.0;
	}
	else {
		ctx->bkgd1alpha = bkgd1.c[3];
		ctx->bkgd2alpha = bkgd2.c[3];
	}

	if(ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_LATE && (ctx->img2.imgtype==IW_IMGTYPE_RGB ||
		ctx->img2.imgtype==IW_IMGTYPE_RGBA))
	{
		for(i=0;i<3;i++) {
			ctx->img2_ci[i].bkgd1_color_lin = bkgd1.c[i];
		}
		if(ctx->bkgd_checkerboard) {
			for(i=0;i<3;i++) {
				ctx->img2_ci[i].bkgd2_color_lin = bkgd2.c[i];
			}
		}
	}
	else if(ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_LATE && (ctx->img2.imgtype==IW_IMGTYPE_GRAY ||
		ctx->img2.imgtype==IW_IMGTYPE_GRAYA))
	{
		ctx->img2_ci[0].bkgd1_color_lin = iw_color_to_grayscale(ctx,bkgd1.c[0],bkgd1.c[1],bkgd1.c[2]);
		if(ctx->bkgd_checkerboard) {
			ctx->img2_ci[0].bkgd2_color_lin = iw_color_to_grayscale(ctx,bkgd2.c[0],bkgd2.c[1],bkgd2.c[2]);
		}
	}
	else if(ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY && ctx->img2.imgtype==IW_IMGTYPE_RGB) {
		for(i=0;i<3;i++) {
			ctx->intermed_ci[i].bkgd_color_lin = bkgd1.c[i];
		}
	}
	else if(ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY && ctx->img2.imgtype==IW_IMGTYPE_GRAY) {
		ctx->intermed_ci[0].bkgd_color_lin = iw_color_to_grayscale(ctx,bkgd1.c[0],bkgd1.c[1],bkgd1.c[2]);
	}
}

#define IW_STRAT1_G_G       0x011 // -grayscale
#define IW_STRAT1_G_RGB     0x013 // default
#define IW_STRAT1_GA_G      0x021 // -grayscale, BKGD_STRATEGY_EARLY (never happens?)
#define IW_STRAT1_GA_GA     0x022 // -grayscale
#define IW_STRAT1_GA_RGB    0x023 // BKGD_STRATEGY_EARLY
#define IW_STRAT1_GA_RGBA   0x024 // default
#define IW_STRAT1_RGB_G     0x031 // -grayscale
#define IW_STRAT1_RGB_RGB   0x033 // default
#define IW_STRAT1_RGBA_G    0x041 // -grayscale, BKGD_STRATEGY_EARLY (never happens?)
#define IW_STRAT1_RGBA_GA   0x042 // -grayscale
#define IW_STRAT1_RGBA_RGB  0x043 // BKGD_STRATEGY_EARLY
#define IW_STRAT1_RGBA_RGBA 0x044 // default

#define IW_STRAT2_G_G       0x111 // -grayscale
#define IW_STRAT2_GA_G      0x121 // -grayscale, BKGD_STRATEGY_LATE
#define IW_STRAT2_GA_GA     0x122 // -grayscale
#define IW_STRAT2_RGB_RGB   0x133 // default
#define IW_STRAT2_RGBA_RGB  0x143 // BKGD_STRATEGY_LATE
#define IW_STRAT2_RGBA_RGBA 0x144 // default


static void iw_restrict_to_range(int r1, int r2, int *pvar)
{
	if(*pvar < r1) *pvar = r1;
	else if(*pvar > r2) *pvar = r2;
}

static void decide_strategy(struct iw_context *ctx, int *ps1, int *ps2)
{
	int s1, s2;

	// Start with a default strategy
	switch(ctx->img1_imgtype_logical) {
	case IW_IMGTYPE_RGBA:
		if(ctx->to_grayscale) {
			s1=IW_STRAT1_RGBA_GA;
			s2=IW_STRAT2_GA_GA;
		}
		else {
			s1=IW_STRAT1_RGBA_RGBA;
			s2=IW_STRAT2_RGBA_RGBA;
		}
		break;
	case IW_IMGTYPE_RGB:
		if(ctx->to_grayscale) {
			s1=IW_STRAT1_RGB_G;
			s2=IW_STRAT2_G_G;
		}
		else {
			s1=IW_STRAT1_RGB_RGB;
			s2=IW_STRAT2_RGB_RGB;
		}
		break;
	case IW_IMGTYPE_GRAYA:
		if(ctx->to_grayscale) {
			s1=IW_STRAT1_GA_GA;
			s2=IW_STRAT2_GA_GA;
		}
		else {
			s1=IW_STRAT1_GA_RGBA;
			s2=IW_STRAT2_RGBA_RGBA;
		}
		break;
	default:
		if(ctx->to_grayscale) {
			s1=IW_STRAT1_G_G;
			s2=IW_STRAT2_G_G;
		}
		else {
			s1=IW_STRAT1_G_RGB;
			s2=IW_STRAT2_RGB_RGB;
		}
	}

	if(ctx->apply_bkgd && ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY) {
		// Applying background before resizing
		if(s1==IW_STRAT1_RGBA_RGBA) {
			s1=IW_STRAT1_RGBA_RGB;
			s2=IW_STRAT2_RGB_RGB;
		}
		else if(s1==IW_STRAT1_GA_GA) {
			s1=IW_STRAT1_GA_G;
			s2=IW_STRAT2_G_G;
		}
		else if(s1==IW_STRAT1_GA_RGBA) {
			s1=IW_STRAT1_GA_RGB;
			s2=IW_STRAT2_RGB_RGB;
		}
		else if(s1==IW_STRAT1_RGBA_GA) {
			s1=IW_STRAT1_RGBA_G;
			s2=IW_STRAT2_G_G;
		}
	}

	if(ctx->apply_bkgd && !iw_bkgd_has_transparency(ctx)) {
		if(s2==IW_STRAT2_GA_GA) {
			s2=IW_STRAT2_GA_G;
		}
		else if(s2==IW_STRAT2_RGBA_RGBA) {
			s2=IW_STRAT2_RGBA_RGB;
		}
	}

	*ps1 = s1;
	*ps2 = s2;
}

// Choose our strategy for applying a background to the image.
// Uses:
//   - ctx->img1_imgtype_logical (set by init_channel_info())
//   - ctx->req.bkgd_valid (was background set by caller?)
//   - ctx->req.bkgd_checkerboard (set by caller)
//   - ctx->bkgd_check_size (set by caller)
//   - ctx->resize_settings[d].use_offset
// Sets:
//   - ctx->apply_bkgd (flag indicating whether we'll apply a background)
//   - ctx->apply_bkgd_strategy (flag indicating *when* we'll apply a background)
//   - ctx->bkgd_color_source (where to get the background color)
//   - ctx->bkgd_checkerboard
//   - ctx->bkgd_check_size (sanitized)
// May emit a warning if the caller's settings can't be honored.
static void decide_how_to_apply_bkgd(struct iw_context *ctx)
{
	if(!IW_IMGTYPE_HAS_ALPHA(ctx->img1_imgtype_logical)) {
		// If we know the image does not have any transparency,
		// we don't have to do anything.
		ctx->apply_bkgd=0;
		return;
	}

	// Figure out where to get the background color from, on the assumption
	// that we'll use one.
	if(ctx->img1_bkgd_label_set &&
		(ctx->req.use_bkgd_label_from_file || !ctx->req.bkgd_valid))
	{
		// The input file has a background color label, and either we are
		// requested to prefer it to the caller's background color, or
		// the caller did not give us a background color.
		// Use the color from the input file.
		ctx->bkgd_color_source = IW_BKGD_COLOR_SOURCE_FILE;
	}
	else if(ctx->req.bkgd_valid) {
		// Use the background color given by the caller.
		ctx->bkgd_color_source = IW_BKGD_COLOR_SOURCE_REQ;
		// Tentatively use the caller's checkerboard setting.
		// This may be overridden if we can't support checkerboard backgrounds
		// for some reason.
		ctx->bkgd_checkerboard = ctx->req.bkgd_checkerboard;
	}
	else {
		// No background color available. If we need one, we'll have to invent one.
		ctx->bkgd_color_source = IW_BKGD_COLOR_SOURCE_NONE;
	}

	if(ctx->bkgd_checkerboard) {
		if(ctx->bkgd_check_size<1) ctx->bkgd_check_size=1;
	}

	if(ctx->req.bkgd_valid) {
		// Caller told us to apply a background.
		ctx->apply_bkgd=1;
	}

	if(!(ctx->output_profile&IW_PROFILE_TRANSPARENCY)) {
		if(!ctx->req.bkgd_valid && !ctx->apply_bkgd) {
			iw_warning(ctx,"This image may have transparency, which is incompatible with the output format. A background color will be applied.");
		}
		ctx->apply_bkgd=1;
	}

	if(ctx->resize_settings[IW_DIMENSION_H].use_offset ||
		ctx->resize_settings[IW_DIMENSION_V].use_offset)
	{
		// If channel offset is enabled, and the image has transparency, we
		// must apply a solid color background (and we must apply it before
		// resizing), regardless of whether the user asked for it. It's the
		// only strategy we support.
		if(!ctx->req.bkgd_valid && !ctx->apply_bkgd) {
			iw_warning(ctx,"This image may have transparency, which is incompatible with a channel offset. A background color will be applied.");
		}
		ctx->apply_bkgd=1;

		if(ctx->bkgd_checkerboard && ctx->req.bkgd_checkerboard) {
			iw_warning(ctx,"Checkerboard backgrounds are not supported when using a channel offset.");
			ctx->bkgd_checkerboard=0;
		}
		ctx->apply_bkgd_strategy=IW_BKGD_STRATEGY_EARLY;
		return;
	}

	if(!ctx->apply_bkgd) {
		// No reason to apply a background color.
		return;
	}

	if(ctx->bkgd_checkerboard) {
		// Non-solid-color backgrounds must be applied after resizing.
		ctx->apply_bkgd_strategy=IW_BKGD_STRATEGY_LATE;
		return;
	}

	// At this point, either Early or Late background application is possible,
	// and (I think) would, in an idealized situation, yield the same result.
	// Things that can cause it to be different include
	// * using a different resampling algorithm for the alpha channel (this is
	//   no longer supported)
	// * 'intermediate clamping'
	//
	// Setting this to Late is the safe, though it is slower than Early.
	ctx->apply_bkgd_strategy=IW_BKGD_STRATEGY_LATE;
}

static void iw_set_auto_resizetype(struct iw_context *ctx, int size1, int size2,
	int dimension)
{
	// If not changing the size, default to "null" resize if we can.
	// (We can't do that if using a translation or channel offset.)
	if(size2==size1 && !ctx->resize_settings[dimension].use_offset &&
		!ctx->req.out_true_valid &&
		ctx->resize_settings[dimension].translate==0.0)
	{
		iw_set_resize_alg(ctx, dimension, IW_RESIZETYPE_NULL, 1.0, 0.0, 0.0);
		return;
	}

	// Otherwise, default to Catmull-Rom
	iw_set_resize_alg(ctx, dimension, IW_RESIZETYPE_CUBIC, 1.0, 0.0, 0.5);
}

static void init_channel_info(struct iw_context *ctx)
{
	int i;

	ctx->img1_imgtype_logical = ctx->img1.imgtype;

	if(ctx->resize_settings[IW_DIMENSION_H].edge_policy==IW_EDGE_POLICY_TRANSPARENT ||
		ctx->resize_settings[IW_DIMENSION_V].edge_policy==IW_EDGE_POLICY_TRANSPARENT)
	{
		// Add a virtual alpha channel
		if(ctx->img1.imgtype==IW_IMGTYPE_GRAY) {
			ctx->img1_imgtype_logical = IW_IMGTYPE_GRAYA;
		}
		else if(ctx->img1.imgtype==IW_IMGTYPE_RGB)
			ctx->img1_imgtype_logical = IW_IMGTYPE_RGBA;
	}

	ctx->img1_numchannels_physical = iw_imgtype_num_channels(ctx->img1.imgtype);
	ctx->img1_numchannels_logical = iw_imgtype_num_channels(ctx->img1_imgtype_logical);
	ctx->img1_alpha_channel_index = iw_imgtype_alpha_channel_index(ctx->img1_imgtype_logical);

	iw_set_input_channeltypes(ctx);

	ctx->img2.imgtype = ctx->img1_imgtype_logical; // default
	ctx->img2_numchannels = ctx->img1_numchannels_logical; // default
	ctx->intermed_numchannels = ctx->img1_numchannels_logical; // default

	for(i=0;i<ctx->img1_numchannels_logical;i++) {
		ctx->intermed_ci[i].channeltype = ctx->img1_ci[i].channeltype;
		ctx->intermed_ci[i].corresponding_input_channel = i;
		ctx->img2_ci[i].channeltype = ctx->img1_ci[i].channeltype;
		if(i>=ctx->img1_numchannels_physical) {
			// This is a virtual channel, which is handled by get_raw_sample().
			// But some optimizations cause that function to be bypassed, so we
			// have to disable those optimizations.
			ctx->img1_ci[i].disable_fast_get_sample = 1;
		}
	}
}

// Set the weights for the grayscale algorithm, if needed.
static void prepare_grayscale(struct iw_context *ctx)
{
	switch(ctx->grayscale_formula) {
	case IW_GSF_STANDARD:
		ctx->grayscale_formula = IW_GSF_WEIGHTED;
		iw_set_grayscale_weights(ctx,0.212655,0.715158,0.072187);
		break;
	case IW_GSF_COMPATIBLE:
		ctx->grayscale_formula = IW_GSF_WEIGHTED;
		iw_set_grayscale_weights(ctx,0.299,0.587,0.114);
		break;
	}
}

// Set up some things before we do the resize, and check to make
// sure everything looks okay.
static int iw_prepare_processing(struct iw_context *ctx, int w, int h)
{
	int i,j;
	int output_maxcolorcode_int;
	int strategy1, strategy2;
	int flag;

	if(ctx->output_profile==0) {
		iw_set_error(ctx,"Output profile not set");
		return 0;
	}

	if(!ctx->prng) {
		// TODO: It would be better to only create the random number generator
		// if we will need it.
		ctx->prng = iwpvt_prng_create(ctx);
	}

	if(ctx->randomize) {
		// Acquire and record a random seed. This also seeds the PRNG, but
		// that's irrelevant. It will be re-seeded before it is used.
		ctx->random_seed = iwpvt_util_randomize(ctx->prng);
	}

	if(ctx->req.out_true_valid) {
		ctx->resize_settings[IW_DIMENSION_H].out_true_size = ctx->req.out_true_width;
		ctx->resize_settings[IW_DIMENSION_V].out_true_size = ctx->req.out_true_height;
	}
	else {
		ctx->resize_settings[IW_DIMENSION_H].out_true_size = (double)w;
		ctx->resize_settings[IW_DIMENSION_V].out_true_size = (double)h;
	}

	if(!iw_check_image_dimensions(ctx,ctx->img1.width,ctx->img1.height)) {
		return 0;
	}
	if(!iw_check_image_dimensions(ctx,w,h)) {
		return 0;
	}

	if(ctx->to_grayscale) {
		prepare_grayscale(ctx);
	}

	init_channel_info(ctx);

	ctx->img2.width = w;
	ctx->img2.height = h;

	// Figure out the region of the source image to read from.
	if(ctx->input_start_x<0) ctx->input_start_x=0;
	if(ctx->input_start_y<0) ctx->input_start_y=0;
	if(ctx->input_start_x>ctx->img1.width-1) ctx->input_start_x=ctx->img1.width-1;
	if(ctx->input_start_y>ctx->img1.height-1) ctx->input_start_x=ctx->img1.height-1;
	if(ctx->input_w<0) ctx->input_w = ctx->img1.width - ctx->input_start_x;
	if(ctx->input_h<0) ctx->input_h = ctx->img1.height - ctx->input_start_y;
	if(ctx->input_w<1) ctx->input_w = 1;
	if(ctx->input_h<1) ctx->input_h = 1;
	if(ctx->input_w>(ctx->img1.width-ctx->input_start_x)) ctx->input_w=ctx->img1.width-ctx->input_start_x;
	if(ctx->input_h>(ctx->img1.height-ctx->input_start_y)) ctx->input_h=ctx->img1.height-ctx->input_start_y;

	// Decide on the output colorspace.
	if(ctx->req.output_cs_valid) {
		// Try to use colorspace requested by caller.
		ctx->img2cs = ctx->req.output_cs;

		if(ctx->output_profile&IW_PROFILE_ALWAYSLINEAR) {
			if(ctx->img2cs.cstype!=IW_CSTYPE_LINEAR) {
				iw_warning(ctx,"Forcing output colorspace to linear; required by the output format.");
				iw_make_linear_csdescr(&ctx->img2cs);
			}
		}
	}
	else {
		// By default, set the output colorspace to sRGB in most cases.
		if(ctx->output_profile&IW_PROFILE_ALWAYSLINEAR) {
			iw_make_linear_csdescr(&ctx->img2cs);
		}
		else {
			iw_make_srgb_csdescr_2(&ctx->img2cs);
		}
	}

	// Make sure maxcolorcodes are set.
	if(ctx->img1.sampletype!=IW_SAMPLETYPE_FLOATINGPOINT) {
		ctx->input_maxcolorcode_int = (1 << ctx->img1.bit_depth)-1;
		ctx->input_maxcolorcode = (double)ctx->input_maxcolorcode_int;

		for(i=0;i<IW_CI_COUNT;i++) {
			if(ctx->img1_ci[i].maxcolorcode_int<=0) {
				ctx->img1_ci[i].maxcolorcode_int = ctx->input_maxcolorcode_int;
			}
			ctx->img1_ci[i].maxcolorcode_dbl = (double)ctx->img1_ci[i].maxcolorcode_int;

			if(ctx->img1_ci[i].maxcolorcode_int != ctx->input_maxcolorcode_int) {
				// This is overzealous: We could enable it per-channel.
				// But it's probably not worth the trouble.
				ctx->support_reduced_input_bitdepths = 1;
			}
		}
	}

	if(ctx->support_reduced_input_bitdepths ||
		ctx->img1.sampletype==IW_SAMPLETYPE_FLOATINGPOINT)
	{
		for(i=0;i<ctx->img1_numchannels_physical;i++) {
			ctx->img1_ci[i].disable_fast_get_sample=1;
		}
	}

	// Set the .use_offset flags, based on whether the caller set any
	// .channel_offset[]s.
	for(i=0;i<2;i++) { // horizontal, vertical
		for(j=0;j<3;j++) { // red, green, blue
			if(fabs(ctx->resize_settings[i].channel_offset[j])>0.00001) {
				ctx->resize_settings[i].use_offset=1;
			}
		}
	}

	if(ctx->to_grayscale &&
		(ctx->resize_settings[IW_DIMENSION_H].use_offset ||
		ctx->resize_settings[IW_DIMENSION_V].use_offset) )
	{
		iw_warning(ctx,"Disabling channel offset, due to grayscale output.");
		ctx->resize_settings[IW_DIMENSION_H].use_offset=0;
		ctx->resize_settings[IW_DIMENSION_V].use_offset=0;
	}

	decide_how_to_apply_bkgd(ctx);

	// Decide if we can cache the resize settings.
	for(i=0;i<2;i++) {
		if(ctx->resize_settings[i].use_offset ||
		  (ctx->apply_bkgd &&
		   ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY &&
		   ctx->resize_settings[i].edge_policy==IW_EDGE_POLICY_TRANSPARENT))
		{
			// If a channel offset is used, we have to disable caching, because the
			// offset is stored in the cache, and it won't be the same for all channels.
			// If transparent virtual pixels will be converted to the background color
			// during the resize, we have to disable caching, because the background
			// sample value is stored in the cache, and it may be different for each
			// channel.
			ctx->resize_settings[i].disable_rrctx_cache=1;
		}
	}

	decide_strategy(ctx,&strategy1,&strategy2);

	switch(strategy1) { // input-to-intermediate
	case IW_STRAT1_RGBA_RGBA:
		ctx->intermed_imgtype = IW_IMGTYPE_RGBA;
		break;
	case IW_STRAT1_GA_RGBA:
		ctx->intermed_imgtype = IW_IMGTYPE_RGBA;
		ctx->intermed_ci[0].corresponding_input_channel=0;
		ctx->intermed_ci[1].corresponding_input_channel=0;
		ctx->intermed_ci[2].corresponding_input_channel=0;
		ctx->intermed_ci[3].corresponding_input_channel=1;
		break;
	case IW_STRAT1_RGB_RGB:
	case IW_STRAT1_RGBA_RGB:
		ctx->intermed_imgtype = IW_IMGTYPE_RGB;
		break;
	case IW_STRAT1_G_RGB:
	case IW_STRAT1_GA_RGB:
		ctx->intermed_imgtype = IW_IMGTYPE_RGB;
		ctx->intermed_ci[0].corresponding_input_channel=0;
		ctx->intermed_ci[1].corresponding_input_channel=0;
		ctx->intermed_ci[2].corresponding_input_channel=0;
		break;
	case IW_STRAT1_RGBA_GA:
		ctx->intermed_imgtype = IW_IMGTYPE_GRAYA;
		ctx->intermed_ci[0].cvt_to_grayscale=1;
		ctx->intermed_ci[0].corresponding_input_channel=0;
		ctx->intermed_ci[1].corresponding_input_channel=3;
		break;
	case IW_STRAT1_GA_GA:
		ctx->intermed_imgtype = IW_IMGTYPE_GRAYA;
		break;
	case IW_STRAT1_RGB_G:
		ctx->intermed_imgtype = IW_IMGTYPE_GRAY;
		ctx->intermed_ci[0].cvt_to_grayscale=1;
		ctx->intermed_ci[0].corresponding_input_channel=0;
		break;
	case IW_STRAT1_G_G:
		ctx->intermed_imgtype = IW_IMGTYPE_GRAY;
		ctx->intermed_ci[0].corresponding_input_channel=0;
		break;
	default:
		iw_set_errorf(ctx,"Internal error, unknown strategy %d",strategy1);
		return 0;
	}

	ctx->intermed_numchannels = iw_imgtype_num_channels(ctx->intermed_imgtype);
	ctx->intermed_alpha_channel_index = iw_imgtype_alpha_channel_index(ctx->intermed_imgtype);

	// Start with default mapping:
	for(i=0;i<ctx->intermed_numchannels;i++) {
		ctx->intermed_ci[i].corresponding_output_channel = i;
	}

	switch(strategy2) { // intermediate-to-output
	case IW_STRAT2_RGBA_RGBA:
		ctx->img2.imgtype = IW_IMGTYPE_RGBA;
		break;
	case IW_STRAT2_RGB_RGB:
		ctx->img2.imgtype = IW_IMGTYPE_RGB;
		break;
	case IW_STRAT2_RGBA_RGB:
		ctx->img2.imgtype = IW_IMGTYPE_RGB;
		ctx->intermed_ci[3].corresponding_output_channel= -1;
		break;
	case IW_STRAT2_GA_GA:
		ctx->img2.imgtype = IW_IMGTYPE_GRAYA;
		break;
	case IW_STRAT2_G_G:
		ctx->img2.imgtype = IW_IMGTYPE_GRAY;
		break;
	case IW_STRAT2_GA_G:
		ctx->img2.imgtype = IW_IMGTYPE_GRAY;
		ctx->intermed_ci[1].corresponding_output_channel= -1;
		break;
	default:
		iw_set_error(ctx,"Internal error");
		return 0;
	}

	ctx->img2_numchannels = iw_imgtype_num_channels(ctx->img2.imgtype);

	iw_set_intermed_channeltypes(ctx);
	iw_set_out_channeltypes(ctx);

	// If an alpha channel is present, set a flag on the other channels to indicate
	// that we have to process them differently.
	if(IW_IMGTYPE_HAS_ALPHA(ctx->intermed_imgtype)) {
		for(i=0;i<ctx->intermed_numchannels;i++) {
			if(ctx->intermed_ci[i].channeltype!=IW_CHANNELTYPE_ALPHA)
				ctx->intermed_ci[i].need_unassoc_alpha_processing = 1;
		}
	}


	decide_output_bit_depth(ctx);

	if(ctx->img2.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
		flag=0;
		for(i=0;i<IW_NUM_CHANNELTYPES;i++) {
			if(ctx->req.color_count[i]) flag=1;
		}
		if(flag) {
			iw_warning(ctx,"Posterization is not supported with floating point output.");
		}
	}
	else {
		output_maxcolorcode_int = (1 << ctx->img2.bit_depth)-1;

		// Set the default maxcolorcodes
		for(i=0;i<ctx->img2_numchannels;i++) {
			ctx->img2_ci[i].maxcolorcode_int = output_maxcolorcode_int;
		}

		// Check for special "reduced" colorcodes.
		if((ctx->output_profile&IW_PROFILE_REDUCEDBITDEPTHS)) {
			for(i=0;i<ctx->img2_numchannels;i++) {
				int mccr;
				mccr = ctx->req.output_maxcolorcode[ctx->img2_ci[i].channeltype];
				if(mccr>0) {
					if(mccr>output_maxcolorcode_int) mccr=output_maxcolorcode_int;
					ctx->img2_ci[i].maxcolorcode_int = mccr;
				}
			}
		}

		// Set some flags, and set the floating-point versions of the maxcolorcodes.
		for(i=0;i<ctx->img2_numchannels;i++) {
			if(ctx->img2_ci[i].maxcolorcode_int != output_maxcolorcode_int) {
				ctx->reduced_output_maxcolor_flag = 1;
				ctx->disable_output_lookup_tables = 1;
			}

			ctx->img2_ci[i].maxcolorcode_dbl = (double)ctx->img2_ci[i].maxcolorcode_int;
		}
	}

	for(i=0;i<ctx->img2_numchannels;i++) {
		ctx->img2_ci[i].color_count = ctx->req.color_count[ctx->img2_ci[i].channeltype];
		if(ctx->img2_ci[i].color_count) {
			iw_restrict_to_range(2,ctx->img2_ci[i].maxcolorcode_int,&ctx->img2_ci[i].color_count);
		}
		if(ctx->img2_ci[i].color_count==1+ctx->img2_ci[i].maxcolorcode_int) {
			ctx->img2_ci[i].color_count = 0;
		}

		ctx->img2_ci[i].ditherfamily = ctx->ditherfamily_by_channeltype[ctx->img2_ci[i].channeltype];
		ctx->img2_ci[i].dithersubtype = ctx->dithersubtype_by_channeltype[ctx->img2_ci[i].channeltype];
	}

	// Scan the output channels to see whether certain types of dithering are used.
	for(i=0;i<ctx->img2_numchannels;i++) {
		if(ctx->img2_ci[i].ditherfamily==IW_DITHERFAMILY_ERRDIFF) {
			ctx->uses_errdiffdither=1;
		}
	}

	if(!ctx->support_reduced_input_bitdepths && ctx->img1.sampletype==IW_SAMPLETYPE_UINT) {
		iw_make_x_to_linear_table(ctx,&ctx->input_color_corr_table,&ctx->img1,&ctx->img1cs);
	}

	if(ctx->img1_bkgd_label_set) {
		// Convert the background color to a linear colorspace.
		for(i=0;i<3;i++) {
			ctx->img1_bkgd_label_lin.c[i] = x_to_linear_sample(ctx->img1_bkgd_label_inputcs.c[i],&ctx->img1cs);
		}
		ctx->img1_bkgd_label_lin.c[3] = ctx->img1_bkgd_label_inputcs.c[3];
	}

	if(ctx->apply_bkgd) {
		prepare_apply_bkgd(ctx);
	}

	if(ctx->req.output_rendering_intent==IW_INTENT_UNKNOWN) {
		// User didn't request a specific intent; copy from input file.
		ctx->img2.rendering_intent = ctx->img1.rendering_intent;
	}
	else {
		ctx->img2.rendering_intent = ctx->req.output_rendering_intent;
	}

	if(ctx->resize_settings[IW_DIMENSION_H].family==IW_RESIZETYPE_AUTO) {
		iw_set_auto_resizetype(ctx,ctx->input_w,ctx->img2.width,IW_DIMENSION_H);
	}
	if(ctx->resize_settings[IW_DIMENSION_V].family==IW_RESIZETYPE_AUTO) {
		iw_set_auto_resizetype(ctx,ctx->input_h,ctx->img2.height,IW_DIMENSION_V);
	}

	if(IW_IMGTYPE_HAS_ALPHA(ctx->img2.imgtype)) {
		if(!ctx->opt_strip_alpha) {
			// If we're not allowed to strip the alpha channel, also disable
			// other optimizations that would implicitly remove the alpha
			// channel. (The optimization routines may do weird things if we
			// were to allow this.)
			ctx->opt_palette = 0;
			ctx->opt_binary_trns = 0;
		}
	}

	return 1;
}

IW_IMPL(int) iw_process_image(struct iw_context *ctx)
{
	int ret;
	int retval = 0;

	if(ctx->use_count>0) {
		iw_set_error(ctx,"Internal: Incorrect attempt to reprocess image");
		goto done;
	}
	ctx->use_count++;

	ret = iw_prepare_processing(ctx,ctx->canvas_width,ctx->canvas_height);
	if(!ret) goto done;

	ret = iw_process_internal(ctx);
	if(!ret) goto done;

	iwpvt_optimize_image(ctx);

	retval = 1;
done:
	return retval;
}
