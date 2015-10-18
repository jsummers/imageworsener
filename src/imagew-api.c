// imagew-api.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// Most of the functions declared in imagew.h are defined here.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "imagew-internals.h"

// Translate a string, using the given flags.
IW_IMPL(void) iw_translate(struct iw_context *ctx, unsigned int flags,
	char *dst, size_t dstlen, const char *src)
{
	int ret;

	dst[0]='\0';

	if(ctx && ctx->translate_fn) {
		ret = (*ctx->translate_fn)(ctx,flags,dst,dstlen,src);
	}
	else {
		ret = 0;
	}

	if(!ret) {
		// Not translated. Just copy the string.
		iw_strlcpy(dst,src,dstlen);
	}
}

// Formats and translates, and returns the resulting string in buf.
// 'ctx' can be NULL, in which case no tranlation will happen.
IW_IMPL(void) iw_translatev(struct iw_context *ctx, unsigned int flags,
	char *dst, size_t dstlen, const char *fmt, va_list ap)
{
	char buf1[IW_MSG_MAX];
	char buf2[IW_MSG_MAX];

	// If not translating, just format the string directly.
	if(!ctx || !ctx->translate_fn) {
		iw_vsnprintf(dst,dstlen,fmt,ap);
		return;
	}

	// String is now in fmt.
	iw_translate(ctx,IW_TRANSLATEFLAG_FORMAT|flags,buf1,sizeof(buf1),fmt);
	// String is now in buf1.
	iw_vsnprintf(buf2,sizeof(buf2),buf1,ap);
	// String is now in buf2.
	iw_translate(ctx,IW_TRANSLATEFLAG_POSTFORMAT|flags,dst,dstlen,buf2);
	// String is now in dst.
}

// Formats and translates, and returns the resulting string in buf
IW_IMPL(void) iw_translatef(struct iw_context *ctx, unsigned int flags,
	char *dst, size_t dstlen, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	iw_translatev(ctx,flags,dst,dstlen,fmt,ap);
	va_end(ap);
}

static void iw_warning_internal(struct iw_context *ctx, const char *s)
{
	if(!ctx->warning_fn) return;

	(*ctx->warning_fn)(ctx,s);
}

IW_IMPL(void) iw_warning(struct iw_context *ctx, const char *s)
{
	char buf[IW_MSG_MAX];

	if(!ctx->warning_fn) return;
	iw_translate(ctx,IW_TRANSLATEFLAG_WARNINGMSG,buf,sizeof(buf),s);
	iw_warning_internal(ctx,buf);
}

IW_IMPL(void) iw_warningv(struct iw_context *ctx, const char *fmt, va_list ap)
{
	char buf[IW_MSG_MAX];

	if(!ctx->warning_fn) return;
	iw_translatev(ctx,IW_TRANSLATEFLAG_WARNINGMSG,buf,sizeof(buf),fmt,ap);
	iw_warning_internal(ctx,buf);
}

// Call the caller's warning function, if defined.
IW_IMPL(void) iw_warningf(struct iw_context *ctx, const char *fmt, ...)
{
	va_list ap;

	if(!ctx->warning_fn) return;
	va_start(ap, fmt);
	iw_warningv(ctx,fmt,ap);
	va_end(ap);
}

static void iw_set_error_internal(struct iw_context *ctx, const char *s)
{
	if(ctx->error_flag) return; // Only record the first error.
	ctx->error_flag = 1;

	if(!ctx->error_msg) {
		ctx->error_msg=iw_malloc_ex(ctx,IW_MALLOCFLAG_NOERRORS,IW_MSG_MAX*sizeof(char));
		if(!ctx->error_msg) {
			return;
		}
	}

	iw_strlcpy(ctx->error_msg,s,IW_MSG_MAX);
}

IW_IMPL(void) iw_set_error(struct iw_context *ctx, const char *s)
{
	char buf[IW_MSG_MAX];

	if(ctx->error_flag) return; // Only record the first error.
	iw_translate(ctx,IW_TRANSLATEFLAG_ERRORMSG,buf,sizeof(buf),s);
	iw_set_error_internal(ctx,buf);
}

IW_IMPL(void) iw_set_errorv(struct iw_context *ctx, const char *fmt, va_list ap)
{
	char buf[IW_MSG_MAX];

	if(ctx->error_flag) return; // Only record the first error.
	iw_translatev(ctx,IW_TRANSLATEFLAG_ERRORMSG,buf,sizeof(buf),fmt,ap);
	iw_set_error_internal(ctx,buf);
}

IW_IMPL(void) iw_set_errorf(struct iw_context *ctx, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iw_set_errorv(ctx,fmt,ap);
	va_end(ap);
}

IW_IMPL(const char*) iw_get_errormsg(struct iw_context *ctx, char *buf, int buflen)
{
	if(ctx->error_msg) {
		iw_strlcpy(buf,ctx->error_msg,buflen);
	}
	else {
		iw_translate(ctx,IW_TRANSLATEFLAG_ERRORMSG,buf,buflen,"Error message not available");
	}

	return buf;
}

IW_IMPL(int) iw_get_errorflag(struct iw_context *ctx)
{
	return ctx->error_flag;
}

// Given a color type, returns the number of channels.
IW_IMPL(int) iw_imgtype_num_channels(int t)
{
	switch(t) {
	case IW_IMGTYPE_RGBA:
		return 4;
	case IW_IMGTYPE_RGB:
		return 3;
	case IW_IMGTYPE_GRAYA:
		return 2;
	}
	return 1;
}

IW_IMPL(size_t) iw_calc_bytesperrow(int num_pixels, int bits_per_pixel)
{
	return (size_t)(((num_pixels*bits_per_pixel)+7)/8);
}

IW_IMPL(int) iw_check_image_dimensions(struct iw_context *ctx, int w, int h)
{
	if(w>ctx->max_width || h>ctx->max_height) {
		iw_set_errorf(ctx,"Image dimensions too large (%d\xc3\x97%d)",w,h);
		return 0;
	}

	if(w<1 || h<1) {
		iw_set_errorf(ctx,"Invalid image dimensions (%d\xc3\x97%d)",w,h);
		return 0;
	}

	return 1;
}

IW_IMPL(int) iw_is_valid_density(double density_x, double density_y, int density_code)
{
	if(density_x<0.0001 || density_y<0.0001) return 0;
	if(density_x>10000000.0 || density_y>10000000.0) return 0;
	if(density_x/10.0>density_y) return 0;
	if(density_y/10.0>density_x) return 0;
	if(density_code!=IW_DENSITY_UNITS_UNKNOWN && density_code!=IW_DENSITY_UNITS_PER_METER)
		return 0;
	return 1;
}

static void default_resize_settings(struct iw_resize_settings *rs)
{
	int i;
	rs->family = IW_RESIZETYPE_AUTO;
	rs->edge_policy = IW_EDGE_POLICY_STANDARD;
	rs->blur_factor = 1.0;
	rs->translate = 0.0;
	for(i=0;i<3;i++) {
		rs->channel_offset[i] = 0.0;
	}
}

IW_IMPL(struct iw_context*) iw_create_context(struct iw_init_params *params)
{
	struct iw_context *ctx;

	if(params && params->mallocfn) {
		ctx = (*params->mallocfn)(params->userdata,IW_MALLOCFLAG_ZEROMEM,sizeof(struct iw_context));
	}
	else {
		ctx = iwpvt_default_malloc(NULL,IW_MALLOCFLAG_ZEROMEM,sizeof(struct iw_context));
	}

	if(!ctx) return NULL;

	if(params) {
		ctx->userdata = params->userdata;
		ctx->caller_api_version = params->api_version;
	}

	if(params && params->mallocfn) {
		ctx->mallocfn = params->mallocfn;
		ctx->freefn = params->freefn;
	}
	else {
		ctx->mallocfn = iwpvt_default_malloc;
		ctx->freefn = iwpvt_default_free;
	}

	ctx->max_malloc = IW_DEFAULT_MAX_MALLOC;
	ctx->max_width = ctx->max_height = IW_DEFAULT_MAX_DIMENSION;
	default_resize_settings(&ctx->resize_settings[IW_DIMENSION_H]);
	default_resize_settings(&ctx->resize_settings[IW_DIMENSION_V]);
	ctx->input_w = -1;
	ctx->input_h = -1;
	iw_make_srgb_csdescr_2(&ctx->img1cs);
	iw_make_srgb_csdescr_2(&ctx->img2cs);
	ctx->to_grayscale=0;
	ctx->grayscale_formula = IW_GSF_STANDARD;
	ctx->req.include_screen = 1;
	ctx->opt_grayscale = 1;
	ctx->opt_palette = 1;
	ctx->opt_16_to_8 = 1;
	ctx->opt_strip_alpha = 1;
	ctx->opt_binary_trns = 1;

	return ctx;
}

IW_IMPL(void) iw_destroy_context(struct iw_context *ctx)
{
	int i;
	if(!ctx) return;
	if(ctx->req.options) {
		for(i=0; i<=ctx->req.options_count; i++) {
			iw_free(ctx, ctx->req.options[i].name);
			iw_free(ctx, ctx->req.options[i].val);
		}
		iw_free(ctx, ctx->req.options);
	}
	if(ctx->img1.pixels) iw_free(ctx,ctx->img1.pixels);
	if(ctx->img2.pixels) iw_free(ctx,ctx->img2.pixels);
	if(ctx->error_msg) iw_free(ctx,ctx->error_msg);
	if(ctx->optctx.tmp_pixels) iw_free(ctx,ctx->optctx.tmp_pixels);
	if(ctx->optctx.palette) iw_free(ctx,ctx->optctx.palette);
	if(ctx->input_color_corr_table) iw_free(ctx,ctx->input_color_corr_table);
	if(ctx->output_rev_color_corr_table) iw_free(ctx,ctx->output_rev_color_corr_table);
	if(ctx->nearest_color_table) iw_free(ctx,ctx->nearest_color_table);
	if(ctx->prng) iwpvt_prng_destroy(ctx,ctx->prng);
	iw_free(ctx,ctx);
}

IW_IMPL(void) iw_get_output_image(struct iw_context *ctx, struct iw_image *img)
{
	int k;

	iw_zeromem(img,sizeof(struct iw_image));
	img->width = ctx->optctx.width;
	img->height = ctx->optctx.height;
	img->imgtype = ctx->optctx.imgtype;
	img->sampletype = ctx->img2.sampletype;
	img->bit_depth = ctx->optctx.bit_depth;
	img->pixels = (iw_byte*)ctx->optctx.pixelsptr;
	img->bpr = ctx->optctx.bpr;
	img->density_code = ctx->img2.density_code;
	img->density_x = ctx->img2.density_x;
	img->density_y = ctx->img2.density_y;
	img->rendering_intent = ctx->img2.rendering_intent;

	img->has_bkgdlabel = ctx->optctx.has_bkgdlabel;
	for(k=0;k<4;k++) {
		if(ctx->optctx.bit_depth==8) {
			img->bkgdlabel.c[k] = ((double)ctx->optctx.bkgdlabel[k])/255.0;
		}
		else {
			img->bkgdlabel.c[k] = ((double)ctx->optctx.bkgdlabel[k])/65535.0;
		}
	}

	img->has_colorkey_trns = ctx->optctx.has_colorkey_trns;
	img->colorkey[0] = ctx->optctx.colorkey[0];
	img->colorkey[1] = ctx->optctx.colorkey[1];
	img->colorkey[2] = ctx->optctx.colorkey[2];
	if(ctx->reduced_output_maxcolor_flag) {
		img->reduced_maxcolors = 1;
		if(IW_IMGTYPE_IS_GRAY(img->imgtype)) {
			img->maxcolorcode[IW_CHANNELTYPE_GRAY] = ctx->img2_ci[0].maxcolorcode_int;
			if(IW_IMGTYPE_HAS_ALPHA(img->imgtype)) {
				img->maxcolorcode[IW_CHANNELTYPE_ALPHA] = ctx->img2_ci[1].maxcolorcode_int;
			}
		}
		else {
			img->maxcolorcode[IW_CHANNELTYPE_RED]   = ctx->img2_ci[0].maxcolorcode_int;
			img->maxcolorcode[IW_CHANNELTYPE_GREEN] = ctx->img2_ci[1].maxcolorcode_int;
			img->maxcolorcode[IW_CHANNELTYPE_BLUE]  = ctx->img2_ci[2].maxcolorcode_int;
			if(IW_IMGTYPE_HAS_ALPHA(img->imgtype)) {
				img->maxcolorcode[IW_CHANNELTYPE_ALPHA] = ctx->img2_ci[3].maxcolorcode_int;
			}
		}
	}
}

IW_IMPL(void) iw_get_output_colorspace(struct iw_context *ctx, struct iw_csdescr *csdescr)
{
	*csdescr = ctx->img2cs; // struct copy
}

IW_IMPL(const struct iw_palette*) iw_get_output_palette(struct iw_context *ctx)
{
	return ctx->optctx.palette;
}

IW_IMPL(void) iw_set_output_canvas_size(struct iw_context *ctx, int w, int h)
{
	ctx->canvas_width = w;
	ctx->canvas_height = h;
}

IW_IMPL(void) iw_set_output_image_size(struct iw_context *ctx, double w, double h)
{
	ctx->req.out_true_width = w;
	if(ctx->req.out_true_width<0.01) ctx->req.out_true_width=0.01;
	ctx->req.out_true_height = h;
	if(ctx->req.out_true_height<0.01) ctx->req.out_true_height=0.01;
	ctx->req.out_true_valid = 1;
}

IW_IMPL(void) iw_set_input_crop(struct iw_context *ctx, int x, int y, int w, int h)
{
	ctx->input_start_x = x;
	ctx->input_start_y = y;
	ctx->input_w = w;
	ctx->input_h = h;
}

IW_IMPL(void) iw_set_output_profile(struct iw_context *ctx, unsigned int n)
{
	ctx->output_profile = n;
}

IW_IMPL(void) iw_set_output_depth(struct iw_context *ctx, int bps)
{
	ctx->req.output_depth = bps;
}

IW_IMPL(void) iw_set_output_max_color_code(struct iw_context *ctx, int channeltype, int n)
{
	if(channeltype>=0 && channeltype<IW_NUM_CHANNELTYPES) {
		ctx->req.output_maxcolorcode[channeltype] = n;
	}
}

IW_IMPL(void) iw_set_dither_type(struct iw_context *ctx, int channeltype, int f, int s)
{
	if(channeltype>=0 && channeltype<IW_NUM_CHANNELTYPES) {
		ctx->ditherfamily_by_channeltype[channeltype] = f;
		ctx->dithersubtype_by_channeltype[channeltype] = s;
	}

	switch(channeltype) {
	case IW_CHANNELTYPE_ALL:
		ctx->ditherfamily_by_channeltype[IW_CHANNELTYPE_ALPHA] = f;
		ctx->dithersubtype_by_channeltype[IW_CHANNELTYPE_ALPHA] = s;
		// fall thru
	case IW_CHANNELTYPE_NONALPHA:
		ctx->ditherfamily_by_channeltype[IW_CHANNELTYPE_RED] = f;
		ctx->dithersubtype_by_channeltype[IW_CHANNELTYPE_RED] = s;
		ctx->ditherfamily_by_channeltype[IW_CHANNELTYPE_GREEN] = f;
		ctx->dithersubtype_by_channeltype[IW_CHANNELTYPE_GREEN] = s;
		ctx->ditherfamily_by_channeltype[IW_CHANNELTYPE_BLUE] = f;
		ctx->dithersubtype_by_channeltype[IW_CHANNELTYPE_BLUE] = s;
		ctx->ditherfamily_by_channeltype[IW_CHANNELTYPE_GRAY] = f;
		ctx->dithersubtype_by_channeltype[IW_CHANNELTYPE_GRAY] = s;
		break;
	}
}

IW_IMPL(void) iw_set_color_count(struct iw_context *ctx, int channeltype, int c)
{
	if(channeltype>=0 && channeltype<IW_NUM_CHANNELTYPES) {
		ctx->req.color_count[channeltype] = c;
	}

	switch(channeltype) {
	case IW_CHANNELTYPE_ALL:
		ctx->req.color_count[IW_CHANNELTYPE_ALPHA] = c;
		// fall thru
	case IW_CHANNELTYPE_NONALPHA:
		ctx->req.color_count[IW_CHANNELTYPE_RED] = c;
		ctx->req.color_count[IW_CHANNELTYPE_GREEN] = c;
		ctx->req.color_count[IW_CHANNELTYPE_BLUE] = c;
		ctx->req.color_count[IW_CHANNELTYPE_GRAY] = c;
		break;
	}
}

IW_IMPL(void) iw_set_channel_offset(struct iw_context *ctx, int channeltype, int dimension, double offs)
{
	if(channeltype<0 || channeltype>2) return;
	if(dimension<0 || dimension>1) dimension=0;
	ctx->resize_settings[dimension].channel_offset[channeltype] = offs;
}

IW_IMPL(void) iw_set_input_max_color_code(struct iw_context *ctx, int input_channel, int c)
{
	if(input_channel>=0 && input_channel<IW_CI_COUNT) {
		ctx->img1_ci[input_channel].maxcolorcode_int = c;
	}
}

IW_IMPL(void) iw_set_input_bkgd_label_2(struct iw_context *ctx, const struct iw_color *clr)
{
	ctx->img1_bkgd_label_set = 1;
	ctx->img1_bkgd_label_inputcs = *clr;
}

IW_IMPL(void) iw_set_input_bkgd_label(struct iw_context *ctx, double r, double g, double b)
{
	struct iw_color clr;
	clr.c[0] = r;
	clr.c[1] = g;
	clr.c[2] = b;
	clr.c[3] = 1.0;
	iw_set_input_bkgd_label_2(ctx, &clr);
}

IW_IMPL(void) iw_set_output_bkgd_label_2(struct iw_context *ctx, const struct iw_color *clr)
{
	ctx->req.output_bkgd_label_valid = 1;
	ctx->req.output_bkgd_label = *clr;
}

IW_IMPL(void) iw_set_output_bkgd_label(struct iw_context *ctx, double r, double g, double b)
{
	struct iw_color clr;
	clr.c[0] = r;
	clr.c[1] = g;
	clr.c[2] = b;
	clr.c[3] = 1.0;
	iw_set_output_bkgd_label_2(ctx, &clr);
}

IW_IMPL(int) iw_get_input_density(struct iw_context *ctx,
   double *px, double *py, int *pcode)
{
	*px = 1.0;
	*py = 1.0;
	*pcode = ctx->img1.density_code;
	if(ctx->img1.density_code!=IW_DENSITY_UNKNOWN) {
		*px = ctx->img1.density_x;
		*py = ctx->img1.density_y;
		return 1;
	}
	return 0;
}

IW_IMPL(void) iw_set_output_density(struct iw_context *ctx,
   double x, double y, int code)
{
	ctx->img2.density_code = code;
	ctx->img2.density_x = x;
	ctx->img2.density_y = y;
}

// Detect a "gamma" colorspace that is actually linear.
static void optimize_csdescr(struct iw_csdescr *cs)
{
	if(cs->cstype!=IW_CSTYPE_GAMMA) return;
	if(cs->gamma>=0.999995 && cs->gamma<=1.000005) {
		cs->cstype = IW_CSTYPE_LINEAR;
	}
}

IW_IMPL(void) iw_make_linear_csdescr(struct iw_csdescr *cs)
{
	cs->cstype = IW_CSTYPE_LINEAR;
	cs->gamma = 0.0;
	cs->srgb_intent = 0;
}

// This function is deprecated, and should not be used.
IW_IMPL(void) iw_make_srgb_csdescr(struct iw_csdescr *cs, int srgb_intent)
{
	cs->cstype = IW_CSTYPE_SRGB;
	cs->gamma = 0.0;
	cs->srgb_intent = srgb_intent;
}

IW_IMPL(void) iw_make_srgb_csdescr_2(struct iw_csdescr *cs)
{
	cs->cstype = IW_CSTYPE_SRGB;
	cs->gamma = 0.0;
}

IW_IMPL(void) iw_make_rec709_csdescr(struct iw_csdescr *cs)
{
	cs->cstype = IW_CSTYPE_REC709;
	cs->gamma = 0.0;
}

IW_IMPL(void) iw_make_gamma_csdescr(struct iw_csdescr *cs, double gamma)
{
	cs->cstype = IW_CSTYPE_GAMMA;
	cs->gamma = gamma;
	if(cs->gamma<0.1) cs->gamma=0.1;
	if(cs->gamma>10.0) cs->gamma=10.0;
	cs->srgb_intent = 0;
	optimize_csdescr(cs);
}

IW_IMPL(void) iw_set_output_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr)
{
	ctx->req.output_cs = *csdescr; // struct copy
	optimize_csdescr(&ctx->req.output_cs);
	ctx->req.output_cs_valid = 1;
}

IW_IMPL(void) iw_set_input_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr)
{
	ctx->img1cs = *csdescr; // struct copy
	optimize_csdescr(&ctx->img1cs);
}

IW_IMPL(void) iw_set_apply_bkgd_2(struct iw_context *ctx, const struct iw_color *clr)
{
	ctx->req.bkgd_valid=1;
	ctx->req.bkgd = *clr;
}

IW_IMPL(void) iw_set_apply_bkgd(struct iw_context *ctx, double r, double g, double b)
{
	struct iw_color clr;
	clr.c[IW_CHANNELTYPE_RED]=r;
	clr.c[IW_CHANNELTYPE_GREEN]=g;
	clr.c[IW_CHANNELTYPE_BLUE]=b;
	clr.c[IW_CHANNELTYPE_ALPHA]=1.0;
	iw_set_apply_bkgd_2(ctx, &clr);
}

IW_IMPL(void) iw_set_bkgd_checkerboard_2(struct iw_context *ctx, int checkersize,
	const struct iw_color *clr)
{
	ctx->req.bkgd_checkerboard=1;
	ctx->bkgd_check_size=checkersize;
	ctx->req.bkgd2 = *clr;
}

IW_IMPL(void) iw_set_bkgd_checkerboard(struct iw_context *ctx, int checkersize,
    double r2, double g2, double b2)
{
	struct iw_color clr;
	clr.c[IW_CHANNELTYPE_RED]=r2;
	clr.c[IW_CHANNELTYPE_GREEN]=g2;
	clr.c[IW_CHANNELTYPE_BLUE]=b2;
	clr.c[IW_CHANNELTYPE_ALPHA]=1.0;
	iw_set_bkgd_checkerboard_2(ctx, checkersize, &clr);
}

IW_IMPL(void) iw_set_bkgd_checkerboard_origin(struct iw_context *ctx, int x, int y)
{
	ctx->bkgd_check_origin[IW_DIMENSION_H] = x;
	ctx->bkgd_check_origin[IW_DIMENSION_V] = y;
}

IW_IMPL(void) iw_set_max_malloc(struct iw_context *ctx, size_t n)
{
	ctx->max_malloc = n;
}

IW_IMPL(void) iw_set_random_seed(struct iw_context *ctx, int randomize, int rand_seed)
{
	ctx->randomize = randomize;
	ctx->random_seed = rand_seed;
}

IW_IMPL(void) iw_set_userdata(struct iw_context *ctx, void *userdata)
{
	ctx->userdata = userdata;
}

IW_IMPL(void*) iw_get_userdata(struct iw_context *ctx)
{
	return ctx->userdata;
}

IW_IMPL(void) iw_set_translate_fn(struct iw_context *ctx, iw_translatefn_type xlatefn)
{
	ctx->translate_fn = xlatefn;
}

IW_IMPL(void) iw_set_warning_fn(struct iw_context *ctx, iw_warningfn_type warnfn)
{
	ctx->warning_fn = warnfn;
}

IW_IMPL(void) iw_set_input_image(struct iw_context *ctx, const struct iw_image *img)
{
	ctx->img1 = *img; // struct copy
}

IW_IMPL(void) iw_set_resize_alg(struct iw_context *ctx, int dimension, int family,
    double blur, double param1, double param2)
{
	struct iw_resize_settings *rs;

	if(dimension<0 || dimension>1) dimension=0;
	rs=&ctx->resize_settings[dimension];

	rs->family = family;
	rs->blur_factor = blur;
	rs->param1 = param1;
	rs->param2 = param2;
}

IW_IMPL(void) iw_reorient_image(struct iw_context *ctx, unsigned int x)
{
	static const unsigned int transpose_tbl[8] = { 4,6,5,7,0,2,1,3 };
	int tmpi;
	double tmpd;

	x = x & 0x07;

	// If needed, perform a 'transpose' of the current transform.
	if(x&0x04) {
		ctx->img1.orient_transform = transpose_tbl[ctx->img1.orient_transform];

		// We swapped the width and height, so we need to fix up some things.
		tmpi = ctx->img1.width;
		ctx->img1.width = ctx->img1.height;
		ctx->img1.height = tmpi;

		tmpd = ctx->img1.density_x;
		ctx->img1.density_x = ctx->img1.density_y;
		ctx->img1.density_y = tmpd;
	}

	// Do horizontal and vertical mirroring.
	ctx->img1.orient_transform ^= (x&0x03);
}

IW_IMPL(int) iw_get_sample_size(void)
{
	return (int)sizeof(iw_float32);
}

IW_IMPL(int) iw_get_version_int(void)
{
	return IW_VERSION_INT;
}

IW_IMPL(char*) iw_get_version_string(struct iw_context *ctx, char *s, int s_len)
{
	int ver;
	ver = iw_get_version_int();
	iw_snprintf(s,s_len,"%d.%d.%d",
		(ver&0xff0000)>>16, (ver&0xff00)>>8, (ver&0xff) );
	return s;
}

IW_IMPL(char*) iw_get_copyright_string(struct iw_context *ctx, char *dst, int dstlen)
{
	iw_translatef(ctx,0,dst,dstlen,"Copyright \xc2\xa9 %s %s",IW_COPYRIGHT_YEAR,"Jason Summers");
	return dst;
}

IW_IMPL(void) iw_set_zlib_module(struct iw_context *ctx, struct iw_zlib_module *z)
{
	ctx->zlib_module = z;
}

IW_IMPL(struct iw_zlib_module*) iw_get_zlib_module(struct iw_context *ctx)
{
	return ctx->zlib_module;
}

IW_IMPL(void) iw_set_allow_opt(struct iw_context *ctx, int opt, int n)
{
	iw_byte v;
	v = n?1:0;

	switch(opt) {
	case IW_OPT_GRAYSCALE: ctx->opt_grayscale = v; break;
	case IW_OPT_PALETTE: ctx->opt_palette = v; break;
	case IW_OPT_16_TO_8: ctx->opt_16_to_8 = v; break;
	case IW_OPT_STRIP_ALPHA: ctx->opt_strip_alpha = v; break;
	case IW_OPT_BINARY_TRNS: ctx->opt_binary_trns = v; break;
	}
}

IW_IMPL(void) iw_set_grayscale_weights(struct iw_context *ctx,
	double r, double g, double b)
{
	double tot;

	//ctx->grayscale_formula = IW_GSF_WEIGHTED;

	// Normalize, so the weights add up to 1.
	tot = r+g+b;
	if(tot==0.0) tot=1.0;
	ctx->grayscale_weight[0] = r/tot;
	ctx->grayscale_weight[1] = g/tot;
	ctx->grayscale_weight[2] = b/tot;
}

IW_IMPL(unsigned int) iw_color_get_int_sample(struct iw_color *clr, int channel,
	unsigned int maxcolorcode)
{
	int n;
	n = (int)(0.5+(clr->c[channel] * (double)maxcolorcode));
	if(n<0) n=0;
	else if(n>(int)maxcolorcode) n=(int)maxcolorcode;
	return (unsigned int)n;
}

IW_IMPL(void) iw_set_value(struct iw_context *ctx, int code, int n)
{
	switch(code) {
	case IW_VAL_API_VERSION:
		ctx->caller_api_version = n;
		break;
	case IW_VAL_CVT_TO_GRAYSCALE:
		ctx->to_grayscale = n;
		break;
	case IW_VAL_DISABLE_GAMMA:
		ctx->no_gamma = n;
		break;
	case IW_VAL_NO_CSLABEL:
		ctx->req.suppress_output_cslabel = n;
		break;
	case IW_VAL_INT_CLAMP:
		ctx->intclamp = n;
		break;
	case IW_VAL_EDGE_POLICY_X:
		ctx->resize_settings[IW_DIMENSION_H].edge_policy = n;
		break;
	case IW_VAL_EDGE_POLICY_Y:
		ctx->resize_settings[IW_DIMENSION_V].edge_policy = n;
		break;
	case IW_VAL_PREF_UNITS:
		ctx->pref_units = n;
		break;
	case IW_VAL_GRAYSCALE_FORMULA:
		ctx->grayscale_formula = n;
		break;
	case IW_VAL_INPUT_NATIVE_GRAYSCALE:
		ctx->img1.native_grayscale = n;
		break;
	case IW_VAL_COMPRESSION:
		ctx->req.compression = n;
		break;
	case IW_VAL_PAGE_TO_READ:
		ctx->req.page_to_read = n;
		break;
	case IW_VAL_INCLUDE_SCREEN:
		ctx->req.include_screen = n;
		break;
	case IW_VAL_JPEG_QUALITY:
		// For backward compatibility only.
		iw_set_option(ctx, "jpeg:quality", iwpvt_strdup_dbl(ctx, (double)n));
		break;
	case IW_VAL_JPEG_SAMP_FACTOR_H:
		// For backward compatibility only.
		iw_set_option(ctx, "jpeg:sampling-x", iwpvt_strdup_dbl(ctx, (double)n));
		break;
	case IW_VAL_JPEG_SAMP_FACTOR_V:
		// For backward compatibility only.
		iw_set_option(ctx, "jpeg:sampling-y", iwpvt_strdup_dbl(ctx, (double)n));
		break;
	case IW_VAL_JPEG_ARITH_CODING:
		// For backward compatibility only.
		iw_set_option(ctx, "jpeg:arith", iwpvt_strdup_dbl(ctx, (double)n));
		break;
	case IW_VAL_DEFLATE_CMPR_LEVEL:
		// For backward compatibility only.
		iw_set_option(ctx, "deflate:cmprlevel", iwpvt_strdup_dbl(ctx, (double)n)); 
		break;
	case IW_VAL_OUTPUT_INTERLACED:
		ctx->req.interlaced = n;
		break;
	case IW_VAL_USE_BKGD_LABEL:
		ctx->req.use_bkgd_label_from_file = n;
		break;
	case IW_VAL_BMP_NO_FILEHEADER:
		ctx->req.bmp_no_fileheader = n;
		break;
	case IW_VAL_BMP_VERSION:
		// For backward compatibility only.
		iw_set_option(ctx, "bmp:version", iwpvt_strdup_dbl(ctx, (double)n));
		break;
	case IW_VAL_MAX_WIDTH:
		ctx->max_width = n;
		break;
	case IW_VAL_MAX_HEIGHT:
		ctx->max_height = n;
		break;
	case IW_VAL_NO_BKGD_LABEL:
		ctx->req.suppress_output_bkgd_label = n;
		break;
	case IW_VAL_INTENT:
		ctx->req.output_rendering_intent = n;
		break;
	case IW_VAL_OUTPUT_SAMPLE_TYPE:
		ctx->req.output_sample_type = n;
		break;
	case IW_VAL_OUTPUT_COLOR_TYPE:
		// For backward compatibility only.
		if(n==IW_COLORTYPE_RGB) {
			iw_set_option(ctx, "deflate:colortype", "rgb"); 
		}
		break;
	case IW_VAL_OUTPUT_FORMAT:
		ctx->req.output_format = n;
		break;
	case IW_VAL_NEGATE_TARGET:
		ctx->req.negate_target = n;
		break;
	}
}

IW_IMPL(int) iw_get_value(struct iw_context *ctx, int code)
{
	int ret=0;

	switch(code) {
	case IW_VAL_API_VERSION:
		ret = ctx->caller_api_version;
		break;
	case IW_VAL_CVT_TO_GRAYSCALE:
		ret = ctx->to_grayscale;
		break;
	case IW_VAL_DISABLE_GAMMA:
		ret = ctx->no_gamma;
		break;
	case IW_VAL_NO_CSLABEL:
		ret = ctx->req.suppress_output_cslabel;
		break;
	case IW_VAL_INT_CLAMP:
		ret = ctx->intclamp;
		break;
	case IW_VAL_EDGE_POLICY_X:
		ret = ctx->resize_settings[IW_DIMENSION_H].edge_policy;
		break;
	case IW_VAL_EDGE_POLICY_Y:
		ret = ctx->resize_settings[IW_DIMENSION_V].edge_policy;
		break;
	case IW_VAL_PREF_UNITS:
		ret = ctx->pref_units;
		break;
	case IW_VAL_GRAYSCALE_FORMULA:
		ret = ctx->grayscale_formula;
		break;
	case IW_VAL_INPUT_NATIVE_GRAYSCALE:
		ret = ctx->img1.native_grayscale;
		break;
	case IW_VAL_INPUT_WIDTH:
		if(ctx->img1.width<1) ret=1;
		else ret = ctx->img1.width;
		break;
	case IW_VAL_INPUT_HEIGHT:
		if(ctx->img1.height<1) ret=1;
		else ret = ctx->img1.height;
		break;
	case IW_VAL_INPUT_IMAGE_TYPE:
		ret = ctx->img1.imgtype;
		break;
	case IW_VAL_INPUT_DEPTH:
		ret = ctx->img1.bit_depth;
		break;
	case IW_VAL_COMPRESSION:
		ret = ctx->req.compression;
		break;
	case IW_VAL_PAGE_TO_READ:
		ret = ctx->req.page_to_read;
		break;
	case IW_VAL_INCLUDE_SCREEN:
		ret = ctx->req.include_screen;
		break;
	case IW_VAL_OUTPUT_PALETTE_GRAYSCALE:
		ret = ctx->optctx.palette_is_grayscale;
		break;
	case IW_VAL_OUTPUT_INTERLACED:
		ret = ctx->req.interlaced;
		break;
	case IW_VAL_USE_BKGD_LABEL:
		ret = ctx->req.use_bkgd_label_from_file;
		break;
	case IW_VAL_BMP_NO_FILEHEADER:
		ret = ctx->req.bmp_no_fileheader;
		break;
	case IW_VAL_MAX_WIDTH:
		ret = ctx->max_width;
		break;
	case IW_VAL_MAX_HEIGHT:
		ret = ctx->max_height;
		break;
	case IW_VAL_PRECISION:
		ret = 32;
		break;
	case IW_VAL_NO_BKGD_LABEL:
		ret = ctx->req.suppress_output_bkgd_label;
		break;
	case IW_VAL_INTENT:
		ret = ctx->req.output_rendering_intent;
		break;
	case IW_VAL_OUTPUT_SAMPLE_TYPE:
		ret = ctx->req.output_sample_type;
		break;
	case IW_VAL_OUTPUT_FORMAT:
		ret = ctx->req.output_format;
		break;
	case IW_VAL_NEGATE_TARGET:
		ret = ctx->req.negate_target;
		break;
	}

	return ret;
}

IW_IMPL(void) iw_set_value_dbl(struct iw_context *ctx, int code, double n)
{
	switch(code) {
	case IW_VAL_WEBP_QUALITY:
		// For backward compatibility only.
		iw_set_option(ctx, "webp:quality", iwpvt_strdup_dbl(ctx, n)); 
		break;
	case IW_VAL_TRANSLATE_X:
		ctx->resize_settings[IW_DIMENSION_H].translate = n;
		break;
	case IW_VAL_TRANSLATE_Y:
		ctx->resize_settings[IW_DIMENSION_V].translate = n;
		break;
	}
}

IW_IMPL(double) iw_get_value_dbl(struct iw_context *ctx, int code)
{
	double ret = 0.0;

	switch(code) {
	case IW_VAL_TRANSLATE_X:
		ret = ctx->resize_settings[IW_DIMENSION_H].translate;
		break;
	case IW_VAL_TRANSLATE_Y:
		ret = ctx->resize_settings[IW_DIMENSION_V].translate;
		break;
	}

	return ret;
}

IW_IMPL(void) iw_set_option(struct iw_context *ctx, const char *name, const char *val)
{
#define IW_MAX_OPTIONS 32
	int i;

	if(val==NULL || val[0]=='\0') {
		// An empty value can be used to mean "turn on this option".
		// To make that easier, set such values to "1".
		val = "1";
	}

	// Allocate req.options if that hasn't been done yet.
	if(!ctx->req.options) {
		ctx->req.options = iw_mallocz(ctx, IW_MAX_OPTIONS*sizeof(struct iw_option_struct));
		if(!ctx->req.options) return;
		ctx->req.options_numalloc = IW_MAX_OPTIONS;
		ctx->req.options_count = 0;
	}

	// If option already exists, replace it.
	for(i=0; i<ctx->req.options_count; i++) {
		if(ctx->req.options[i].name && !strcmp(ctx->req.options[i].name, name)) {
			iw_free(ctx, ctx->req.options[i].val);
			ctx->req.options[i].val = iw_strdup(ctx, val);
			return;
		}
	}

	// Add the new option.
	if(ctx->req.options_count>=IW_MAX_OPTIONS) return;
	ctx->req.options[ctx->req.options_count].name = iw_strdup(ctx, name);
	ctx->req.options[ctx->req.options_count].val = iw_strdup(ctx, val);
	ctx->req.options_count++;
}

// Return the value of the first option with the given name.
// Return NULL if not found.
IW_IMPL(const char*) iw_get_option(struct iw_context *ctx, const char *name)
{
	int i;
	for(i=0; i<ctx->req.options_count; i++) {
		if(ctx->req.options[i].name && !strcmp(ctx->req.options[i].name, name)) {
			return ctx->req.options[i].val;
		}
	}
	return NULL;
}
