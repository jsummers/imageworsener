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
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
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

#define IW_INCLUDE_UTIL_FUNCTIONS // Needed for iw_parse_number(), etc.
#include "imagew.h"

#ifdef IW_WINDOWS
#include <strsafe.h>
#endif

#define IWCMD_ENCODING_AUTO   0
#define IWCMD_ENCODING_ASCII  1
#define IWCMD_ENCODING_UTF8   2
#define IWCMD_ENCODING_UTF16  3

struct resize_alg {
	int family;
	double param1, param2;
};

struct resize_blur {
	int is_set; // Did the user set this blur option?
	double blur;
	int interpolate; // If set, multiply 'blur' by the scaling factor (if downscaling)
};

struct dither_setting {
	int family;
	int subtype;
};

struct uri_struct {
#define IWCMD_SCHEME_FILE       1
#define IWCMD_SCHEME_CLIPBOARD  2
#define IWCMD_SCHEME_STDIN      3
#define IWCMD_SCHEME_STDOUT     4
	int scheme;
	const char *uri;

	// May point to a position in ->uri, or to a static string.
	// If scheme==FILE, this is the actual filename.
	// Otherwise, this is a display name.
	const char *filename;
};

struct iw_option_struct {
	char *name;
	char *val;
};

struct params_struct {
	struct uri_struct input_uri;
	struct uri_struct output_uri;
#define IWCMD_MSGS_TO_STDOUT 1
#define IWCMD_MSGS_TO_STDERR 2
	int msgsdest; // IWCMD_MSGS_TO_*
	FILE *msgsfile;
	int nowarn;
	int noinfo;
	int src_width, src_height;
	int adjusted_src_width, adjusted_src_height;
	int dst_width_req, dst_height_req;
	int rel_width_flag, rel_height_flag;
	int noresize_flag;
	double rel_width, rel_height;
	int dst_width, dst_height;
	struct resize_alg resize_alg_x;
	struct resize_alg resize_alg_y;
	struct resize_blur resize_blur_x;
	struct resize_blur resize_blur_y;
	int bestfit;
	int bestfit_option;
	int depth; // Overall depth
	int sample_type;
	int channel_depth[5]; // Per-channeltype depth, indexed by IW_CHANNELTYPE
	int depthcc;
	int compression;
	int grayscale, condgrayscale;
	double offset_h[3]; // Indexed by IW_CHANNELTYPE_[RED..BLUE]
	double offset_v[3];
	double translate_x, translate_y;
	int translate_src_flag; // If 1, translate_[xy] is in source pixels.
	int translate_set;
	double imagesize_x, imagesize_y;
	int imagesize_set;
	struct dither_setting dither[5]; // Indexed by IW_CHANNELTYPE_[RED..GRAY]
	struct dither_setting dither_all;
	struct dither_setting dither_nonalpha;
	int color_count[5]; // Per-channeltype color count, indexed by IW_CHANNELTYPE.
	int color_count_all, color_count_nonalpha;
	int apply_bkgd;
	int bkgd_checkerboard;
	int bkgd_check_size;
	int bkgd_check_origin_x, bkgd_check_origin_y;
	int use_bkgd_label;
	int negate;

	int bkgd_label_set;
	struct iw_color bkgd_label; // Uses linear colorspace
	int no_bkgd_label;

	int use_crop, crop_x, crop_y, crop_w, crop_h;
	unsigned int reorient;
	struct iw_color bkgd;
	struct iw_color bkgd2;
	int page_to_read;
	int bmp_version;
	int bmp_trns;
	int interlace;
	int randomize;
	int random_seed;
	int infmt;
	int outfmt;
	int no_gamma;
	int intclamp;
	int edge_policy_x,edge_policy_y;

#define IWCMD_DENSITY_POLICY_AUTO    0
#define IWCMD_DENSITY_POLICY_NONE    1 // Don't write a density (if possible)
#define IWCMD_DENSITY_POLICY_KEEP    2 // Keep density the same
#define IWCMD_DENSITY_POLICY_ADJUST  3 // Keep physical image size the same
#define IWCMD_DENSITY_POLICY_FORCED  4 // Use a specific density
	int density_policy;

	int pref_units;
	double density_forced_x, density_forced_y; // in pixels/meter
	int grayscale_formula;
	double grayscale_weight[3];
	int no_cslabel;
	int include_screen;
	int noopt_grayscale,noopt_binarytrns,noopt_palette;
	int noopt_reduceto8,noopt_stripalpha;
	int cs_in_set, cs_out_set;
	struct iw_csdescr cs_in;
	struct iw_csdescr cs_out;
	int rendering_intent;
	int output_encoding;
	int output_encoding_req;
#ifdef IW_WINDOWS
	// Used when pasting (reading) from clipboard:
	int cb_r_clipboard_is_open;
	HANDLE cb_r_data_handle;
	iw_byte *cb_r_data;
	SIZE_T cb_r_data_size;
	SIZE_T cb_r_data_pos;
	// Used when copying (writing) to clipboard:
	iw_byte *cb_w_data;
	size_t cb_w_data_alloc;
	size_t cb_w_data_pos; // Current "file position"
	size_t cb_w_data_high_water_mark; // Current "file size"
	size_t cb_w_predicted_filesize;
#endif

	unsigned char input_initial_bytes[12];
	size_t input_initial_bytes_stored;
	size_t input_initial_bytes_consumed;

#define IWCMD_MAX_OPTIONS 32
	struct iw_option_struct options[IWCMD_MAX_OPTIONS];
	int options_count;
};

#ifndef IW_WINDOWS
static void iwcmd_strlcpy(char *dst, const char *src, size_t dstlen)
{
	size_t n;
	n = strlen(src);
	if(n>dstlen-1) n=dstlen-1;
	memcpy(dst,src,n);
	dst[n]='\0';
}
#endif

#ifdef IW_WINDOWS
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

// Output a NUL-terminated string.
// The input string is encoded in UTF-8.
// If the output encoding is not UTF-8, it will be converted.
static void iwcmd_puts_utf8(struct params_struct *p, const char *s)
{
	char buf[500];
#ifdef IW_WINDOWS
	WCHAR bufW[500];
#endif

	switch(p->output_encoding) {
#ifdef IW_WINDOWS
	case IWCMD_ENCODING_UTF16:
		iwcmd_utf8_to_utf16(s,bufW,sizeof(bufW)/sizeof(WCHAR));
		fputws(bufW,p->msgsfile);
		break;
#endif
	case IWCMD_ENCODING_UTF8:
		fputs(s,p->msgsfile);
		break;
	default:
		iw_utf8_to_ascii(s,buf,sizeof(buf));
		fputs(buf,p->msgsfile);
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
  iw_gnuc_attribute ((format (printf, 2, 3)));

static void iwcmd_message(struct params_struct *p, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iwcmd_vprint_utf8(p,fmt,ap);
	va_end(ap);
}

static void iwcmd_warning(struct params_struct *p, const char *fmt, ...)
  iw_gnuc_attribute ((format (printf, 2, 3)));

static void iwcmd_warning(struct params_struct *p, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iwcmd_vprint_utf8(p,fmt,ap);
	va_end(ap);
}

static void iwcmd_error(struct params_struct *p, const char *fmt, ...)
  iw_gnuc_attribute ((format (printf, 2, 3)));

static void iwcmd_error(struct params_struct *p, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iwcmd_vprint_utf8(p,fmt,ap);
	va_end(ap);
}

// Wrappers for fopen()
#ifdef IW_WINDOWS

static FILE* iwcmd_fopen(const char *fn, const char *mode, char *errmsg, size_t errmsg_len)
{
	FILE *f = NULL;
	errno_t errcode;
	WCHAR *fnW;
	WCHAR *modeW;

	fnW = iwcmd_utf8_to_utf16_strdup(fn);
	modeW = iwcmd_utf8_to_utf16_strdup(mode);

	errcode = _wfopen_s(&f,fnW,modeW);

	free(fnW);
	free(modeW);

	errmsg[0]='\0';
	if(errcode!=0) {
		// failure
		strerror_s(errmsg,errmsg_len,(int)errcode);
		f=NULL;
	}
	return f;
}

#else

static FILE* iwcmd_fopen(const char *fn, const char *mode, char *errmsg, size_t errmsg_len)
{
	FILE *f;
	int errcode;

	f=fopen(fn,mode);
	if(!f) {
		errcode = errno;
		iwcmd_strlcpy(errmsg, strerror(errcode), errmsg_len);
	}
	return f;
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
	if(!strcmp(s,"pnm")) return IW_FORMAT_PNM;
	if(!strcmp(s,"ppm")) return IW_FORMAT_PPM;
	if(!strcmp(s,"pgm")) return IW_FORMAT_PGM;
	if(!strcmp(s,"pbm")) return IW_FORMAT_PBM;
	if(!strcmp(s,"pam")) return IW_FORMAT_PAM;
	return IW_FORMAT_UNKNOWN;
}

// Reads the first few bytes in the file to try to figure out
// the file format.
// Records the bytes that were read, so they can be reused without seeking.
static int detect_fmt_of_file(struct params_struct *p, FILE *fp)
{
	p->input_initial_bytes_consumed = 0;
	p->input_initial_bytes_stored=fread(p->input_initial_bytes,1,12,fp);
	clearerr(fp);
	if(p->input_initial_bytes_stored<2) return IW_FORMAT_UNKNOWN;

	return iw_detect_fmt_of_file((const iw_byte*)p->input_initial_bytes,p->input_initial_bytes_stored);
}

// Updates p->dst_width and p->dst_height.
// Returns 0 if we fit to width, 1 if we fit to height.
static int do_bestfit(struct params_struct *p)
{
	int x;
	double exp_factor;
	int retval = 0;

	// If we fit-width, what would the height be?
	exp_factor = ((double)p->dst_width) / p->adjusted_src_width;
	x = (int)(0.5+ ((double)p->adjusted_src_height) * exp_factor);
	if(x<=p->dst_height) {
		// It fits. Use it.
		p->dst_height = x;
		goto done;
	}

	// Fit to height instead.
	retval = 1;
	exp_factor = ((double)p->dst_height) / p->adjusted_src_height;
	x = (int)(0.5+ ((double)p->adjusted_src_width) * exp_factor);
	if(x<p->dst_width) {
		p->dst_width = x;
	}

done:
	if(p->dst_width<1) p->dst_width=1;
	if(p->dst_height<1) p->dst_height=1;
	return retval;
}

static void iwcmd_set_resize(struct iw_context *ctx, int dimension,
	struct resize_alg *alg, struct resize_blur *rblur)
{
	iw_set_resize_alg(ctx,dimension,alg->family,rblur->blur,alg->param1,alg->param2);
}

static int my_readfn(struct iw_context *ctx, struct iw_iodescr *iodescr, void *buf, size_t nbytes,
   size_t *pbytesread)
{
	size_t recorded_bytes_remaining;
	struct params_struct *p = (struct params_struct *)iw_get_userdata(ctx);

	recorded_bytes_remaining = p->input_initial_bytes_stored - p->input_initial_bytes_consumed;
	if(recorded_bytes_remaining > 0) {
		if(recorded_bytes_remaining >= nbytes) {
			// The read can be satisfied from the recorded bytes.
			memcpy(buf,&p->input_initial_bytes[p->input_initial_bytes_consumed],nbytes);
			p->input_initial_bytes_consumed += nbytes;
			*pbytesread = nbytes;
			return 1;
		}
		else {
			size_t bytes_to_read_from_file;
			size_t bytes_read_from_file;

			// Need to use some recorded bytes ...
			memcpy(buf,&p->input_initial_bytes[p->input_initial_bytes_consumed],recorded_bytes_remaining);
			p->input_initial_bytes_consumed += recorded_bytes_remaining;

			// ... and read the rest from the file.
			bytes_to_read_from_file = nbytes - recorded_bytes_remaining;
			bytes_read_from_file = fread(&((unsigned char*)buf)[recorded_bytes_remaining],1,bytes_to_read_from_file,(FILE*)iodescr->fp);
			*pbytesread = recorded_bytes_remaining + bytes_read_from_file;
			return 1;
		}
	}

	*pbytesread = fread(buf,1,nbytes,(FILE*)iodescr->fp);
	return 1;
}

static int my_getfilesizefn(struct iw_context *ctx, struct iw_iodescr *iodescr, iw_int64 *pfilesize)
{
	int ret;
	long lret;
	struct params_struct *p = (struct params_struct *)iw_get_userdata(ctx);

	FILE *fp = (FILE*)iodescr->fp;

	// TODO: Rewrite this to support >4GB file sizes.
	ret=fseek(fp,0,SEEK_END);
	if(ret!=0) return 0;
	lret=ftell(fp);
	if(lret<0) return 0;
	*pfilesize = (iw_int64)lret;
	fseek(fp,(long)p->input_initial_bytes_stored,SEEK_SET);
	return 1;
}

static int my_seekfn(struct iw_context *ctx, struct iw_iodescr *iodescr, iw_int64 offset, int whence)
{
	FILE *fp = (FILE*)iodescr->fp;
	fseek(fp,(long)offset,whence);
	return 1;
}

static int my_writefn(struct iw_context *ctx, struct iw_iodescr *iodescr, const void *buf, size_t nbytes)
{
	fwrite(buf,1,nbytes,(FILE*)iodescr->fp);
	return 1;
}

////////////////// Windows clipboard I/O //////////////////
#ifdef IW_WINDOWS

static void iwcmd_close_clipboard_r(struct params_struct *p, struct iw_context *ctx)
{
	if(p->cb_r_clipboard_is_open) {
		if(p->cb_r_data) {
			GlobalUnlock(p->cb_r_data_handle);
			p->cb_r_data = NULL;
		}
		p->cb_r_data_handle = NULL;
		CloseClipboard();
		p->cb_r_clipboard_is_open=0;
	}
}

static int iwcmd_open_clipboard_for_read(struct params_struct *p, struct iw_context *ctx)
{
	BOOL b;
	HANDLE hClip = NULL;
	int use_dibv5 = 0;
	UINT tmpfmt = 0;

	b=OpenClipboard(GetConsoleWindow());
	if(!b) {
		iw_set_error(ctx,"Failed to open the clipboard");
		return 0;
	}

	p->cb_r_clipboard_is_open = 1;

	// Window can convert CF_DIB <--> CF_DIBV5, but we'd like to avoid that if
	// possible.
	// Enumerate the available clipboard formats. If we see CF_DIBV5 before
	// CF_DIB, that probably means the image originated in CF_DIBV5 format, so
	// that's the format we'll request.
	while(1) {
		tmpfmt = EnumClipboardFormats(tmpfmt);
		if(tmpfmt==0 || tmpfmt==CF_DIB) break;
		if(tmpfmt==CF_DIBV5) {
			use_dibv5 = 1;
			break;
		}
	}

	p->cb_r_data_handle = GetClipboardData(use_dibv5 ? CF_DIBV5 : CF_DIB);
	if(!p->cb_r_data_handle) {
		iw_set_error(ctx,"Can\xe2\x80\x99t find an image on the clipboard");
		iwcmd_close_clipboard_r(p,ctx);
		return 0;
	}

	p->cb_r_data_size = GlobalSize(p->cb_r_data_handle);
	p->cb_r_data = (iw_byte*)GlobalLock(p->cb_r_data_handle);
	p->cb_r_data_pos = 0;
	return 1;
}

static int my_clipboard_readfn(struct iw_context *ctx, struct iw_iodescr *iodescr, void *buf, size_t nbytes,
   size_t *pbytesread)
{
	struct params_struct *p;
	size_t nbytes_to_return;

	p = (struct params_struct *)iw_get_userdata(ctx);
	if(!p->cb_r_data) return 0;
	nbytes_to_return = nbytes;
	if(nbytes_to_return > p->cb_r_data_size-p->cb_r_data_pos)
		nbytes_to_return = p->cb_r_data_size-p->cb_r_data_pos;
	memcpy(buf,&p->cb_r_data[p->cb_r_data_pos],nbytes_to_return);
	*pbytesread = nbytes_to_return;
	p->cb_r_data_pos += nbytes_to_return;
	return 1;
}

static int my_clipboard_getfilesizefn(struct iw_context *ctx, struct iw_iodescr *iodescr, iw_int64 *pfilesize)
{
	struct params_struct *p;
	p = (struct params_struct *)iw_get_userdata(ctx);
	*pfilesize = (iw_int64)p->cb_r_data_size;
	return 1;
}

static int my_clipboard_w_seekfn(struct iw_context *ctx,
	struct iw_iodescr *iodescr, iw_int64 offset, int whence)
{
	struct params_struct *p;
	p = (struct params_struct *)iw_get_userdata(ctx);

	// The only seeks the BMP encoder is ever expected to do are SEEK_SETs
	// to near the beginning of the file, and SEEK_END with offset=0. So we
	// really don't have to worry about integer overflow or underflow, and
	// casting to size_t is safe.

	switch(whence) {
	case SEEK_SET:
		p->cb_w_data_pos = (size_t)offset;
		break;
	case SEEK_CUR:
		p->cb_w_data_pos += (size_t)offset;
		break;
	case SEEK_END:
		p->cb_w_data_pos = (size_t)(p->cb_w_data_high_water_mark + offset);
		break;
	}
	return 1;
}

static int my_clipboard_writefn(struct iw_context *ctx, struct iw_iodescr *iodescr,
	const void *buf, size_t nbytes)
{
	struct params_struct *p;
	iw_byte *newmem;
	int read_14th_byte = 0;
	size_t new_size;

	p = (struct params_struct *)iw_get_userdata(ctx);

	// Make sure we have enough memory allocated
	if(p->cb_w_data) {
		if(p->cb_w_data_pos+nbytes > p->cb_w_data_alloc) {
			// Not enough memory allocated

			// How much to allocate?
			// Start with the minimum amount we need.
			new_size = p->cb_w_data_pos+nbytes;

			if(p->cb_w_predicted_filesize) {
				if(new_size < p->cb_w_predicted_filesize) {
					// We know the predicted file size, and it's big enough, so
					// use that.
					new_size = p->cb_w_predicted_filesize;
				}
				else {
					// The predicted file size is not big enough, presumably
					// because this is a compressed BMP, whose size was therefore
					// not predictable.
					// Double the current allocated amount if that's big enough.
					if(p->cb_w_data_alloc*2 > new_size) {
						new_size = p->cb_w_data_alloc*2;
					}
				}
			}
			
			newmem = realloc(p->cb_w_data,new_size);
			if(!newmem) return 0;
			p->cb_w_data_alloc = new_size;
			p->cb_w_data = newmem;
		}
	}
	else {
		// Nothing allocated yet. Allocate exactly as much as we need
		// at the moment. We'll have to realloc next time, but hopefully by
		// then we'll know the total amount we need.
		p->cb_w_data_alloc = p->cb_w_data_pos+nbytes;
		p->cb_w_data = malloc(p->cb_w_data_alloc);
		if(!p->cb_w_data) return 0;
	}

	if(p->cb_w_data_pos<14 && p->cb_w_data_pos+nbytes>=14)
		read_14th_byte = 1;

	// Store the data being sent to us.
	memcpy(&p->cb_w_data[p->cb_w_data_pos],buf,nbytes);
	p->cb_w_data_pos += nbytes;
	if(p->cb_w_data_pos > p->cb_w_data_high_water_mark) {
		p->cb_w_data_high_water_mark = p->cb_w_data_pos;
	}

	if(read_14th_byte) {
		// If we've read the whole fileheader, look at it to figure out the file size
		p->cb_w_predicted_filesize = 
			p->cb_w_data[2] | (p->cb_w_data[3]<<8) |
			p->cb_w_data[4]<<16 | (p->cb_w_data[5]<<24);

		if(p->cb_w_predicted_filesize<14 || p->cb_w_predicted_filesize>500000000) { // Sanity check
			p->cb_w_predicted_filesize = 0;
		}
	}

	return 1;
}

// Call after writing the image to a memory block, to put the
// image onto the clipboard.
static int finish_clipboard_write(struct params_struct *p, struct iw_context *ctx)
{
	int retval = 0;
	HANDLE cb_data_handle = NULL;
	iw_byte *cb_data = NULL;
	SIZE_T dib_size;
	int opened_clipboard = 0;

	if(!p->cb_w_data) goto done;
	if(p->cb_w_data_high_water_mark <= 14) goto done;
	dib_size = p->cb_w_data_high_water_mark-14;

	// Copy the image to a memory block appropriate for the clipboard.
	cb_data_handle = GlobalAlloc(GMEM_ZEROINIT|GMEM_MOVEABLE,dib_size);
	if(!cb_data_handle) goto done;
	cb_data = GlobalLock(cb_data_handle);
	if(!cb_data) goto done;

	memcpy(cb_data,&p->cb_w_data[14],dib_size);

	// Release our lock on the data we'll put on the clipboard.
	GlobalUnlock(cb_data_handle);
	cb_data = NULL;

	// Prevent the clipboard from being changed by other apps.
	if(!OpenClipboard(GetConsoleWindow())) {
		return 0;
	}
	opened_clipboard = 1;

	// Claim ownership of the clipboard
	if(!EmptyClipboard()) {
		goto done;
	}

	if(!SetClipboardData(p->bmp_version>=5 ? CF_DIBV5 : CF_DIB, cb_data_handle)) {
		goto done;
	}

	// SetClipboardData succeeded, so the clipboard now owns the data.
	// Clear our copy of the handle.
	cb_data_handle = NULL;

	retval = 1;
done:
	if(opened_clipboard) CloseClipboard();

	if(cb_data) {
		GlobalUnlock(cb_data_handle);
	}
	if(cb_data_handle) {
		GlobalFree(cb_data_handle);
	}
	if(p->cb_w_data) {
		free(p->cb_w_data);
		p->cb_w_data = NULL;
	}
	if(!retval) {
		iw_set_error(ctx,"Failed to set clipboard data");
	}
	return retval;
}

#endif // IW_WINDOWS
/////////////////////////////////////////////////

static int iwcmd_calc_rel_size(double rel, int d)
{
	int n;
	n = (int)(0.5 + rel * (double)d);
	if(n<1) n=1;
	return n;
}

static void* my_mallocfn(void *userdata, unsigned int flags, size_t n)
{
	void *mem=NULL;

	if(flags & IW_MALLOCFLAG_ZEROMEM)
		mem = calloc(n,1);
	else
		mem = malloc(n);
	return mem;
}

static void my_freefn(void *userdata, void *mem)
{
	free(mem);
}

static void figure_out_size_and_density(struct params_struct *p, struct iw_context *ctx)
{
	int fit_flag = 0;
	int fit_dimension = -1; // 0 = We're fitting to a specific width. 1=height. -1=neither.
	double xdens,ydens; // density read from source image
	int density_code; // from src image
	double adjusted_dens_x, adjusted_dens_y;
	int nonsquare_pixels_flag = 0;
	int width_specified, height_specified;
	double newdens_x,newdens_y;
	int imagesize_changed;

	iw_get_input_density(ctx,&xdens,&ydens,&density_code);

	if(p->noresize_flag) {
		// Pretend the user requested a target height and width that are exactly
		// those of the source image.
		p->dst_width_req = p->src_width;
		p->dst_height_req = p->src_height;

		// These options shouldn't have been used, but if so, pretend they weren't.
		p->rel_width_flag = 0;
		p->rel_height_flag = 0;
		p->bestfit = 0;
	}

	if(density_code!=IW_DENSITY_UNKNOWN) {
		if(fabs(xdens-ydens)>=0.00001) {
			nonsquare_pixels_flag = 1;
		}
	}

	width_specified = p->dst_width_req>0 || p->rel_width_flag;
	height_specified = p->dst_height_req>0 || p->rel_height_flag;
	// If the user failed to specify a width, or a height, or used the 'bestfit'
	// option, then we need to "fit" the image in some way.
	fit_flag = !width_specified || !height_specified || p->bestfit;

	// Set adjusted_* variables, which will be different from the original ones
	// of there are nonsquare pixels. For certain operations, we'll pretend that
	// the adjusted settings are the real setting.
	p->adjusted_src_width = p->src_width;
	p->adjusted_src_height = p->src_height;
	adjusted_dens_x = xdens;
	adjusted_dens_y = ydens;
	if(nonsquare_pixels_flag && fit_flag) {
		if(xdens > ydens) {
			p->adjusted_src_height = (int)(0.5+ (xdens/ydens) * (double)p->adjusted_src_height);
			adjusted_dens_y = xdens;
		}
		else {
			p->adjusted_src_width = (int)(0.5+ (ydens/xdens) * (double)p->adjusted_src_width);
			adjusted_dens_x = ydens;
		}
	}

	p->dst_width = p->dst_width_req;
	p->dst_height = p->dst_height_req;

	if(p->dst_width<0) p->dst_width = -1;
	if(p->dst_height<0) p->dst_height = -1;
	if(p->dst_width==0) p->dst_width = 1;
	if(p->dst_height==0) p->dst_height = 1;

	if(p->rel_width_flag) {
		p->dst_width = iwcmd_calc_rel_size(p->rel_width, p->adjusted_src_width);
	}
	if(p->rel_height_flag) {
		p->dst_height = iwcmd_calc_rel_size(p->rel_height, p->adjusted_src_height);
	}

	if(p->dst_width == -1 && p->dst_height == -1) {
		// Neither -width nor -height specified. Keep image the same size.
		// (But if the pixels were not square, pretend the image was a different
		// size, and had square pixels.)
		p->dst_width=p->adjusted_src_width;
		p->dst_height=p->adjusted_src_height;
	}
	else if(p->dst_height == -1) {
		// -width given but not -height. Fit to width.
		p->dst_height=1000000;
		do_bestfit(p);
		fit_dimension=0;
	}
	else if(p->dst_width == -1) {
		// -height given but not -width. Fit to height.
		p->dst_width=1000000;
		do_bestfit(p);
		fit_dimension=1;
	}
	else if(p->bestfit) {
		// -width and -height and -bestfit all given. Best-fit into the given dimensions.
		fit_dimension=do_bestfit(p);
	}
	else {
		// -width and -height given but not -bestfit. Use the exact dimensions given.
		;
	}

	if(p->dst_width<1) p->dst_width=1;
	if(p->dst_height<1) p->dst_height=1;

	imagesize_changed = 0;
	if(p->imagesize_set) {
		if(fabs(p->imagesize_x - (double)p->dst_width) > 0.00001) imagesize_changed=1;
		if(fabs(p->imagesize_y - (double)p->dst_height) > 0.00001) imagesize_changed=1;
	}
	else {
		if(p->dst_width!=p->src_width || p->dst_height!=p->src_height)
			imagesize_changed = 1;
	}

	// Figure out what policy=AUTO means.
	if(p->density_policy==IWCMD_DENSITY_POLICY_AUTO) {
		if(density_code==IW_DENSITY_UNKNOWN) {
			p->density_policy=IWCMD_DENSITY_POLICY_NONE;
		}
		else if(!imagesize_changed) {
			p->density_policy=IWCMD_DENSITY_POLICY_KEEP;
		}
		else if(!width_specified && !height_specified && !p->imagesize_set) {
			// If the user did not request the size to be changed, but we're
			// changing it anyway (presumably due to nonsquare pixels), keep
			// the image the same physical size.
			p->density_policy=IWCMD_DENSITY_POLICY_ADJUST;
		}
		else {
			p->density_policy=IWCMD_DENSITY_POLICY_NONE;
		}
	}

	// Finally, set the new density, based on the POLICY.
	if(p->density_policy==IWCMD_DENSITY_POLICY_KEEP) {
		if(density_code!=IW_DENSITY_UNKNOWN) {
			iw_set_output_density(ctx,adjusted_dens_x,adjusted_dens_y,density_code);
		}
	}
	else if(p->density_policy==IWCMD_DENSITY_POLICY_ADJUST && p->imagesize_set) {
		if(density_code!=IW_DENSITY_UNKNOWN) {
			newdens_x = adjusted_dens_x*(p->imagesize_x/p->adjusted_src_width);
			newdens_y = adjusted_dens_y*(p->imagesize_y/p->adjusted_src_height);
			iw_set_output_density(ctx,newdens_x,newdens_y,density_code);
		}
	}
	else if(p->density_policy==IWCMD_DENSITY_POLICY_ADJUST) {
		if(density_code!=IW_DENSITY_UNKNOWN) {
			// If we don't do anything to prevent it, the "adjust" policy will
			// tend to create images whose pixels are slightly non-square. While
			// not *wrong*, this is usually undesirable.
			// So, if the source image had square pixels, fudge the
			// density label so that the target image also has square pixels, even
			// if that makes the label less accurate.
			// If possible, fix it up by changing the density of the dimension whose
			// size wasn't set by the user.
			newdens_x = adjusted_dens_x*(((double)p->dst_width)/p->adjusted_src_width);
			newdens_y = adjusted_dens_y*(((double)p->dst_height)/p->adjusted_src_height);
			if(fit_flag) {
				if(fit_dimension==0) {
					// X dimension is the important one; tweak the Y density.
					newdens_y = newdens_x;
				}
				else if(fit_dimension==1) {
					newdens_x = newdens_y;
				}
				else {
					// Don't know which dimension is important; use the average.
					newdens_x = (newdens_x+newdens_y)/2.0;
					newdens_y = newdens_x;
				}
			}
			iw_set_output_density(ctx,newdens_x,newdens_y,density_code);
		}
	}
	else if(p->density_policy==IWCMD_DENSITY_POLICY_FORCED) {
		iw_set_output_density(ctx,p->density_forced_x,p->density_forced_y,
			IW_DENSITY_UNITS_PER_METER);
	}
}

static void iwcmd_set_bitdepth(struct params_struct *p, struct iw_context *ctx)
{
	int k;
	int overall_depth;

	// Make a copy of p->depth, so we can mess with it.
	overall_depth = p->depth;

	// -depthcc overrides other settings
	if(p->depthcc>=2) {
		while(1<<overall_depth < p->depthcc) {
			overall_depth++;
		}
		for(k=0;k<5;k++) {
			iw_set_output_max_color_code(ctx,k,p->depthcc-1);
		}
		return;
	}

	// Make sure overall_depth is at least the max depth of any channel
	for(k=0;k<5;k++) {
		if(overall_depth < p->channel_depth[k]) {
			overall_depth = p->channel_depth[k];
		}
	}

	if(!overall_depth) {
		// User made no requests; use the defaults for everything.
		return;
	}

	// Sanitize the overall depth setting.
	// HACK: We shouldn't have to hardcode this information here.
	if(p->outfmt==IW_FORMAT_MIFF) {
		if(overall_depth<=32) overall_depth=32;
		else overall_depth=64;
	}
	else {
		if(overall_depth<=8) overall_depth=8;
		else overall_depth=16;
	}

	iw_set_output_depth(ctx,overall_depth);

	// Set the requested depth for all unset channels to the requested depth.
	if(p->depth) {
		for(k=0;k<5;k++) {
			if(!p->channel_depth[k]) {
				p->channel_depth[k] = p->depth;
			}
		}
	}

	for(k=0;k<5;k++) {
		// Tell the library the requested channel-specific depths
		// (if the depth is known, and less than the overall depth).
		if(p->channel_depth[k]>0 && p->channel_depth[k]<overall_depth) {
			iw_set_output_max_color_code(ctx,k,(1<<p->channel_depth[k])-1);
		}
	}
}

static int iwcmd_run(struct params_struct *p)
{
	int retval = 0;
	struct iw_context *ctx = NULL;
	//int imgtype_read;
	struct iw_iodescr readdescr;
	struct iw_iodescr writedescr;
	char errmsg[200];
	struct iw_init_params init_params;
	const char *s;
	unsigned int profile;
	int i;
	int k;
	int tmpflag;

	memset(&init_params,0,sizeof(struct iw_init_params));
	memset(&readdescr,0,sizeof(struct iw_iodescr));
	memset(&writedescr,0,sizeof(struct iw_iodescr));

	if(!p->noinfo) {
		iwcmd_message(p,"%s \xe2\x86\x92 %s\n",p->input_uri.filename,p->output_uri.filename);
	}

	init_params.api_version = IW_VERSION_INT;
	init_params.userdata = (void*)p;
	init_params.mallocfn = my_mallocfn;
	init_params.freefn = my_freefn;

	ctx = iw_create_context(&init_params);
	if(!ctx) goto done;

	iw_set_warning_fn(ctx,my_warning_handler);

	// Decide on the output format as early as possible, so we can give up
	// quickly if it's not supported.
	if(p->outfmt==IW_FORMAT_UNKNOWN) {
		if(p->output_uri.scheme==IWCMD_SCHEME_FILE) {
			p->outfmt=iw_detect_fmt_from_filename(p->output_uri.filename);
		}
		else if(p->output_uri.scheme==IWCMD_SCHEME_CLIPBOARD) {
			p->outfmt=IW_FORMAT_BMP;
		}
	}

	if(p->outfmt==IW_FORMAT_UNKNOWN) {
		iw_set_error(ctx,"Unknown output format; use -outfmt.");
		goto done;
	}
	else if(!iw_is_output_fmt_supported(p->outfmt)) {
		s = iw_get_fmt_name(p->outfmt);
		if(!s) s="(unknown)";
		iw_set_errorf(ctx,"Writing %s files is not supported.",s);
		goto done;
	}

	if(p->output_uri.scheme==IWCMD_SCHEME_CLIPBOARD) {
		if(p->outfmt!=IW_FORMAT_BMP) {
			iw_set_error(ctx,"Only BMP images can be copied to the clipboard");
			goto done;
		}
	}

	for(i=0; i<p->options_count; i++) {
		iw_set_option(ctx, p->options[i].name, p->options[i].val);

		if(!strcmp(p->options[i].name, "bmp:version")) {
			// A hack, but we need to know the BMP version to know
			// whether to enable transparency.
			p->bmp_version = iw_parse_int(p->options[i].val);
		}
	}

	if(p->random_seed!=0 || p->randomize) {
		iw_set_random_seed(ctx,p->randomize, p->random_seed);
	}

	if(p->sample_type>=0) iw_set_value(ctx,IW_VAL_OUTPUT_SAMPLE_TYPE,p->sample_type);
	if(p->no_gamma) iw_set_value(ctx,IW_VAL_DISABLE_GAMMA,1);
	if(p->intclamp) iw_set_value(ctx,IW_VAL_INT_CLAMP,1);
	if(p->no_cslabel) iw_set_value(ctx,IW_VAL_NO_CSLABEL,1);
	if(p->noopt_grayscale) iw_set_allow_opt(ctx,IW_OPT_GRAYSCALE,0);
	if(p->noopt_palette) iw_set_allow_opt(ctx,IW_OPT_PALETTE,0);
	if(p->noopt_reduceto8) iw_set_allow_opt(ctx,IW_OPT_16_TO_8,0);
	if(p->noopt_stripalpha) iw_set_allow_opt(ctx,IW_OPT_STRIP_ALPHA,0);
	if(p->noopt_binarytrns) iw_set_allow_opt(ctx,IW_OPT_BINARY_TRNS,0);
	if(p->edge_policy_x>=0) iw_set_value(ctx,IW_VAL_EDGE_POLICY_X,p->edge_policy_x);
	if(p->edge_policy_y>=0) iw_set_value(ctx,IW_VAL_EDGE_POLICY_Y,p->edge_policy_y);
	if(p->grayscale_formula>=0) {
		iw_set_value(ctx,IW_VAL_GRAYSCALE_FORMULA,p->grayscale_formula);
		if(p->grayscale_formula==IW_GSF_WEIGHTED || p->grayscale_formula==IW_GSF_ORDERBYVALUE) {
			iw_set_grayscale_weights(ctx,p->grayscale_weight[0],p->grayscale_weight[1],p->grayscale_weight[2]);
		}
	}
	if(p->page_to_read>0) iw_set_value(ctx,IW_VAL_PAGE_TO_READ,p->page_to_read);
	if(p->include_screen>=0) iw_set_value(ctx,IW_VAL_INCLUDE_SCREEN,p->include_screen);
	if(p->negate) iw_set_value(ctx,IW_VAL_NEGATE_TARGET,1);

	if(p->input_uri.scheme==IWCMD_SCHEME_FILE) {
		readdescr.read_fn = my_readfn;
		readdescr.getfilesize_fn = my_getfilesizefn;
		readdescr.fp = (void*)iwcmd_fopen(p->input_uri.filename, "rb", errmsg, sizeof(errmsg));
		if(!readdescr.fp) {
			iw_set_errorf(ctx,"Failed to open %s for reading: %s", p->input_uri.filename, errmsg);
			goto done;
		}
	}
	else if(p->input_uri.scheme==IWCMD_SCHEME_STDIN) {
#ifdef IW_WINDOWS
		_setmode(_fileno(stdin),_O_BINARY);
#endif
		readdescr.read_fn = my_readfn;
		readdescr.fp = (void*)stdin;
	}
#ifdef IW_WINDOWS
	else if(p->input_uri.scheme==IWCMD_SCHEME_CLIPBOARD) {
		if(!iwcmd_open_clipboard_for_read(p,ctx)) goto done;
		readdescr.read_fn = my_clipboard_readfn;
		readdescr.getfilesize_fn = my_clipboard_getfilesizefn;
		readdescr.fp = NULL;
	}
#endif
	else {
		iw_set_error(ctx,"Unsupported input scheme");
		goto done;
	}

	// Decide on the input format.
	if(p->infmt==IW_FORMAT_UNKNOWN) {
		switch(p->input_uri.scheme) {
		case IWCMD_SCHEME_FILE:
		case IWCMD_SCHEME_STDIN:
			p->infmt=detect_fmt_of_file(p,(FILE*)readdescr.fp);
			break;
		case IWCMD_SCHEME_CLIPBOARD:
			p->infmt=IW_FORMAT_BMP;
		}
	}

	if(p->infmt==IW_FORMAT_UNKNOWN) {
		iw_set_error(ctx,"Unknown input file format.");
		goto done;
	}

	if(p->input_uri.scheme==IWCMD_SCHEME_CLIPBOARD && p->infmt==IW_FORMAT_BMP) {
		iw_set_value(ctx,IW_VAL_BMP_NO_FILEHEADER,1);
	}

	if(!iw_read_file_by_fmt(ctx,&readdescr,p->infmt)) goto done;

	if(p->input_uri.scheme==IWCMD_SCHEME_FILE) {
		fclose((FILE*)readdescr.fp);
	}
	readdescr.fp=NULL;

	if(p->reorient) {
		iw_reorient_image(ctx,p->reorient);
	}

	// imgtype_read = iw_get_value(ctx,IW_VAL_INPUT_IMAGE_TYPE);

	// We have to tell the library the output format, so it can know what
	// kinds of images are allowed (e.g. whether transparency is allowed).
	profile = iw_get_profile_by_fmt(p->outfmt);
	if(p->bmp_trns && p->outfmt==IW_FORMAT_BMP) {
		// TODO: This is part of a "temporary" hack.
		// We support BMP transparency, but with a maximum of 255 opaque
		// colors, instead of the full 256 that ought to be supported.
		profile |= IW_PROFILE_PALETTETRNS|IW_PROFILE_TRANSPARENCY|IW_PROFILE_RGB8_BKGD;
	}
	if(p->outfmt==IW_FORMAT_BMP && p->bmp_version>=5) {
		profile |= IW_PROFILE_TRANSPARENCY;
	}
	if(p->outfmt==IW_FORMAT_BMP) {
		profile |= IW_PROFILE_16BPS;
	}
	iw_set_output_profile(ctx, profile);

	iwcmd_set_bitdepth(p,ctx);

	if(p->cs_in_set) {
		iw_set_input_colorspace(ctx,&p->cs_in);
	}
	if(p->cs_out_set) {
		iw_set_output_colorspace(ctx,&p->cs_out);
	}
	if(p->rendering_intent!=IW_INTENT_UNKNOWN) {
		iw_set_value(ctx, IW_VAL_INTENT, p->rendering_intent);
	}

	if(p->dither_all.family>=0)      iw_set_dither_type(ctx,IW_CHANNELTYPE_ALL     ,p->dither_all.family     ,p->dither_all.subtype);
	if(p->dither_nonalpha.family>=0) iw_set_dither_type(ctx,IW_CHANNELTYPE_NONALPHA,p->dither_nonalpha.family,p->dither_nonalpha.subtype);
	for(k=0;k<5;k++) {
		if(p->dither[k].family>=0) iw_set_dither_type(ctx,k,p->dither[k].family,p->dither[k].subtype);
	}

	// Force bi-level formats to use 2 colors.
	if(p->outfmt==IW_FORMAT_PBM) {
		p->color_count_all = 2;
	}

	if(p->color_count_all) iw_set_color_count  (ctx,IW_CHANNELTYPE_ALL  ,p->color_count_all);
	if(p->color_count_nonalpha) iw_set_color_count(ctx,IW_CHANNELTYPE_NONALPHA,p->color_count_nonalpha);
	for(k=0;k<5;k++) {
		if(p->color_count[k])   iw_set_color_count(ctx,k,p->color_count[k]);
	}

	// Force graysale if the format only supports grayscale.
	if(p->outfmt==IW_FORMAT_PGM || p->outfmt==IW_FORMAT_PBM) {
		p->grayscale = 1;
		p->condgrayscale = 0;
	}

	if(p->condgrayscale) {
		if(iw_get_value(ctx,IW_VAL_INPUT_NATIVE_GRAYSCALE)) {
			iw_set_value(ctx,IW_VAL_CVT_TO_GRAYSCALE,1);
		}
	}
	else if(p->grayscale) {
		iw_set_value(ctx,IW_VAL_CVT_TO_GRAYSCALE,1);
	}

	for(k=0;k<3;k++) {
		if(p->offset_h[k]!=0.0) iw_set_channel_offset(ctx,k,IW_DIMENSION_H,p->offset_h[k]);
		if(p->offset_v[k]!=0.0) iw_set_channel_offset(ctx,k,IW_DIMENSION_V,p->offset_v[k]);
	}

	if(p->apply_bkgd) {

		// iw_set_applybkgd() requires background color to be in a linear
		// colorspace, so convert it (from sRGB) if needed.
		if(!p->no_gamma) {
			struct iw_csdescr cs_srgb;

			// Make an sRGB descriptor to use with iw_convert_sample_to_linear.
			iw_make_srgb_csdescr_2(&cs_srgb);

			for(k=0;k<3;k++) {
				p->bkgd.c[k] = iw_convert_sample_to_linear(p->bkgd.c[k],&cs_srgb);
				if(p->bkgd_checkerboard) {
					p->bkgd2.c[k] = iw_convert_sample_to_linear(p->bkgd2.c[k],&cs_srgb);
				}
			}
		}

		iw_set_apply_bkgd_2(ctx,&p->bkgd);
		if(p->bkgd_checkerboard) {
			iw_set_bkgd_checkerboard_2(ctx,p->bkgd_check_size,&p->bkgd2);
			iw_set_bkgd_checkerboard_origin(ctx,p->bkgd_check_origin_x,p->bkgd_check_origin_y);
		}
	}

	if(p->use_bkgd_label) {
		iw_set_value(ctx,IW_VAL_USE_BKGD_LABEL,1);
	}
	if(p->no_bkgd_label) {
		if(p->no_bkgd_label) iw_set_value(ctx,IW_VAL_NO_BKGD_LABEL,1);
	}
	else if(p->bkgd_label_set) {
		iw_set_output_bkgd_label_2(ctx,&p->bkgd_label);
	}

	p->src_width=iw_get_value(ctx,IW_VAL_INPUT_WIDTH);
	p->src_height=iw_get_value(ctx,IW_VAL_INPUT_HEIGHT);

	// If we're cropping, adjust the src_width and height accordingly.
	if(p->use_crop) {
		if(p->crop_x<0) p->crop_x=0;
		if(p->crop_y<0) p->crop_y=0;
		if(p->crop_x>p->src_width-1) p->crop_x=p->src_width-1;
		if(p->crop_y>p->src_height-1) p->crop_y=p->src_height-1;
		if(p->crop_w<0 || p->crop_w>p->src_width-p->crop_x) p->crop_w=p->src_width-p->crop_x;
		if(p->crop_h<0 || p->crop_h>p->src_height-p->crop_y) p->crop_h=p->src_height-p->crop_y;
		if(p->crop_w<1) p->crop_w=1;
		if(p->crop_h<1) p->crop_h=1;

		p->src_width = p->crop_w;
		p->src_height = p->crop_h;
	}

	figure_out_size_and_density(p,ctx);

	if((p->edge_policy_x!=IW_EDGE_POLICY_REPLICATE &&
		p->edge_policy_x!=IW_EDGE_POLICY_TRANSPARENT) ||
		(p->edge_policy_y!=IW_EDGE_POLICY_REPLICATE &&
		p->edge_policy_y!=IW_EDGE_POLICY_TRANSPARENT))
	{
		if(p->imagesize_set)
			iw_warning(ctx,"\xe2\x80\x9c-edge t\xe2\x80\x9d is recommended when using -imagesize");
		else if(p->translate_set)
			iw_warning(ctx,"\xe2\x80\x9c-edge t\xe2\x80\x9d is recommended when using -translate");
	}

	if(p->translate_set) {
		if(p->translate_src_flag) {
			// Convert from dst pixels to src pixels
			if(p->translate_x!=0.0) {
				p->translate_x *= ((double)p->dst_width)/p->src_width;
			}
			if(p->translate_y!=0.0) {
				p->translate_y *= ((double)p->dst_height)/p->src_height;
			}
		}
		iw_set_value_dbl(ctx,IW_VAL_TRANSLATE_X,p->translate_x);
		iw_set_value_dbl(ctx,IW_VAL_TRANSLATE_Y,p->translate_y);
	}

	tmpflag = 0; // Have we displayed a "gaussian filter" warning yet?
	if(p->resize_blur_x.is_set && !p->resize_alg_x.family) {
		if(!p->nowarn) {
			iwcmd_warning(p,"Notice: Selecting gaussian filter for blurring\n");
			tmpflag = 1;
		}
		p->resize_alg_x.family = IW_RESIZETYPE_GAUSSIAN;
	}
	if(p->resize_blur_y.is_set && !p->resize_alg_y.family) {
		if(!p->nowarn && !tmpflag) {
			iwcmd_warning(p,"Notice: Selecting gaussian filter for blurring\n");
		}
		p->resize_alg_y.family = IW_RESIZETYPE_GAUSSIAN;
	}

	// Wait until we know the target image size to set the resize algorithm, so
	// that we can support our "interpolate" option.
	if(p->resize_alg_x.family) {
		if(p->resize_blur_x.interpolate && p->dst_width<p->src_width) {
			// If downscaling, "sharpen" the filter to emulate interpolation.
			p->resize_blur_x.blur *= ((double)p->dst_width)/p->src_width;
		}
		iwcmd_set_resize(ctx,IW_DIMENSION_H,&p->resize_alg_x,&p->resize_blur_x);
	}
	if(p->resize_alg_y.family) {
		if(p->resize_blur_y.interpolate && p->dst_height<p->src_height) {
			p->resize_blur_y.blur *= ((double)p->dst_height)/p->src_height;
		}
		iwcmd_set_resize(ctx,IW_DIMENSION_V,&p->resize_alg_y,&p->resize_blur_y);
	}

	if(p->noinfo) {
		;
	}
	else if(p->dst_width==p->src_width && p->dst_height==p->src_height) {
		iwcmd_message(p,"Processing: %d\xc3\x97%d\n",p->dst_width,p->dst_height);
	}
	else {
		iwcmd_message(p,"Resizing: %d\xc3\x97%d \xe2\x86\x92 %d\xc3\x97%d\n",p->src_width,p->src_height,
			p->dst_width,p->dst_height);
	}

	iw_set_output_canvas_size(ctx,p->dst_width,p->dst_height);
	if(p->imagesize_set) {
		iw_set_output_image_size(ctx,p->imagesize_x,p->imagesize_y);
	}
	if(p->use_crop) {
		iw_set_input_crop(ctx,p->crop_x,p->crop_y,p->crop_w,p->crop_h);
	}

	if(!iw_process_image(ctx)) goto done;

	if(p->compression>0) {
		iw_set_value(ctx,IW_VAL_COMPRESSION,p->compression);
	}
	if(p->interlace) {
		iw_set_value(ctx,IW_VAL_OUTPUT_INTERLACED,1);
	}

	if(p->output_uri.scheme==IWCMD_SCHEME_FILE) {
		writedescr.write_fn = my_writefn;
		writedescr.seek_fn = my_seekfn;
		writedescr.fp = (void*)iwcmd_fopen(p->output_uri.filename, "wb", errmsg, sizeof(errmsg));
		if(!writedescr.fp) {
			iw_set_errorf(ctx,"Failed to open %s for writing: %s", p->output_uri.filename, errmsg);
			goto done;
		}
	}
	else if(p->output_uri.scheme==IWCMD_SCHEME_STDOUT) {
#ifdef IW_WINDOWS
		_setmode(_fileno(stdout),_O_BINARY);
#endif
		writedescr.write_fn = my_writefn;
		writedescr.fp = (void*)stdout;
	}
#ifdef IW_WINDOWS
	else if(p->output_uri.scheme==IWCMD_SCHEME_CLIPBOARD) {
		writedescr.write_fn = my_clipboard_writefn;
		writedescr.seek_fn = my_clipboard_w_seekfn;
	}
#endif
	else {
		iw_set_error(ctx,"Unsupported output scheme");
		goto done;
	}

	if(!iw_write_file_by_fmt(ctx,&writedescr,p->outfmt)) goto done;

	if(p->output_uri.scheme==IWCMD_SCHEME_FILE) {
		fclose((FILE*)writedescr.fp);
	}
#ifdef IW_WINDOWS
	else if(p->output_uri.scheme==IWCMD_SCHEME_CLIPBOARD) {
		finish_clipboard_write(p,ctx);
	}
#endif
	writedescr.fp=NULL;

	retval = 1;

done:
#ifdef IW_WINDOWS
	iwcmd_close_clipboard_r(p,ctx);
#endif
	if(readdescr.fp) fclose((FILE*)readdescr.fp);
	if(writedescr.fp) fclose((FILE*)writedescr.fp);

	if(ctx) {
		if(iw_get_errorflag(ctx)) {
			iwcmd_error(p,"imagew error: %s\n",iw_get_errormsg(ctx,errmsg,sizeof(errmsg)));
		}
	}

	iw_destroy_context(ctx);

	for(i=0; i<p->options_count; i++) {
		free(p->options[i].name);
		free(p->options[i].val);
	}
	p->options_count=0;

	return retval;
}

// Parse two numbers separated by a comma.
// If the string doesn't contain enough numbers, some parameters will be
// left unchanged.
static void iwcmd_parse_dbl_pair(const char *s, double *n1, double *n2)
{
	double nums[2];
	int count;

	count = iw_parse_number_list(s,2,nums);
	if(count>=1) *n1 = nums[0];
	if(count>=2) *n2 = nums[1];
}

// Parse two integers separated by a comma.
// If the string doesn't contain enough numbers, some parameters will be
// left unchanged.
static void iwcmd_parse_int_pair(const char *s, int *i1, int *i2)
{
	double nums[2];
	int count;

	count = iw_parse_number_list(s,2,nums);
	if(count>=1) *i1 = iw_round_to_int(nums[0]);
	if(count>=2) *i2 = iw_round_to_int(nums[1]);
}

// Parse up to four integers separated by commas.
// If the string doesn't contain enough numbers, some parameters will be
// left unchanged.
static void iwcmd_parse_int_4(const char *s, int *i1, int *i2, int *i3, int *i4)
{
	double nums[4];
	int count;

	count = iw_parse_number_list(s,4,nums);
	if(count>=1) *i1 = iw_round_to_int(nums[0]);
	if(count>=2) *i2 = iw_round_to_int(nums[1]);
	if(count>=3) *i3 = iw_round_to_int(nums[2]);
	if(count>=4) *i4 = iw_round_to_int(nums[3]);
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
static void parse_bkgd_color(struct iw_color *clr, const char *s, size_t s_len)
{
	int k;

	clr->c[IW_CHANNELTYPE_ALPHA] = 1.0;

	if(s_len==3) {
		for(k=0;k<3;k++) clr->c[k] = hexvalue1(s[k]);
	}
	else if(s_len==4) {
		for(k=0;k<4;k++) clr->c[k] = hexvalue1(s[k]);
	}
	else if(s_len==6) {
		for(k=0;k<3;k++) clr->c[k] = hexvalue2(s[k*2],s[k*2+1]);
	}
	else if(s_len==8) {
		for(k=0;k<4;k++) clr->c[k] = hexvalue2(s[k*2],s[k*2+1]);
	}
	else if(s_len==12) {
		for(k=0;k<3;k++) clr->c[k] = hexvalue4(s[k*4],s[k*4+1],s[k*4+2],s[k*4+3]);
	}
	else if(s_len==16) {
		for(k=0;k<4;k++) clr->c[k] = hexvalue4(s[k*4],s[k*4+1],s[k*4+2],s[k*4+3]);
	}
	else {
		// Invalid color description.
		clr->c[0] = 1.0;
		clr->c[1] = 0.0;
		clr->c[2] = 1.0;
	}
}

// 's' is either a single color, or two colors separated with a comma.
static void iwcmd_option_bkgd(struct params_struct *p, const char *s)
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

static void iwcmd_option_bkgd_label(struct params_struct *p, const char *s)
{
	struct iw_csdescr cs_srgb;

	parse_bkgd_color(&p->bkgd_label,s,strlen(s));
	iw_make_srgb_csdescr_2(&cs_srgb);
	p->bkgd_label.c[0] = iw_convert_sample_to_linear(p->bkgd_label.c[0],&cs_srgb);
	p->bkgd_label.c[1] = iw_convert_sample_to_linear(p->bkgd_label.c[1],&cs_srgb);
	p->bkgd_label.c[2] = iw_convert_sample_to_linear(p->bkgd_label.c[2],&cs_srgb);
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

// Decode a resize algorithm.
// Puts the result in 'alg' (supplied by caller).
// Returns -1 on failure (for consistency with other decode_ functions.
static int iwcmd_decode_resizetype(struct params_struct *p,
	const char *s, struct resize_alg *alg)
{
	int i;
	int len, namelen;
	struct resizetable_struct {
		const char *name;
		int resizetype;
	};
	static const struct resizetable_struct resizetable[] = {
		{"mix",IW_RESIZETYPE_MIX},
		{"nearest",IW_RESIZETYPE_NEAREST},
		{"point",IW_RESIZETYPE_NEAREST},
		{"linear",IW_RESIZETYPE_TRIANGLE},
		{"triangle",IW_RESIZETYPE_TRIANGLE},
		{"quadratic",IW_RESIZETYPE_QUADRATIC},
		{"hermite",IW_RESIZETYPE_HERMITE},
		{"box",IW_RESIZETYPE_BOX},
		{"boxavg",IW_RESIZETYPE_BOXAVG},
		{"gaussian",IW_RESIZETYPE_GAUSSIAN},
		{"auto",IW_RESIZETYPE_AUTO},
		{"null",IW_RESIZETYPE_NULL},
		{NULL,0}
	};

	memset(alg,0,sizeof(struct resize_alg));
	alg->param1=0.0;
	alg->param2=0.0;

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
			alg->param1 = iw_parse_number(&s[namelen]);
		else
			alg->param1 = 3.0;
		alg->family = IW_RESIZETYPE_LANCZOS;
		return 1;
	}
	else if((namelen==4 && !strncmp(s,"hann",namelen)) ||
		    (namelen==7 && !strncmp(s,"hanning",namelen)) )
	{
		if(len>namelen)
			alg->param1 = iw_parse_number(&s[namelen]);
		else
			alg->param1 = 4.0;
		alg->family = IW_RESIZETYPE_HANNING;
		return 1;
	}
	else if(namelen==8 && !strncmp(s,"blackman",namelen)) {
		if(len>namelen)
			alg->param1 = iw_parse_number(&s[namelen]);
		else
			alg->param1 = 4.0;
		alg->family = IW_RESIZETYPE_BLACKMAN;
		return 1;
	}
	else if(namelen==4 && !strncmp(s,"sinc",namelen)) {
		if(len>namelen)
			alg->param1 = iw_parse_number(&s[namelen]);
		else
			alg->param1 = 4.0;
		alg->family = IW_RESIZETYPE_SINC;
		return 1;
	}
	else if(!strcmp(s,"catrom")) {
		alg->family = IW_RESIZETYPE_CUBIC;
		alg->param1 = 0.0; alg->param2 = 0.5;
		return 1;
	}
	else if(!strcmp(s,"mitchell")) {
		alg->family = IW_RESIZETYPE_CUBIC;
		alg->param1 = 1.0/3; alg->param2 = 1.0/3;
		return 1;
	}
	else if(!strcmp(s,"bspline")) {
		alg->family = IW_RESIZETYPE_CUBIC;
		alg->param1 = 1.0; alg->param2 = 0.0;
		return 1;
	}
	else if(namelen==5 && !strncmp(s,"cubic",namelen)) {
		// Format is "cubic<B>,<C>"
		char *cpos;
		if(len < namelen+3) goto done; // error
		cpos = strchr(s,',');
		if(!cpos) goto done;
		alg->param1 = iw_parse_number(&s[namelen]);
		alg->param2 = iw_parse_number(cpos+1);
		alg->family = IW_RESIZETYPE_CUBIC;
		return 1;
	}
	else if(namelen==4 && !strncmp(s,"keys",namelen)) {
		// Format is "keys<alpha>"
		if(len>namelen)
			alg->param2 = iw_parse_number(&s[namelen]);
		else
			alg->param2 = 0.5;
		alg->param1 = 1.0-2.0*alg->param2;
		alg->family = IW_RESIZETYPE_CUBIC;
		return 1;
	}

done:
	iwcmd_error(p,"Unknown resize type \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
	return -1;
}

// If this ever failed, it would return -1.
static int iwcmd_decode_blur_option(struct params_struct *p,
	const char *s, struct resize_blur *rblur)
{
	int namelen;

	rblur->is_set = 1;
	namelen=iwcmd_get_name_len(s);

	if(namelen==1 && !strncmp(s,"x",namelen)) {
		rblur->interpolate = 1;
		if(strlen(s)==1)
			rblur->blur = 1.0;
		else
			rblur->blur = iw_parse_number(&s[namelen]);
		return 1;
	}

	rblur->interpolate = 0;
	rblur->blur = iw_parse_number(s);
	return 1;
}

// Populates 'di' (supplied by caller).
// Returns -1 on failure.
static int iwcmd_decode_dithertype(struct params_struct *p,const char *s,struct dither_setting *di)
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
	 {"none"      ,IW_DITHERFAMILY_NONE   ,IW_DITHERSUBTYPE_DEFAULT},
	 {NULL        ,0                      ,0}
	};

	for(i=0; dithertable[i].name; i++) {
		if(!strcmp(s,dithertable[i].name)) {
			di->family  = dithertable[i].ditherfamily;
			di->subtype = dithertable[i].dithersubtype;
			return 1;
		}
	}

	iwcmd_message(p,"Unknown dither type \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
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
		g = iw_parse_number(&s[namelen]);
		iw_make_gamma_csdescr(cs,g);
	}
	else if(!strcmp(s,"linear")) {
		iw_make_linear_csdescr(cs);
	}
	else if(len>=4 && !strncmp(s,"srgb",4)) {
		switch(s[4]) {
			case 'p': p->rendering_intent=IW_INTENT_PERCEPTUAL; break;
			case 'r': p->rendering_intent=IW_INTENT_RELATIVE; break;
			case 's': p->rendering_intent=IW_INTENT_SATURATION; break;
			case 'a': p->rendering_intent=IW_INTENT_ABSOLUTE; break;
		}
		iw_make_srgb_csdescr_2(cs);
	}
	else if(!strcmp(s,"rec709")) {
		iw_make_rec709_csdescr(cs);
	}
	else {
		iwcmd_error(p,"Unknown color space \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
		return -1;
	}
	return 1;
}

static int iwcmd_parse_noopt(struct params_struct *p, const char *s)
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

static void iwcmd_option_forced_density(struct params_struct *p, const char *s)
{
	double nums[2];
	int count;

	nums[0] = 0.0;
	nums[1] = 0.0;
	count = iw_parse_number_list(&s[1],2,nums);
	if(count<1) return;

	p->density_policy = IWCMD_DENSITY_POLICY_FORCED;

	p->density_forced_x = nums[0];
	if(count>=2)
		p->density_forced_y = nums[1];
	else
		p->density_forced_y = nums[0];

	if(s[0]=='c') {
		// Density was given in dots/cm.
		p->pref_units = IW_PREF_UNITS_METRIC;
		p->density_forced_x *= 100.0; // Convert to dots/meter.
		p->density_forced_y *= 100.0;
	}
	else {
		// Density was given in dots/inch.
		p->pref_units = IW_PREF_UNITS_IMPERIAL;
		p->density_forced_x *= 100.0/2.54; // Convert to dots/meter.
		p->density_forced_y *= 100.0/2.54;
	}
}

static int iwcmd_option_density(struct params_struct *p, const char *s)
{
	int namelen;

	namelen=iwcmd_get_name_len(s);

	if(namelen==1 && (s[0]=='i' || s[0]=='c')) {
		iwcmd_option_forced_density(p,s);
	}
	else if(!strcmp(s,"auto")) {
		p->density_policy = IWCMD_DENSITY_POLICY_AUTO;
	}
	else if(!strcmp(s,"none")) {
		p->density_policy = IWCMD_DENSITY_POLICY_NONE;
	}
	else if(!strcmp(s,"keep")) {
		p->density_policy = IWCMD_DENSITY_POLICY_KEEP;
	}
	else if(!strcmp(s,"adjust")) {
		p->density_policy = IWCMD_DENSITY_POLICY_ADJUST;
	}
	else {
		iwcmd_error(p,"Invalid density \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
		return 0;
	}
	return 1;
}

static int iwcmd_decode_edge_policy(struct params_struct *p, const char *s)
{
	if(s[0]=='s') return IW_EDGE_POLICY_STANDARD;
	else if(s[0]=='r') return IW_EDGE_POLICY_REPLICATE;
	else if(s[0]=='t') return IW_EDGE_POLICY_TRANSPARENT;
	iwcmd_error(p,"Unknown edge policy\n");
	return -1;
}

static int iwcmd_option_gsf(struct params_struct *p, const char *s)
{
	int namelen;
	int count;

	namelen=iwcmd_get_name_len(s);

	if(!strcmp(s,"s")) p->grayscale_formula=IW_GSF_STANDARD;
	else if(!strcmp(s,"c")) p->grayscale_formula=IW_GSF_COMPATIBLE;
	else if(namelen==1 && !strncmp(s,"w",1)) {
		count = iw_parse_number_list(&s[1],3,p->grayscale_weight);
		if(count!=3) {
			iwcmd_error(p,"Invalid grayscale formula\n");
			return 0;
		}
		p->grayscale_formula=IW_GSF_WEIGHTED;
	}
	else if(namelen==1 && !strncmp(s,"v",1)) {
		count = iw_parse_number_list(&s[1],3,p->grayscale_weight);
		if(count<1) {
			iwcmd_error(p,"Invalid grayscale formula\n");
			return 0;
		}
		p->grayscale_formula=IW_GSF_ORDERBYVALUE;
	}
	else {
		iwcmd_error(p,"Unknown grayscale formula\n");
		return 0;
	}
	return 1;
}

static int iwcmd_option_intent(struct params_struct *p, const char *s)
{
	if(!strcmp(s,"p") || !strcmp(s,"perceptual")) {
		p->rendering_intent = IW_INTENT_PERCEPTUAL;
	}
	else if(!strcmp(s,"r") || !strcmp(s,"relative")) {
		p->rendering_intent = IW_INTENT_RELATIVE;
	}
	else if(!strcmp(s,"s") || !strcmp(s,"saturation")) {
		p->rendering_intent = IW_INTENT_SATURATION;
	}
	else if(!strcmp(s,"a") || !strcmp(s,"absolute")) {
		p->rendering_intent = IW_INTENT_ABSOLUTE;
	}
	else if(!strcmp(s,"default")) {
		p->rendering_intent = IW_INTENT_DEFAULT;
	}
	else if(!strcmp(s,"none")) {
		p->rendering_intent = IW_INTENT_NONE;
	}
	else {
		iwcmd_error(p,"Unknown intent \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
		return 0;
	}
	return 1;

}

static int iwcmd_option_reorient(struct params_struct *p, const char *s)
{
	if(s[0]>='0' && s[0]<='9') {
		p->reorient = (unsigned int)iw_parse_int(s);
		return 1;
	}

	if(!strcmp(s,"fliph")) p->reorient = IW_REORIENT_FLIP_H;
	else if(!strcmp(s,"flipv")) p->reorient = IW_REORIENT_FLIP_V;
	else if(!strcmp(s,"rotate90")) p->reorient = IW_REORIENT_ROTATE_90;
	else if(!strcmp(s,"rotate180")) p->reorient = IW_REORIENT_ROTATE_180;
	else if(!strcmp(s,"rotate270")) p->reorient = IW_REORIENT_ROTATE_270;
	else if(!strcmp(s,"transpose")) p->reorient = IW_REORIENT_TRANSPOSE;
	else if(!strcmp(s,"transverse")) p->reorient = IW_REORIENT_TRANSVERSE;
	else {
		iwcmd_error(p,"Unknown orientation transform \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
		return -1;
	}
	return 1;
}

static int iwcmd_decode_compression_name(struct params_struct *p, const char *s)
{
	if(!strcmp(s,"none")) return IW_COMPRESSION_NONE;
	else if(!strcmp(s,"zip")) return IW_COMPRESSION_ZIP;
	else if(!strcmp(s,"lzw")) return IW_COMPRESSION_LZW;
	else if(!strcmp(s,"jpeg")) return IW_COMPRESSION_JPEG;
	else if(!strcmp(s,"rle")) return IW_COMPRESSION_RLE;
	iwcmd_error(p,"Unknown compression \xe2\x80\x9c%s\xe2\x80\x9d\n",s);
	return -1;
}

static void usage_message(struct params_struct *p)
{
	iwcmd_message(p,
		"Usage: imagew [-w <width>] [-h <height>] [options] <in-file> <out-file>\n"
		"Options include -filter, -grayscale, -depth, -cc, -dither, -bkgd, -cs,\n"
		" -crop, -quiet, -version.\n"
		"See the ImageWorsener documentation for more information.\n"
	);
}

static void iwcmd_printversion(struct params_struct *p)
{
	char buf[200];
	int buflen;

	buflen = (int)(sizeof(buf)/sizeof(char));

	iwcmd_message(p,"ImageWorsener version %s\n",iw_get_version_string(NULL,buf,buflen));
	iwcmd_message(p,"%s\n",iw_get_copyright_string(NULL,buf,buflen));
	iwcmd_message(p,"Features: %d-bit",(int)(8*sizeof(void*)));
	iwcmd_message(p,"\n");

#if IW_SUPPORT_JPEG == 1
	iwcmd_message(p,"Uses libjpeg version %s\n",iw_get_libjpeg_version_string(buf,buflen));
#endif
#if IW_SUPPORT_PNG == 1
	iwcmd_message(p,"Uses libpng version %s\n",iw_get_libpng_version_string(buf,buflen));
#endif
#if IW_SUPPORT_ZLIB == 1
	// TODO: WebP might use zlib, even if IW_SUPPORT_ZLIB is not set.
	iwcmd_message(p,"Uses zlib version %s\n",iw_get_zlib_version_string(buf,buflen));
#endif
#if IW_SUPPORT_WEBP == 1
	iwcmd_message(p,"Uses libwebp encoder v%s",iw_get_libwebp_enc_version_string(buf,buflen));
	iwcmd_message(p,", decoder v%s\n",iw_get_libwebp_dec_version_string(buf,buflen));
#endif
}

enum iwcmd_param_types {
 PT_NONE=0, PT_WIDTH, PT_HEIGHT, PT_SIZE, PT_EXACTSIZE, PT_OPT, PT_SAMPLETYPE,
 PT_DEPTH, PT_DEPTHGRAY, PT_DEPTHALPHA, PT_DEPTHCC, PT_INPUTCS, PT_CS, PT_INTENT,
 PT_PRECISION, PT_RESIZETYPE, PT_RESIZETYPE_X, PT_RESIZETYPE_Y,
 PT_BLUR, PT_BLUR_X, PT_BLUR_Y,
 PT_DITHER, PT_DITHERCOLOR, PT_DITHERALPHA, PT_DITHERRED, PT_DITHERGREEN, PT_DITHERBLUE, PT_DITHERGRAY,
 PT_CC, PT_CCCOLOR, PT_CCALPHA, PT_CCRED, PT_CCGREEN, PT_CCBLUE, PT_CCGRAY,
 PT_BKGD, PT_BKGD2, PT_CHECKERSIZE, PT_CHECKERORG, PT_CROP, PT_REORIENT,
 PT_OFFSET_R_H, PT_OFFSET_G_H, PT_OFFSET_B_H, PT_OFFSET_R_V, PT_OFFSET_G_V,
 PT_OFFSET_B_V, PT_OFFSET_RB_H, PT_OFFSET_RB_V, PT_TRANSLATE, PT_IMAGESIZE,
 PT_COMPRESS, PT_JPEGQUALITY, PT_JPEGSAMPLING, PT_JPEGARITH, PT_BMPTRNS, PT_BMPVERSION,
 PT_WEBPQUALITY, PT_ZIPCMPRLEVEL, PT_INTERLACE, PT_COLORTYPE, PT_NEGATE,
 PT_RANDSEED, PT_INFMT, PT_OUTFMT, PT_EDGE_POLICY, PT_EDGE_POLICY_X,
 PT_EDGE_POLICY_Y, PT_GRAYSCALEFORMULA,
 PT_DENSITY_POLICY, PT_PAGETOREAD, PT_INCLUDESCREEN, PT_NOINCLUDESCREEN,
 PT_BESTFIT, PT_NOBESTFIT, PT_NORESIZE, PT_GRAYSCALE, PT_CONDGRAYSCALE, PT_NOGAMMA,
 PT_INTCLAMP, PT_NOCSLABEL, PT_NOOPT, PT_USEBKGDLABEL, PT_BKGDLABEL, PT_NOBKGDLABEL,
 PT_MSGSTOSTDOUT, PT_MSGSTOSTDERR,
 PT_QUIET, PT_NOWARN, PT_NOINFO, PT_VERSION, PT_HELP, PT_ENCODING
};

struct parsestate_struct {
	enum iwcmd_param_types param_type;
	int untagged_param_count;
	int printversion;
	int showhelp;
};

static void add_opt(struct params_struct *p, const char *name, const char *val);

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
		{"s",PT_SIZE,1},
		{"S",PT_EXACTSIZE,1},
		{"opt",PT_OPT,1},
		{"precision",PT_PRECISION,1},
		{"depth",PT_DEPTH,1},
		{"depthcc",PT_DEPTHCC,1},
		{"depthgray",PT_DEPTHGRAY,1},
		{"depthalpha",PT_DEPTHALPHA,1},
		{"sampletype",PT_SAMPLETYPE,1},
		{"inputcs",PT_INPUTCS,1},
		{"cs",PT_CS,1},
		{"filter",PT_RESIZETYPE,1},
		{"filterx",PT_RESIZETYPE_X,1},
		{"filtery",PT_RESIZETYPE_Y,1},
		{"blur",PT_BLUR,1},
		{"blurx",PT_BLUR_X,1},
		{"blury",PT_BLUR_Y,1},
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
		{"bkgdlabel",PT_BKGDLABEL,1},
		{"checkersize",PT_CHECKERSIZE,1},
		{"checkerorigin",PT_CHECKERORG,1},
		{"crop",PT_CROP,1},
		{"reorient",PT_REORIENT,1},
		{"offsetred",PT_OFFSET_R_H,1},
		{"offsetgreen",PT_OFFSET_G_H,1},
		{"offsetblue",PT_OFFSET_B_H,1},
		{"offsetrb",PT_OFFSET_RB_H,1},
		{"offsetvred",PT_OFFSET_R_V,1},
		{"offsetvgreen",PT_OFFSET_G_V,1},
		{"offsetvblue",PT_OFFSET_B_V,1},
		{"offsetvrb",PT_OFFSET_RB_V,1},
		{"translate",PT_TRANSLATE,1},
		{"imagesize",PT_IMAGESIZE,1},
		{"compress",PT_COMPRESS,1},
		{"colortype",PT_COLORTYPE,1},
		{"page",PT_PAGETOREAD,1},
		{"jpegquality",PT_JPEGQUALITY,1},
		{"jpegsampling",PT_JPEGSAMPLING,1},
		{"webpquality",PT_WEBPQUALITY,1},
		{"zipcmprlevel",PT_ZIPCMPRLEVEL,1},
		{"pngcmprlevel",PT_ZIPCMPRLEVEL,1},
		{"bmpversion",PT_BMPVERSION,1},
		{"randseed",PT_RANDSEED,1},
		{"infmt",PT_INFMT,1},
		{"outfmt",PT_OUTFMT,1},
		{"edge",PT_EDGE_POLICY,1},
		{"edgex",PT_EDGE_POLICY_X,1},
		{"edgey",PT_EDGE_POLICY_Y,1},
		{"density",PT_DENSITY_POLICY,1},
		{"gsf",PT_GRAYSCALEFORMULA,1},
		{"grayscaleformula",PT_GRAYSCALEFORMULA,1},
		{"intent",PT_INTENT,1},
		{"noopt",PT_NOOPT,1},
		{"encoding",PT_ENCODING,1},
		{"interlace",PT_INTERLACE,0},
		{"bestfit",PT_BESTFIT,0},
		{"nobestfit",PT_NOBESTFIT,0},
		{"noresize",PT_NORESIZE,0},
		{"grayscale",PT_GRAYSCALE,0},
		{"condgrayscale",PT_CONDGRAYSCALE,0},
		{"nogamma",PT_NOGAMMA,0},
		{"intclamp",PT_INTCLAMP,0},
		{"nocslabel",PT_NOCSLABEL,0},
		{"usebkgdlabel",PT_USEBKGDLABEL,0},
		{"nobkgdlabel",PT_NOBKGDLABEL,0},
		{"includescreen",PT_INCLUDESCREEN,0},
		{"noincludescreen",PT_NOINCLUDESCREEN,0},
		{"jpegarith",PT_JPEGARITH,0},
		{"bmptrns",PT_BMPTRNS,0},
		{"negate",PT_NEGATE,0},
		{"quiet",PT_QUIET,0},
		{"nowarn",PT_NOWARN,0},
		{"noinfo",PT_NOINFO,0},
		{"msgstostdout",PT_MSGSTOSTDOUT,0},
		{"msgstostderr",PT_MSGSTOSTDERR,0},
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
		p->bestfit_option=1;
		break;
	case PT_NOBESTFIT:
		p->bestfit_option=0;
		break;
	case PT_NORESIZE:
		p->noresize_flag=1;
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
	case PT_NOBKGDLABEL:
		p->no_bkgd_label=1;
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
	case PT_JPEGARITH:
		add_opt(p, "jpeg:arith", "");
		break;
	case PT_BMPTRNS:
		p->bmp_trns=1;
		p->compression=IW_COMPRESSION_RLE;
		break;
	case PT_NEGATE:
		p->negate=1;
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
	case PT_MSGSTOSTDOUT:
	case PT_MSGSTOSTDERR:
		// Already handled.
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
		*new_rel_d = iw_parse_number(&v[1]);
	}
	else {
		// This is a number of pixels.
		*new_d = iw_parse_int(v);
		return;
	}
}

static int iwcmd_decode_size(struct params_struct *p, const char *v)
{
	char *cpos;

	cpos = strchr(v,',');
	if(!cpos) {
		iwcmd_error(p,"Bad size option\n");
		return -1;
	}
	iwcmd_read_w_or_h(p,v,     &p->dst_width_req,&p->rel_width_flag,&p->rel_width);
	iwcmd_read_w_or_h(p,cpos+1,&p->dst_height_req,&p->rel_height_flag,&p->rel_height);
	return 1;
}

static int iwcmd_read_depth(struct params_struct *p, const char *v)
{
	if(strchr(v,',')) {
		iwcmd_parse_int_4(v,
			&p->channel_depth[IW_CHANNELTYPE_RED],
			&p->channel_depth[IW_CHANNELTYPE_GREEN],
			&p->channel_depth[IW_CHANNELTYPE_BLUE],
			&p->channel_depth[IW_CHANNELTYPE_ALPHA]);
	}
	else {
		p->depth=iw_parse_int(v);
	}
	return 1;
}

static int iwcmd_read_cc(struct params_struct *p, const char *v)
{
	if(strchr(v,',')) {
		iwcmd_parse_int_4(v,
			&p->color_count[IW_CHANNELTYPE_RED],
			&p->color_count[IW_CHANNELTYPE_GREEN],
			&p->color_count[IW_CHANNELTYPE_BLUE],
			&p->color_count[IW_CHANNELTYPE_ALPHA]);
	}
	else {
		p->color_count_all=iw_parse_int(v);
	}
	return 1;
}

static int iwcmd_decode_sampletype(struct params_struct *p, const char *v)
{
	if(v[0]=='u') {
		p->sample_type = IW_SAMPLETYPE_UINT;
	}
	else if(v[0]=='f') {
		p->sample_type = IW_SAMPLETYPE_FLOATINGPOINT;
	}
	else {
		iwcmd_error(p,"Unknown sample type \xe2\x80\x9c%s\xe2\x80\x9d\n",v);
		return -1;
	}
	return 1;
}

static void add_opt(struct params_struct *p, const char *name, const char *val)
{
	size_t nlen, vlen;

	if(p->options_count>=IWCMD_MAX_OPTIONS) return;

	nlen = strlen(name);
	p->options[p->options_count].name = malloc(nlen+1);
	memcpy(p->options[p->options_count].name, name, nlen+1);

	vlen = strlen(val);
	p->options[p->options_count].val = malloc(vlen+1);
	memcpy(p->options[p->options_count].val, val, vlen+1);

	p->options_count++;
}

// v is in "name=value" format.
static void add_opt_raw(struct params_struct *p, const char *v)
{
	char *eq;
	size_t nlen, vlen;

	if(p->options_count>=IWCMD_MAX_OPTIONS) return;
	eq = strchr(v, '=');
	if(eq==NULL) {
		// No "value". Add an option whose value is an empty string.
		nlen = strlen(v);
		p->options[p->options_count].name = malloc(nlen+1);
		memcpy(p->options[p->options_count].name, v, nlen+1);
		p->options[p->options_count].val = malloc(1);
		p->options[p->options_count].val[0] = '\0';
	}
	else {
		nlen = eq - v; // pointer arithmetic
		p->options[p->options_count].name = malloc(nlen+1);
		memcpy(p->options[p->options_count].name, v, nlen);
		p->options[p->options_count].name[nlen] = '\0';

		vlen = strlen(eq+1);
		p->options[p->options_count].val = malloc(vlen+1);
		memcpy(p->options[p->options_count].val, eq+1, vlen+1);
	}

	p->options_count++;
}

static int process_option_arg(struct params_struct *p, struct parsestate_struct *ps, const char *v)
{
	int ret;

	switch(ps->param_type) {
	case PT_WIDTH:
		iwcmd_read_w_or_h(p,v,&p->dst_width_req,&p->rel_width_flag,&p->rel_width);
		break;
	case PT_HEIGHT:
		iwcmd_read_w_or_h(p,v,&p->dst_height_req,&p->rel_height_flag,&p->rel_height);
		break;
	case PT_SIZE:
		ret=iwcmd_decode_size(p,v);
		p->bestfit=1;
		if(ret<0) return 0;
		break;
	case PT_EXACTSIZE:
		ret=iwcmd_decode_size(p,v);
		p->bestfit=0;
		if(ret<0) return 0;
		break;
	case PT_OPT:
		add_opt_raw(p, v);
		break;
	case PT_PRECISION:
		// This option is obsolete.
		break;
	case PT_DEPTH:
		ret=iwcmd_read_depth(p,v);
		if(ret<0) return 0;
		break;
	case PT_DEPTHCC:
		p->depthcc = iw_parse_int(v);
		break;
	case PT_DEPTHGRAY:
		p->channel_depth[IW_CHANNELTYPE_GRAY] = iw_parse_int(v);
		break;
	case PT_DEPTHALPHA:
		p->channel_depth[IW_CHANNELTYPE_ALPHA] = iw_parse_int(v);
		break;
	case PT_SAMPLETYPE:
		ret=iwcmd_decode_sampletype(p,v);
		if(ret<0) return 0;
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
		ret=iwcmd_decode_resizetype(p,v,&p->resize_alg_x);
		if(ret<0) return 0;
		p->resize_alg_y=p->resize_alg_x;
		break;
	case PT_RESIZETYPE_X:
		ret=iwcmd_decode_resizetype(p,v,&p->resize_alg_x);
		if(ret<0) return 0;
		break;
	case PT_RESIZETYPE_Y:
		ret=iwcmd_decode_resizetype(p,v,&p->resize_alg_y);
		if(ret<0) return 0;
		break;
	case PT_BLUR:
		ret=iwcmd_decode_blur_option(p,v,&p->resize_blur_x);
		if(ret<0) return 0;
		p->resize_blur_y=p->resize_blur_x;
		break;
	case PT_BLUR_X:
		ret=iwcmd_decode_blur_option(p,v,&p->resize_blur_x);
		if(ret<0) return 0;
		break;
	case PT_BLUR_Y:
		ret=iwcmd_decode_blur_option(p,v,&p->resize_blur_y);
		if(ret<0) return 0;
		break;
	case PT_DITHER:
		ret=iwcmd_decode_dithertype(p,v,&p->dither_all);
		if(ret<0) return 0;
		break;
	case PT_DITHERCOLOR:
		ret=iwcmd_decode_dithertype(p,v,&p->dither_nonalpha);
		if(ret<0) return 0;
		break;
	case PT_DITHERALPHA:
		ret=iwcmd_decode_dithertype(p,v,&p->dither[IW_CHANNELTYPE_ALPHA]);
		if(ret<0) return 0;
		break;
	case PT_DITHERRED:
		ret=iwcmd_decode_dithertype(p,v,&p->dither[IW_CHANNELTYPE_RED]);
		if(ret<0) return 0;
		break;
	case PT_DITHERGREEN:
		ret=iwcmd_decode_dithertype(p,v,&p->dither[IW_CHANNELTYPE_GREEN]);
		if(ret<0) return 0;
		break;
	case PT_DITHERBLUE:
		ret=iwcmd_decode_dithertype(p,v,&p->dither[IW_CHANNELTYPE_BLUE]);
		if(ret<0) return 0;
		break;
	case PT_DITHERGRAY:
		ret=iwcmd_decode_dithertype(p,v,&p->dither[IW_CHANNELTYPE_GRAY]);
		if(ret<0) return 0;
		break;
	case PT_CC:
		iwcmd_read_cc(p,v);
		break;
	case PT_CCCOLOR:
		p->color_count_nonalpha=iw_parse_int(v);
		break;
	case PT_CCRED:
		p->color_count[IW_CHANNELTYPE_RED]=iw_parse_int(v);
		break;
	case PT_CCGREEN:
		p->color_count[IW_CHANNELTYPE_GREEN]=iw_parse_int(v);
		break;
	case PT_CCBLUE:
		p->color_count[IW_CHANNELTYPE_BLUE]=iw_parse_int(v);
		break;
	case PT_CCGRAY:
		p->color_count[IW_CHANNELTYPE_GRAY]=iw_parse_int(v);
		break;
	case PT_CCALPHA:
		p->color_count[IW_CHANNELTYPE_ALPHA]=iw_parse_int(v);
		break;
	case PT_BKGD:
		p->apply_bkgd=1;
		iwcmd_option_bkgd(p,v);
		break;
	case PT_BKGDLABEL:
		p->bkgd_label_set=1;
		iwcmd_option_bkgd_label(p,v);
		break;
	case PT_CHECKERSIZE:
		p->bkgd_check_size=iw_parse_int(v);
		break;
	case PT_CHECKERORG:
		iwcmd_parse_int_pair(v,&p->bkgd_check_origin_x,&p->bkgd_check_origin_y);
		break;
	case PT_CROP:
		p->crop_x = p->crop_y = 0;
		p->crop_w = p->crop_h = -1;
		iwcmd_parse_int_4(v,&p->crop_x,&p->crop_y,&p->crop_w,&p->crop_h);
		p->use_crop=1;
		break;
	case PT_REORIENT:
		if(iwcmd_option_reorient(p,v) < 0)
			return 0;
		break;
	case PT_OFFSET_R_H:
		p->offset_h[IW_CHANNELTYPE_RED]=iw_parse_number(v);
		break;
	case PT_OFFSET_G_H:
		p->offset_h[IW_CHANNELTYPE_GREEN]=iw_parse_number(v);
		break;
	case PT_OFFSET_B_H:
		p->offset_h[IW_CHANNELTYPE_BLUE]=iw_parse_number(v);
		break;
	case PT_OFFSET_R_V:
		p->offset_v[IW_CHANNELTYPE_RED]=iw_parse_number(v);
		break;
	case PT_OFFSET_G_V:
		p->offset_v[IW_CHANNELTYPE_GREEN]=iw_parse_number(v);
		break;
	case PT_OFFSET_B_V:
		p->offset_v[IW_CHANNELTYPE_BLUE]=iw_parse_number(v);
		break;
	case PT_OFFSET_RB_H:
		// Shortcut for shifting red and blue in opposite directions.
		p->offset_h[IW_CHANNELTYPE_RED]=iw_parse_number(v);
		p->offset_h[IW_CHANNELTYPE_BLUE]= -p->offset_h[IW_CHANNELTYPE_RED];
		break;
	case PT_OFFSET_RB_V:
		// Shortcut for shifting red and blue vertically in opposite directions.
		p->offset_v[IW_CHANNELTYPE_RED]=iw_parse_number(v);
		p->offset_v[IW_CHANNELTYPE_BLUE]= -p->offset_v[IW_CHANNELTYPE_RED];
		break;
	case PT_TRANSLATE:
		if(v[0]=='s') {
			iwcmd_parse_dbl_pair(&v[1],&p->translate_x,&p->translate_y);
			p->translate_src_flag=1;
		}
		else {
			iwcmd_parse_dbl_pair(v,&p->translate_x,&p->translate_y);
		}
		p->translate_set = 1;
		break;
	case PT_IMAGESIZE:
		iwcmd_parse_dbl_pair(v,&p->imagesize_x,&p->imagesize_y);
		if(p->imagesize_x>0.0 && p->imagesize_y>0.0)
			p->imagesize_set = 1;
		break;
	case PT_COMPRESS:
		p->compression=iwcmd_decode_compression_name(p,v);
		if(p->compression<0) return 0;
		break;
	case PT_COLORTYPE:
		add_opt(p, "jpeg:colortype", v);
		break;
	case PT_PAGETOREAD:
		p->page_to_read = iw_parse_int(v);
		break;
	case PT_JPEGQUALITY:
		add_opt(p, "jpeg:quality", v);
		break;
	case PT_JPEGSAMPLING:
		add_opt(p, "jpeg:sampling", v);
		break;
	case PT_WEBPQUALITY:
		add_opt(p, "webp:quality", v);
		break;
	case PT_ZIPCMPRLEVEL:
		add_opt(p, "deflate:cmprlevel", v);
		break;
	case PT_BMPVERSION:
		add_opt(p, "bmp:version", v);
		break;
	case PT_RANDSEED:
		if(v[0]=='r') {
			p->randomize = 1;
		}
		else {
			p->random_seed=iw_parse_int(v);
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
		p->edge_policy_x = iwcmd_decode_edge_policy(p,v);
		if(p->edge_policy_x<0) return 0;
		p->edge_policy_y = p->edge_policy_x;
		break;
	case PT_EDGE_POLICY_X:
		p->edge_policy_x = iwcmd_decode_edge_policy(p,v);
		if(p->edge_policy_x<0) return 0;
		break;
	case PT_EDGE_POLICY_Y:
		p->edge_policy_y = iwcmd_decode_edge_policy(p,v);
		if(p->edge_policy_y<0) return 0;
		break;
	case PT_DENSITY_POLICY:
		if(!iwcmd_option_density(p,v)) {
			return 0;
		}
		break;
	case PT_GRAYSCALEFORMULA:
		if(!iwcmd_option_gsf(p,v)) {
			return 0;
		}
		p->grayscale=1;
		break;
	case PT_INTENT:
		if(!iwcmd_option_intent(p,v)) {
			return 0;
		}
		break;
	case PT_NOOPT:
		if(!iwcmd_parse_noopt(p,v))
			return 0;
		break;
	case PT_ENCODING:
		// Already handled.
		break;

	case PT_NONE:
		// This is presumably the input or output filename.

		if(ps->untagged_param_count==0) {
			p->input_uri.uri = v;
		}
		else if(ps->untagged_param_count==1) {
			p->output_uri.uri = v;
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
	if(!strcmp(v,"auto")) {
		p->output_encoding_req = IWCMD_ENCODING_AUTO;
	}
	else if(!strcmp(v,"ascii")) {
		p->output_encoding_req = IWCMD_ENCODING_ASCII;
	}
	else if(!strcmp(v,"utf8")) {
		p->output_encoding_req = IWCMD_ENCODING_UTF8;
	}
#ifdef IW_WINDOWS
	else if(!strcmp(v,"utf16")) {
		p->output_encoding_req = IWCMD_ENCODING_UTF16;
	}
#endif
	else {
		iwcmd_error(p,"Unknown encoding \xe2\x80\x9c%s\xe2\x80\x9d\n",v);
		return 0;
	}

	return 1;
}

// Figure out what output character encoding to use, and do other
// encoding-related setup as needed.
static int handle_encoding(struct params_struct *p, int argc, char* argv[])
{
	int i;
#ifdef IW_WINDOWS
	int is_windows_console = 0;
	BOOL b;
	DWORD consolemode=0;
#endif

	// Pre-scan the arguments for an "encoding" option.
	// This has to be done before we process the other options, so that we
	// can correctly print messages while parsing the other options.
	for(i=1;i<argc-1;i++) {
		if(!strcmp(argv[i],"-encoding")) {
			if(!read_encoding_option(p, argv[i+1])) {
				return 0;
			}
		}
	}

	p->output_encoding = p->output_encoding_req; // Initial default

#ifdef IW_WINDOWS
	b=GetConsoleMode(
		GetStdHandle(p->msgsdest==IWCMD_MSGS_TO_STDERR?STD_ERROR_HANDLE:STD_OUTPUT_HANDLE),
		&consolemode);
	// According to the documentation of WriteConsole(), if GetConsoleMode()
	// succeeds, we've got a real console.
	if(b) is_windows_console = 1;

	if(!is_windows_console) {
		// A real Windows console will not have excessive buffering, but
		// otherwise, disable buffering to make sure that printed text is
		// displayed immediately.
		// This is mostly for the benefit of mintty users.
		// It's overkill, but we print so little text that it shouldn't be a
		// problem.
		setvbuf(p->msgsfile,0,_IONBF,0);
	}
#endif

#ifdef IW_WINDOWS
	// If the user didn't set an encoding, and this is a Windows
	// build, use UTF-16 if we're writing to a real Windows console, or UTF-8
	// otherwise (e.g. if we're redirected to a file).
	// I think we could call _setmode(...,_O_U8TEXT) to do essentially the
	// same thing as this. We avoid that because it would mean we'd have to
	// convert our internal UTF-8 text to UTF-16, only to have it be
	// immediately converted back to UTF-8.
	if(p->output_encoding_req==IWCMD_ENCODING_AUTO) {
		if(is_windows_console) {
			p->output_encoding = IWCMD_ENCODING_UTF16;
		}
		else {
			p->output_encoding = IWCMD_ENCODING_UTF8;
		}
	}

#elif !defined(IW_NO_LOCALE)
	// For non-Windows builds with "locale" features enabled.
	setlocale(LC_CTYPE,"");
	// If the user didn't set an encoding, try to detect if we should use
	// UTF-8.
	if(p->output_encoding_req==IWCMD_ENCODING_AUTO) {
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
	if(p->output_encoding==IWCMD_ENCODING_UTF16) {
		// Tell the C library (e.g. fputws()) not to translate our UTF-16
		// text to an "ANSI" encoding, or anything else.
		_setmode(_fileno(p->msgsfile),_O_U16TEXT);
	}
#endif

	return 1;
}

// Our "schemes" consist of 2-32 lowercase letters, digits, and {+,-,.}.
static int uri_has_scheme(const char *s)
{
	int i;
	for(i=0;s[i];i++) {
		if(s[i]==':' && i>1) return 1;
		if(i>=32) return 0;
		if( (s[i]>='a' && s[i]<='z') || (s[i]>='0' && s[i]<='9') ||
			s[i]=='+' || s[i]=='-' || s[i]=='.')
		{
			;
		}
		else {
			return 0;
		}
	}
	return 0;
}

// Sets the other fields in u, based on u->uri.
static int parse_uri(struct params_struct *p, struct uri_struct *u, unsigned int is_output)
{
	u->filename = u->uri; // By default, point filename to the start of uri.

	if(uri_has_scheme(u->uri)) {
		if(!strncmp("file:",u->uri,5)) {
			u->scheme = IWCMD_SCHEME_FILE;
			u->filename = &u->uri[5];
		}
		else if(!strncmp("clip:",u->uri,5)) {
			u->scheme = IWCMD_SCHEME_CLIPBOARD;
			u->filename = "[clipboard]";
		}
		else if(!strncmp("stdin:",u->uri,6)) {
			u->scheme = IWCMD_SCHEME_STDIN;
			u->filename = "[stdin]";
		}
		else if(!strncmp("stdout:",u->uri,7)) {
			u->scheme = IWCMD_SCHEME_STDOUT;
			u->filename = "[stdout]";
		}
		else {
			iwcmd_error(p,"Don\xe2\x80\x99t understand \xe2\x80\x9c%s\xe2\x80\x9d; try \xe2\x80\x9c"
				"file:%s\xe2\x80\x9d.\n",u->uri,u->uri);
			return 0;
		}
	}
	else {
		// No scheme. Default to "file", unless name is "-".
		if(!strcmp(u->uri,"-")) {
			if(is_output) {
				u->scheme = IWCMD_SCHEME_STDOUT;
				u->filename = "[stdout]";
			}
			else {
				u->scheme = IWCMD_SCHEME_STDIN;
				u->filename = "[stdin]";
			}
		}
		else {
			u->scheme = IWCMD_SCHEME_FILE;
		}
	}
	return 1;
}

#define IWCMD_ACTION_EXIT_FAIL      0
#define IWCMD_ACTION_RUN            1
#define IWCMD_ACTION_USAGE_SUCCESS  2
#define IWCMD_ACTION_USAGE_FAIL     3
#define IWCMD_ACTION_SHOWVERSION    4

// Returns an IWCMD_ACTION code.
static int iwcmd_read_commandline(struct params_struct *p, int argc, char* argv[])
{
	struct parsestate_struct ps;
	int i;
	const char *optname;

	memset(&ps,0,sizeof(struct parsestate_struct));
	ps.param_type=PT_NONE;
	ps.untagged_param_count=0;
	ps.printversion=0;
	ps.showhelp=0;

	// Pre-scan command line to figure out where to print error messages.
	for(i=1;i<argc;i++) {
		if(!strcmp(argv[i],"-msgstostdout")) {
			p->msgsdest = IWCMD_MSGS_TO_STDOUT;
			p->msgsfile = stdout;
		}
		else if(!strcmp(argv[i],"-msgstostderr")) {
			p->msgsdest = IWCMD_MSGS_TO_STDERR;
			p->msgsfile = stderr;
		}
	}

	if(!handle_encoding(p,argc,argv)) {
		return IWCMD_ACTION_EXIT_FAIL;
	}

	for(i=1;i<argc;i++) {
		if(ps.param_type==PT_NONE && argv[i][0]=='-' && argv[i][1]!='\0') {
			optname = &argv[i][1];
			// If the second char is also a '-', ignore it.
			if(argv[i][1]=='-')
				optname = &argv[i][2];
			if(!process_option_name(p, &ps, optname)) {
				return IWCMD_ACTION_EXIT_FAIL;
			}
		}
		else {
			// Process a parameter of the previous option.

			if(!process_option_arg(p, &ps, argv[i])) {
				return IWCMD_ACTION_EXIT_FAIL;
			}

			ps.param_type = PT_NONE;
		}
	}

	if(ps.showhelp) {
		return IWCMD_ACTION_USAGE_SUCCESS;
	}

	if(ps.printversion) {
		return IWCMD_ACTION_SHOWVERSION;
	}

	if(ps.untagged_param_count!=2 || ps.param_type!=PT_NONE) {
		return IWCMD_ACTION_USAGE_FAIL;
	}

	if(!parse_uri(p,&p->input_uri,0)) {
		return IWCMD_ACTION_EXIT_FAIL;
	}
	if(!parse_uri(p,&p->output_uri,1)) {
		return IWCMD_ACTION_EXIT_FAIL;
	}

	// Make sure it doesn't matter where on the command line -bestfit/-nobestfit
	// were given.
	if(p->bestfit_option>=0) p->bestfit = p->bestfit_option;
	return IWCMD_ACTION_RUN;
}

static void init_params(struct params_struct *p)
{
	int k;
	memset(p,0,sizeof(struct params_struct));
	p->msgsdest = IWCMD_MSGS_TO_STDERR;
	p->msgsfile = stderr;
	p->dst_width_req = -1;
	p->dst_height_req = -1;
	p->sample_type = -1;
	p->edge_policy_x = -1;
	p->edge_policy_y = -1;
	p->density_policy = IWCMD_DENSITY_POLICY_AUTO;
	p->bkgd_check_size = 16;
	p->bestfit = 0;
	p->bestfit_option = -1;
	for(k=0;k<3;k++) {
		p->offset_h[k]=0.0;
		p->offset_v[k]=0.0;
	}
	p->translate_x=0.0; p->translate_y=0.0;
	p->infmt=IW_FORMAT_UNKNOWN;
	p->outfmt=IW_FORMAT_UNKNOWN;
	p->output_encoding=IWCMD_ENCODING_AUTO;
	p->output_encoding_req=IWCMD_ENCODING_AUTO;
	p->resize_blur_x.blur = 1.0;
	p->resize_blur_y.blur = 1.0;
	p->include_screen = -1;
	for(k=0;k<5;k++) p->dither[k].family = -1;
	p->dither_all.family = p->dither_nonalpha.family = -1;
	p->grayscale_formula = -1;
}

static int iwcmd_main(int argc, char* argv[])
{
	struct params_struct p;
	int ret;

	init_params(&p);

	ret = iwcmd_read_commandline(&p,argc,argv);

	if(ret==IWCMD_ACTION_RUN) {
		ret=iwcmd_run(&p);
		return ret?0:1;
	}
	else if(ret==IWCMD_ACTION_USAGE_SUCCESS) {
		usage_message(&p);
		return 0;
	}
	else if(ret==IWCMD_ACTION_USAGE_FAIL) {
		usage_message(&p);
		return 1;
	}
	else if(ret==IWCMD_ACTION_SHOWVERSION) {
		iwcmd_printversion(&p);
		return 0;
	}

	return 1;
}

#ifdef IW_WINDOWS

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
