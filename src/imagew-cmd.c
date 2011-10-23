// imagew-cmd.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// This file implements a command-line application, and is not
// part of the ImageWorsener library.

// Note that applications that are not distributed with ImageWorsener are
// not expected to include imagew-config.h.
#include "imagew-config.h"

#ifdef IW_WINDOWS
#define IW_NO_LOCALE
#endif

#ifdef IW_WINDOWS
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifdef IW_WINDOWS
#include <malloc.h>
#include <fcntl.h>
#include <io.h> // for _setmode
#endif

#ifndef IW_NO_LOCALE
#include <locale.h>
#include <langinfo.h>
#endif

#include "imagew.h"

#ifdef IW_WINDOWS
#include <strsafe.h>
#endif

#define IWCMD_ENCODING_AUTO   0
#define IWCMD_ENCODING_ASCII  1
#define IWCMD_ENCODING_UTF8   2
#define IWCMD_ENCODING_UTF16  3

struct rgb_color {
	double r,g,b;
};

struct resize_alg {
	int family;
	double blur;
	int lobes;
	double b,c;
};

struct params_struct {
	const char *infn;
	const char *outfn;
	int nowarn;
	int noinfo;
	int new_width;
	int new_height;
	int rel_width_flag, rel_height_flag;
	double rel_width, rel_height;
	struct resize_alg resize_alg_x;
	struct resize_alg resize_alg_y;
	struct resize_alg resize_alg_alpha;
	int bestfit;
	int depth;
	int grayscale, condgrayscale;
	double offset_r_h, offset_g_h, offset_b_h;
	double offset_r_v, offset_g_v, offset_b_v;
	int dither_family_all, dither_family_nonalpha, dither_family_alpha;
	int dither_family_red, dither_family_green, dither_family_blue, dither_family_gray;
	int dither_subtype_all, dither_subtype_nonalpha, dither_subtype_alpha;
	int dither_subtype_red, dither_subtype_green, dither_subtype_blue, dither_subtype_gray;
	int color_count_all, color_count_nonalpha, color_count_alpha;
	int color_count_red, color_count_green, color_count_blue, color_count_gray;
	int apply_bkgd;
	int bkgd_checkerboard;
	int bkgd_check_size;
	int bkgd_check_origin_x, bkgd_check_origin_y;
	int use_bkgd_label;
	int use_crop, crop_x, crop_y, crop_w, crop_h;
	struct rgb_color bkgd;
	struct rgb_color bkgd2;
	int page_to_read;
	int jpeg_quality;
	int jpeg_samp_factor_h, jpeg_samp_factor_v;
	double webp_quality;
	int pngcmprlevel;
	int pngcmprlevel_set;
	int interlace;
	int randomize;
	int random_seed;
	int infmt;
	int outfmt;
	int no_gamma;
	int intclamp;
	int edge_policy;
	int density_policy;
	int grayscale_formula;
	int no_cslabel;
	int include_screen;
	int noopt_grayscale,noopt_binarytrns,noopt_palette;
	int noopt_reduceto8,noopt_stripalpha;
	int cs_in_set, cs_out_set;
	struct iw_csdescr cs_in;
	struct iw_csdescr cs_out;
	int output_encoding;
	int output_encoding_setmode;
	int density_code;
	double xdens, ydens;
};

#ifdef _UNICODE
static char *iwcmd_utf16_to_utf8_strdup(const WCHAR *src)
{
	char *dst;
	int dstlen;
	int ret;

	// Calculate the size required by the target string.
	ret = WideCharToMultiByte(CP_UTF8,0,src,-1,NULL,0,NULL,NULL);
	if(ret<1) return NULL;

	dstlen = ret;
	dst = (char*)malloc(dstlen*sizeof(char));
	if(!dst) return NULL;

	ret = WideCharToMultiByte(CP_UTF8,0,src,-1,dst,dstlen,NULL,NULL);
	if(ret<1) {
		free(dst);
		return NULL;
	}
	return dst;
}

static WCHAR *iwcmd_utf8_to_utf16_strdup(const char *src)
{
	WCHAR *dst;
	int dstlen;
	int ret;

	// Calculate the size required by the target string.
	ret = MultiByteToWideChar(CP_UTF8,0,src,-1,NULL,0);
	if(ret<1) return NULL;

	dstlen = ret;
	dst = (WCHAR*)malloc(dstlen*sizeof(WCHAR));
	if(!dst) return NULL;

	ret = MultiByteToWideChar(CP_UTF8,0,src,-1,dst,dstlen);
	if(ret<1) {
		free(dst);
		return NULL;
	}
	return dst;
}

static void iwcmd_utf8_to_utf16(const char *src, WCHAR *dst, int dstlen)
{
	MultiByteToWideChar(CP_UTF8,0,src,-1,dst,dstlen);
}
#endif

#ifdef _UNICODE
static void iwcmd_utf8_to_wchar(const char *src, WCHAR *dst, int dstlen)
{
	iwcmd_utf8_to_utf16(src,dst,dstlen);
}
#endif

// Output a NUL-terminated string.
// The input string is encoded in UTF-8.
// If the output encoding is not UTF-8, it will be converted.
static void iwcmd_puts_utf8(struct params_struct *p, const char *s)
{
	char buf[500];
#ifdef _UNICODE
	WCHAR bufW[500];
#endif

	switch(p->output_encoding) {
#ifdef _UNICODE
	case IWCMD_ENCODING_UTF16:
		iwcmd_utf8_to_wchar(s,bufW,sizeof(bufW)/sizeof(WCHAR));
		fputws(bufW,stdout);
		break;
#endif
	case IWCMD_ENCODING_UTF8:
		fputs(s,stdout);
		break;
	default:
		iw_utf8_to_ascii(s,buf,sizeof(buf));
		fputs(buf,stdout);
	}
}

static void iwcmd_vprint_utf8(struct params_struct *p, const char *fmt, va_list ap)
{
	char buf[500];

#ifdef IW_WINDOWS

	StringCbVPrintfA(buf,sizeof(buf),fmt,ap);

#else

	vsnprintf(buf,sizeof(buf),fmt,ap);
	buf[sizeof(buf)-1]='\0';

#endif

	iwcmd_puts_utf8(p,buf);
}

static void iwcmd_message(struct params_struct *p, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iwcmd_vprint_utf8(p,fmt,ap);
	va_end(ap);
}

static void iwcmd_warning(struct params_struct *p, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iwcmd_vprint_utf8(p,fmt,ap);
	va_end(ap);
}

static void iwcmd_error(struct params_struct *p, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iwcmd_vprint_utf8(p,fmt,ap);
	va_end(ap);
}

// Wrappers for fopen()
#if defined(IW_WINDOWS) && defined(_UNICODE)

static FILE* iwcmd_fopen(const char *fn, const char *mode)
{
	FILE *f = NULL;
	errno_t ret;
	WCHAR *fnW;
	WCHAR *modeW;

	fnW = iwcmd_utf8_to_utf16_strdup(fn);
	modeW = iwcmd_utf8_to_utf16_strdup(mode);

	ret = _wfopen_s(&f,fnW,modeW);

	free(fnW);
	free(modeW);

	if(ret!=0) {
		// failure
		if(f) fclose(f);
		f=NULL;
	}
	return f;
}

#elif defined(IW_WINDOWS) && !defined(_UNICODE)

static FILE* iwcmd_fopen(const char *fn, const char *mode)
{
	FILE *f = NULL;
	errno_t ret;

	ret = fopen_s(&f,fn,mode);
	if(ret!=0) {
		// failure
		if(f) fclose(f);
		f=NULL;
	}
	return f;
}

#else

static FILE* iwcmd_fopen(const char *fn, const char *mode)
{
	return fopen(fn,mode);
}

#endif

static void my_warning_handler(struct iw_context *ctx, const char *msg)
{
	struct params_struct *p;
	p = (struct params_struct *)iw_get_userdata(ctx);
	if(!p->nowarn) {
		iwcmd_warning(p,"Warning: %s\n",msg);
	}
}

// This is used to process the parameter of -infmt/-outfmt.
static int get_fmt_from_name(const char *s)
{
	if(!strcmp(s,"png")) return IW_FORMAT_PNG;
	if(!strcmp(s,"jpg")) return IW_FORMAT_JPEG;
	if(!strcmp(s,"jpeg")) return IW_FORMAT_JPEG;
	if(!strcmp(s,"bmp")) return IW_FORMAT_BMP;
	if(!strcmp(s,"tif")) return IW_FORMAT_TIFF;
	if(!strcmp(s,"tiff")) return IW_FORMAT_TIFF;
	if(!strcmp(s,"miff")) return IW_FORMAT_MIFF;
	if(!strcmp(s,"webp")) return IW_FORMAT_WEBP;
	if(!strcmp(s,"gif")) return IW_FORMAT_GIF;
	return IW_FORMAT_UNKNOWN;
}

static const char *get_fmt_name(int fmt)
{
	static const char *n;
	n="(unknown)";
	switch(fmt) {
	case IW_FORMAT_PNG:  n="PNG";  break;
	case IW_FORMAT_JPEG: n="JPEG"; break;
	case IW_FORMAT_BMP:  n="BMP";  break;
	case IW_FORMAT_TIFF: n="TIFF"; break;
	case IW_FORMAT_MIFF: n="MIFF"; break;
	case IW_FORMAT_WEBP: n="WebP"; break;
	case IW_FORMAT_GIF:  n="GIF";  break;
	}
	return n;
}

// Reads the first few bytes in the file to try to figure out
// the file format. Then sets the file pointer back to the
// beginning of the file.
static int detect_fmt_of_file(FILE *fp)
{
	unsigned char buf[12];
	size_t n;

	n=fread(buf,1,12,fp);
	clearerr(fp);
	fseek(fp,0,SEEK_SET);
	if(n<2) return IW_FORMAT_UNKNOWN;

	return iw_detect_fmt_of_file((const iw_byte*)buf,n);
}

// Updates p->new_width and p->new_height
static void do_bestfit(struct params_struct *p, int old_width, int old_height)
{
	int x;
	double exp_factor;

	// If we fit-width, what would the height be?
	exp_factor = ((double)p->new_width) / old_width;
	exp_factor *= (p->xdens/p->ydens);
	x = (int)(0.5+ ((double)old_height) * exp_factor);
	if(x<=p->new_height) {
		// It fits. Use it.
		p->new_height = x;
		goto done;
	}
	
	// Fit to height instead.
	exp_factor = ((double)p->new_height) / old_height;
	exp_factor *= (p->ydens/p->xdens);
	x = (int)(0.5+ ((double)old_width) * exp_factor);
	if(x<p->new_width) {
		p->new_width = x;
	}

done:
	if(p->new_width<1) p->new_width=1;
	if(p->new_height<1) p->new_height=1;
}

static void iwcmd_set_resize(struct iw_context *ctx, int channel, int dimension, struct resize_alg *alg)
{
	switch(alg->family) {
	case IW_RESIZETYPE_CUBIC:
		iw_set_resize_alg(ctx,channel,dimension,alg->family,alg->blur,alg->b,alg->c);
		break;
	case IW_RESIZETYPE_LANCZOS: case IW_RESIZETYPE_HANNING:
	case IW_RESIZETYPE_BLACKMAN: case IW_RESIZETYPE_SINC:
		iw_set_resize_alg(ctx,channel,dimension,alg->family,alg->blur,alg->lobes,0.0);
		break;
	default:
		iw_set_resize_alg(ctx,channel,dimension,alg->family,alg->blur,0.0,0.0);
	}
}

static int my_readfn(struct iw_context *ctx, struct iw_iodescr *iodescr, void *buf, size_t nbytes,
   size_t *pbytesread)
{
	*pbytesread = fread(buf,1,nbytes,(FILE*)iodescr->fp);
	return 1;
}

static int my_getfilesizefn(struct iw_context *ctx, struct iw_iodescr *iodescr, iw_int64 *pfilesize)
{
	int ret;
	long lret;
	FILE *fp = (FILE*)iodescr->fp;

	// TODO: Rewrite this to support >4GB file sizes.
	ret=fseek(fp,0,SEEK_END);
	if(ret!=0) return 0;
	lret=ftell(fp);
	if(lret<0) return 0;
	*pfilesize = (iw_int64)lret;
	fseek(fp,0,SEEK_SET);
	return 1;
}

static int my_writefn(struct iw_context *ctx, struct iw_iodescr *iodescr, const void *buf, size_t nbytes)
{
	fwrite(buf,1,nbytes,(FILE*)iodescr->fp);
	return 1;
}

static int iwcmd_calc_rel_size(double rel, int d)
{
	int n;
	n = (int)(0.5 + rel * (double)d);
	if(n<1) n=1;
	return n;
}

static int is_input_fmt_supported(int fmt)
{
	switch(fmt) {
#if IW_SUPPORT_PNG == 1
	case IW_FORMAT_PNG:
#endif
#if IW_SUPPORT_JPEG == 1
	case IW_FORMAT_JPEG:
#endif
#if IW_SUPPORT_WEBP == 1
	case IW_FORMAT_WEBP:
#endif
	case IW_FORMAT_MIFF:
	case IW_FORMAT_GIF:
		return 1;
	}
	return 0;
}

static int is_output_fmt_supported(int fmt)
{
	switch(fmt) {
#if IW_SUPPORT_PNG == 1
	case IW_FORMAT_PNG:
#endif
#if IW_SUPPORT_JPEG == 1
	case IW_FORMAT_JPEG:
#endif
#if IW_SUPPORT_WEBP == 1
	case IW_FORMAT_WEBP:
#endif
	case IW_FORMAT_BMP:
	case IW_FORMAT_TIFF:
	case IW_FORMAT_MIFF:
		return 1;
	}
	return 0;
}

static int run(struct params_struct *p)
{
	int retval = 0;
	struct iw_context *ctx = NULL;
	int imgtype_read;
	int old_width,old_height;
	struct iw_iodescr readdescr;
	struct iw_iodescr writedescr;
	char errmsg[200];

	memset(&readdescr,0,sizeof(struct iw_iodescr));
	memset(&writedescr,0,sizeof(struct iw_iodescr));

	if(!p->noinfo) {
		iwcmd_message(p,"%s \xe2\x86\x92 %s\n",p->infn,p->outfn);
	}

	ctx = iw_create_context();
	if(!ctx) goto done;

	iw_set_api_version(ctx,IW_VERSION_INT);
	iw_set_userdata(ctx,(void*)p);
	iw_set_warning_fn(ctx,my_warning_handler);

	// Decide on the output format as early as possible, so we can give up
	// quickly if it's not supported.
	if(p->outfmt==IW_FORMAT_UNKNOWN)
		p->outfmt=iw_detect_fmt_from_filename(p->outfn);

	if(p->outfmt==IW_FORMAT_UNKNOWN) {
		iw_set_error(ctx,"Unknown output format; use -outfmt.");
		goto done;
	}
	else if(!is_output_fmt_supported(p->outfmt)) {
		iw_set_errorf(ctx,"Writing %s files is not supported.",get_fmt_name(p->outfmt));
		goto done;
	}

	if(p->random_seed!=0 || p->randomize) {
		iw_set_random_seed(ctx,p->randomize, p->random_seed);
	}

	if(p->no_gamma) iw_set_value(ctx,IW_VAL_DISABLE_GAMMA,1);
	if(p->intclamp) iw_set_value(ctx,IW_VAL_INT_CLAMP,1);
	if(p->no_cslabel) iw_set_value(ctx,IW_VAL_NO_CSLABEL,1);
	if(p->noopt_grayscale) iw_set_allow_opt(ctx,IW_OPT_GRAYSCALE,0);
	if(p->noopt_palette) iw_set_allow_opt(ctx,IW_OPT_PALETTE,0);
	if(p->noopt_reduceto8) iw_set_allow_opt(ctx,IW_OPT_16_TO_8,0);
	if(p->noopt_stripalpha) iw_set_allow_opt(ctx,IW_OPT_STRIP_ALPHA,0);
	if(p->noopt_binarytrns) iw_set_allow_opt(ctx,IW_OPT_BINARY_TRNS,0);
	if(p->edge_policy>=0) iw_set_value(ctx,IW_VAL_EDGE_POLICY,p->edge_policy);
	if(p->density_policy>=0) iw_set_value(ctx,IW_VAL_DENSITY_POLICY,p->density_policy);
	if(p->grayscale_formula>0) iw_set_value(ctx,IW_VAL_GRAYSCALE_FORMULA,p->grayscale_formula);
	if(p->page_to_read>0) iw_set_value(ctx,IW_VAL_PAGE_TO_READ,p->page_to_read);
	if(p->include_screen>=0) iw_set_value(ctx,IW_VAL_INCLUDE_SCREEN,p->include_screen);

	readdescr.read_fn = my_readfn;
	readdescr.getfilesize_fn = my_getfilesizefn;
	readdescr.fp = (void*)iwcmd_fopen(p->infn,"rb");
	if(!readdescr.fp) {
		iw_set_errorf(ctx,"Failed to open for reading (error code=%d)",(int)errno);
		goto done;
	}

	// Decide on the input format.
	if(p->infmt==IW_FORMAT_UNKNOWN)
		p->infmt=detect_fmt_of_file((FILE*)readdescr.fp);

	if(p->infmt==IW_FORMAT_UNKNOWN) {
		iw_set_error(ctx,"Unknown input file format.");
		goto done;
	}
	else if(!is_input_fmt_supported(p->infmt)) {
		iw_set_errorf(ctx,"Reading %s files is not supported.",get_fmt_name(p->infmt));
	}

	switch(p->infmt) {
#if IW_SUPPORT_PNG == 1
	case IW_FORMAT_PNG:
		if(!iw_read_png_file(ctx,&readdescr)) goto done;
		break;
#endif
#if IW_SUPPORT_JPEG == 1
	case IW_FORMAT_JPEG:
		if(!iw_read_jpeg_file(ctx,&readdescr)) goto done;
		break;
#endif
#if IW_SUPPORT_WEBP == 1
	case IW_FORMAT_WEBP:
		if(!iw_read_webp_file(ctx,&readdescr)) goto done;
		break;
#endif
	case IW_FORMAT_MIFF:
		if(!iw_read_miff_file(ctx,&readdescr)) goto done;
		break;
	case IW_FORMAT_GIF:
		if(!iw_read_gif_file(ctx,&readdescr)) goto done;
		break;
	default:
		goto done;
	}

	fclose((FILE*)readdescr.fp);
	readdescr.fp=NULL;

	imgtype_read = iw_get_value(ctx,IW_VAL_INPUT_IMAGE_TYPE);

	// We have to tell the library the output format, so it can know what
	// kinds of images are allowed (e.g. whether transparency is allowed).
	iw_set_output_profile(ctx, iw_get_profile_by_fmt(p->outfmt));

	if(p->depth != -1) {
		iw_set_output_depth(ctx,p->depth);
	}

	if(p->cs_in_set) {
		iw_set_input_colorspace(ctx,&p->cs_in);
	}
	if(p->cs_out_set) {
		iw_set_output_colorspace(ctx,&p->cs_out);
	}

	if(p->resize_alg_x.family) {
		iwcmd_set_resize(ctx,IW_CHANNELTYPE_ALL,IW_DIMENSION_H,&p->resize_alg_x);
	}
	if(p->resize_alg_y.family) {
		iwcmd_set_resize(ctx,IW_CHANNELTYPE_ALL,IW_DIMENSION_V,&p->resize_alg_y);
	}
	if(p->resize_alg_alpha.family) {
		iwcmd_set_resize(ctx,IW_CHANNELTYPE_ALPHA,IW_DIMENSION_V,&p->resize_alg_alpha);
	}

	if( (!p->resize_alg_x.family && p->resize_alg_x.blur!=1.0) ||
		(!p->resize_alg_y.family && p->resize_alg_y.blur!=1.0) ||
		(!p->resize_alg_alpha.family && p->resize_alg_alpha.blur!=1.0) )
	{
		if(!p->nowarn)
			iwcmd_warning(p,"Warning: -blur option requires -filter\n");
	}

	if(p->dither_family_all)   iw_set_dither_type(ctx,IW_CHANNELTYPE_ALL  ,p->dither_family_all  ,p->dither_subtype_all);
	if(p->dither_family_nonalpha) iw_set_dither_type(ctx,IW_CHANNELTYPE_NONALPHA,p->dither_family_nonalpha,p->dither_subtype_nonalpha);
	if(p->dither_family_red)   iw_set_dither_type(ctx,IW_CHANNELTYPE_RED  ,p->dither_family_red  ,p->dither_subtype_red);
	if(p->dither_family_green) iw_set_dither_type(ctx,IW_CHANNELTYPE_GREEN,p->dither_family_green,p->dither_subtype_green);
	if(p->dither_family_blue)  iw_set_dither_type(ctx,IW_CHANNELTYPE_BLUE ,p->dither_family_blue ,p->dither_subtype_blue);
	if(p->dither_family_gray)  iw_set_dither_type(ctx,IW_CHANNELTYPE_GRAY ,p->dither_family_gray ,p->dither_subtype_gray);
	if(p->dither_family_alpha) iw_set_dither_type(ctx,IW_CHANNELTYPE_ALPHA,p->dither_family_alpha,p->dither_subtype_alpha);

	if(p->color_count_all) iw_set_color_count  (ctx,IW_CHANNELTYPE_ALL  ,p->color_count_all);
	if(p->color_count_nonalpha) iw_set_color_count(ctx,IW_CHANNELTYPE_NONALPHA,p->color_count_nonalpha);
	if(p->color_count_red)   iw_set_color_count(ctx,IW_CHANNELTYPE_RED  ,p->color_count_red);
	if(p->color_count_green) iw_set_color_count(ctx,IW_CHANNELTYPE_GREEN,p->color_count_green);
	if(p->color_count_blue)  iw_set_color_count(ctx,IW_CHANNELTYPE_BLUE ,p->color_count_blue);
	if(p->color_count_gray)  iw_set_color_count(ctx,IW_CHANNELTYPE_GRAY ,p->color_count_gray);
	if(p->color_count_alpha) iw_set_color_count(ctx,IW_CHANNELTYPE_ALPHA,p->color_count_alpha);

	if(p->grayscale) {
		iw_set_value(ctx,IW_VAL_CVT_TO_GRAYSCALE,1);
	}
	else if(p->condgrayscale) {
		if(iw_get_value(ctx,IW_VAL_INPUT_NATIVE_GRAYSCALE)) {
			iw_set_value(ctx,IW_VAL_CVT_TO_GRAYSCALE,1);
		}
	}

	if(p->offset_r_h!=0.0) iw_set_channel_offset(ctx,IW_CHANNELTYPE_RED,  IW_DIMENSION_H,p->offset_r_h);
	if(p->offset_g_h!=0.0) iw_set_channel_offset(ctx,IW_CHANNELTYPE_GREEN,IW_DIMENSION_H,p->offset_g_h);
	if(p->offset_b_h!=0.0) iw_set_channel_offset(ctx,IW_CHANNELTYPE_BLUE, IW_DIMENSION_H,p->offset_b_h);
	if(p->offset_r_v!=0.0) iw_set_channel_offset(ctx,IW_CHANNELTYPE_RED,  IW_DIMENSION_V,p->offset_r_v);
	if(p->offset_g_v!=0.0) iw_set_channel_offset(ctx,IW_CHANNELTYPE_GREEN,IW_DIMENSION_V,p->offset_g_v);
	if(p->offset_b_v!=0.0) iw_set_channel_offset(ctx,IW_CHANNELTYPE_BLUE, IW_DIMENSION_V,p->offset_b_v);

	if(p->apply_bkgd) {

		// iw_set_applybkgd() requires background color to be in a linear
		// colorspace, so convert it (from sRGB) if needed.
		if(!p->no_gamma) {
			struct iw_csdescr cs_srgb;

			// Make an sRGB descriptor to use with iw_convert_sample_to_linear.
			iw_make_srgb_csdescr(&cs_srgb,IW_SRGB_INTENT_PERCEPTUAL);

			p->bkgd.r = iw_convert_sample_to_linear(p->bkgd.r,&cs_srgb);
			p->bkgd.g = iw_convert_sample_to_linear(p->bkgd.g,&cs_srgb);
			p->bkgd.b = iw_convert_sample_to_linear(p->bkgd.b,&cs_srgb);

			if(p->bkgd_checkerboard) {
				p->bkgd2.r = iw_convert_sample_to_linear(p->bkgd2.r,&cs_srgb);
				p->bkgd2.g = iw_convert_sample_to_linear(p->bkgd2.g,&cs_srgb);
				p->bkgd2.b = iw_convert_sample_to_linear(p->bkgd2.b,&cs_srgb);
			}
		}

		iw_set_apply_bkgd(ctx,p->bkgd.r,p->bkgd.g,p->bkgd.b);
		if(p->bkgd_checkerboard) {
			iw_set_bkgd_checkerboard(ctx,p->bkgd_check_size,p->bkgd2.r,p->bkgd2.g,p->bkgd2.b);
			iw_set_bkgd_checkerboard_origin(ctx,p->bkgd_check_origin_x,p->bkgd_check_origin_y);
		}
	}

	if(p->use_bkgd_label) {
		iw_set_value(ctx,IW_VAL_USE_BKGD_LABEL,1);
	}

	iw_get_input_image_density(ctx,&p->xdens,&p->ydens,&p->density_code);

	old_width=iw_get_value(ctx,IW_VAL_INPUT_WIDTH);
	old_height=iw_get_value(ctx,IW_VAL_INPUT_HEIGHT);

	if(p->use_crop) {
		// If we're cropping, adjust some things so that "bestfit" works.
		if(p->crop_x<0) p->crop_x=0;
		if(p->crop_y<0) p->crop_y=0;
		if(p->crop_x>old_width-1) p->crop_x=old_width-1;
		if(p->crop_y>old_height-1) p->crop_y=old_height-1;
		if(p->crop_w<0 || p->crop_w>old_width-p->crop_x) p->crop_w=old_width-p->crop_x;
		if(p->crop_h<0 || p->crop_h>old_height-p->crop_y) p->crop_h=old_height-p->crop_y;
		if(p->crop_w<1) p->crop_w=1;
		if(p->crop_h<1) p->crop_h=1;

		old_width = p->crop_w;
		old_height = p->crop_h;
	}

	if(p->new_width<0) p->new_width = -1;
	if(p->new_height<0) p->new_height = -1;
	if(p->new_width==0) p->new_width = 1;
	if(p->new_height==0) p->new_height = 1;

	if(p->rel_width_flag) {
		p->new_width = iwcmd_calc_rel_size(p->rel_width, old_width);
	}
	if(p->rel_height_flag) {
		p->new_height = iwcmd_calc_rel_size(p->rel_height, old_height);
	}

	if(p->new_width == -1 && p->new_height == -1) {
		// Neither -width nor -height specified. Keep image the same size.
		p->new_width=old_width;
		p->new_height=old_height;
	}
	else if(p->new_height == -1) {
		// -width given but not -height. Fit to width.
		p->new_height=1000000;
		do_bestfit(p,old_width,old_height);
	}
	else if(p->new_width == -1) {
		// -height given but not -width. Fit to height.
		p->new_width=1000000;
		do_bestfit(p,old_width,old_height);
	}
	else if(p->bestfit) {
		// -width and -height and -bestfit all given. Best-fit into the given dimensions.
		do_bestfit(p,old_width,old_height);
	}
	else {
		// -width and -height given but not -bestfit. Use the exact dimensions given.
		;
	}

	if(p->new_width<1) p->new_width=1;
	if(p->new_height<1) p->new_height=1;

	if(p->noinfo) {
		;
	}
	else if(p->new_width==old_width && p->new_height==old_height) {
		iwcmd_message(p,"Processing (%d\xc3\x97%d)\n",p->new_width,p->new_height);
	}
	else {
		iwcmd_message(p,"Resizing (%d\xc3\x97%d) \xe2\x86\x92 (%d\xc3\x97%d)\n",old_width,old_height,
			p->new_width,p->new_height);
	}

	iw_set_output_canvas_size(ctx,p->new_width,p->new_height);
	if(p->use_crop) {
		iw_set_input_crop(ctx,p->crop_x,p->crop_y,p->crop_w,p->crop_h);
	}

	if(!iw_process_image(ctx)) goto done;

	if(p->interlace) {
		iw_set_value(ctx,IW_VAL_OUTPUT_INTERLACED,1);
	}

	writedescr.write_fn = my_writefn;
	writedescr.fp = (void*)iwcmd_fopen(p->outfn,"wb");
	if(!writedescr.fp) {
		iw_set_errorf(ctx,"Failed to open for writing (error code=%d)",(int)errno);
		goto done;
	}

	if(p->outfmt==IW_FORMAT_JPEG) {
#if IW_SUPPORT_JPEG == 1
		if(p->jpeg_quality>0) iw_set_value(ctx,IW_VAL_JPEG_QUALITY,p->jpeg_quality);
		if(p->jpeg_samp_factor_h>0)
			iw_set_value(ctx,IW_VAL_JPEG_SAMP_FACTOR_H,p->jpeg_samp_factor_h);
		if(p->jpeg_samp_factor_v>0)
			iw_set_value(ctx,IW_VAL_JPEG_SAMP_FACTOR_V,p->jpeg_samp_factor_v);
		if(!iw_write_jpeg_file(ctx,&writedescr)) goto done;
#else
		iw_set_error(ctx,"JPEG is not supported by this copy of imagew.");
#endif
	}
	else if(p->outfmt==IW_FORMAT_BMP) {
		if(!iw_write_bmp_file(ctx,&writedescr)) goto done;
	}
	else if(p->outfmt==IW_FORMAT_TIFF) {
		if(!iw_write_tiff_file(ctx,&writedescr)) goto done;
	}
	else if(p->outfmt==IW_FORMAT_MIFF) {
		if(!iw_write_miff_file(ctx,&writedescr)) goto done;
	}
	else if(p->outfmt==IW_FORMAT_WEBP) {
#if IW_SUPPORT_WEBP == 1
		if(p->webp_quality>=0) iw_set_value_dbl(ctx,IW_VAL_WEBP_QUALITY,p->webp_quality);
		if(!iw_write_webp_file(ctx,&writedescr)) goto done;
#else
		iw_set_error(ctx,"WebP is not supported by this copy of imagew.");
#endif
	}
	else {
#if IW_SUPPORT_PNG == 1
		if(p->pngcmprlevel_set)
			iw_set_value(ctx,IW_VAL_PNG_CMPR_LEVEL,p->pngcmprlevel);
		if(!iw_write_png_file(ctx,&writedescr)) goto done;
#else
		iw_set_error(ctx,"PNG is not supported by this copy of imagew.");
#endif
	}

	fclose((FILE*)writedescr.fp);
	writedescr.fp=NULL;

	retval = 1;

done:
	if(readdescr.fp) fclose((FILE*)readdescr.fp);
	if(writedescr.fp) fclose((FILE*)writedescr.fp);

	if(ctx) {
		if(iw_get_errorflag(ctx)) {
			iwcmd_error(p,"imagew error: %s\n",iw_get_errormsg(ctx,errmsg,200));
		}
	}

	iw_destroy_context(ctx);
	return retval;
}


// Find where a number ends (at a comma, or the end of string),
// and record if it contained a slash.
static int iwcmd_get_number_len(const char *s, int *pslash_pos)
{
	int i;
	for(i=0;s[i];i++) {
		if(s[i]=='/') {
			*pslash_pos = i;
		}
		if(s[i]!=',') continue;
		return i;
	}
	return i;
}

// Returns 0 if no valid number found.
static int iwcmd_parse_number_internal(const char *s,
		   double *presult, int *pcharsread)
{
	int len;
	int slash_pos = -1;

	*presult = 0.0;
	*pcharsread = 0;

	len = iwcmd_get_number_len(s,&slash_pos);
	if(len<1) return 0;
	*pcharsread = len;

	if(slash_pos>=0) {
		// a rational number
		double numer, denom;
		numer = atof(s);
		denom = atof(s+slash_pos+1);
		if(denom==0.0)
			*presult = 0.0;
		else
			*presult = numer/denom;
	}
	else {
		*presult = atof(s);
	}
	return 1;
}

static void iwcmd_parse_number_list(const char *s,
	int max_numbers, // max number of numbers to parse
	double *results, // array of doubles to hold the results
	int *pnumresults) // number of numbers parsed
{
	int n;
	int charsread;
	int curpos=0;
	int ret;

	*pnumresults = 0;
	for(n=0;n<max_numbers;n++) {
		results[n]=0.0;
	}

	for(n=0;n<max_numbers;n++) {
		ret=iwcmd_parse_number_internal(&s[curpos], &results[n], &charsread);
		if(!ret) return;
		(*pnumresults)++;
		curpos+=charsread;
		if(s[curpos]==',') {
			curpos++;
		}
		else {
			return;
		}
	}
}

static double iwcmd_parse_dbl(const char *s)
{
	double result;
	int charsread;
	iwcmd_parse_number_internal(s, &result, &charsread);
	return result;
}

static int iwcmd_parse_int(const char *s)
{
	double result;
	int charsread;
	iwcmd_parse_number_internal(s, &result, &charsread);
	if(result<0.0) return -(int)(0.5-result);
	return (int)(0.5+result);
}

// Parse two integers separated by a comma.
static void iwcmd_parse_int_pair(const char *s, int *i1, int *i2)
{
	double nums[2];
	int count;

	iwcmd_parse_number_list(s,2,nums,&count);
	*i1 = (int)(0.5+nums[0]);
	*i2 = (int)(0.5+nums[1]);
}

// Parse four integers separated by commas.
static void iwcmd_parse_int_4(const char *s, int *i1, int *i2, int *i3, int *i4)
{
	double nums[4];
	int count;

	iwcmd_parse_number_list(s,4,nums,&count);
	*i1 = (int)(0.5+nums[0]);
	*i2 = (int)(0.5+nums[1]);
	*i3 = (int)(0.5+nums[2]);
	*i4 = (int)(0.5+nums[3]);
}

static int hexdigit_value(char d)
{
	if(d>='0' && d<='9') return ((int)d)-'0';
	if(d>='a' && d<='f') return ((int)d)+10-'a';
	if(d>='A' && d<='F') return ((int)d)+10-'A';
	return 0;
}

static double hexvalue1(char d1)
{
	return ((double)hexdigit_value(d1))/15.0;
}

static double hexvalue2(char d1, char d2)
{
	return ((double)(16*hexdigit_value(d1) + hexdigit_value(d2)))/255.0;
}

static double hexvalue4(char d1, char d2, char d3, char d4)
{
	return ((double)(4096*hexdigit_value(d1) + 256*hexdigit_value(d2) +
		16*hexdigit_value(d3) + hexdigit_value(d4)))/65535.0;
}

// Allowed formats: 3 hex digits, 6 hex digits, or 12 hex digits.
static void parse_bkgd_color(struct rgb_color *c, const char *s, size_t s_len)
{
	if(s_len==3) {
		c->r = hexvalue1(s[0]);
		c->g = hexvalue1(s[1]);
		c->b = hexvalue1(s[2]);
	}
	else if(s_len==6) {
		c->r = hexvalue2(s[0],s[1]);
		c->g = hexvalue2(s[2],s[3]);
		c->b = hexvalue2(s[4],s[5]);
	}
	else if(s_len==12) {
		c->r = hexvalue4(s[0],s[1],s[2] ,s[3]);
		c->g = hexvalue4(s[4],s[5],s[6] ,s[7]);
		c->b = hexvalue4(s[8],s[9],s[10],s[11]);
	}
	else {
		// Invalid color description.
		c->r = 1.0;
		c->g = 0.0;
		c->b = 1.0;
	}
}

// 's' is either a single color, or two colors separated with a comma.
static void parse_bkgd(struct params_struct *p, const char *s)
{
	char *cpos;
	cpos = strchr(s,',');
	if(!cpos) {
		// Just a single color
		parse_bkgd_color(&p->bkgd,s,strlen(s));
		return;
	}

	// Two colors
	p->bkgd_checkerboard=1;
	parse_bkgd_color(&p->bkgd,s,cpos-s);
	parse_bkgd_color(&p->bkgd2,cpos+1,strlen(cpos+1));
}

// Find where the "name" ends and the parameters (numbers) begin.
static int iwcmd_get_name_len(const char *s)
{
	int i;
	for(i=0;s[i];i++) {
		if(s[i]>='a' && s[i]<='z') continue;
		if(s[i]>='A' && s[i]<='Z') continue;
		return i;
	}
	return i;
}

static int iwcmd_string_to_resizetype(struct params_struct *p,
	const char *s, struct resize_alg *alg)
{
	int i;
	int len, namelen;
	double blur;
	struct resizetable_struct {
		const char *name;
		int resizetype;
	};
	static const struct resizetable_struct resizetable[] = {
		{"mix",IW_RESIZETYPE_MIX},
		{"nearest",IW_RESIZETYPE_NEAREST},
		{"point",IW_RESIZETYPE_NEAREST},
		{"linear",IW_RESIZETYPE_LINEAR},
		{"triangle",IW_RESIZETYPE_LINEAR},
		{"quadratic",IW_RESIZETYPE_QUADRATIC},
		{"hermite",IW_RESIZETYPE_HERMITE},
		{"box",IW_RESIZETYPE_BOX},
		{"gaussian",IW_RESIZETYPE_GAUSSIAN},
		{"auto",IW_RESIZETYPE_AUTO},
		{"null",IW_RESIZETYPE_NULL},
		{NULL,0}
	};

	blur = alg->blur;
	memset(alg,0,sizeof(struct resize_alg));
	// Hack: The 'blur' should be really be part of the string, but for now
	// it is a separate parameter, so we must not modify it here.
	alg->blur = blur;

	for(i=0; resizetable[i].name!=NULL; i++) {
		if(!strcmp(s,resizetable[i].name)) {
			alg->family = resizetable[i].resizetype;
			return 1;
		}
	}

	len=(int)strlen(s);
	namelen=iwcmd_get_name_len(s);

	if(namelen==7 && !strncmp(s,"lanczos",namelen)) {
		if(len>namelen)
			alg->lobes = iwcmd_parse_int(&s[namelen]);
		else
			alg->lobes = 3;
		alg->family = IW_RESIZETYPE_LANCZOS;
		return 1;
	}
	else if((namelen==4 && !strncmp(s,"hann",namelen)) ||
		    (namelen==7 && !strncmp(s,"hanning",namelen)) )
	{
		if(len>namelen)
			alg->lobes = iwcmd_parse_int(&s[namelen]);
		else
			alg->lobes = 4;
		alg->family = IW_RESIZETYPE_HANNING;
		return 1;
	}
	else if(namelen==8 && !strncmp(s,"blackman",namelen)) {
		if(len>namelen)
			alg->lobes = iwcmd_parse_int(&s[namelen]);
		else
			alg->lobes = 4;
		alg->family = IW_RESIZETYPE_BLACKMAN;
		return 1;
	}
	else if(namelen==4 && !strncmp(s,"sinc",namelen)) {
		if(len>namelen)
			alg->lobes = iwcmd_parse_int(&s[namelen]);
		else
			alg->lobes = 4;
		alg->family = IW_RESIZETYPE_SINC;
		return 1;
	}
	else if(!strcmp(s,"catrom")) {
		alg->family = IW_RESIZETYPE_CUBIC;
		alg->b = 0.0; alg->c = 0.5;
		return 1;
	}
	else if(!strcmp(s,"mitchell")) {
		alg->family = IW_RESIZETYPE_CUBIC;
		alg->b = 1.0/3; alg->c = 1.0/3;
		return 1;
	}
	else if(!strcmp(s,"bspline")) {
		alg->family = IW_RESIZETYPE_CUBIC;
		alg->b = 1.0; alg->c = 0.0;
		return 1;
	}
	else if(namelen==5 && !strncmp(s,"cubic",namelen)) {
		// Format is "cubic<B>,<C>"
		char *cpos;
		if(len < namelen+3) goto done; // error
		cpos = strchr(s,',');
		if(!cpos) goto done;
		alg->b = iwcmd_parse_dbl(&s[namelen]);
		alg->c = iwcmd_parse_dbl(cpos+1);
		alg->family = IW_RESIZETYPE_CUBIC;
		return 1;
	}
	else if(namelen==4 && !strncmp(s,"keys",namelen)) {
		// Format is "keys<alpha>"
		if(len>namelen)
			alg->c = iwcmd_parse_dbl(&s[namelen]);
		else
			alg->c = 0.5;
		alg->b = 1.0-2.0*alg->c;
		alg->family = IW_RESIZETYPE_CUBIC;
		return 1;
	}

done:
	iwcmd_error(p,"Unknown resize type \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
	return -1;
}

static int iwcmd_string_to_dithertype(struct params_struct *p,const char *s,int *psubtype)
{
	int i;
	struct dithertable_struct {
		const char *name;
		int ditherfamily;
		int dithersubtype;
	};
	static const struct dithertable_struct dithertable[] = {
	 {"f"         ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_FS},
	 {"fs"        ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_FS},
	 {"o"         ,IW_DITHERFAMILY_ORDERED,IW_DITHERSUBTYPE_DEFAULT},
	 {"halftone"  ,IW_DITHERFAMILY_ORDERED,IW_DITHERSUBTYPE_HALFTONE},
	 {"r"         ,IW_DITHERFAMILY_RANDOM ,IW_DITHERSUBTYPE_DEFAULT},
	 {"r2"        ,IW_DITHERFAMILY_RANDOM ,IW_DITHERSUBTYPE_SAMEPATTERN},
	 {"jjn"       ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_JJN},
	 {"stucki"    ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_STUCKI},
	 {"burkes"    ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_BURKES},
	 {"sierra"    ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_SIERRA3},
	 {"sierra3"   ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_SIERRA3},
	 {"sierra2"   ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_SIERRA2},
	 {"sierralite",IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_SIERRA42A},
	 {"atkinson"  ,IW_DITHERFAMILY_ERRDIFF,IW_DITHERSUBTYPE_ATKINSON},
	 {"none"      ,IW_DITHERFAMILY_NONE   ,IW_DITHERSUBTYPE_DEFAULT} // This line must be last.
	};

	for(i=0; dithertable[i].ditherfamily!=IW_DITHERFAMILY_NONE; i++) {
		if(!strcmp(s,dithertable[i].name)) {
			*psubtype = dithertable[i].dithersubtype;
			return dithertable[i].ditherfamily;
		}
	}

	iwcmd_message(p,"Unknown dither type \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
	*psubtype = IW_DITHERSUBTYPE_DEFAULT;
	return -1;
}


static int iwcmd_string_to_colorspace(struct params_struct *p,
  struct iw_csdescr *cs, const char *s)
{
	int namelen;
	int len;

	len=(int)strlen(s);
	namelen = iwcmd_get_name_len(s);

	if(namelen==5 && len>5 && !strncmp(s,"gamma",namelen)) {
		double g;
		g = iwcmd_parse_dbl(&s[namelen]);
		iw_make_gamma_csdescr(cs,g);
	}
	else if(!strcmp(s,"linear")) {
		iw_make_linear_csdescr(cs);
	}
	else if(len>=4 && !strncmp(s,"srgb",4)) {
		int intent;
		switch(s[4]) {
			case 'p': intent=IW_SRGB_INTENT_PERCEPTUAL; break;
			case 'r': intent=IW_SRGB_INTENT_RELATIVE; break;
			case 's': intent=IW_SRGB_INTENT_SATURATION; break;
			case 'a': intent=IW_SRGB_INTENT_ABSOLUTE; break;
			default:  intent=IW_SRGB_INTENT_PERCEPTUAL; break;
		}
		iw_make_srgb_csdescr(cs,intent);
	}
	else {
		iwcmd_error(p,"Unknown color space \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
		return -1;
	}
	return 1;
}

static int iwcmd_process_noopt(struct params_struct *p, const char *s)
{
	if(!strcmp(s,"all")) {
		p->noopt_grayscale=1;
		p->noopt_palette=1;
		p->noopt_reduceto8=1;
		p->noopt_stripalpha=1;
		p->noopt_binarytrns=1;
	}
	else if(!strcmp(s,"grayscale")) {
		p->noopt_grayscale=1;
	}
	else if(!strcmp(s,"palette")) {
		p->noopt_palette=1;
	}
	else if(!strcmp(s,"reduceto8")) {
		p->noopt_reduceto8=1;
	}
	else if(!strcmp(s,"stripalpha")) {
		p->noopt_stripalpha=1;
	}
	else if(!strcmp(s,"binarytrns")) {
		p->noopt_binarytrns=1;
	}
	else {
		iwcmd_error(p,"Unknown optimization \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
		return 0;
	}

	return 1;
}

static int iwcmd_process_density(struct params_struct *p, const char *s)
{
	if(!strcmp(s,"auto")) {
		p->density_policy = IW_DENSITY_POLICY_AUTO;
	}
	else if(!strcmp(s,"none")) {
		p->density_policy = IW_DENSITY_POLICY_NONE;
	}
	else if(!strcmp(s,"keep")) {
		p->density_policy = IW_DENSITY_POLICY_KEEP;
	}
	else if(!strcmp(s,"adjust")) {
		p->density_policy = IW_DENSITY_POLICY_ADJUST;
	}
	else {
		iwcmd_error(p,"Invalid density \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
		return 0;
	}
	return 1;
}

static void usage_message(struct params_struct *p)
{
	iwcmd_message(p,
		"Usage: imagew [-width <n>] [-height <n>] [options] <in-file> <out-file>\n"
		"Options include -filter, -grayscale, -depth, -cc, -dither, -bkgd, -cs,\n"
		" -quiet, -version.\n"
		"See the readme.txt file for more information.\n"
	);
}

static void do_printversion(struct params_struct *p)
{
	char buf[200];
	int buflen;
	int ver;

	buflen = (int)(sizeof(buf)/sizeof(char));

	ver = iw_get_version_int();
	iwcmd_message(p,"ImageWorsener version %s (%d-bit, %d bits/sample)\n",
		iw_get_version_string(NULL,buf,buflen),
		(int)(8*sizeof(void*)), 8*iw_get_sample_size() );

	iwcmd_message(p,"%s\n",iw_get_copyright_string(NULL,buf,buflen));

#if IW_SUPPORT_JPEG == 1
	iwcmd_message(p,"Uses libjpeg version %s\n",iw_get_libjpeg_version_string(buf,buflen));
#endif
#if IW_SUPPORT_PNG == 1
	iwcmd_message(p,"Uses libpng version %s\n",iw_get_libpng_version_string(buf,buflen));
	// TODO: WebP might also use zlib, so we shouldn't just assume zlib is bundled
	// with libpng.
	iwcmd_message(p,"Uses zlib version %s\n",iw_get_zlib_version_string(buf,buflen));
#endif
#if IW_SUPPORT_WEBP == 1
	iwcmd_message(p,"Uses libwebp encoder v%s",iw_get_libwebp_enc_version_string(buf,buflen));
	iwcmd_message(p,", decoder v%s\n",iw_get_libwebp_dec_version_string(buf,buflen));
#endif
}

enum iwcmd_param_types {
 PT_NONE=0, PT_WIDTH, PT_HEIGHT, PT_DEPTH, PT_INPUTCS, PT_CS,
 PT_RESIZETYPE, PT_RESIZETYPE_X, PT_RESIZETYPE_Y, PT_RESIZETYPE_ALPHA,
 PT_BLUR_FACTOR, PT_BLUR_FACTOR_X, PT_BLUR_FACTOR_Y, PT_BLUR_FACTOR_ALPHA,
 PT_DITHER, PT_DITHERCOLOR, PT_DITHERALPHA, PT_DITHERRED, PT_DITHERGREEN, PT_DITHERBLUE, PT_DITHERGRAY,
 PT_CC, PT_CCCOLOR, PT_CCALPHA, PT_CCRED, PT_CCGREEN, PT_CCBLUE, PT_CCGRAY,
 PT_BKGD, PT_BKGD2, PT_CHECKERSIZE, PT_CHECKERORG, PT_CROP,
 PT_OFFSET_R_H, PT_OFFSET_G_H, PT_OFFSET_B_H, PT_OFFSET_R_V, PT_OFFSET_G_V,
 PT_OFFSET_B_V, PT_OFFSET_RB_H, PT_OFFSET_RB_V,
 PT_JPEGQUALITY, PT_JPEGSAMPLING, PT_WEBPQUALITY, PT_PNGCMPRLEVEL, PT_INTERLACE,
 PT_RANDSEED, PT_INFMT, PT_OUTFMT, PT_EDGE_POLICY, PT_GRAYSCALEFORMULA,
 PT_DENSITY_POLICY, PT_PAGETOREAD, PT_INCLUDESCREEN, PT_NOINCLUDESCREEN,
 PT_BESTFIT, PT_NOBESTFIT, PT_GRAYSCALE, PT_CONDGRAYSCALE, PT_NOGAMMA,
 PT_INTCLAMP, PT_NOCSLABEL, PT_NOOPT, PT_USEBKGDLABEL,
 PT_QUIET, PT_NOWARN, PT_NOINFO, PT_VERSION, PT_HELP, PT_ENCODING
};

struct parsestate_struct {
	enum iwcmd_param_types param_type;
	int untagged_param_count;
	int printversion;
	int showhelp;
};

static int process_option_name(struct params_struct *p, struct parsestate_struct *ps, const char *n)
{
	struct opt_struct {
		const char *name;
		enum iwcmd_param_types code;
		int has_param;
	};
	static const struct opt_struct opt_info[] = {
		{"w",PT_WIDTH,1},
		{"width",PT_WIDTH,1},
		{"h",PT_HEIGHT,1},
		{"height",PT_HEIGHT,1},
		{"depth",PT_DEPTH,1},
		{"inputcs",PT_INPUTCS,1},
		{"cs",PT_CS,1},
		{"filter",PT_RESIZETYPE,1},
		{"filterx",PT_RESIZETYPE_X,1},
		{"filtery",PT_RESIZETYPE_Y,1},
		{"filteralpha",PT_RESIZETYPE_ALPHA,1},
		{"blur",PT_BLUR_FACTOR,1},
		{"blurx",PT_BLUR_FACTOR_X,1},
		{"blury",PT_BLUR_FACTOR_Y,1},
		{"bluralpha",PT_BLUR_FACTOR_ALPHA,1},
		{"dither",PT_DITHER,1},
		{"dithercolor",PT_DITHERCOLOR,1},
		{"ditheralpha",PT_DITHERALPHA,1},
		{"ditherred",PT_DITHERRED,1},
		{"dithergreen",PT_DITHERGREEN,1},
		{"ditherblue",PT_DITHERBLUE,1},
		{"dithergray",PT_DITHERGRAY,1},
		{"cc",PT_CC,1},
		{"cccolor",PT_CCCOLOR,1},
		{"ccalpha",PT_CCALPHA,1},
		{"ccred",PT_CCRED,1},
		{"ccgreen",PT_CCGREEN,1},
		{"ccblue",PT_CCBLUE,1},
		{"ccgray",PT_CCGRAY,1},
		{"bkgd",PT_BKGD,1},
		{"checkersize",PT_CHECKERSIZE,1},
		{"checkerorigin",PT_CHECKERORG,1},
		{"crop",PT_CROP,1},
		{"offsetred",PT_OFFSET_R_H,1},
		{"offsetgreen",PT_OFFSET_G_H,1},
		{"offsetblue",PT_OFFSET_B_H,1},
		{"offsetrb",PT_OFFSET_RB_H,1},
		{"offsetvred",PT_OFFSET_R_V,1},
		{"offsetvgreen",PT_OFFSET_G_V,1},
		{"offsetvblue",PT_OFFSET_B_V,1},
		{"offsetvrb",PT_OFFSET_RB_V,1},
		{"page",PT_PAGETOREAD,1},
		{"jpegquality",PT_JPEGQUALITY,1},
		{"jpegsampling",PT_JPEGSAMPLING,1},
		{"webpquality",PT_WEBPQUALITY,1},
		{"pngcmprlevel",PT_PNGCMPRLEVEL,1},
		{"randseed",PT_RANDSEED,1},
		{"infmt",PT_INFMT,1},
		{"outfmt",PT_OUTFMT,1},
		{"edge",PT_EDGE_POLICY,1},
		{"density",PT_DENSITY_POLICY,1},
		{"grayscaleformula",PT_GRAYSCALEFORMULA,1},
		{"noopt",PT_NOOPT,1},
		{"encoding",PT_ENCODING,1},
		{"interlace",PT_INTERLACE,0},
		{"bestfit",PT_BESTFIT,0},
		{"nobestfit",PT_NOBESTFIT,0},
		{"grayscale",PT_GRAYSCALE,0},
		{"condgrayscale",PT_CONDGRAYSCALE,0},
		{"nogamma",PT_NOGAMMA,0},
		{"intclamp",PT_INTCLAMP,0},
		{"nocslabel",PT_NOCSLABEL,0},
		{"usebkgdlabel",PT_USEBKGDLABEL,0},
		{"includescreen",PT_INCLUDESCREEN,0},
		{"noincludescreen",PT_NOINCLUDESCREEN,0},
		{"quiet",PT_QUIET,0},
		{"nowarn",PT_NOWARN,0},
		{"noinfo",PT_NOINFO,0},
		{"version",PT_VERSION,0},
		{"help",PT_HELP,0},
		{NULL,PT_NONE,0}
	};
	enum iwcmd_param_types pt;
	int i;

	pt=PT_NONE;

	// Search for the option name.
	for(i=0;opt_info[i].name;i++) {
		if(!strcmp(n,opt_info[i].name)) {
			if(opt_info[i].has_param) {
				// Found option with a parameter. Record it and return.
				ps->param_type=opt_info[i].code;
				return 1;
			}
			// Found parameterless option.
			pt=opt_info[i].code;
			break;
		}
	}

	// Handle parameterless options.
	switch(pt) {
	case PT_BESTFIT:
		p->bestfit=1;
		break;
	case PT_NOBESTFIT:
		p->bestfit=0;
		break;
	case PT_GRAYSCALE:
		p->grayscale=1;
		break;
	case PT_CONDGRAYSCALE:
		p->condgrayscale=1;
		break;
	case PT_NOGAMMA:
		p->no_gamma=1;
		break;
	case PT_INTCLAMP:
		p->intclamp=1;
		break;
	case PT_NOCSLABEL:
		p->no_cslabel=1;
		break;
	case PT_USEBKGDLABEL:
		p->use_bkgd_label=1;
		break;
	case PT_INCLUDESCREEN:
		p->include_screen=1;
		break;
	case PT_NOINCLUDESCREEN:
		p->include_screen=0;
		break;
	case PT_INTERLACE:
		p->interlace=1;
		break;
	case PT_QUIET:
		p->nowarn=1;
		p->noinfo=1;
		break;
	case PT_NOWARN:
		p->nowarn=1;
		break;
	case PT_NOINFO:
		p->noinfo=1;
		break;
	case PT_VERSION:
		ps->printversion=1;
		break;
	case PT_HELP:
		ps->showhelp=1;
		break;
	default:
		iwcmd_error(p,"Unknown option \xe2\x80\x9c%s\xe2\x80\x9d\n",n);
		return 0;
	}

	return 1;
}

static void iwcmd_read_w_or_h(struct params_struct *p, const char *v,
   int *new_d, int *rel_flag, double *new_rel_d)
{
	if(v[0]=='x') {
		// This is a relative size like "x1.5".
		*rel_flag = 1;
		*new_rel_d = iwcmd_parse_dbl(&v[1]);
	}
	else {
		// This is a number of pixels.
		*new_d = iwcmd_parse_int(v);
		return;
	}
}

static int process_option_arg(struct params_struct *p, struct parsestate_struct *ps, const char *v)
{
	int ret;

	switch(ps->param_type) {
	case PT_WIDTH:
		iwcmd_read_w_or_h(p,v,&p->new_width,&p->rel_width_flag,&p->rel_width);
		break;
	case PT_HEIGHT:
		iwcmd_read_w_or_h(p,v,&p->new_height,&p->rel_height_flag,&p->rel_height);
		break;
	case PT_DEPTH:
		p->depth=iwcmd_parse_int(v);
		break;
	case PT_INPUTCS:
		ret=iwcmd_string_to_colorspace(p,&p->cs_in,v);
		if(ret<0) return 0;
		p->cs_in_set=1;
		break;
	case PT_CS:
		ret=iwcmd_string_to_colorspace(p,&p->cs_out,v);
		if(ret<0) return 0;
		p->cs_out_set=1;
		break;
	case PT_RESIZETYPE:
		ret=iwcmd_string_to_resizetype(p,v,&p->resize_alg_x);
		if(ret<0) return 0;
		p->resize_alg_y=p->resize_alg_x;
		break;
	case PT_RESIZETYPE_X:
		ret=iwcmd_string_to_resizetype(p,v,&p->resize_alg_x);
		if(ret<0) return 0;
		break;
	case PT_RESIZETYPE_Y:
		ret=iwcmd_string_to_resizetype(p,v,&p->resize_alg_y);
		if(ret<0) return 0;
		break;
	case PT_RESIZETYPE_ALPHA:
		ret=iwcmd_string_to_resizetype(p,v,&p->resize_alg_alpha);
		if(ret<0) return 0;
		break;
	case PT_BLUR_FACTOR:
		p->resize_alg_x.blur = p->resize_alg_y.blur = iwcmd_parse_dbl(v);
		break;
	case PT_BLUR_FACTOR_X:
		p->resize_alg_x.blur = iwcmd_parse_dbl(v);
		break;
	case PT_BLUR_FACTOR_Y:
		p->resize_alg_y.blur = iwcmd_parse_dbl(v);
		break;
	case PT_BLUR_FACTOR_ALPHA:
		p->resize_alg_alpha.blur = iwcmd_parse_dbl(v);
		break;
	case PT_DITHER:
		p->dither_family_all=iwcmd_string_to_dithertype(p,v,&p->dither_subtype_all);
		if(p->dither_family_all<0) return 0;
		break;
	case PT_DITHERCOLOR:
		p->dither_family_nonalpha=iwcmd_string_to_dithertype(p,v,&p->dither_subtype_nonalpha);
		if(p->dither_family_nonalpha<0) return 0;
		break;
	case PT_DITHERALPHA:
		p->dither_family_alpha=iwcmd_string_to_dithertype(p,v,&p->dither_subtype_alpha);
		if(p->dither_family_alpha<0) return 0;
		break;
	case PT_DITHERRED:
		p->dither_family_red=iwcmd_string_to_dithertype(p,v,&p->dither_subtype_red);
		if(p->dither_family_red<0) return 0;
		break;
	case PT_DITHERGREEN:
		p->dither_family_green=iwcmd_string_to_dithertype(p,v,&p->dither_subtype_green);
		if(p->dither_family_green<0) return 0;
		break;
	case PT_DITHERBLUE:
		p->dither_family_blue=iwcmd_string_to_dithertype(p,v,&p->dither_subtype_blue);
		if(p->dither_family_blue<0) return 0;
		break;
	case PT_DITHERGRAY:
		p->dither_family_gray=iwcmd_string_to_dithertype(p,v,&p->dither_subtype_gray);
		if(p->dither_family_gray<0) return 0;
		break;
	case PT_CC:
		p->color_count_all=iwcmd_parse_int(v);
		break;
	case PT_CCCOLOR:
		p->color_count_nonalpha=iwcmd_parse_int(v);
		break;
	case PT_CCALPHA:
		p->color_count_alpha=iwcmd_parse_int(v);
		break;
	case PT_BKGD:
		p->apply_bkgd=1;
		parse_bkgd(p,v);
		break;
	case PT_CHECKERSIZE:
		p->bkgd_check_size=iwcmd_parse_int(v);
		break;
	case PT_CHECKERORG:
		iwcmd_parse_int_pair(v,&p->bkgd_check_origin_x,&p->bkgd_check_origin_y);
		break;
	case PT_CROP:
		iwcmd_parse_int_4(v,&p->crop_x,&p->crop_y,&p->crop_w,&p->crop_h);
		p->use_crop=1;
		break;
	case PT_CCRED:
		p->color_count_red=iwcmd_parse_int(v);
		break;
	case PT_CCGREEN:
		p->color_count_green=iwcmd_parse_int(v);
		break;
	case PT_CCBLUE:
		p->color_count_blue=iwcmd_parse_int(v);
		break;
	case PT_CCGRAY:
		p->color_count_gray=iwcmd_parse_int(v);
		break;
	case PT_OFFSET_R_H:
		p->offset_r_h=iwcmd_parse_dbl(v);
		break;
	case PT_OFFSET_G_H:
		p->offset_g_h=iwcmd_parse_dbl(v);
		break;
	case PT_OFFSET_B_H:
		p->offset_b_h=iwcmd_parse_dbl(v);
		break;
	case PT_OFFSET_R_V:
		p->offset_r_v=iwcmd_parse_dbl(v);
		break;
	case PT_OFFSET_G_V:
		p->offset_g_v=iwcmd_parse_dbl(v);
		break;
	case PT_OFFSET_B_V:
		p->offset_b_v=iwcmd_parse_dbl(v);
		break;
	case PT_OFFSET_RB_H:
		// Shortcut for shifting red and blue in opposite directions.
		p->offset_r_h=iwcmd_parse_dbl(v);
		p->offset_b_h= -p->offset_r_h;
		break;
	case PT_OFFSET_RB_V:
		// Shortcut for shifting red and blue vertically in opposite directions.
		p->offset_r_v=iwcmd_parse_dbl(v);
		p->offset_b_v= -p->offset_r_v;
		break;
	case PT_PAGETOREAD:
		p->page_to_read = iwcmd_parse_int(v);
		break;
	case PT_JPEGQUALITY:
		p->jpeg_quality=iwcmd_parse_int(v);
		break;
	case PT_JPEGSAMPLING:
		iwcmd_parse_int_pair(v,&p->jpeg_samp_factor_h,&p->jpeg_samp_factor_v);
		break;
	case PT_WEBPQUALITY:
		p->webp_quality=iwcmd_parse_dbl(v);
		break;
	case PT_PNGCMPRLEVEL:
		p->pngcmprlevel=iwcmd_parse_int(v);
		p->pngcmprlevel_set=1;
		break;
	case PT_RANDSEED:
		if(v[0]=='r') {
			p->randomize = 1;
		}
		else {
			p->random_seed=iwcmd_parse_int(v);
		}
		break;
	case PT_INFMT:
		p->infmt=get_fmt_from_name(v);
		if(p->infmt==IW_FORMAT_UNKNOWN) {
			iwcmd_error(p,"Unknown format \xe2\x80\x9c%s\xe2\x80\x9d\n",v);
			return 0;
		}
		break;
	case PT_OUTFMT:
		p->outfmt=get_fmt_from_name(v);
		if(p->outfmt==IW_FORMAT_UNKNOWN) {
			iwcmd_error(p,"Unknown format \xe2\x80\x9c%s\xe2\x80\x9d\n",v);
			return 0;
		}
		break;
	case PT_EDGE_POLICY:
		if(v[0]=='s') p->edge_policy=IW_EDGE_POLICY_STANDARD;
		else if(v[0]=='r') p->edge_policy=IW_EDGE_POLICY_REPLICATE;
		else {
			iwcmd_error(p,"Unknown edge policy\n");
			return 0;
		}
		break;
	case PT_DENSITY_POLICY:
		if(!iwcmd_process_density(p,v)) {
			return 0;
		}
		break;
	case PT_GRAYSCALEFORMULA:
		if(v[0]=='s') p->grayscale_formula=0;
		else if(v[0]=='c') p->grayscale_formula=1;
		else {
			iwcmd_error(p,"Unknown grayscale formula\n");
			return 0;
		}
		break;
	case PT_NOOPT:
		if(!iwcmd_process_noopt(p,v))
			return 0;
		break;
	case PT_ENCODING:
		// Already handled.
		break;

	case PT_NONE:
		// This is presumably the input or output filename.

		if(ps->untagged_param_count==0) {
			p->infn = v;
		}
		else if(ps->untagged_param_count==1) {
			p->outfn = v;
		}
		ps->untagged_param_count++;
		break;

	default:
		iwcmd_error(p,"Internal error: unhandled param\n");
		return 0;
	}

	return 1;
}

static int read_encoding_option(struct params_struct *p, const char *v)
{
	if(!strcmp(v,"ascii")) {
		p->output_encoding = IWCMD_ENCODING_ASCII;
	}
	else if(!strcmp(v,"utf8")) {
#ifdef _UNICODE
		// In Windows, if we set the output mode to UTF-8, we have to print
		// using UTF-16, and let Windows do the conversion.
		p->output_encoding = IWCMD_ENCODING_UTF16;
		p->output_encoding_setmode = IWCMD_ENCODING_UTF8;
#else
		p->output_encoding = IWCMD_ENCODING_UTF8;
#endif
	}
	else if(!strcmp(v,"utf8raw")) {
		p->output_encoding = IWCMD_ENCODING_UTF8;
	}
#ifdef _UNICODE
	else if(!strcmp(v,"utf16")) {
		p->output_encoding = IWCMD_ENCODING_UTF16;
		p->output_encoding_setmode = IWCMD_ENCODING_UTF16;
	}
	else if(!strcmp(v,"utf16raw")) {
		p->output_encoding = IWCMD_ENCODING_UTF16;
	}
#endif
	else {
		return 0;
	}

	return 1;
}

// Figure out what output character encoding to use, and do other
// encoding-related setup as needed.
static void handle_encoding(struct params_struct *p, int argc, char* argv[])
{
	int i;

	// Pre-scan the arguments for an "encoding" option.
	// This has to be done before we process the other options, so that we
	// can correctly print messages while parsing the other options.
	for(i=1;i<argc-1;i++) {
		if(!strcmp(argv[i],"-encoding")) {
			read_encoding_option(p, argv[i+1]);
		}
	}

#if defined(_UNICODE)
	// If the user didn't set an encoding, and this is a Windows Unicode
	// build, use UTF-16.
	if(p->output_encoding==IWCMD_ENCODING_AUTO) {
		p->output_encoding = IWCMD_ENCODING_UTF16;
		p->output_encoding_setmode = IWCMD_ENCODING_UTF16;
	}

#elif !defined(IW_NO_LOCALE)
	// For non-Windows builds with "locale" features enabled.
	setlocale(LC_CTYPE,"");
	// If the user didn't set an encoding, try to detect if we should use
	// UTF-8.
	if(p->output_encoding==IWCMD_ENCODING_AUTO) {
		if(strcmp(nl_langinfo(CODESET), "UTF-8") == 0) {
			p->output_encoding = IWCMD_ENCODING_UTF8;
		}
	}

#endif

	// If we still haven't decided on an encoding, use ASCII.
	if(p->output_encoding==IWCMD_ENCODING_AUTO) {
		p->output_encoding=IWCMD_ENCODING_ASCII;
	}

#ifdef IW_WINDOWS
	// In Windows, set the output mode, if appropriate.
	if(p->output_encoding_setmode==IWCMD_ENCODING_UTF8) {
		_setmode(_fileno(stdout),_O_U8TEXT);
	}
	// TODO: Not sure how much of this works in non-Unicode builds. Need to
	// investigate that (or remove support for non-Unicode builds).
#ifdef _UNICODE
	if(p->output_encoding_setmode==IWCMD_ENCODING_UTF16) {
		_setmode(_fileno(stdout),_O_U16TEXT);
	}
#endif
#endif
}

static int iwcmd_main(int argc, char* argv[])
{
	struct params_struct p;
	struct parsestate_struct ps;
	int ret;
	int i;
	const char *optname;

	memset(&ps,0,sizeof(struct parsestate_struct));
	ps.param_type=PT_NONE;
	ps.untagged_param_count=0;
	ps.printversion=0;
	ps.showhelp=0;

	memset(&p,0,sizeof(struct params_struct));
	p.new_width = -1;
	p.new_height = -1;
	p.depth = -1;
	p.edge_policy = -1;
	p.density_policy = -1;
	p.bkgd_check_size = 16;
	p.bestfit = 0;
	p.offset_r_h=0.0; p.offset_g_h=0.0; p.offset_b_h=0.0;
	p.offset_r_v=0.0; p.offset_g_v=0.0; p.offset_b_v=0.0;
	p.infmt=IW_FORMAT_UNKNOWN;
	p.outfmt=IW_FORMAT_UNKNOWN;
	p.output_encoding=IWCMD_ENCODING_AUTO;
	p.output_encoding_setmode=IWCMD_ENCODING_AUTO;
	p.resize_alg_x.blur = 1.0;
	p.resize_alg_y.blur = 1.0;
	p.resize_alg_alpha.blur = 1.0;
	p.webp_quality = -1.0;
	p.include_screen = -1;

	handle_encoding(&p,argc,argv);

	for(i=1;i<argc;i++) {
		if(argv[i][0]=='-' && ps.param_type==PT_NONE) {
			optname = &argv[i][1];
			// If the second char is also a '-', ignore it.
			if(argv[i][1]=='-')
				optname = &argv[i][2];
			if(!process_option_name(&p, &ps, optname)) {
				return 1;
			}
		}
		else {
			// Process a parameter of the previous option.

			if(!process_option_arg(&p, &ps, argv[i])) {
				return 1;
			}

			ps.param_type = PT_NONE;
		}
	}

	if(ps.showhelp) {
		usage_message(&p);
		return 0;
	}

	if(ps.printversion) {
		do_printversion(&p);
		return 0;
	}

	if(ps.untagged_param_count!=2 || ps.param_type!=PT_NONE) {
		usage_message(&p);
		return 1;
	}

	ret=run(&p);
	return ret?0:1;
}

#ifdef _UNICODE

int wmain(int argc, WCHAR* argvW[])
{
	int retval;
	int i;
	char **argvUTF8;

	argvUTF8 = (char**)malloc(argc*sizeof(char*));
	if(!argvUTF8) return 1;

	// Convert parameters to UTF-8
	for(i=0;i<argc;i++) {
		argvUTF8[i] = iwcmd_utf16_to_utf8_strdup(argvW[i]);
		if(!argvUTF8[i]) return 1;
	}

	retval = iwcmd_main(argc,argvUTF8);

	for(i=0;i<argc;i++) {
		free(argvUTF8[i]);
	}

	return retval;
}

#else

int main(int argc, char* argv[])
{
	return iwcmd_main(argc,argv);
}

#endif
