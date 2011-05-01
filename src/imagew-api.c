// imagew-api.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// Most of the functions declared in imagew.h are defined here.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "imagew-internals.h"

// Call the caller's warning function, if defined.
void iw_warning(struct iw_context *ctx, const char *fmt, ...)
{
	char buf[IW_ERRMSG_MAX];

	va_list ap;
	if(!ctx->warning_fn) return;

	va_start(ap, fmt);
	iw_vsnprintf(buf,IW_ERRMSG_MAX,fmt,ap);
	va_end(ap);

	(*ctx->warning_fn)(ctx,buf);
}

void iw_seterror(struct iw_context *ctx, const char *fmt, ...)
{
	va_list ap;

	if(ctx->error_flag) return; // Only record the first error.
	ctx->error_flag = 1;

	if(!ctx->error_msg) {
		ctx->error_msg=iw_malloc_lowlevel(IW_ERRMSG_MAX*sizeof(char));
		if(!ctx->error_msg) {
			return;
		}
	}

	va_start(ap, fmt);
	iw_vsnprintf(ctx->error_msg,IW_ERRMSG_MAX,fmt,ap);
	va_end(ap);
}

const char *iw_get_errormsg(struct iw_context *ctx, char *buf, int buflen)
{
	if(ctx->error_msg) {
		iw_snprintf(buf,buflen,"%s",ctx->error_msg);
	}
	else {
		iw_snprintf(buf,buflen,iwcore_get_string(ctx,iws_err_msg_not_avail));
	}

	return buf;
}

int iw_get_errorflag(struct iw_context *ctx)
{
	return ctx->error_flag;
}

size_t iw_calc_bytesperrow(int num_pixels, int bits_per_pixel)
{
	return (size_t)(((num_pixels*bits_per_pixel)+7)/8);
}

int iw_check_image_dimensons(struct iw_context *ctx, int w, int h)
{
	if(w>IW_MAX_DIMENSION || h>IW_MAX_DIMENSION) {
		iw_seterror(ctx,iwcore_get_string(ctx,iws_dimensions_too_large),w,h);
		return 0;
	}

	if(w<1 || h<1) {
		iw_seterror(ctx,iwcore_get_string(ctx,iws_dimensions_invalid),w,h);
		return 0;
	}

	return 1;
}

static void default_resize_settings(struct iw_resize_settings *rs)
{
	int i;
	rs->family = IW_RESIZETYPE_AUTO;
	rs->blur_factor = 1.0;
	for(i=0;i<3;i++) {
		rs->channel_offset[i] = 0.0;
	}
}

struct iw_stringtableentry iw_corestringtable[] = {
	{ iws_nomem, "Out of memory" },
	{ iws_warn_reduce_to_8, "Reducing depth to 8; required by the output format." },
	{ iws_warn_disable_offset_grayscale, "Disabling channel offset, due to grayscale output." },
	{ iws_warn_trans_incomp_format, "This image may have transparency, which is incompatible with the output format. A background color will be applied." },
	{ iws_warn_trans_incomp_offset, "This image may have transparency, which is incompatible with a channel offset. A background color will be applied." },
	{ iws_warn_chkb_incomp_offset, "Checkerboard backgrounds are not supported when using a channel offset." },
	{ iws_warn_output_forced_srgb, "Forcing output colorspace to sRGB; required by the output format." },
	{ iws_output_prof_not_set, "Output profile not set" },
	{ iws_internal_error, "Internal error" },
	{ iws_internal_unk_strategy, "Internal error, unknown strategy %d" },
	{ iws_image_too_large, "Image too large to process" },
	{ iws_dimensions_too_large, "Image dimensions too large (%d\xc3\x97%d)" },
	{ iws_dimensions_invalid, "Invalid image dimensions (%d\xc3\x97%d)" },
	{ iws_copyright, "Copyright \xc2\xa9 %s Jason Summers" },
	{ iws_err_msg_not_avail, "Error message not available" },
	{ 0, NULL }
};

static void init_context(struct iw_context *ctx)
{
	memset(ctx,0,sizeof(struct iw_context));

	ctx->max_malloc = IW_DEFAULT_MAX_MALLOC;
	iw_set_string_table(ctx,IW_STRINGTABLENUM_CORE,iw_corestringtable);

	default_resize_settings(&ctx->resize_settings[IW_DIMENSION_H]);
	default_resize_settings(&ctx->resize_settings[IW_DIMENSION_V]);
	default_resize_settings(&ctx->resize_settings_alpha);
	ctx->input_w = -1;
	ctx->input_h = -1;
	ctx->img1cs.cstype = IW_CSTYPE_SRGB;
	ctx->img2cs.cstype = IW_CSTYPE_SRGB;
	ctx->to_grayscale=0;
	ctx->edge_policy = IW_EDGE_POLICY_STANDARD;
	ctx->img1cs.sRGB_intent=IW_sRGB_INTENT_PERCEPTUAL;
	ctx->bkgd.c[IW_CHANNELTYPE_RED]=1.0; // Default background color
	ctx->bkgd.c[IW_CHANNELTYPE_GREEN]=0.0;
	ctx->bkgd.c[IW_CHANNELTYPE_BLUE]=1.0;
	ctx->colorspace_of_bkgd = IW_BKGDCOLORSPACE_LINEAR;
	ctx->pngcmprlevel = -1;
	ctx->opt_grayscale = 1;
	ctx->opt_palette = 1;
	ctx->opt_16_to_8 = 1;
	ctx->opt_strip_alpha = 1;
	ctx->opt_binary_trns = 1;
}

struct iw_context *iw_create_context(void)
{
	struct iw_context *ctx;

	ctx = iw_malloc_lowlevel(sizeof(struct iw_context));
	if(!ctx) return NULL;
	init_context(ctx);
	return ctx;
}

void iw_destroy_context(struct iw_context *ctx)
{
	if(!ctx) return;
	if(ctx->img1.pixels) iw_free(ctx->img1.pixels);
	if(ctx->img2.pixels) iw_free(ctx->img2.pixels);
	if(ctx->error_msg) iw_free(ctx->error_msg);
	if(ctx->optctx.tmp_pixels) iw_free(ctx->optctx.tmp_pixels);
	if(ctx->optctx.palette) iw_free(ctx->optctx.palette);
	if(ctx->input_color_corr_table) iw_free(ctx->input_color_corr_table);
	if(ctx->output_rev_color_corr_table) iw_free(ctx->output_rev_color_corr_table);
	iw_free(ctx);
}

void iw_get_output_image(struct iw_context *ctx, struct iw_image *img)
{
	memset(img,0,sizeof(struct iw_image));
	img->width = ctx->optctx.width;
	img->height = ctx->optctx.height;
	img->imgtype = ctx->optctx.imgtype;
	img->sampletype = ctx->output_sampletype;
	img->bit_depth = ctx->optctx.bit_depth;
	img->pixels = (unsigned char*)ctx->optctx.pixelsptr;
	img->bpr = ctx->optctx.bpr;
	img->density_code = ctx->img2.density_code;
	img->density_x = ctx->img2.density_x;
	img->density_y = ctx->img2.density_y;
	img->has_colorkey_trns = ctx->optctx.has_colorkey_trns;
	img->colorkey_r = ctx->optctx.colorkey_r;
	img->colorkey_g = ctx->optctx.colorkey_g;
	img->colorkey_b = ctx->optctx.colorkey_b;
}

void iw_get_output_colorspace(struct iw_context *ctx, struct iw_csdescr *csdescr)
{
	*csdescr = ctx->img2cs; // struct copy
}

const struct iw_palette *iw_get_output_palette(struct iw_context *ctx)
{
	return ctx->optctx.palette;
}

void iw_set_output_canvas_size(struct iw_context *ctx, int w, int h)
{
	ctx->canvas_width = w;
	ctx->canvas_height = h;
}

void iw_set_input_crop(struct iw_context *ctx, int x, int y, int w, int h)
{
	ctx->input_start_x = x;
	ctx->input_start_y = y;
	ctx->input_w = w;
	ctx->input_h = h;
}

void iw_set_output_profile(struct iw_context *ctx, unsigned int n)
{
	ctx->output_profile = n;
}

void iw_set_output_depth(struct iw_context *ctx, int bps)
{
	ctx->output_depth = bps;
}

void iw_set_dither_type(struct iw_context *ctx, int channeltype, int f, int s)
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

void iw_set_color_count(struct iw_context *ctx, int channeltype, int c)
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

void iw_set_channel_offset(struct iw_context *ctx, int channeltype, int dimension, double offs)
{
	if(channeltype<0 || channeltype>2) return;
	if(dimension<0 || dimension>1) dimension=0;

	if(offs != 0.0) ctx->offset_color_channels=1;

	ctx->resize_settings[dimension].channel_offset[channeltype] = offs;
}

void iw_set_input_sbit(struct iw_context *ctx, int channeltype, int d)
{
	if(channeltype<0 || channeltype>4) return;
	ctx->significant_bits[channeltype] = d;
}

void iw_set_input_bkgd_label(struct iw_context *ctx, double r, double g, double b)
{
	ctx->img1_bkgd_label.c[0] = r;
	ctx->img1_bkgd_label.c[1] = g;
	ctx->img1_bkgd_label.c[2] = b;
	ctx->img1_bkgd_label_set = 1;
}

int iw_get_input_image_density(struct iw_context *ctx,
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

void iw_set_output_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr,
			int warn_if_invalid)
{
	ctx->caller_set_output_csdescr = 1;
	ctx->warn_invalid_output_csdescr = warn_if_invalid;
	ctx->img2cs = *csdescr; // struct copy

	// Detect linear colorspace:
	if(ctx->img2cs.cstype == IW_CSTYPE_GAMMA) {
		if(ctx->img2cs.gamma>=0.999995 && ctx->img2cs.gamma<=1.000005) {
			ctx->img2cs.cstype = IW_CSTYPE_LINEAR;
		}
	}
}

void iw_set_input_colorspace(struct iw_context *ctx, const struct iw_csdescr *csdescr)
{
	ctx->img1cs = *csdescr; // struct copy

	// Detect linear colorspace:
	if(ctx->img1cs.cstype == IW_CSTYPE_GAMMA) {
		if(ctx->img1cs.gamma>=0.999995 && ctx->img1cs.gamma<=1.000005) {
			ctx->img1cs.cstype = IW_CSTYPE_LINEAR;
		}
	}
}

void iw_set_applybkgd(struct iw_context *ctx, int cs, double r, double g, double b)
{
	ctx->apply_bkgd=1;
	ctx->caller_set_bkgd=1;
	ctx->colorspace_of_bkgd=cs;
	ctx->bkgd.c[IW_CHANNELTYPE_RED]=r;
	ctx->bkgd.c[IW_CHANNELTYPE_GREEN]=g;
	ctx->bkgd.c[IW_CHANNELTYPE_BLUE]=b;
}

void iw_set_bkgd_checkerboard(struct iw_context *ctx, int checksize,
    double r2, double g2, double b2)
{
	ctx->bkgd_checkerboard=1;
	ctx->bkgd_check_size=checksize;
	ctx->bkgd2.c[IW_CHANNELTYPE_RED]=r2;
	ctx->bkgd2.c[IW_CHANNELTYPE_GREEN]=g2;
	ctx->bkgd2.c[IW_CHANNELTYPE_BLUE]=b2;
}

void iw_set_bkgd_checkerboard_origin(struct iw_context *ctx, int x, int y)
{
	ctx->bkgd_check_origin[IW_DIMENSION_H] = x;
	ctx->bkgd_check_origin[IW_DIMENSION_V] = y;
}

void iw_set_max_malloc(struct iw_context *ctx, size_t n)
{
	ctx->max_malloc = n;
}

void iw_set_random_seed(struct iw_context *ctx, int randomize, int rand_seed)
{
	ctx->randomize = randomize;
	ctx->random_seed = rand_seed;
}

void iw_set_userdata(struct iw_context *ctx, void *userdata)
{
	ctx->userdata = userdata;
}

void *iw_get_userdata(struct iw_context *ctx)
{
	return ctx->userdata;
}

void iw_set_warning_fn(struct iw_context *ctx, iw_warningfn_type warnfn)
{
	ctx->warning_fn = warnfn;
}

void iw_set_input_image(struct iw_context *ctx, const struct iw_image *img)
{
	ctx->img1 = *img; // struct copy
}

void iw_set_resize_alg(struct iw_context *ctx, int channeltype, int dimension, int family,
    double blur, double param1, double param2)
{
	struct iw_resize_settings *rs;

	if(dimension<0 || dimension>1) dimension=0;
	if(channeltype==IW_CHANNELTYPE_ALPHA) {
		ctx->use_resize_settings_alpha=1;
		rs=&ctx->resize_settings_alpha;
	}
	else {
		rs=&ctx->resize_settings[dimension];
	}

	rs->family = family;
	rs->blur_factor = blur;
	rs->radius = 1.0; // default

	switch(family) {
	case IW_RESIZETYPE_QUADRATIC:
		rs->radius = 1.5;
		break;
	case IW_RESIZETYPE_CUBIC:
		rs->radius = 2.0;
		if(param1 < -10.0) param1= -10.0;
		if(param1 >  10.0) param1=  10.0;
		if(param2 < -10.0) param2= -10.0;
		if(param2 >  10.0) param2=  10.0;
		ctx->resize_settings[dimension].param1 = param1; // B
		ctx->resize_settings[dimension].param2 = param2; // C
		break;
	case IW_RESIZETYPE_GAUSSIAN:
		rs->radius = 2.0;
		break;
	case IW_RESIZETYPE_LANCZOS:
	case IW_RESIZETYPE_HANNING:
	case IW_RESIZETYPE_BLACKMAN:
	case IW_RESIZETYPE_SINC:
		rs->radius = floor(param1+0.5); // "lobes"
		if(rs->radius<2.0) rs->radius=2.0;
		if(rs->radius>10.0) rs->radius=10.0;
		break;
	}
}

void iw_set_resize_withradius(struct iw_context *ctx, int dimension, int r, int lobes)
{
	struct iw_resize_settings *rs;

	if(dimension<0 || dimension>1) dimension=0;
	rs=&ctx->resize_settings[dimension];
	rs->family = r;
	if(lobes<2) lobes=2;
	if(lobes>10) lobes=10;
	rs->radius = (double)lobes;
}

int iw_get_sample_size(void)
{
	return (int)sizeof(IW_SAMPLE);
}

int iw_get_version_int(void)
{
	return IW_VERSION_INT;
}

char *iw_get_version_string(struct iw_context *ctx, char *s, int s_len)
{
	int ver;
	ver = iw_get_version_int();
	iw_snprintf(s,s_len,"%d.%d.%d",
		(ver&0xff0000)>>16, (ver&0xff00)>>8, (ver&0xff) );
	return s;
}

char *iw_get_copyright_string(struct iw_context *ctx, char *s, int s_len)
{
	if(ctx) {
		iw_snprintf(s,s_len,iwcore_get_string(ctx,iws_copyright),IW_COPYRIGHT_YEAR);
	}
	else {
		iw_snprintf(s,s_len,iw_get_string_direct(iw_corestringtable,iws_copyright),IW_COPYRIGHT_YEAR);
	}
	return s;
}

void iw_set_allow_opt(struct iw_context *ctx, int opt, int n)
{
	unsigned char v;
	v = n?1:0;

	switch(opt) {
	case IW_OPT_GRAYSCALE: ctx->opt_grayscale = v; break;
	case IW_OPT_PALETTE: ctx->opt_palette = v; break;
	case IW_OPT_16_TO_8: ctx->opt_16_to_8 = v; break;
	case IW_OPT_STRIP_ALPHA: ctx->opt_strip_alpha = v; break;
	case IW_OPT_BINARY_TRNS: ctx->opt_binary_trns = v; break;
	}
}

// Set a string table if it's not already set.
// You can always reset a string table by setting st=NULL.
void iw_set_string_table(struct iw_context *ctx, int tablenum,
	const struct iw_stringtableentry *st)
{
	if(tablenum<0 || tablenum>=IW_NUMSTRINGTABLES) return;

	if(st==NULL || ctx->stringtable[tablenum]==NULL) {
		ctx->stringtable[tablenum] = st;
	}
}

void iw_set_value(struct iw_context *ctx, int code, int n)
{
	switch(code) {
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
	case IW_VAL_EDGE_POLICY:
		ctx->edge_policy = n;
		break;
	case IW_VAL_GRAYSCALE_FORMULA:
		ctx->grayscale_formula = n;
		break;
	case IW_VAL_INPUT_NATIVE_GRAYSCALE:
		ctx->img1.native_grayscale = n;
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
	case IW_VAL_PNG_CMPR_LEVEL:
		ctx->pngcmprlevel = n;
		break;
	case IW_VAL_OUTPUT_INTERLACED:
		ctx->interlaced = n;
		break;
	case IW_VAL_USE_BKGD_LABEL:
		ctx->use_bkgd_label = n;
		break;
	}
}

int iw_get_value(struct iw_context *ctx, int code)
{
	int ret=0;

	switch(code) {
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
	case IW_VAL_EDGE_POLICY:
		ret = ctx->edge_policy;
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
	case IW_VAL_JPEG_QUALITY:
		ret = ctx->jpeg_quality;
		break;
	case IW_VAL_JPEG_SAMP_FACTOR_H:
		ret = ctx->jpeg_samp_factor_h;
		break;
	case IW_VAL_JPEG_SAMP_FACTOR_V:
		ret = ctx->jpeg_samp_factor_v;
		break;
	case IW_VAL_PNG_CMPR_LEVEL:
		ret = ctx->pngcmprlevel;
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

// Get a string, given a pointer to a string table.
const char *iw_get_string_direct(const struct iw_stringtableentry *st, int n)
{
	int i;

	if(!st) {
		return "[missing string table]";
	}

	for(i=0; st[i].s!=NULL; i++) {
		if(st[i].n==n) {
			return st[i].s;
		}
	}
	return "[missing string]";

}

// Get a string, given a string table number
const char *iw_get_string(struct iw_context *ctx, int tablenum, int n)
{
	if(tablenum<0 || tablenum>=IW_NUMSTRINGTABLES || !ctx) {
		return "[missing string table]";
	}

	return iw_get_string_direct(ctx->stringtable[tablenum],n);
}
