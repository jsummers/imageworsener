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

typedef void (*iw_resizerowfn_type)(struct iw_context *ctx, struct iw_rr_ctx *rrctx);
typedef double (*iw_filterfn_type)(struct iw_rr_ctx *rrctx, double x);

struct iw_weight_struct {
	int src_pix;
	int dst_pix;
	double weight;
};

struct iw_rr_ctx {
	struct iw_context *ctx;

	// int family; // Oddly, we don't need this field at all.
	double radius; // (Does not take .blur_factor into account.)
	double cubic_b;
	double cubic_c;
	double mix_param;

	double blur_factor;
	double offset;
	int edge_policy;

	iw_resizerowfn_type resizerow_fn;
	iw_filterfn_type filter_fn;
#define IW_FFF_STANDARD   0x01 // A filter that uses iw_create_weightlist_std()
#define IW_FFF_ASYMMETRIC 0x02 // Currently unused.
#define IW_FFF_SINCBASED  0x04
#define IW_FFF_BOXFILTERHACK 0x08
	unsigned int family_flags; // Misc. information about the filter family

	struct iw_weight_struct *wl; // weightlist
	int wl_used;
	int wl_alloc;
};


static IW_INLINE double iw_sinc(double x)
{
	if(x==0.0) return 1.0;
	return sin(M_PI*x)/(M_PI*x);
}

static double iw_filter_lanczos(struct iw_rr_ctx *rrctx, double x)
{
	if(x<rrctx->radius)
		return iw_sinc(x)*iw_sinc(x/rrctx->radius);
	return 0.0;
}

static double iw_filter_hann(struct iw_rr_ctx *rrctx, double x)
{
	if(x<rrctx->radius)
		return iw_sinc(x)*(0.5*cos(M_PI*x/rrctx->radius)+0.5);
	return 0.0;
}

static double iw_filter_blackman(struct iw_rr_ctx *rrctx, double x)
{
	if(x<rrctx->radius) {
		return iw_sinc(x) * (
			0.5*cos(M_PI*x/rrctx->radius) +
			0.08*cos(2.0*M_PI*x/rrctx->radius) +
			0.42 );
	}
	return 0.0;
}

static double iw_filter_sinc(struct iw_rr_ctx *rrctx, double x)
{
	if(x<rrctx->radius)
		return iw_sinc(x);
	return 0.0;
}

static double iw_filter_gaussian(struct iw_rr_ctx *rrctx, double x)
{
	// The 0.797 constant is 1.0/sqrt(0.5*M_PI)
	return exp(-2.0*x*x) * 0.79788456080286535587989;
}

static double iw_filter_triangle(struct iw_rr_ctx *rrctx, double x)
{
	if(x<1.0) return 1.0-x;
	return 0.0;
}

static double iw_filter_quadratic(struct iw_rr_ctx *rrctx, double x)
{
	if(x<0.5) return 0.75-x*x;
	if(x<1.5) return 0.50*(x-1.5)*(x-1.5);
	return 0.0;
}

// General cubic resampling based on Mitchell-Netravali definition.
// (radius=2)
static double iw_filter_cubic(struct iw_rr_ctx *rrctx, double x)
{
	double b = rrctx->cubic_b;
	double c = rrctx->cubic_c;

	if(x<1.0) {
		return (
		  ( 12.0 -  9.0*b - 6.0*c) *x*x*x +
		  (-18.0 + 12.0*b + 6.0*c) *x*x +
		  (  6.0 -  2.0*b        ) )/6.0;
	}
	else if(x<2.0) {
		return (
		  (     -b -  6.0*c) *x*x*x +
		  (  6.0*b + 30.0*c) *x*x +
		  (-12.0*b - 48.0*c) *x +
		  (  8.0*b + 24.0*c) )/6.0;
	}
	return 0.0;
}

static double iw_filter_hermite(struct iw_rr_ctx *rrctx, double x)
{
	if(x<1.0) {
		return 2.0*x*x*x -3.0*x*x +1.0;
	}
	return 0.0;
}

static double iw_filter_box(struct iw_rr_ctx *rrctx, double x)
{
	if(x<=0.5)
		return 1.0;
	return 0.0;
}

static double iw_filter_mix(struct iw_rr_ctx *rrctx, double x)
{
	if(x < (0.5-rrctx->mix_param/2.0))
		return 1.0;
	if(x < (0.5+rrctx->mix_param/2.0))
		return 0.5-(x-0.5)/rrctx->mix_param;
	return 0.0;
}

static void weightlist_ensure_alloc(struct iw_rr_ctx *rrctx, int n)
{
	if(rrctx->wl_alloc>=n) return;
	rrctx->wl_alloc = n+32;
	if(rrctx->wl) {
		rrctx->wl = iw_realloc(rrctx->ctx,rrctx->wl,sizeof(struct iw_weight_struct)*rrctx->wl_alloc);
	}
	else {
		rrctx->wl = iw_malloc(rrctx->ctx,sizeof(struct iw_weight_struct)*rrctx->wl_alloc);
	}
	if(!rrctx->wl) {
		rrctx->wl_alloc = 0;
		rrctx->wl_used = 0;
	}
}

static void iw_weightlist_free(struct iw_rr_ctx *rrctx)
{
	if(rrctx->wl) {
		iw_free(rrctx->wl);
		rrctx->wl = NULL;
		rrctx->wl_alloc = 0;
		rrctx->wl_used = 0;
	}
}

static void iw_add_to_weightlist(struct iw_rr_ctx *rrctx, int src_pix, int dst_pix, double v)
{
	if(v==0.0) return;
	if(rrctx->wl_used>=rrctx->wl_alloc) {
		weightlist_ensure_alloc(rrctx,rrctx->wl_used+1);
		if(!rrctx->wl) return;
	}
	rrctx->wl[rrctx->wl_used].src_pix = src_pix;
	rrctx->wl[rrctx->wl_used].dst_pix = dst_pix;
	rrctx->wl[rrctx->wl_used].weight = v;
	rrctx->wl_used++;
}

static void iw_create_weightlist_std(struct iw_context *ctx, struct iw_rr_ctx *rrctx)
{
	int out_pix;
	double reduction_factor;
	double out_pix_center;
	double pos_in_inpix;
	double pos;
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

	rrctx->wl_used = 0;

	if(ctx->num_out_pix<ctx->num_in_pix) {
		reduction_factor = ((double)ctx->num_in_pix) / ctx->num_out_pix;
	}
	else {
		reduction_factor = 1.0;
	}
	reduction_factor *= rrctx->blur_factor;

	// Estimate the size of the weight list we'll need.
	est_nweights = (int)(2.0*rrctx->radius*reduction_factor*ctx->num_out_pix);
	weightlist_ensure_alloc(rrctx,est_nweights);
	if(!rrctx->wl) {
		return;
	}

	for(out_pix=0;out_pix<ctx->num_out_pix;out_pix++) {
		out_pix_center = (0.5+(double)out_pix-rrctx->offset)/(double)ctx->num_out_pix;
		pos_in_inpix = out_pix_center*(double)ctx->num_in_pix -0.5;

		// There are up to radius*reduction_factor input pixels input pixels on
		// each side of the output pixel that we need to look at.

		first_input_pixel = (int)ceil(pos_in_inpix - rrctx->radius*reduction_factor);
		last_input_pixel = (int)floor(pos_in_inpix + rrctx->radius*reduction_factor);

		// Remember which item in the weightlist was the first one for this
		// target sample.
		start_weight_idx = rrctx->wl_used;

		v_sum=0.0;
		v_count=0;
		for(input_pixel=first_input_pixel;input_pixel<=last_input_pixel;input_pixel++) {
			if(rrctx->edge_policy==IW_EDGE_POLICY_STANDARD) {
			// Try to avoid using "virtual pixels".
				if(input_pixel<0 || input_pixel>=ctx->num_in_pix) {
					continue;
				}
			}

			pos = (((double)input_pixel)-pos_in_inpix)/reduction_factor;
			if(!(rrctx->family_flags & IW_FFF_ASYMMETRIC)) {
				// If the filter is symmetric, then for convenience, don't call it with
				// negative numbers.
				if(pos<0.0)
					pos = -pos;
			}
			v = (*rrctx->filter_fn)(rrctx, pos);
			if(v==0.0) continue;

			v_sum += v;
			v_count++;
			if(input_pixel<0) pix_to_read=0;
			else if(input_pixel>ctx->num_in_pix-1) pix_to_read = ctx->num_in_pix-1;
			else pix_to_read = input_pixel;

			iw_add_to_weightlist(rrctx,pix_to_read,out_pix,v);
		}

		if(v_count>0) {

			if(v_sum!=0.0) {
				// Normalize the weights we just added to the list.
				for(i=start_weight_idx;i<rrctx->wl_used;i++) {
					rrctx->wl[i].weight /= v_sum;
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
				for(i=start_weight_idx;i<rrctx->wl_used;i++) {
					rrctx->wl[i].weight = 0.0;
				}
			}
		}
	}
}

 static void iw_resize_row_std(struct iw_context *ctx, struct iw_rr_ctx *rrctx)
{
	int i;
	struct iw_weight_struct *w;

	if(!rrctx->wl) return;

	for(i=0;i<ctx->num_out_pix;i++) {
		ctx->out_pix[i] = 0.0;
	}

	for(i=0;i<rrctx->wl_used;i++) {
		w = &rrctx->wl[i];
		ctx->out_pix[w->dst_pix] += ctx->in_pix[w->src_pix] * w->weight;
	}
}

static void iw_resize_row_nearest(struct iw_context *ctx, struct iw_rr_ctx *rrctx)
{
	int out_pix;
	double out_pix_center;
	int input_pixel;
	int pix_to_read;

	for(out_pix=0;out_pix<ctx->num_out_pix;out_pix++) {
		out_pix_center = (0.5+(double)out_pix-rrctx->offset)/(double)ctx->num_out_pix;
		input_pixel = (int)floor(out_pix_center*(double)ctx->num_in_pix);

		if(input_pixel<0) pix_to_read=0;
		else if(input_pixel>ctx->num_in_pix-1) pix_to_read = ctx->num_in_pix-1;
		else pix_to_read = input_pixel;
		ctx->out_pix[out_pix] = ctx->in_pix[pix_to_read];
	}
}

// Caution: Does not support offsets.
static void iw_resize_row_null(struct iw_context *ctx, struct iw_rr_ctx *rrctx)
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

struct iw_rr_ctx *iwpvt_resize_rows_init(struct iw_context *ctx,
  struct iw_resize_settings *rs, int channeltype)
{
	struct iw_rr_ctx *rrctx = NULL;

	rrctx = iw_malloc(ctx,sizeof(struct iw_rr_ctx));
	if(!rrctx) goto done;
	memset(rrctx,0,sizeof(struct iw_rr_ctx));

	// rrctx stores the internal settings we'll use to resize (the current
	// dimension of) the image.
	// The settings will be copied/translated from the 'rs' struct, and other
	// places.

	rrctx->ctx = ctx;
	//rrctx->family = rs->family
	rrctx->resizerow_fn = iw_resize_row_std;  // Initial default

	// Gather filter-specific information.
	switch(rs->family) {
	case IW_RESIZETYPE_NULL:
		rrctx->resizerow_fn = iw_resize_row_null;
		rrctx->family_flags = 0;
		break;
	case IW_RESIZETYPE_NEAREST:
		rrctx->resizerow_fn = iw_resize_row_nearest;
		rrctx->family_flags = IW_FFF_BOXFILTERHACK;
		break;
	case IW_RESIZETYPE_MIX:
		rrctx->filter_fn = iw_filter_mix;
		rrctx->family_flags = IW_FFF_STANDARD;
		// Pixel mixing is implemented using a trapezoid-shaped filter
		// whose exact shape depends on the scale factor.
		rrctx->mix_param = ((double)ctx->num_out_pix)/ctx->num_in_pix;
		if(rrctx->mix_param > 1.0) rrctx->mix_param = 1.0/rrctx->mix_param;
		rrctx->radius = 0.5 + rrctx->mix_param;
		break;
	case IW_RESIZETYPE_BOX:
		rrctx->filter_fn = iw_filter_box;
		rrctx->family_flags = IW_FFF_STANDARD|IW_FFF_BOXFILTERHACK;
		rrctx->radius = 1.0;
		break;
	case IW_RESIZETYPE_TRIANGLE:
		rrctx->filter_fn = iw_filter_triangle;
		rrctx->family_flags = IW_FFF_STANDARD;
		rrctx->radius = 1.0;
		break;
	case IW_RESIZETYPE_QUADRATIC:
		rrctx->filter_fn = iw_filter_quadratic;
		rrctx->family_flags = IW_FFF_STANDARD;
		rrctx->radius = 1.5;
		break;
	case IW_RESIZETYPE_GAUSSIAN:
		rrctx->filter_fn = iw_filter_gaussian;
		rrctx->family_flags = IW_FFF_STANDARD;
		rrctx->radius = 2.0; // = 4*sigma
		break;
	case IW_RESIZETYPE_HERMITE:
		rrctx->filter_fn = iw_filter_hermite;
		rrctx->family_flags = IW_FFF_STANDARD;
		rrctx->radius = 1.0;
		break;
	case IW_RESIZETYPE_CUBIC:
		rrctx->filter_fn = iw_filter_cubic;
		rrctx->family_flags = IW_FFF_STANDARD;
		rrctx->radius = 2.0;
		rrctx->cubic_b = rs->param1;
		rrctx->cubic_c = rs->param2;
		if(rrctx->cubic_b < -10.0) rrctx->cubic_b= -10.0;
		if(rrctx->cubic_b >  10.0) rrctx->cubic_b=  10.0;
		if(rrctx->cubic_c < -10.0) rrctx->cubic_c= -10.0;
		if(rrctx->cubic_c >  10.0) rrctx->cubic_c=  10.0;
		break;
	case IW_RESIZETYPE_LANCZOS:
		rrctx->filter_fn = iw_filter_lanczos;
		rrctx->family_flags = IW_FFF_STANDARD|IW_FFF_SINCBASED;
		break;
	case IW_RESIZETYPE_HANNING:
		rrctx->filter_fn = iw_filter_hann;
		rrctx->family_flags = IW_FFF_STANDARD|IW_FFF_SINCBASED;
		break;
	case IW_RESIZETYPE_BLACKMAN:
		rrctx->filter_fn = iw_filter_blackman;
		rrctx->family_flags = IW_FFF_STANDARD|IW_FFF_SINCBASED;
		break;
	case IW_RESIZETYPE_SINC:
		rrctx->filter_fn = iw_filter_sinc;
		rrctx->family_flags = IW_FFF_STANDARD|IW_FFF_SINCBASED;
		break;
	default:
		rrctx->resizerow_fn = NULL;
		iw_set_error(ctx,"Internal: Unknown resize algorithm");
		goto done;
	}

	if(rrctx->family_flags & IW_FFF_SINCBASED) {
		rrctx->radius = floor(rs->param1+0.5); // "lobes"
		if(rrctx->radius<2.0) rrctx->radius=2.0;
		if(rrctx->radius>10.0) rrctx->radius=10.0;
	}

	rrctx->edge_policy = rs->edge_policy;

	rrctx->blur_factor = rs->blur_factor;
	if(rrctx->blur_factor<0.0001) rrctx->blur_factor=0.0001;
	if(rrctx->blur_factor>10000.0) rrctx->blur_factor=10000.0;

	rrctx->offset = rs->translate;
	if(rrctx->family_flags & IW_FFF_BOXFILTERHACK) {
		// This is a cheap way to avoid putting a pixel into more than one or less
		// than one "box": change the alignment just a bit, so that a pixel will
		// never be aligned exactly on the border between two boxes.
		rrctx->offset -= 0.00000000001;
	}
	if(rs->use_offset && channeltype>=0 && channeltype<=2)
		rrctx->offset += rs->channel_offset[channeltype];

	if(rrctx->family_flags & IW_FFF_STANDARD) {
		// This is a "standard" filter.
		iw_create_weightlist_std(ctx,rrctx);
		goto done;
	}

done:
	return rrctx;
}

void iwpvt_resize_rows_done(struct iw_context *ctx, struct iw_rr_ctx *rrctx)
{
	if(!rrctx) return;
	iw_weightlist_free(rrctx);
}

void iwpvt_resize_row_main(struct iw_context *ctx, struct iw_rr_ctx *rrctx)
{
	if(!rrctx || !rrctx->resizerow_fn) return;
	(*rrctx->resizerow_fn)(ctx,rrctx);
}
