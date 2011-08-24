// imagew-resize.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// The low-level resizing functions.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "imagew-internals.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


typedef double (*iw_resamplefn_type)(struct iw_resize_settings *params, double x);

static IW_INLINE double iw_sinc(double x)
{
	if(x==0.0) return 1.0;
	return sin(M_PI*x)/(M_PI*x);
}

static double iw_filter_lanczos(struct iw_resize_settings *params, double x)
{
	if(x<0.0) x = -x;
	if(x<params->radius)
		return iw_sinc(x)*iw_sinc(x/params->radius);
	return 0.0;
}

static double iw_filter_hann(struct iw_resize_settings *params, double x)
{
	if(x<0.0) x = -x;
	if(x<params->radius)
		return iw_sinc(x)*(0.5*cos(M_PI*x/params->radius)+0.5);
	return 0.0;
}

static double iw_filter_blackman(struct iw_resize_settings *params, double x)
{
	if(x<0.0) x = -x;
	if(x<params->radius) {
		return iw_sinc(x) * (
			0.5*cos(M_PI*x/params->radius) +
			0.08*cos(2.0*M_PI*x/params->radius) +
			+0.42
			);
	}
	return 0.0;
}

static double iw_filter_sinc(struct iw_resize_settings *params, double x)
{
	if(x<0.0) x = -x;
	if(x<params->radius)
		return iw_sinc(x);
	return 0.0;
}

static double iw_filter_gaussian(struct iw_resize_settings *params, double x)
{
	//if(x>2.0 || x<=(-2.0)) return 0.0;

	// The 0.797 constant is 1.0/sqrt(0.5*M_PI)
	return exp(-2.0*x*x) * 0.79788456080286535587989;
}

static double iw_filter_triangle(struct iw_resize_settings *params, double x)
{
	if(x<0.0) x = -x;
	if(x<1.0) return 1.0-x;
	return 0.0;
}

static double iw_filter_quadratic(struct iw_resize_settings *params, double x)
{
	if(x<0.0) x = -x;
	if(x<0.5) return 0.75-x*x;
	if(x<1.5) return 0.50*(x-1.5)*(x-1.5);
	return 0.0;
}

// General cubic resampling based on Mitchell-Netravali definition.
// (radius=2)
static double iw_filter_generalcubic(struct iw_resize_settings *params, double x)
{
	double b = params->param1;
	double c = params->param2;
	if(x<0.0) x = -x;

	if(x<1.0) {
		return (
		  ( 12.0 -  9.0*b - 6.0*c) *x*x*x +
	      (-18.0 + 12.0*b + 6.0*c) *x*x +
		  (  6.0 -  2.0*b        ) )/6;
	}
	else if(x<2.0) {
		return (
		  (     -b -  6.0*c) *x*x*x +
		  (  6.0*b + 30.0*c) *x*x +
		  (-12.0*b - 48.0*c) *x +
		  (  8.0*b + 24.0*c) )/6;
	}
	return 0.0;
}

static double iw_filter_hermite(struct iw_resize_settings *params, double x)
{
	if(x<0.0) x = -x;

	if(x<1.0) {
		return 2.0*x*x*x -3.0*x*x +1.0;
	}
	return 0.0;
}

static double iw_filter_box(struct iw_resize_settings *params, double x)
{
	if(x > -0.5 && x <= 0.5)
		return 1.0;
	return 0.0;
}

static void weightlist_ensure_alloc(struct iw_context *ctx, int n)
{
	if(ctx->weightlist.alloc>=n) return;
	ctx->weightlist.alloc = n+32;
	if(ctx->weightlist.w) {
		ctx->weightlist.w = iw_realloc(ctx,ctx->weightlist.w,sizeof(struct iw_weight_struct)*ctx->weightlist.alloc);
	}
	else {
		ctx->weightlist.w = iw_malloc(ctx,sizeof(struct iw_weight_struct)*ctx->weightlist.alloc);
	}
	if(!ctx->weightlist.w) {
		ctx->weightlist.alloc = 0;
		ctx->weightlist.used = 0;
	}
}

void iwpvt_weightlist_free(struct iw_context *ctx)
{
	if(ctx->weightlist.w) {
		iw_free(ctx->weightlist.w);
		ctx->weightlist.w = NULL;
		ctx->weightlist.alloc = 0;
		ctx->weightlist.used = 0;
	}
}

static void iw_add_to_weightlist(struct iw_context *ctx, int src_pix, int dst_pix, double v)
{
	if(v==0.0) return;
	if(ctx->weightlist.used>=ctx->weightlist.alloc) {
		weightlist_ensure_alloc(ctx,ctx->weightlist.used+1);
		if(!ctx->weightlist.w) return;
	}
	ctx->weightlist.w[ctx->weightlist.used].src_pix = src_pix;
	ctx->weightlist.w[ctx->weightlist.used].dst_pix = dst_pix;
	ctx->weightlist.w[ctx->weightlist.used].weight = v;
	ctx->weightlist.used++;
}

static void iw_resample_row_create_weightlist(struct iw_context *ctx, iw_resamplefn_type rfn,
 struct iw_resize_settings *params)
{
	int out_pix;
	double reduction_factor;
	double out_pix_center;
	double pos_in_inpix;
	int input_pixel;
	int first_input_pixel;
	int last_input_pixel;
	int pix_to_read;
	double v;
	double v_sum;
	int v_count;
	int start_weight_idx;
	int est_nweights;
	int i;

	ctx->weightlist.used = 0;
	ctx->weightlist.isvalid = 1;

	if(ctx->num_out_pix<ctx->num_in_pix) {
		reduction_factor = ((double)ctx->num_in_pix) / ctx->num_out_pix;
	}
	else {
		reduction_factor = 1.0;
	}
	reduction_factor *= params->blur_factor;

	// Estimate the size of the weight list we'll need.
	est_nweights = (int)(2.0*params->radius*reduction_factor*ctx->num_out_pix);
	weightlist_ensure_alloc(ctx,est_nweights);
	if(!ctx->weightlist.w) {
		return;
	}

	for(out_pix=0;out_pix<ctx->num_out_pix;out_pix++) {

		out_pix_center = (0.5+(double)out_pix-ctx->cur_offset)/(double)ctx->num_out_pix;
		pos_in_inpix = out_pix_center*(double)ctx->num_in_pix -0.5;

		// There are up to radius*reduction_factor input pixels input pixels on
		// each side of the output pixel that we need to look at.

		first_input_pixel = (int)ceil(pos_in_inpix - params->radius*reduction_factor);
		last_input_pixel = (int)floor(pos_in_inpix + params->radius*reduction_factor);

		// Remember which item in the weightlist was the first one for this
		// target sample.
		start_weight_idx = ctx->weightlist.used;

		v_sum=0.0;
		v_count=0;
		for(input_pixel=first_input_pixel;input_pixel<=last_input_pixel;input_pixel++) {
			if(ctx->edge_policy==IW_EDGE_POLICY_STANDARD) {
				// Try to avoid using "virtual pixels".
				if(input_pixel<0 || input_pixel>=ctx->num_in_pix) {
					continue;
				}
			}

			v = (*rfn)(params, (((double)input_pixel)-pos_in_inpix)/reduction_factor);
			v_sum += v;
			v_count++;
			if(input_pixel<0) pix_to_read=0;
			else if(input_pixel>ctx->num_in_pix-1) pix_to_read = ctx->num_in_pix-1;
			else pix_to_read = input_pixel;

			iw_add_to_weightlist(ctx,pix_to_read,out_pix,v);
		}

		if(v_count>0) {

			if(v_sum!=0.0) {
				// Normalize the weights we just added to the list.
				for(i=start_weight_idx;i<ctx->weightlist.used;i++) {
					ctx->weightlist.w[i].weight /= v_sum;
				}

			}
			else {
				// This sum *should* never get very small (or negative).
				// If it does, apparently the input samples aren't
				// contributing in a meaningful way to the output sample
				// (or maybe the programmer doesn't really understand the
				// situation...).
				// It could happen if we used a *really* bad resample filter.
				// It could happen if a channel offset is used and we used
				// the "Standard" edge policy (but we don't allow that).
				// This code path is here to avoid dividing by zero,
				// not to produce a correct sample value.
				for(i=start_weight_idx;i<ctx->weightlist.used;i++) {
					ctx->weightlist.w[i].weight = 0.0;
				}
			}
		}
		else {
			// No usable input samples were found. Just copy one of the edge samples.
			if(first_input_pixel<0)
				pix_to_read = 0;
			else
				pix_to_read = ctx->num_in_pix-1;
			iw_add_to_weightlist(ctx,pix_to_read,out_pix,1.0);
		}
	}
}

 static void iw_resample_row(struct iw_context *ctx)
{
	int i;
	struct iw_weight_struct *w;

	if(!ctx->weightlist.w) return;

	for(i=0;i<ctx->num_out_pix;i++) {
		ctx->out_pix[i] = 0.0;
	}

	for(i=0;i<ctx->weightlist.used;i++) {
		w = &ctx->weightlist.w[i];
		ctx->out_pix[w->dst_pix] += ctx->in_pix[w->src_pix] * w->weight;
	}
}
	
static void iw_resize_row_nearestneighbor(struct iw_context *ctx, double offset)
{
	int out_pix;
	double out_pix_center;
	int input_pixel;
	int pix_to_read;

	for(out_pix=0;out_pix<ctx->num_out_pix;out_pix++) {

		out_pix_center = (0.5+(double)out_pix-offset)/(double)ctx->num_out_pix;
		input_pixel = (int)floor(out_pix_center*(double)ctx->num_in_pix);

		if(input_pixel<0) pix_to_read=0;
		else if(input_pixel>ctx->num_in_pix-1) pix_to_read = ctx->num_in_pix-1;
		else pix_to_read = input_pixel;
		ctx->out_pix[out_pix] = ctx->in_pix[pix_to_read];
	}
}

static void iw_pixmix_create_weightlist(struct iw_context *ctx)
{
	int cur_in_pix, cur_out_pix;
	int safe_in_pix;
	double in_pix_right_pos;
	double out_pix_right_pos;
	double cur_pos;
	int est_nweights;

	ctx->weightlist.used = 0;
	ctx->weightlist.isvalid = 1;
	est_nweights = ctx->num_out_pix + ctx->num_in_pix;
	weightlist_ensure_alloc(ctx,est_nweights);
	if(!ctx->weightlist.w) {
		return;
	}

	cur_in_pix = 0;
	cur_out_pix = 0;

	cur_pos = (-ctx->cur_offset)/(double)ctx->num_out_pix;
	out_pix_right_pos = (-ctx->cur_offset+1.0)/(double)ctx->num_out_pix;

	in_pix_right_pos = ((double)(cur_in_pix+1))/(double)ctx->num_in_pix;
	while(in_pix_right_pos<cur_pos) {
		// Skip over any initial input pixels that don't overlap any output pixels.
		cur_in_pix++;
		in_pix_right_pos = ((double)(cur_in_pix+1))/(double)ctx->num_in_pix;
	}

	safe_in_pix=cur_in_pix;
	if(safe_in_pix>ctx->num_in_pix-1) safe_in_pix=ctx->num_in_pix-1;

	// Simultaneously scan through the input and output pixels, stopping
	// at the next pixel border for either of them.

	while(cur_out_pix<ctx->num_out_pix) {
		// Figure out whether the next pixel border is of an input pixel, or an
		// output pixel. The code to handle these cases is similar, but not
		// identical.

		if(in_pix_right_pos<=out_pix_right_pos) {
			// Next border is of an input pixel.
			// Put remainder of this pixel into the output pixel.

			iw_add_to_weightlist(ctx,safe_in_pix,cur_out_pix, (in_pix_right_pos - cur_pos)*ctx->num_out_pix );

			// Advance to the next input pixel.
			// Advance cur_pos to what will be the left edge of the next input pixel.
			cur_in_pix++;
			safe_in_pix=cur_in_pix;
			if(safe_in_pix>ctx->num_in_pix-1) safe_in_pix=ctx->num_in_pix-1;

			cur_pos = in_pix_right_pos;
			in_pix_right_pos = ((double)(cur_in_pix+1))/(double)ctx->num_in_pix;
		}
		else {
			// Next border is of an output pixel.
			// Put part of the input pixel into the output pixel.

			iw_add_to_weightlist(ctx,safe_in_pix,cur_out_pix, (out_pix_right_pos - cur_pos)*ctx->num_out_pix );

			// Advance to the next output pixel.
			// Advance cur_pos to what will be the left edge of the next output pixel.
			cur_out_pix++;
			cur_pos = out_pix_right_pos;
			out_pix_right_pos = (-ctx->cur_offset+(double)(cur_out_pix+1))/(double)ctx->num_out_pix;
		}
	}
}

// Caution: Does not support offsets.
static void resize_row_null(struct iw_context *ctx)
{
	int i;
	for(i=0;i<ctx->num_out_pix;i++) {
		if(i<ctx->num_in_pix) {
			ctx->out_pix[i] = ctx->in_pix[i];
		}
		else {
			ctx->out_pix[i] = 0.0;
		}
	}
}

void iwpvt_resize_row_precalculate(struct iw_context *ctx, struct iw_resize_settings *rs, int channeltype)
{
	// Set ctx->cur_offset, to be used as the offset until this function is
	// called again.
	if(ctx->offset_color_channels && channeltype>=0 && channeltype<=2)
		ctx->cur_offset=rs->channel_offset[channeltype];
	else
		ctx->cur_offset=0.0;

	if(rs->family<IW_FIRST_PRECALC_FILTER) return; // This algorithm doesn't precalculate weights.

	switch(rs->family) {
	case IW_RESIZETYPE_MIX:
		iw_pixmix_create_weightlist(ctx);
		break;
	case IW_RESIZETYPE_HERMITE:
		iw_resample_row_create_weightlist(ctx,iw_filter_hermite,rs);
		break;
	case IW_RESIZETYPE_CUBIC:
		iw_resample_row_create_weightlist(ctx,iw_filter_generalcubic,rs);
		break;
	case IW_RESIZETYPE_LANCZOS:
		iw_resample_row_create_weightlist(ctx,iw_filter_lanczos,rs);
		break;
	case IW_RESIZETYPE_HANNING:
		iw_resample_row_create_weightlist(ctx,iw_filter_hann,rs);
		break;
	case IW_RESIZETYPE_BLACKMAN:
		iw_resample_row_create_weightlist(ctx,iw_filter_blackman,rs);
		break;
	case IW_RESIZETYPE_SINC:
		iw_resample_row_create_weightlist(ctx,iw_filter_sinc,rs);
		break;
	case IW_RESIZETYPE_GAUSSIAN:
		iw_resample_row_create_weightlist(ctx,iw_filter_gaussian,rs);
		break;
	case IW_RESIZETYPE_LINEAR:
		iw_resample_row_create_weightlist(ctx,iw_filter_triangle,rs);
		break;
	case IW_RESIZETYPE_QUADRATIC:
		iw_resample_row_create_weightlist(ctx,iw_filter_quadratic,rs);
		break;
	case IW_RESIZETYPE_BOX:
		iw_resample_row_create_weightlist(ctx,iw_filter_box,rs);
		break;
	}
}

void iwpvt_resize_row_main(struct iw_context *ctx, struct iw_resize_settings *rs, int dimension)
{
	int i;
	int clamp;

	if(rs->family>=IW_FIRST_PRECALC_FILTER) {
		iw_resample_row(ctx);
		goto resizedone;
	}

	switch(rs->family) {
	case IW_RESIZETYPE_NEAREST:
		iw_resize_row_nearestneighbor(ctx,ctx->cur_offset);
		break;
	case IW_RESIZETYPE_NULL:
		resize_row_null(ctx);
		break;
	default:
		iw_resize_row_nearestneighbor(ctx,ctx->cur_offset);
		break;
	}

resizedone:
	// The horizontal dimension is always resized last.
	// After it's done, we can always clamp the pixels to the [0.0,1.0] range,
	// unless the output format supports floating point.
	// If the intclamp flag is set, we clamp after every stage.

	if(ctx->img2.sampletype==IW_SAMPLETYPE_FLOATINGPOINT) {
		clamp = ctx->intclamp;
	}
	else {
		clamp = (ctx->intclamp || dimension==IW_DIMENSION_H);
	}

	if(clamp) {
		for(i=0;i<ctx->num_out_pix;i++) {
			if(ctx->out_pix[i]<0.0) ctx->out_pix[i]=0.0;
			else if(ctx->out_pix[i]>1.0) ctx->out_pix[i]=1.0;
		}
	}
}
