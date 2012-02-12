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
	if(w>IW_MAX_DIMENSION || h>IW_MAX_DIMENSION) {
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
		ctx->reallocfn = params->reallocfn;
		ctx->freefn = params->freefn;
	}
	else {
		ctx->mallocfn = iwpvt_default_malloc;
		ctx->reallocfn = iwpvt_default_realloc;
		ctx->freefn = iwpvt_default_free;
	}

	ctx->max_malloc = IW_DEFAULT_MAX_MALLOC;
	default_resize_settings(&ctx->resize_settings[IW_DIMENSION_H]);
	default_resize_settings(&ctx->resize_settings[IW_DIMENSION_V]);
	ctx->input_w = -1;
	ctx->input_h = -1;
	iw_make_srgb_csdescr(&ctx->img1cs,IW_SRGB_INTENT_PERCEPTUAL);
	iw_make_srgb_csdescr(&ctx->img2cs,IW_SRGB_INTENT_PERCEPTUAL);
	ctx->to_grayscale=0;
	ctx->grayscale_formula = IW_GSF_STANDARD;
	ctx->density_policy = IW_DENSITY_POLICY_AUTO;
	ctx->bkgd.c[IW_CHANNELTYPE_RED]=1.0; // Default background color
	ctx->bkgd.c[IW_CHANNELTYPE_GREEN]=0.0;
	ctx->bkgd.c[IW_CHANNELTYPE_BLUE]=1.0;
	ctx->include_screen = 1;
	ctx->webp_quality = -1.0;
	ctx->deflatecmprlevel = 9;
	ctx->opt_grayscale = 1;
	ctx->opt_palette = 1;
	ctx->opt_16_to_8 = 1;
	ctx->opt_strip_alpha = 1;
	ctx->opt_binary_trns = 1;

	return ctx;
}

IW_IMPL(void) iw_destroy_context(struct iw_context *ctx)
{
	if(!ctx) return;
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
	img->has_colorkey_trns = ctx->optctx.has_colorkey_trns;
	img->colorkey_r = ctx->optctx.colorkey_r;
	img->colorkey_g = ctx->optctx.colorkey_g;
	img->colorkey_b = ctx->optctx.colorkey_b;
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
	ctx->output_depth = bps;
}

IW_IMPL(void) iw_set_dither_type(struct iw_context *ctx, int channeltype, int f, int s)
{
	if(channeltype>=0 && channeltype<=4) {
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
	if(channeltype>=0 && channeltype<=4) {
		ctx->color_count[channeltype] = c;
	}

	switch(channeltype) {
	case IW_CHANNELTYPE_ALL:
		ctx->color_count[IW_CHANNELTYPE_ALPHA] = c;
		// fall thru
	case IW_CHANNELTYPE_NONALPHA:
		ctx->color_count[IW_CHANNELTYPE_RED] = c;
		ctx->color_count[IW_CHANNELTYPE_GREEN] = c;
		ctx->color_count[IW_CHANNELTYPE_BLUE] = c;
		ctx->color_count[IW_CHANNELTYPE_GRAY] = c;
		break;
	}
}

IW_IMPL(void) iw_set_channel_offset(struct iw_context *ctx, int channeltype, int dimension, double offs)
{
	if(channeltype<0 || channeltype>2) return;
	if(dimension<0 || dimension>1) dimension=0;
	ctx->resize_settings[dimension].channel_offset[channeltype] = offs;
}

IW_IMPL(void) iw_set_input_sbit(struct iw_context *ctx, int channeltype, int d)
{
	if(channeltype<0 || channeltype>4) return;
	ctx->significant_bits[channeltype] = d;
}

IW_IMPL(void) iw_set_input_bkgd_label(struct iw_context *ctx, double r, double g, double b)
{
	ctx->img1_bkgd_label.c[0] = r;
	ctx->img1_bkgd_label.c[1] = g;
	ctx->img1_bkgd_label.c[2] = b;
	ctx->img1_bkgd_label_set = 1;
}

IW_IMPL(int) iw_get_input_image_density(struct iw_context *ctx,
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

IW_IMPL(void) iw_make_srgb_csdescr(struct iw_csdescr *cs, int srgb_intent)
{
	cs->cstype = IW_CSTYPE_SRGB;
	cs->gamma = 0.0;
	cs->srgb_intent = srgb_intent;
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
	ctx->caller_set_output_csdescr = 1;
	ctx->warn_invalid_output_csdescr = 1;
	ctx->img2cs = *csdescr; // struct copy
	optimize_csdescr(&ctx->img2cs);
}

IW_IMPL(void) iw_set_input_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr)
{
	ctx->img1cs = *csdescr; // struct copy
	optimize_csdescr(&ctx->img1cs);
}

IW_IMPL(void) iw_set_apply_bkgd(struct iw_context *ctx, double r, double g, double b)
{
	ctx->apply_bkgd=1;
	ctx->caller_set_bkgd=1;
	ctx->bkgd.c[IW_CHANNELTYPE_RED]=r;
	ctx->bkgd.c[IW_CHANNELTYPE_GREEN]=g;
	ctx->bkgd.c[IW_CHANNELTYPE_BLUE]=b;
}

IW_IMPL(void) iw_set_bkgd_checkerboard(struct iw_context *ctx, int checksize,
    double r2, double g2, double b2)
{
	ctx->bkgd_checkerboard=1;
	ctx->bkgd_check_size=checksize;
	ctx->bkgd2.c[IW_CHANNELTYPE_RED]=r2;
	ctx->bkgd2.c[IW_CHANNELTYPE_GREEN]=g2;
	ctx->bkgd2.c[IW_CHANNELTYPE_BLUE]=b2;
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

IW_IMPL(int) iw_get_sample_size(void)
{
	return (int)sizeof(IW_SAMPLE);
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
		ctx->no_cslabel = n;
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
	case IW_VAL_DENSITY_POLICY:
		ctx->density_policy = n;
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
		ctx->compression = n;
		break;
	case IW_VAL_PAGE_TO_READ:
		ctx->page_to_read = n;
		break;
	case IW_VAL_INCLUDE_SCREEN:
		ctx->include_screen = n;
		break;
	case IW_VAL_JPEG_QUALITY:
		ctx->jpeg_quality = n;
		break;
	case IW_VAL_JPEG_SAMP_FACTOR_H:
		ctx->jpeg_samp_factor_h = n;
		break;
	case IW_VAL_JPEG_SAMP_FACTOR_V:
		ctx->jpeg_samp_factor_v = n;
		break;
	case IW_VAL_JPEG_ARITH_CODING:
		ctx->jpeg_arith_coding = n;
		break;
	case IW_VAL_DEFLATE_CMPR_LEVEL:
		ctx->deflatecmprlevel = n;
		break;
	case IW_VAL_OUTPUT_INTERLACED:
		ctx->interlaced = n;
		break;
	case IW_VAL_USE_BKGD_LABEL:
		ctx->use_bkgd_label = n;
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
		ret = ctx->no_cslabel;
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
	case IW_VAL_DENSITY_POLICY:
		ret = ctx->density_policy;
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
		ret = ctx->compression;
		break;
	case IW_VAL_PAGE_TO_READ:
		ret = ctx->page_to_read;
		break;
	case IW_VAL_INCLUDE_SCREEN:
		ret = ctx->include_screen;
		break;
	case IW_VAL_JPEG_QUALITY:
		ret = ctx->jpeg_quality;
		break;
	case IW_VAL_JPEG_SAMP_FACTOR_H:
		ret = ctx->jpeg_samp_factor_h;
		break;
	case IW_VAL_JPEG_SAMP_FACTOR_V:
		ret = ctx->jpeg_samp_factor_v;
		break;
	case IW_VAL_JPEG_ARITH_CODING:
		ret = ctx->jpeg_arith_coding;
		break;
	case IW_VAL_DEFLATE_CMPR_LEVEL:
		ret = ctx->deflatecmprlevel;
		break;
	case IW_VAL_OUTPUT_PALETTE_GRAYSCALE:
		ret = ctx->optctx.palette_is_grayscale;
		break;
	case IW_VAL_OUTPUT_INTERLACED:
		ret = ctx->interlaced;
		break;
	case IW_VAL_USE_BKGD_LABEL:
		ret = ctx->use_bkgd_label;
		break;
	}

	return ret;
}

IW_IMPL(void) iw_set_value_dbl(struct iw_context *ctx, int code, double n)
{
	switch(code) {
	case IW_VAL_WEBP_QUALITY:
		ctx->webp_quality = n;
		break;
	case IW_VAL_DENSITY_FORCED_X:
		ctx->density_forced_x = n;
		break;
	case IW_VAL_DENSITY_FORCED_Y:
		ctx->density_forced_y = n;
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
	case IW_VAL_WEBP_QUALITY:
		ret = ctx->webp_quality;
		break;
	case IW_VAL_DENSITY_FORCED_X:
		ret = ctx->density_forced_x;
		break;
	case IW_VAL_DENSITY_FORCED_Y:
		ret = ctx->density_forced_y;
		break;
	case IW_VAL_TRANSLATE_X:
		ret = ctx->resize_settings[IW_DIMENSION_H].translate;
		break;
	case IW_VAL_TRANSLATE_Y:
		ret = ctx->resize_settings[IW_DIMENSION_V].translate;
		break;
	}

	return ret;
}
