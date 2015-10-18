// imagew-resize.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// The low-level resizing functions.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "imagew-internals.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef void (*iw_resizerowfn_type)(struct iw_rr_ctx *rrctx);
typedef double (*iw_filterfn_type)(struct iw_rr_ctx *rrctx, double x);

struct iw_weight_struct {
	int src_pix; // -1 means to use a virtual pixel
	int dst_pix;
	double weight;
};

struct iw_rr_ctx {
	struct iw_context *ctx;

	int num_in_pix;
	int num_out_pix;
	iw_tmpsample *in_pix; // A single row of source samples to resample.
	iw_tmpsample *out_pix; // The resulting resampled row.

	// int family; // Oddly, we don't need this field at all.
	double radius; // (Does not take .blur_factor into account.)
	double cubic_b;
	double cubic_c;
	double mix_param;

	double blur_factor;
	double out_true_size;
	double offset;
	int edge_policy;
	double edge_sample_value;

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


static double iw_sinc(double x)
{
	if(x<=0.000000005) return 1.0;
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

// Gaussian filter, evaluated out to 2.0 (4*sigma).
static double iw_filter_gaussian(struct iw_rr_ctx *rrctx, double x)
{
	double value;

	if(x>=2.0) return 0.0;
	// The 0.797 constant is 1.0/sqrt(0.5*M_PI)
	value = exp(-2.0*x*x) * 0.79788456080286535587989;
	if(x<=1.999) return value;

	// At 2.0, the filter's value is about 0.00026766, which is visually
	// insignificant, but big enough to change some pixel values. The
	// discontinuity causes indeterminism when evaluating values very near 2.0.
	// To fix this, we'll make the filter continuous, by applying a simple
	// windowing function when the coordinate is near 2.0.
	return 1000.0*(2.0-x)*value;
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

static double iw_filter_boxavg(struct iw_rr_ctx *rrctx, double x)
{
	if(x<0.4999999)
		return 1.0;
	else if(x<=0.5000001) {
		return 0.5;
	}
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
	size_t old_alloc;

	if(rrctx->wl_alloc>=n) return;
	old_alloc = rrctx->wl_alloc;
	rrctx->wl_alloc = n+32;

	// Note that rrctx->wl may be NULL, which iw_realloc() allows.
	rrctx->wl = iw_realloc(rrctx->ctx,rrctx->wl,
		sizeof(struct iw_weight_struct)*old_alloc,
		sizeof(struct iw_weight_struct)*rrctx->wl_alloc);

	if(!rrctx->wl) {
		rrctx->wl_alloc = 0;
		rrctx->wl_used = 0;
	}
}

static void weightlist_free(struct iw_rr_ctx *rrctx)
{
	if(rrctx->wl) {
		iw_free(rrctx->ctx,rrctx->wl);
		rrctx->wl = NULL;
		rrctx->wl_alloc = 0;
		rrctx->wl_used = 0;
	}
}

static void weightlist_add_weight(struct iw_rr_ctx *rrctx, int src_pix, int dst_pix, double v)
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

// If the filter is symmetric, return the absolute value of pos.
// (For convenience, we don't call such filters with negative numbers.)
// Otherwise, return pos.
static double fixup_pos(struct iw_rr_ctx *rrctx, double pos)
{
	if(rrctx->family_flags & IW_FFF_ASYMMETRIC) {
		return pos;
	}
	return (pos<0) ? -pos : pos;
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

	if(rrctx->out_true_size<(double)rrctx->num_in_pix) {
		reduction_factor = ((double)rrctx->num_in_pix) / rrctx->out_true_size;
	}
	else {
		reduction_factor = 1.0;
	}
	reduction_factor *= rrctx->blur_factor;

	// Estimate the size of the weight list we'll need.
	est_nweights = (int)(2.0*rrctx->radius*reduction_factor*rrctx->num_out_pix);
	weightlist_ensure_alloc(rrctx,est_nweights);
	if(!rrctx->wl) {
		return;
	}

	for(out_pix=0;out_pix<rrctx->num_out_pix;out_pix++) {
		out_pix_center = (0.5+(double)out_pix-rrctx->offset)/rrctx->out_true_size;
		pos_in_inpix = out_pix_center*(double)rrctx->num_in_pix -0.5;

		// There are up to radius*reduction_factor source pixels on each side
		// of the target pixel that we need to look at.

		first_input_pixel = (int)ceil(pos_in_inpix - rrctx->radius*reduction_factor -0.0001);
		last_input_pixel = (int)floor(pos_in_inpix + rrctx->radius*reduction_factor +0.0001);

		// Remember which item in the weightlist was the first one for this
		// target sample.
		start_weight_idx = rrctx->wl_used;

		v_sum=0.0;
		v_count=0;
		for(input_pixel=first_input_pixel;input_pixel<=last_input_pixel;input_pixel++) {
			if(rrctx->edge_policy==IW_EDGE_POLICY_STANDARD) {
				// The STANDARD method doesn't use virtual pixels, so we can
				// ignore out-of-range source pixels.
				if(input_pixel<0 || input_pixel>=rrctx->num_in_pix) {
					continue;
				}
			}

			pos = (((double)input_pixel)-pos_in_inpix)/reduction_factor;
			v = (*rrctx->filter_fn)(rrctx, fixup_pos(rrctx,pos));
			if(v==0.0) continue;

			v_sum += v;
			v_count++;

			if(input_pixel<0 || input_pixel>rrctx->num_in_pix-1) {
				// The source pixel we need doesn't exist.
				if(rrctx->edge_policy==IW_EDGE_POLICY_TRANSPARENT) {
					pix_to_read = -1; // Use a virtual pixel
				}
				else { // Assume IW_EDGE_POLICY_REPLICATE
					if(input_pixel<0) pix_to_read=0;
					else pix_to_read = rrctx->num_in_pix-1;
				}
			}
			else {
				pix_to_read = input_pixel;
			}

			weightlist_add_weight(rrctx,pix_to_read,out_pix,v);
		}

		if(v_count>0) {

			if(v_sum!=0.0) {
				// Normalize the weights we just added to the list.
				for(i=start_weight_idx;i<rrctx->wl_used;i++) {
					rrctx->wl[i].weight /= v_sum;
				}
			}
			else {
				// Just in case we somehow have a nonzero number of weights
				// which sum to zero, set them all to zero instead of trying
				// to normalize.
				// This isn't really a meaningful thing to do, but at least
				// it's predictable, and keeps us from dividing by zero.
				for(i=start_weight_idx;i<rrctx->wl_used;i++) {
					rrctx->wl[i].weight = 0.0;
				}
			}
		}
	}
}

static void iw_resize_row_std(struct iw_rr_ctx *rrctx)
{
	int i;
	struct iw_weight_struct *w;

	if(!rrctx->wl) return;

	for(i=0;i<rrctx->num_out_pix;i++) {
		rrctx->out_pix[i] = 0.0;
	}

	for(i=0;i<rrctx->wl_used;i++) {
		w = &rrctx->wl[i];
		if(w->src_pix>=0) {
			rrctx->out_pix[w->dst_pix] += rrctx->in_pix[w->src_pix] * w->weight;
		}
		else {
			// Use a virtual pixel. The only relevant virtual pixel type is
			// TRANSPARENT (REPLICATE is handled elsewhere).
			// The value to use was previously calculated and stored in
			// ->edge_sample_value (it's almost always 0, i.e. "transparent
			// black").
			rrctx->out_pix[w->dst_pix] += rrctx->edge_sample_value * w->weight;
		}
	}
}

// Although "nearest neighbor" can be implemented using the standard method
// that uses a weightlist, we use a special algorithm for it. For one thing,
// this ensures that it does literally use the nearest neighbor, and is not
// affected by blur settings.
static void iw_resize_row_nearest(struct iw_rr_ctx *rrctx)
{
	int out_pix;
	double out_pix_center;
	int input_pixel;
	int pix_to_read;

	for(out_pix=0;out_pix<rrctx->num_out_pix;out_pix++) {
		out_pix_center = (0.5+(double)out_pix-rrctx->offset)/(double)rrctx->num_out_pix;
		input_pixel = (int)floor(out_pix_center*(double)rrctx->num_in_pix);

		if(input_pixel<0) pix_to_read=0;
		else if(input_pixel>rrctx->num_in_pix-1) pix_to_read = rrctx->num_in_pix-1;
		else pix_to_read = input_pixel;
		rrctx->out_pix[out_pix] = rrctx->in_pix[pix_to_read];
	}
}

// A fast algorithm that just copies the pixels, and does no resizing.
// If the target size is smaller than the source size, pixels will be cropped.
// If it is larger, the extra pixels will be black or transparent.
// Caution: Does not support translation or offsets.
static void iw_resize_row_null(struct iw_rr_ctx *rrctx)
{
	int i;
	for(i=0;i<rrctx->num_out_pix;i++) {
		if(i<rrctx->num_in_pix) {
			rrctx->out_pix[i] =rrctx->in_pix[i];
		}
		else {
			rrctx->out_pix[i] = 0.0;
		}
	}
}

struct iw_rr_ctx *iwpvt_resize_rows_init(struct iw_context *ctx,
  struct iw_resize_settings *rs, int channeltype,
	  int num_in_pix, int num_out_pix)
{
	struct iw_rr_ctx *rrctx = NULL;

	rrctx = iw_mallocz(ctx,sizeof(struct iw_rr_ctx));
	if(!rrctx) goto done;

	// rrctx stores the internal settings we'll use to resize (the current
	// dimension of) the image.
	// The settings will be copied/translated from the 'rs' struct, and other
	// places.

	rrctx->ctx = ctx;
	//rrctx->family = rs->family
	rrctx->resizerow_fn = iw_resize_row_std;  // Initial default

	rrctx->num_in_pix = num_in_pix;
	rrctx->num_out_pix = num_out_pix;
	rrctx->out_true_size = rs->out_true_size;

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
		// Precalculate a parameter (mix_param) that will be used by
		// iw_filter_mix(). It's also used to compute the radius.
		rrctx->mix_param = ((double)rrctx->num_out_pix)/rrctx->num_in_pix;
		if(rrctx->mix_param > 1.0) rrctx->mix_param = 1.0/rrctx->mix_param;
		rrctx->radius = 0.5 + rrctx->mix_param;
		break;
	case IW_RESIZETYPE_BOX:
		rrctx->filter_fn = iw_filter_box;
		rrctx->family_flags = IW_FFF_STANDARD|IW_FFF_BOXFILTERHACK;
		rrctx->radius = 0.501;
		break;
	case IW_RESIZETYPE_BOXAVG:
		rrctx->filter_fn = iw_filter_boxavg;
		rrctx->family_flags = IW_FFF_STANDARD;
		rrctx->radius = 0.501;
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
		if(rrctx->cubic_b < -100.0) rrctx->cubic_b= -100.0;
		if(rrctx->cubic_b >  100.0) rrctx->cubic_b=  100.0;
		if(rrctx->cubic_c < -100.0) rrctx->cubic_c= -100.0;
		if(rrctx->cubic_c >  100.0) rrctx->cubic_c=  100.0;
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
		if(rrctx->radius>100.0) rrctx->radius=100.0;
	}

	rrctx->edge_policy = rs->edge_policy;

	// Record the sample value that may be used for virtual pixels.
	rrctx->edge_sample_value = 0.0;
	if(rrctx->edge_policy==IW_EDGE_POLICY_TRANSPARENT && ctx->apply_bkgd &&
	   ctx->apply_bkgd_strategy==IW_BKGD_STRATEGY_EARLY)
	{
		rrctx->edge_sample_value = ctx->intermed_ci[channeltype].bkgd_color_lin;
	}

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

void iwpvt_resize_rows_done(struct iw_rr_ctx *rrctx)
{
	if(!rrctx) return;
	weightlist_free(rrctx);
	iw_free(rrctx->ctx,rrctx);
}

void iwpvt_resize_row_main(struct iw_rr_ctx *rrctx, iw_tmpsample *in_pix, iw_tmpsample *out_pix)
{
	if(!rrctx || !rrctx->resizerow_fn) return;
	rrctx->in_pix = in_pix;
	rrctx->out_pix = out_pix;
	(*rrctx->resizerow_fn)(rrctx);
}
