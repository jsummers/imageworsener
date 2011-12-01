// imagew-main.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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

static IW_INLINE IW_SAMPLE srgb_to_linear_sample(IW_SAMPLE v_srgb)
{
	if(v_srgb<=0.04045) {
		return v_srgb/12.92;
	}
	else {
		return pow( (v_srgb+0.055)/(1.055) , 2.4);
	}
}

static IW_INLINE IW_SAMPLE gamma_to_linear_sample(IW_SAMPLE v, double gamma)
{
	return pow(v,gamma);
}

static IW_SAMPLE x_to_linear_sample(IW_SAMPLE v, const struct iw_csdescr *csdescr)
{
	if(csdescr->cstype==IW_CSTYPE_LINEAR) {
		return v;
	}
	else if(csdescr->cstype==IW_CSTYPE_GAMMA) {
		return gamma_to_linear_sample(v,csdescr->gamma);
	}
	return srgb_to_linear_sample(v);
}

// Public version of x_to_linear_sample().
IW_IMPL(double) iw_convert_sample_to_linear(double v, const struct iw_csdescr *csdescr)
{
	return (double)x_to_linear_sample(v,csdescr);
}

static IW_INLINE IW_SAMPLE linear_to_srgb_sample(IW_SAMPLE v_linear)
{
	if(v_linear <= 0.0031308) {
		return 12.92*v_linear;
	}
	return 1.055*pow(v_linear,1.0/2.4) - 0.055;
}

static IW_INLINE IW_SAMPLE linear_to_gamma_sample(IW_SAMPLE v_linear, double gamma)
{
	return pow(v_linear,1.0/gamma);
}

static IW_INLINE IW_SAMPLE get_raw_sample_flt64(struct iw_context *ctx,
	   int x, int y, int channel)
{
	size_t z;
	int k;
	// !!! Portability warning: Using a union in this way may be nonportable,
	// and/or may violate strict-aliasing rules.
	union su_union {
		iw_byte c[8];
		iw_float64 f;
	} volatile su;

	z = y*ctx->img1.bpr + (ctx->img1_numchannels*x + channel)*8;
	for(k=0;k<8;k++) {
		su.c[k]=ctx->img1.pixels[z+k];
	}
	return (IW_SAMPLE)su.f;
}

static IW_INLINE IW_SAMPLE get_raw_sample_flt32(struct iw_context *ctx,
	   int x, int y, int channel)
{
	size_t z;
	int k;
	// !!! Portability warning: Using a union in this way may be nonportable,
	// and/or may violate strict-aliasing rules.
	union su_union {
		iw_byte c[4];
		iw_float32 f;
	} volatile su;

	z = y*ctx->img1.bpr + (ctx->img1_numchannels*x + channel)*4;
	for(k=0;k<4;k++) {
		su.c[k]=ctx->img1.pixels[z+k];
	}
	return (IW_SAMPLE)su.f;
}

static IW_INLINE unsigned int get_raw_sample_16(struct iw_context *ctx,
	   int x, int y, int channel)
{
	size_t z;
	unsigned short tmpui16;
	z = y*ctx->img1.bpr + (ctx->img1_numchannels*x + channel)*2;
	tmpui16 = ( ((unsigned short)(ctx->img1.pixels[z+0])) <<8) | ctx->img1.pixels[z+1];
	return tmpui16;
}

static IW_INLINE unsigned int get_raw_sample_8(struct iw_context *ctx,
	   int x, int y, int channel)
{
	unsigned short tmpui8;
	tmpui8 = ctx->img1.pixels[y*ctx->img1.bpr + ctx->img1_numchannels*x + channel];
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

// Returns a value from 0 to 2^(ctx->img1.bit_depth)-1.
// x and y are logical coordinates.
static unsigned int get_raw_sample_int(struct iw_context *ctx,
	   int x, int y, int channel)
{
	int rx,ry; // physical coordinates
	rx = ctx->input_start_x+x;
	ry = ctx->input_start_y+y;

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
static IW_SAMPLE get_raw_sample(struct iw_context *ctx,
	   int x, int y, int channel)
{
	unsigned int v;
	int chtype;

	if(ctx->img1.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
		if(ctx->img1.bit_depth==64) {
			return get_raw_sample_flt64(ctx,x,y,channel);
		}
		return get_raw_sample_flt32(ctx,x,y,channel);
	}

	v = get_raw_sample_int(ctx,x,y,channel);
	if(!ctx->support_reduced_input_bitdepths)
		return ((double)v) / ctx->input_maxcolorcode;

	chtype = ctx->img1_ci[channel].channeltype;
	v >>= ctx->insignificant_bits[chtype];
	return ((double)v) / ctx->input_maxcolorcode_ext[chtype];
}

static IW_INLINE IW_SAMPLE iw_color_to_grayscale(struct iw_context *ctx,
	IW_SAMPLE r, IW_SAMPLE g, IW_SAMPLE b)
{
	if(ctx->grayscale_formula==1) {
		// Compatibility formula
		return 0.299*r + 0.587*g + 0.114*b;
	}
	// Formula for linear colorspace
	return 0.212655*r + 0.715158*g + 0.072187*b;
}

// Based on color depth of the input image.
static IW_SAMPLE cvt_int_sample_to_linear(struct iw_context *ctx,
	unsigned int v, const struct iw_csdescr *csdescr)
{
	IW_SAMPLE s;

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
static IW_SAMPLE cvt_int_sample_to_linear_output(struct iw_context *ctx,
	unsigned int v, const struct iw_csdescr *csdescr)
{
	IW_SAMPLE s;

	if(csdescr->cstype==IW_CSTYPE_LINEAR) {
		return ((double)v) / ctx->output_maxcolorcode;
	}
	else if(ctx->output_rev_color_corr_table) {
		return ctx->output_rev_color_corr_table[v];
	}

	s = ((double)v) / ctx->output_maxcolorcode;
	return x_to_linear_sample(s,csdescr);
}

// Same as get_sample_cvt_to_linear, but for floating-point input.
static IW_SAMPLE get_sample_fltpt_cvt_to_linear(struct iw_context *ctx,
  int x, int y, int channel, const struct iw_csdescr *csdescr)
{
	IW_SAMPLE v1,v2,v3;
	IW_SAMPLE r,g,b;
	int ch;

	ch = ctx->intermed_ci[channel].corresponding_input_channel;

	if(ctx->intermed_ci[channel].cvt_to_grayscale) {
		v1 = get_raw_sample(ctx,x,y,ch+0);
		v2 = get_raw_sample(ctx,x,y,ch+1);
		v3 = get_raw_sample(ctx,x,y,ch+2);
		r = x_to_linear_sample(v1,csdescr);
		g = x_to_linear_sample(v2,csdescr);
		b = x_to_linear_sample(v3,csdescr);
		return iw_color_to_grayscale(ctx,r,g,b);
	}

	v1 = get_raw_sample(ctx,x,y,ch);
	return x_to_linear_sample(v1,csdescr);
}

// Return a sample, converted to a linear colorspace if it isn't already in one.
// Channel is the output channel number.
static IW_SAMPLE get_sample_cvt_to_linear(struct iw_context *ctx,
	   int x, int y, int channel, const struct iw_csdescr *csdescr)
{
	unsigned int v1,v2,v3;
	IW_SAMPLE r,g,b;
	int ch;

	if(ctx->img1.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
		return get_sample_fltpt_cvt_to_linear(ctx, x, y, channel, csdescr);
	}

	ch = ctx->intermed_ci[channel].corresponding_input_channel;

	if(ctx->support_reduced_input_bitdepths) {
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
	// But all channels have to have the nominal input bitdepth.
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
	switch(ctx->output_depth) {
	case 8:  put_raw_sample_8(ctx,s,x,y,channel); break;
	case 16: put_raw_sample_16(ctx,s,x,y,channel); break;
	}
}

// s is from 0.0 to 1.0
static IW_INLINE void put_raw_sample_flt32(struct iw_context *ctx, double s,
	   int x, int y, int channel)
{
	// !!! Portability warning: Using a union in this way may be nonportable,
	// and/or may violate strict-aliasing rules.
	union su_union {
		iw_byte c[4];
		iw_float32 f;
	} volatile su;
	int i;
	size_t pos;

	su.f = (iw_float32)s;
	pos = y*ctx->img2.bpr + (ctx->img2_numchannels*x + channel)*4;

	for(i=0;i<4;i++) {
		ctx->img2.pixels[pos+i] = su.c[i];
	}
}

// s is from 0.0 to 1.0
static IW_INLINE void put_raw_sample_flt64(struct iw_context *ctx, double s,
	   int x, int y, int channel)
{
	// !!! Portability warning: Using a union in this way may be nonportable,
	// and/or may violate strict-aliasing rules.
	union su_union {
		iw_byte c[8];
		iw_float64 f;
	} volatile su;
	int i;
	size_t pos;

	su.f = (iw_float32)s;
	pos = y*ctx->img2.bpr + (ctx->img2_numchannels*x + channel)*8;

	for(i=0;i<8;i++) {
		ctx->img2.pixels[pos+i] = su.c[i];
	}
}

static void put_raw_sample_flt(struct iw_context *ctx, double s,
	   int x, int y, int channel)
{
	switch(ctx->output_depth) {
	case 32: put_raw_sample_flt32(ctx,s,x,y,channel); break;
	case 64: put_raw_sample_flt64(ctx,s,x,y,channel); break;
	}
}

static IW_SAMPLE linear_to_x_sample(IW_SAMPLE samp_lin, const struct iw_csdescr *csdescr)
{
	if(samp_lin > 0.999999999) {
		// This check is not for optimization; it's an attempt to work around a
		// defect in glibc's pow() function.
		// In 64-bit builds, bases very close to (but not equal to) 1.0 can,
		// in rare-but-not-rare-enough cases, cause pow to take about 10,000 (!)
		// times longer to run than it normally does. Incredible as it sounds,
		// this is not a bug. It is by design that glibc sometimes takes 2
		// million clock cycles to perform a single elementary math operation.
		// Here are some examples that will trigger this problem:
		//   pow(1.0000000000000002 , 1.5  )
		//   pow(1.00000000000002   , 1.05 )
		//   pow(1.0000000000000004 , 0.25 )
		//   pow(1.0000000000000004 , 1.25 )
		//   pow(0.999999999999994  , 0.25 )
		//   pow(0.999999999999994  , 0.41666666666666 )
		// Note that the last exponent is 1/2.4, which is the exponent needed for
		// conversion to sRGB. And values very close to 1.0 are inevitably
		// produced when resizing images with many white pixels.
		return 1.0;
	}

	if(csdescr->cstype==IW_CSTYPE_LINEAR) {
		return samp_lin;
	}
	else if(csdescr->cstype==IW_CSTYPE_GAMMA) {
		return linear_to_gamma_sample(samp_lin,csdescr->gamma);
	}
	else { // assume IW_CSTYPE_SRGB
		return linear_to_srgb_sample(samp_lin);
	}
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

static int get_nearest_valid_colors(struct iw_context *ctx, IW_SAMPLE samp_lin,
		int channel, const struct iw_csdescr *csdescr,
		double *s_lin_floor_1, double *s_lin_ceil_1,
		double *s_cvt_floor_full, double *s_cvt_ceil_full)
{
	IW_SAMPLE samp_cvt;
	double samp_cvt_expanded;
	unsigned int floor_int, ceil_int;

	// A prelimary conversion to the target color space.
	samp_cvt = linear_to_x_sample(samp_lin,csdescr);

	if(ctx->img2_ci[channel].color_count==0) {
		// The normal case: we want to use this channel's full available depth.
		samp_cvt_expanded = samp_cvt * ctx->output_maxcolorcode;
		if(samp_cvt_expanded>ctx->output_maxcolorcode) samp_cvt_expanded=ctx->output_maxcolorcode;
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
		double maxcolorcode;
		// Example: color_count = 4, bit_depth = 8;
		// Colors are from 0.0 to 3.0, mapped to 0.0 to 255.0.
		// Reduction factor is 255.0/3.0 = 85.0

		maxcolorcode = (double)(ctx->img2_ci[channel].color_count-1);

		samp_cvt_expanded = samp_cvt * maxcolorcode;
		if(samp_cvt_expanded>maxcolorcode) samp_cvt_expanded=maxcolorcode;
		if(samp_cvt_expanded<0.0) samp_cvt_expanded=0.0;

		// If the number of shades is not 2, 4, 6, 16, 18, 52, 86, or 256 (assuming 8-bit depth),
		// then the shades will not be exactly evenly spaced. For example, if there are 3 shades,
		// they will be 0, 128, and 255. It will often be the case that the shade we want is exactly
		// halfway between the nearest two available shades, and the "0.5000000001" fudge factor is my
		// attempt to make sure it rounds consistently in the same direction.
		*s_cvt_floor_full = floor(0.5000000001 + floor(samp_cvt_expanded) * (ctx->output_maxcolorcode/maxcolorcode));
		*s_cvt_ceil_full  = floor(0.5000000001 + ceil (samp_cvt_expanded) * (ctx->output_maxcolorcode/maxcolorcode));
	}

	floor_int = (unsigned int)(*s_cvt_floor_full);
	ceil_int  = (unsigned int)(*s_cvt_ceil_full);
	if(floor_int == ceil_int) {
		return 1;
	}

	// Convert the candidates to our linear color space
	*s_lin_floor_1 = cvt_int_sample_to_linear_output(ctx,floor_int,csdescr);
	*s_lin_ceil_1 =  cvt_int_sample_to_linear_output(ctx,ceil_int ,csdescr);

	return 0;
}

// channel is the output channel
static void put_sample_convert_from_linear_flt(struct iw_context *ctx, IW_SAMPLE samp_lin,
	   int x, int y, int channel, const struct iw_csdescr *csdescr)
{
	put_raw_sample_flt(ctx,(double)samp_lin,x,y,channel);
}

static double get_final_sample_using_nc_tbl(struct iw_context *ctx, IW_SAMPLE samp_lin)
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
static void put_sample_convert_from_linear(struct iw_context *ctx, IW_SAMPLE samp_lin,
	   int x, int y, int channel, const struct iw_csdescr *csdescr)
{
	double s_lin_floor_1, s_lin_ceil_1;
	double s_cvt_floor_full, s_cvt_ceil_full;
	double d_floor, d_ceil;
	int is_exact;
	double s_full;
	int ditherfamily;
	int dd; // Dither decision: 0 to use floor, 1 to use ceil.

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

	is_exact = get_nearest_valid_colors(ctx,samp_lin,channel,csdescr,
		&s_lin_floor_1, &s_lin_ceil_1,
		&s_cvt_floor_full, &s_cvt_ceil_full);

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

static void clamp_output_samples(struct iw_context *ctx)
{
	int i;

	for(i=0;i<ctx->num_out_pix;i++) {
		if(ctx->out_pix[i]<0.0) ctx->out_pix[i]=0.0;
		else if(ctx->out_pix[i]>1.0) ctx->out_pix[i]=1.0;
	}
}

// 'channel' is an intermediate channel number.
static int iw_process_cols_to_intermediate(struct iw_context *ctx, int channel,
	const struct iw_csdescr *in_csdescr)
{
	int i,j;
	int retval=0;
	IW_SAMPLE tmp_alpha;
	IW_SAMPLE *inpix = NULL;
	IW_SAMPLE *outpix = NULL;
	int is_alpha_channel;
	struct iw_resize_settings *rs;
	struct iw_channelinfo_intermed *int_ci;
	struct iw_rr_ctx *rrctx = NULL;

	ctx->in_pix = NULL;
	ctx->out_pix = NULL;

	int_ci = &ctx->intermed_ci[channel];
	is_alpha_channel = (int_ci->channeltype==IW_CHANNELTYPE_ALPHA);

	ctx->num_in_pix = ctx->input_h;
	inpix = (IW_SAMPLE*)iw_malloc(ctx, ctx->num_in_pix * sizeof(IW_SAMPLE));
	if(!inpix) goto done;
	ctx->in_pix = inpix;

	ctx->num_out_pix = ctx->intermed_height;
	outpix = (IW_SAMPLE*)iw_malloc(ctx, ctx->num_out_pix * sizeof(IW_SAMPLE));
	if(!outpix) goto done;
	ctx->out_pix = outpix;

	if(ctx->use_resize_settings_alpha && is_alpha_channel)
		rs=&ctx->resize_settings_alpha;
	else
		rs=&ctx->resize_settings[IW_DIMENSION_V];

	rrctx = iwpvt_resize_rows_init(ctx,rs,int_ci->channeltype);
	if(!rrctx) goto done;

	for(i=0;i<ctx->input_w;i++) {

		// Read a column of pixels into ctx->in_pix
		for(j=0;j<ctx->input_h;j++) {

			ctx->in_pix[j] = get_sample_cvt_to_linear(ctx,i,j,channel,in_csdescr);

			if(int_ci->need_unassoc_alph_processing) { // We need opacity information also
				tmp_alpha = get_raw_sample(ctx,i,j,ctx->img1_alpha_channel_index);

				// Multiply color amount by opacity
				ctx->in_pix[j] *= tmp_alpha;
			}
			else if(ctx->apply_bkgd && ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY) {
				// We're doing "Early" background color application.
				// All intermediate channels will need the background color
				// applied to them.
				tmp_alpha = get_raw_sample(ctx,i,j,ctx->img1_alpha_channel_index);
				ctx->in_pix[j] = (tmp_alpha)*(ctx->in_pix[j]) +
					(1.0-tmp_alpha)*(int_ci->bkgd_color_lin);

			}
		}

		// Now we have a row in the right format.
		// Resize it and store it in the right place in the intermediate array.

		iwpvt_resize_row_main(ctx,rrctx);

		if(ctx->intclamp)
			clamp_output_samples(ctx);

		// The intermediate pixels are in ctx->out_pix. Copy them to the intermediate array.
		for(j=0;j<ctx->intermed_height;j++) {
			if(is_alpha_channel) {
				ctx->intermediate_alpha[((size_t)j)*ctx->intermed_width + i] = ctx->out_pix[j];
			}
			else {
				ctx->intermediate[((size_t)j)*ctx->intermed_width + i] = ctx->out_pix[j];
			}
		}
	}

	retval=1;

done:
	if(rrctx) iwpvt_resize_rows_done(ctx, rrctx);
	if(inpix) iw_free(inpix);
	if(outpix) iw_free(outpix);
	ctx->in_pix=NULL;
	ctx->out_pix=NULL;
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
	IW_SAMPLE tmpsamp;
	IW_SAMPLE alphasamp = 0.0;
	IW_SAMPLE *outpix = NULL;
	// Do any of the output channels use error-diffusion dithering?
	int using_errdiffdither = 0;
	int output_channel;
	int is_alpha_channel;
	struct iw_resize_settings *rs;
	int ditherfamily, dithersubtype;
	int clamp_after_resize=0;
	int clamp_after_composite=0;
	struct iw_channelinfo_intermed *int_ci;
	struct iw_channelinfo_out *out_ci;
	struct iw_rr_ctx *rrctx = NULL;

	ctx->in_pix = NULL;
	ctx->out_pix = NULL;

	ctx->num_in_pix = ctx->intermed_width;
	ctx->num_out_pix = ctx->img2.width;

	int_ci = &ctx->intermed_ci[intermed_channel];
	output_channel = int_ci->corresponding_output_channel;
	out_ci = &ctx->img2_ci[output_channel];
	is_alpha_channel = (int_ci->channeltype==IW_CHANNELTYPE_ALPHA);

	if(!is_alpha_channel) {
		// For non-alpha channels, allocate a buffer to hold the output samples.
		// (For alpha samples, we'll use ctx->final_alpha directly.)
		outpix = (IW_SAMPLE*)iw_malloc(ctx, ctx->num_out_pix * sizeof(IW_SAMPLE));
		if(!outpix) goto done;
		ctx->out_pix = outpix;
	}

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

	if(ctx->intclamp) {
		// ctx->intclamp means to clamp at pretty much every opportunity.
		// (Setting clamp_after_composite would have no additional effect here.
		clamp_after_resize = 1;
	}
	else if(ctx->img2.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
		// If output is floating point, never clamp unless specifically
		// requested.
		;
	}
	else if(ctx->apply_bkgd && ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_LATE) {
		// If we are applying a background color (after resizing)...
		if(int_ci->need_unassoc_alph_processing) {
			// then the color channels should only be clamped after applying
			// the background...
			clamp_after_composite = 1;
		}
		// (and the alpha channel should not directly be clamped at all).
	}
	else {
		// The typical case: clamp after resizing, because the output format
		// requires it.
		clamp_after_resize = 1;
	}

	if(ctx->use_resize_settings_alpha && is_alpha_channel)
		rs=&ctx->resize_settings_alpha;
	else
		rs=&ctx->resize_settings[IW_DIMENSION_H];

	rrctx = iwpvt_resize_rows_init(ctx,rs,int_ci->channeltype);
	if(!rrctx) goto done;

	for(j=0;j<ctx->intermed_height;j++) {
		if(is_alpha_channel) {
			ctx->in_pix = &ctx->intermediate_alpha[((size_t)j)*ctx->intermed_width];
			ctx->out_pix = &ctx->final_alpha[((size_t)j)*ctx->img2.width];
		}
		else {
			ctx->in_pix = &ctx->intermediate[((size_t)j)*ctx->intermed_width];
		}

		// Resize it to out_pix

		iwpvt_resize_row_main(ctx,rrctx);

		if(clamp_after_resize)
			clamp_output_samples(ctx);

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

			tmpsamp = ctx->out_pix[i];

			if(int_ci->need_unassoc_alph_processing) {
				// Special processing for (partially) transparent pixels.
				alphasamp = ctx->final_alpha[((size_t)j)*ctx->img2.width + i];
				if(alphasamp!=0.0) {
					tmpsamp /= alphasamp;
				}
			}

			if(int_ci->need_unassoc_alph_processing && ctx->apply_bkgd && ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_LATE) {
				// Apply a background color (or checkerboard pattern).
				IW_SAMPLE bkcolor;
				if(ctx->bkgd_checkerboard) {
					if( (((ctx->bkgd_check_origin[IW_DIMENSION_H]+i)/ctx->bkgd_check_size)%2) ==
						(((ctx->bkgd_check_origin[IW_DIMENSION_V]+j)/ctx->bkgd_check_size)%2) )
					{
						bkcolor = out_ci->bkgd_color_lin;
					}
					else {
						bkcolor = out_ci->bkgd2_color_lin;
					}
				}
				else {
					bkcolor = out_ci->bkgd_color_lin;
				}

				tmpsamp = (alphasamp)*(tmpsamp) + (1.0-alphasamp)*(bkcolor);

				if(clamp_after_composite) {
					if(tmpsamp<0.0) tmpsamp=0.0;
					else if(tmpsamp>1.0) tmpsamp=1.0;
				}
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
	if(rrctx) iwpvt_resize_rows_done(ctx, rrctx);
	ctx->in_pix=NULL;
	ctx->out_pix=NULL;
	if(outpix) iw_free(outpix);

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
	if(img->bit_depth != ctx->output_depth) return;

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

static int iw_process_internal(struct iw_context *ctx)
{
	int channel;
	int retval=0;
	int k;
	int ret;
	// A linear color-correction descriptor to use with alpha channels.
	struct iw_csdescr csdescr_linear;

	ctx->intermediate=NULL;
	ctx->intermediate_alpha=NULL;
	ctx->final_alpha=NULL;
	ctx->intermed_width = ctx->input_w;
	ctx->intermed_height = ctx->img2.height;

	iw_make_linear_csdescr(&csdescr_linear);

	ctx->img2.bpr = iw_calc_bytesperrow(ctx->img2.width,ctx->output_depth*ctx->img2_numchannels);

	ctx->img2.pixels = iw_malloc_large(ctx, ctx->img2.bpr, ctx->img2.height);
	if(!ctx->img2.pixels) {
		goto done;
	}

	if(ctx->output_depth<8) {
		// If depth is < 8 (currently not possible), we will write partial
		// pixels at a time, and we have to zero out the memory at the
		// beginning.
		memset(ctx->img2.pixels, 0, ctx->img2.bpr * ctx->img2.height);
	}

	ctx->img2.bit_depth = ctx->output_depth;

	ctx->intermediate = (IW_SAMPLE*)iw_malloc_large(ctx, ctx->intermed_width * ctx->intermed_height, sizeof(IW_SAMPLE));
	if(!ctx->intermediate) {
		goto done;
	}

	if(ctx->uses_errdiffdither) {
		for(k=0;k<IW_DITHER_MAXROWS;k++) {
			ctx->dither_errors[k] = (IW_SAMPLE*)iw_malloc(ctx, ctx->img2.width * sizeof(IW_SAMPLE));
			if(!ctx->dither_errors[k]) goto done;
		}
	}

	iw_make_x_to_linear_table(ctx,&ctx->output_rev_color_corr_table,&ctx->img2,&ctx->img2cs);

	iw_make_nearest_color_table(ctx,&ctx->nearest_color_table,&ctx->img2,&ctx->img2cs);

	// If an alpha channel is present, we have to process it first.
	if(IW_IMGTYPE_HAS_ALPHA(ctx->intermed_imgtype)) {
		ctx->intermediate_alpha = (IW_SAMPLE*)iw_malloc_large(ctx, ctx->intermed_width * ctx->intermed_height, sizeof(IW_SAMPLE));
		if(!ctx->intermediate_alpha) {
			goto done;
		}
		ctx->final_alpha = (IW_SAMPLE*)iw_malloc_large(ctx, ctx->img2.width * ctx->img2.height, sizeof(IW_SAMPLE));
		if(!ctx->final_alpha) {
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
	retval=1;

done:
	if(ctx->intermediate) { iw_free(ctx->intermediate); ctx->intermediate=NULL; }
	if(ctx->intermediate_alpha) { iw_free(ctx->intermediate_alpha); ctx->intermediate_alpha=NULL; }
	if(ctx->final_alpha) { iw_free(ctx->final_alpha); ctx->final_alpha=NULL; }
	for(k=0;k<IW_DITHER_MAXROWS;k++) {
		if(ctx->dither_errors[k]) { iw_free(ctx->dither_errors[k]); ctx->dither_errors[k]=NULL; }
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
	for(i=0;i<ctx->img1_numchannels;i++) {
		ctx->img1_ci[i].channeltype = iw_get_channeltype(ctx->img1.imgtype,i);
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

// decide the sample type and bit depth
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
		if(ctx->output_depth<=0)
			ctx->output_depth=64; // Caller did not set depth. Use 64.

		// Caller set depth. Sanitize it.
		if(ctx->output_depth<=32)
			ctx->output_depth=32;
		else
			ctx->output_depth=64;
		return;
	}

	// Below this point, sample type is UINT.

	if(!(ctx->output_profile&IW_PROFILE_16BPS) && ctx->output_depth>8) {
		// Caller set depth to more than this format can handle. Warn, and fix it.
		iw_warning(ctx,"Reducing depth to 8; required by the output format.");
		ctx->output_depth=8;
	}

	if(ctx->output_depth>0) {
		// Caller set the output depth. Sanitize it.
		if(ctx->output_depth>8 && (ctx->output_profile&IW_PROFILE_16BPS))
			ctx->output_depth=16;
		else
			ctx->output_depth=8;
		return;
	}

	// Caller did not set output depth. Always use 8.
	ctx->output_depth=8;
}

// Make a final decision about what to use for the background color.
// TODO: Maybe some of this should be moved to the decide_how_to_apply_bkgd()
// function, or some other refactoring should be done.
// For example, decide_how_to_apply_bkgd() may warn you about checkerboard
// backgrounds not being supported, even if a checkerboard background
// would not actually have been used, because it would have been overridden in
// this function.
static void prepare_apply_bkgd(struct iw_context *ctx)
{
	struct iw_rgb_color bkgd1; // Main background color in linear colorspace
	struct iw_rgb_color bkgd2; // Secondary background color ...
	int i;

	if(!ctx->apply_bkgd) return;

	if(ctx->img1_bkgd_label_set && !ctx->caller_set_bkgd) {
		// If the user didn't give us a background color, and the file
		// has one, use the file's background color as the default.
		ctx->bkgd = ctx->img1_bkgd_label; // structure copy
	}

	bkgd1.c[0]=0.0; bkgd1.c[1]=0.0; bkgd1.c[2]=0.0;
	bkgd2.c[0]=0.0; bkgd2.c[1]=0.0; bkgd2.c[2]=0.0;

	if(ctx->use_bkgd_label && ctx->img1_bkgd_label_set) {
		// Use the background color label from the input file.
		bkgd1 = ctx->img1_bkgd_label; // sructure copy
		ctx->bkgd_checkerboard = 0;
	}
	else {
		// Use the background color set by the caller, or the default
		// background color.
		bkgd1 = ctx->bkgd;
		if(ctx->bkgd_checkerboard) {
			bkgd2 = ctx->bkgd2;
		}
	}

	// Set up the channelinfo as needed according to the target image type
	// (and we shouldn't be here if it's anything other than RGB or GRAY).

	if(ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_LATE && ctx->img2.imgtype==IW_IMGTYPE_RGB) {
		for(i=0;i<3;i++) {
			ctx->img2_ci[i].bkgd_color_lin = bkgd1.c[i];
		}
		if(ctx->bkgd_checkerboard) {
			for(i=0;i<3;i++) {
				ctx->img2_ci[i].bkgd2_color_lin = bkgd2.c[i];
			}
		}
	}
	else if(ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_LATE && ctx->img2.imgtype==IW_IMGTYPE_GRAY) {
		ctx->img2_ci[0].bkgd_color_lin = iw_color_to_grayscale(ctx,bkgd1.c[0],bkgd1.c[1],bkgd1.c[2]);
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

	if(ctx->bkgd_checkerboard) {
		if(ctx->bkgd_check_size<1) ctx->bkgd_check_size=1;
	}
}

#define IW_STRAT1_G_G       0x011 // source gray -> intermed gray
#define IW_STRAT1_G_RGB     0x013 // e.g. when using a channel offset
#define IW_STRAT1_GA_G      0x021 // could use if applying solid gray bkgd
#define IW_STRAT1_GA_GA     0x022 //
#define IW_STRAT1_GA_RGB    0x023 // could use if applying solid color bkgd
#define IW_STRAT1_GA_RGBA   0x024 // e.g. when using a channel offset
#define IW_STRAT1_RGB_G     0x031 // e.g. -grayscale
#define IW_STRAT1_RGB_RGB   0x033
#define IW_STRAT1_RGBA_G    0x041
#define IW_STRAT1_RGBA_GA   0x042 // e.g. -grayscale
#define IW_STRAT1_RGBA_RGB  0x043 // could use if applying solid bkgd
#define IW_STRAT1_RGBA_RGBA 0x044

#define IW_STRAT2_G_G       0x111 // intermed gray -> output gray
//#define IW_STRAT2_G_RGB     0x113 // e.g. if redcc!=greencc
#define IW_STRAT2_GA_G      0x121 // e.g. if applying a gray background
#define IW_STRAT2_GA_GA     0x122 // ...
//#define IW_STRAT2_GA_RGB    0x123 // e.g. if applying a color background
//#define IW_STRAT2_GA_RGBA   0x124 // e.g. if redcc!=greencc
#define IW_STRAT2_RGB_RGB   0x133
#define IW_STRAT2_RGBA_RGB  0x143 // e.g. if applying a background
#define IW_STRAT2_RGBA_RGBA 0x144


static void iw_restrict_to_range(int r1, int r2, int *pvar)
{
	if(*pvar < r1) *pvar = r1;
	else if(*pvar > r2) *pvar = r2;
}

static void decide_strategy(struct iw_context *ctx, int *ps1, int *ps2)
{
	int s1, s2;

	// Start with a default strategy
	switch(ctx->img1.imgtype) {
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

	if(ctx->apply_bkgd) {
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
static void decide_how_to_apply_bkgd(struct iw_context *ctx)
{
	if(!IW_IMGTYPE_HAS_ALPHA(ctx->img1.imgtype)) {
		// If we know the image does not have any transparency,
		// we don't have to do anything.
		ctx->apply_bkgd=0;
		return;
	}

	if(!(ctx->output_profile&IW_PROFILE_TRANSPARENCY)) {
		if(!ctx->apply_bkgd) {
			iw_warning(ctx,"This image may have transparency, which is incompatible with the output format. A background color will be applied.");
			ctx->apply_bkgd=1;
		}
	}

	if(ctx->offset_color_channels) {
		// If this feature is enabled and the image has transparency,
		// we must apply a solid color background (and we must apply
		// it before resizing), regardless of whether
		// the user asked for it or not. It's the only strategy we support.
		if(!ctx->apply_bkgd) {
			iw_warning(ctx,"This image may have transparency, which is incompatible with a channel offset. A background color will be applied.");
			ctx->apply_bkgd=1;
		}

		if(ctx->bkgd_checkerboard) {
			iw_warning(ctx,"Checkerboard backgrounds are not supported when using a channel offset.");
			ctx->bkgd_checkerboard=0;
		}
		ctx->apply_bkgd_strategy=IW_BKGD_STRATEGY_EARLY;
		return;
	}

	if(!ctx->apply_bkgd) {
		// At this point, we won't be applying a background because
		// the user didn't request it, and we have no other reason to.
		return;
	}

	if(ctx->bkgd_checkerboard) {
		// Non-solid-color backgrounds must be applied after resizing.
		ctx->apply_bkgd_strategy=IW_BKGD_STRATEGY_LATE;
		return;
	}

	// At this point, either Early or Late background application is
	// possible, and (I think) should have the same end result, *provided*
	// that the alpha channel uses the same resampling algorithm as the color
	// channels.
	// The simplest thing to do is to always use Late, so that if the alpha
	// channel does use a different algorithm, it will have an effect.
	ctx->apply_bkgd_strategy=IW_BKGD_STRATEGY_LATE;
}

static void iw_set_auto_resizetype(struct iw_context *ctx, int size1, int size2,
	int channeltype, int dimension)
{
	// If not changing the size, default to "null" resize if we can.
	// (We can't do that if using a channel offset.)
	if(size2==size1 && !ctx->offset_color_channels &&
		ctx->resize_settings[dimension].translate==0.0)
	{
		iw_set_resize_alg(ctx, channeltype, dimension, IW_RESIZETYPE_NULL, 1.0, 0.0, 0.0);
		return;
	}

	// If increasing the size, default is Catmull-Rom
	if(size2>size1) {
		iw_set_resize_alg(ctx, channeltype, dimension, IW_RESIZETYPE_CUBIC, 1.0, 0.0, 0.5);
		return;
	}

	// Otherwise use pixel mixing.
	iw_set_resize_alg(ctx, channeltype, dimension, IW_RESIZETYPE_MIX, 1.0, 0.0, 0.0);
}

static void init_channel_info(struct iw_context *ctx)
{
	int i;

	ctx->img1_numchannels = iw_imgtype_num_channels(ctx->img1.imgtype);
	ctx->img1_alpha_channel_index = iw_imgtype_alpha_channel_index(ctx->img1.imgtype);

	iw_set_input_channeltypes(ctx);

	ctx->img2.imgtype = ctx->img1.imgtype; // default
	ctx->img2_numchannels = ctx->img1_numchannels; // default
	ctx->intermed_numchannels = ctx->img1_numchannels; // default

	for(i=0;i<ctx->img1_numchannels;i++) {
		ctx->intermed_ci[i].channeltype = ctx->img1_ci[i].channeltype;
		ctx->intermed_ci[i].corresponding_input_channel = i;
		ctx->img2_ci[i].channeltype = ctx->img1_ci[i].channeltype;
	}
}

static void iw_convert_density_info(struct iw_context *ctx)
{
	double factor;

	if(ctx->density_policy==IW_DENSITY_POLICY_FORCED) {
		ctx->img2.density_code = IW_DENSITY_UNITS_PER_METER;
		ctx->img2.density_x = ctx->density_forced_x;
		ctx->img2.density_y = ctx->density_forced_y;
		return;
	}

	if(ctx->density_policy==IW_DENSITY_POLICY_NONE) return;

	if(ctx->img1.density_code==IW_DENSITY_UNKNOWN) return;

	if(ctx->density_policy==IW_DENSITY_POLICY_KEEP) {
		ctx->img2.density_code = ctx->img1.density_code;
		ctx->img2.density_x = ctx->img1.density_x;
		ctx->img2.density_y = ctx->img1.density_y;
		return;
	}

	// At this point, the policy is either AUTO or ADJUST.

	if(ctx->input_w!=ctx->img2.width || ctx->input_h!=ctx->img2.height) {
		// If the image size is being changed, don't write a density unless the
		// policy is ADJUST.
		if(ctx->density_policy!=IW_DENSITY_POLICY_ADJUST)
			return;
	}

	ctx->img2.density_code = ctx->img1.density_code;

	factor = ((double)ctx->img2.width)/(double)ctx->input_w;
	ctx->img2.density_x = ctx->img1.density_x * factor;

	factor = ((double)ctx->img2.height)/(double)ctx->input_h;
	ctx->img2.density_y = ctx->img1.density_y * factor;
}

// Set up some things before we do the resize, and check to make
// sure everything looks okay.
static int iw_prepare_processing(struct iw_context *ctx, int w, int h)
{
	int i;
	int output_maxcolorcode_int;
	int strategy1, strategy2;
	int flag;

	if(ctx->output_profile==0) {
		iw_set_error(ctx,"Output profile not set");
		return 0;
	}

	if(ctx->randomize) {
		// Acquire and record a random seed. This also seeds the PRNG, but
		// that's irrelevant. It will be re-seeded before it is used.
		ctx->random_seed = iwpvt_util_randomize(ctx->prng);
	}

	if(!iw_check_image_dimensions(ctx,ctx->img1.width,ctx->img1.height)) {
		return 0;
	}
	if(!iw_check_image_dimensions(ctx,w,h)) {
		return 0;
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

	// By default, set the output colorspace to sRGB in most cases.
	if(!ctx->caller_set_output_csdescr) {
		if(ctx->output_profile&IW_PROFILE_ALWAYSLINEAR) {
			iw_make_linear_csdescr(&ctx->img2cs);
		}
		else {
			iw_make_srgb_csdescr(&ctx->img2cs,IW_SRGB_INTENT_PERCEPTUAL);
		}
	}

	// But if IW_PROFILE_ALWAYSLINEAR is set (i.e. MIFF format), only linear
	// output is allowed.
	if(ctx->output_profile&IW_PROFILE_ALWAYSLINEAR) {
		if(ctx->img2cs.cstype!=IW_CSTYPE_LINEAR) {
			if(ctx->warn_invalid_output_csdescr) {
				iw_warning(ctx,"Forcing output colorspace to linear; required by the output format.");
			}
			iw_make_linear_csdescr(&ctx->img2cs);
		}
	}

	if(ctx->img1.sampletype!=IW_SAMPLETYPE_FLOATINGPOINT) {
		ctx->input_maxcolorcode = (double)((1 << ctx->img1.bit_depth)-1);

		for(i=0;i<5;i++) {
			if(ctx->significant_bits[i]>0 && ctx->significant_bits[i]<ctx->img1.bit_depth) {
				ctx->support_reduced_input_bitdepths = 1; // Set this flag for later.
				ctx->insignificant_bits[i] = ctx->img1.bit_depth - ctx->significant_bits[i];
				ctx->input_maxcolorcode_ext[i] = (double)((1 << ctx->significant_bits[i])-1);
			}
			else {
				ctx->insignificant_bits[i] = 0;
				ctx->input_maxcolorcode_ext[i] = ctx->input_maxcolorcode;
			}
		}
	}

	decide_output_bit_depth(ctx);

	if(ctx->img2.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
		flag=0;
		for(i=0;i<5;i++) {
			if(ctx->color_count[i]) flag=1;
		}
		if(flag) {
			iw_warning(ctx,"Posterization not supported with floating point output.");
		}
	}
	else {
		output_maxcolorcode_int = (1 << ctx->output_depth)-1;
		ctx->output_maxcolorcode = (double)output_maxcolorcode_int;

		for(i=0;i<5;i++) {
			if(ctx->color_count[i]) iw_restrict_to_range(2,output_maxcolorcode_int+1,&ctx->color_count[i]);
			if(ctx->color_count[i]==output_maxcolorcode_int+1) ctx->color_count[i]=0;
		}
	}

	if(ctx->offset_color_channels && ctx->to_grayscale) {
		iw_warning(ctx,"Disabling channel offset, due to grayscale output.");
		ctx->offset_color_channels=0;
	}

	// TODO: edge_policy could be per-dimension.
	if(ctx->offset_color_channels ||
		ctx->resize_settings[IW_DIMENSION_H].translate!=0.0 ||
		ctx->resize_settings[IW_DIMENSION_V].translate!=0.0)
	{
		// If the output samples are shifted with respect to the input pixels, even
		// if some input samples do contribute to the output sample, it may not be
		// enough to result in a meaningful sample value. When we try to normalize
		// them, we could end up dividing by some tiny useless sum, or even
		// by zero.
		// Plus, when we reach the point where no source samples are available,
		// we'd have to suddenly switch to a different strategy, which could result
		// in a visible seam in the image.
		// To avoid these problems, we require virtual pixels to be used if using
		// a channel offset. (This is possibly overkill.)
		if(ctx->edge_policy==IW_EDGE_POLICY_STANDARD) {
			ctx->edge_policy=IW_EDGE_POLICY_REPLICATE;
		}
	}

	decide_how_to_apply_bkgd(ctx);

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
				ctx->intermed_ci[i].need_unassoc_alph_processing = 1;
		}
	}

	for(i=0;i<ctx->img2_numchannels;i++) {
		ctx->img2_ci[i].color_count = ctx->color_count[ctx->img2_ci[i].channeltype];

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
		// Convert the background color to a linear colorspace (in-place).
		for(i=0;i<3;i++) {
			ctx->img1_bkgd_label.c[i] = x_to_linear_sample(ctx->img1_bkgd_label.c[i],&ctx->img1cs);
		}
	}

	if(ctx->apply_bkgd) {
		prepare_apply_bkgd(ctx);
	}

	if(ctx->resize_settings[IW_DIMENSION_H].family==IW_RESIZETYPE_AUTO) {
		iw_set_auto_resizetype(ctx,ctx->input_w,ctx->img2.width,IW_CHANNELTYPE_ALL,IW_DIMENSION_H);
	}
	if(ctx->resize_settings[IW_DIMENSION_V].family==IW_RESIZETYPE_AUTO) {
		iw_set_auto_resizetype(ctx,ctx->input_h,ctx->img2.height,IW_CHANNELTYPE_ALL,IW_DIMENSION_V);
	}

	iw_convert_density_info(ctx);

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

	ret = iw_prepare_processing(ctx,ctx->canvas_width,ctx->canvas_height);
	if(!ret) goto done;

	ret = iw_process_internal(ctx);
	if(!ret) goto done;

	iwpvt_optimize_image(ctx);

	retval = 1;
done:
	return retval;
}
