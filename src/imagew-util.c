// imagew-util.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// This file is mainly for portability wrappers, and any code that
// may require unusual header files (malloc.h, strsafe.h).

#include "imagew-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef IW_WINDOWS
#include <malloc.h>
#endif
#include <stdarg.h>
#include <time.h>

#include "imagew-internals.h"
#ifdef IW_WINDOWS
#include <strsafe.h>
#endif


IW_IMPL(void) iw_free(void *mem)
{
	if(mem) free(mem);
}

IW_IMPL(void*) iw_malloc_lowlevel(size_t n)
{
	return malloc(n);
}

IW_IMPL(void*) iw_realloc_lowlevel(void *m, size_t n)
{
	return realloc(m,n);
}

IW_IMPL(void*) iw_strdup(const char *s)
{
#ifdef IW_WINDOWS
	return _strdup(s);
#else
	return strdup(s);
#endif
}

IW_IMPL(void*) iw_malloc(struct iw_context *ctx, size_t n)
{
	void *mem;

	if(n>ctx->max_malloc) {
		iw_set_error(ctx,"Out of memory");
		return NULL;
	}
	mem = iw_malloc_lowlevel(n);
	if(!mem) {
		iw_set_error(ctx,"Out of memory");
		return NULL;
	}
	return mem;
}

IW_IMPL(void*) iw_realloc(struct iw_context *ctx, void *m, size_t n)
{
	void *mem;

	if(n>ctx->max_malloc) {
		iw_set_error(ctx,"Out of memory");
		return NULL;
	}
	mem = iw_realloc_lowlevel(m,n);
	if(!mem) {
		iw_set_error(ctx,"Out of memory");
		return NULL;
	}
	return mem;
}

// Allocate a large block of memory, presumably for image data.
// Use this if integer overflow is a possibility when multiplying
// two factors together.
IW_IMPL(void*) iw_malloc_large(struct iw_context *ctx, size_t n1, size_t n2)
{
	if(n1 > ctx->max_malloc/n2) {
		iw_set_error(ctx,"Image too large to process");
		return NULL;
	}
	return iw_malloc(ctx,n1*n2);
}

IW_IMPL(void) iw_strlcpy(char *dst, const char *src, size_t dstlen)
{
	size_t n;
	n = strlen(src);
	if(n>dstlen-1) n=dstlen-1;
	memcpy(dst,src,n);
	dst[n]='\0';
}

IW_IMPL(void) iw_vsnprintf(char *buf, size_t buflen, const char *fmt, va_list ap)
{
#ifdef IW_WINDOWS
	StringCchVPrintfA(buf,buflen,fmt,ap);
#else
	vsnprintf(buf,buflen,fmt,ap);
	buf[buflen-1]='\0';
#endif
}

IW_IMPL(void) iw_snprintf(char *buf, size_t buflen, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iw_vsnprintf(buf,buflen,fmt,ap);
	va_end(ap);
}

IW_IMPL(int) iw_stricmp(const char *s1, const char *s2)
{
#ifdef IW_WINDOWS
	return _stricmp(s1,s2);
#else
	return strcasecmp(s1,s2);
#endif
}

////////////////////////////////////////////
// A simple carry-with-multiply PRNG.

struct iw_prng {
	iw_uint32 multiply;
	iw_uint32 carry;
};

struct iw_prng *iwpvt_prng_create(void)
{
	struct iw_prng *prng;
	prng = (struct iw_prng*)iw_malloc_lowlevel(sizeof(struct iw_prng));
	if(!prng) return NULL;
	memset(prng,0,sizeof(struct iw_prng));
	return prng;
}

void iwpvt_prng_destroy(struct iw_prng *prng)
{
	if(prng) iw_free((void*)prng);
}

void iwpvt_prng_set_random_seed(struct iw_prng *prng, int s)
{
	prng->multiply = ((iw_uint32)0x03333333) + s;
	prng->carry    = ((iw_uint32)0x05555555) + s;
}

iw_uint32 iwpvt_prng_rand(struct iw_prng *prng)
{
	iw_uint64 x;
	x = ((iw_uint64)0xfff0bf23) * prng->multiply + prng->carry;
	prng->carry = (iw_uint32)(x>>32);
	prng->multiply = 0xffffffff - (0xffffffff & x);
	return prng->multiply;
}

////////////////////////////////////////////

int iwpvt_util_randomize(struct iw_prng *prng)
{
	int s;
	s = (int)time(NULL);
	iwpvt_prng_set_random_seed(prng, s);
	return s;
}

IW_IMPL(int) iw_file_to_memory(struct iw_context *ctx, struct iw_iodescr *iodescr,
  void **pmem, iw_int64 *psize)
{
	int ret;
	size_t bytesread;

	*pmem=NULL;
	*psize=0;

	if(!iodescr->getfilesize_fn) return 0;

	ret = (*iodescr->getfilesize_fn)(ctx,iodescr,psize);
	if(!ret) return 0;

	*pmem = iw_malloc(ctx,(size_t)*psize);

	ret = (*iodescr->read_fn)(ctx,iodescr,*pmem,(size_t)*psize,&bytesread);
	if(!ret) return 0;
	if((iw_int64)bytesread != *psize) return 0;
	return 1;
}

struct iw_utf8cvt_struct {
	char *dst;
	int dstlen;
	int dp;
};

static void utf8cvt_emitoctet(struct iw_utf8cvt_struct *s, unsigned char c)
{
	if(s->dp > s->dstlen-2) return;
	s->dst[s->dp] = (char)c;
	s->dp++;
}

// Map Unicode characters to ASCII substitutions.
// Not used for codepoints <=127.
static void utf8cvt_emitunichar(struct iw_utf8cvt_struct *s, unsigned int c)
{
	int i;
	int pos;
	struct charmap_struct {
		unsigned int code;
		const char *s;
	};
	static const struct charmap_struct chartable[] = {
	 {0, "?" }, // Default character
	 {0xa9, "(c)" },
	 {0xd7, "x" },
	 {0x2192, "->" },
	 {0x2018, "'" },
	 {0x2019, "'" },
	 {0x201c, "\"" },
	 {0x201d, "\"" },
	 {0, NULL}
	};

	// Try to find the codepoint in the table.
	pos = 0;
	for(i=1;chartable[i].code;i++) {
		if(c==chartable[i].code) {
			pos=i;
			break;
		}
	}

	// Write out the ASCII translation of this code.
	for(i=0;chartable[pos].s[i];i++) {
		utf8cvt_emitoctet(s,(unsigned char)chartable[pos].s[i]);
	}
}

// This UTF-8 converter is intended for use with the UTF-8 strings that are
// hardcoded into this program. It won't work very well with
// user-controlled strings.
IW_IMPL(void) iw_utf8_to_ascii(const char *src, char *dst, int dstlen)
{
	struct iw_utf8cvt_struct s;
	int sp;
	unsigned char c;
	unsigned int pending_char;
	int bytes_expected;

	s.dst = dst;
	s.dstlen = dstlen;
	s.dp = 0;
	pending_char=0;
	bytes_expected=0;

	for(sp=0;src[sp];sp++) {
		c = (unsigned char)src[sp];
		if(c<128) { // Only byte of a 1-byte sequence
			utf8cvt_emitoctet(&s,c);
		}
		else if(c<0xc0) { // Continuation byte
			pending_char = (pending_char<<6)|(c&0x3f);
			bytes_expected--;
			if(bytes_expected<1) {
				utf8cvt_emitunichar(&s,pending_char);
			}
		}
		else if(c<0xe0) { // 1st byte of a 2-byte sequence
			pending_char = c&0x1f;
			bytes_expected=1;
		}
		else if(c<0xf0) { // 1st byte of a 3-byte sequence
			pending_char = c&0x0f;
			bytes_expected=2;
		}
		else if(c<0xf8) { // 1st byte of a 4-byte sequence
			pending_char = c&0x07;
			bytes_expected=3;
		}
	}
	dst[s.dp] = '\0';
}

// Returns 0 if running on a big-endian system, 1 for little-endian.
IW_IMPL(int) iw_get_host_endianness(void)
{
	// !!! Portability warning: Using a union in this way may be nonportable,
	// and/or may violate strict-aliasing rules.
	union en_union {
		iw_byte c[4];
		int ii;
	} volatile en;

	// Test the host's endianness.
	en.c[0]=0;
	en.ii = 1;
	if(en.c[0]!=0) {
		return 1;
	}
	return 0;
}

IW_IMPL(int) iw_detect_fmt_from_filename(const char *fn)
{
	char *s;
	s=strrchr(fn,'.');
	if(!s) return IW_FORMAT_UNKNOWN;
	s++;

	if(!iw_stricmp(s,"png")) return IW_FORMAT_PNG;
	if(!iw_stricmp(s,"jpg")) return IW_FORMAT_JPEG;
	if(!iw_stricmp(s,"jpeg")) return IW_FORMAT_JPEG;
	if(!iw_stricmp(s,"bmp")) return IW_FORMAT_BMP;
	if(!iw_stricmp(s,"tif")) return IW_FORMAT_TIFF;
	if(!iw_stricmp(s,"tiff")) return IW_FORMAT_TIFF;
	if(!iw_stricmp(s,"miff")) return IW_FORMAT_MIFF;
	if(!iw_stricmp(s,"webp")) return IW_FORMAT_WEBP;
	if(!iw_stricmp(s,"gif")) return IW_FORMAT_GIF;
	return IW_FORMAT_UNKNOWN;
}

IW_IMPL(int) iw_detect_fmt_of_file(const iw_byte *buf, size_t n)
{
	int fmt = IW_FORMAT_UNKNOWN;

	if(n<2) return fmt;

	if(buf[0]==0x89 && buf[1]==0x50) {
		fmt=IW_FORMAT_PNG;
	}
	else if(n>=3 && buf[0]=='G' && buf[1]=='I' && buf[2]=='F') {
		fmt=IW_FORMAT_GIF;
	}
	else if(buf[0]==0xff && buf[1]==0xd8) {
		fmt=IW_FORMAT_JPEG;
	}
	else if(buf[0]==0x42 && buf[1]==0x4d) {
		fmt=IW_FORMAT_BMP;
	}
	else if((buf[0]==0x49 || buf[0]==0x4d) && buf[1]==buf[0]) {
		fmt=IW_FORMAT_TIFF;
	}
	else if(buf[0]==0x69 && buf[1]==0x64) {
		fmt=IW_FORMAT_MIFF;
	}
	else if(n>=12 && buf[0]==0x52 && buf[1]==0x49 && buf[2]==0x46 && buf[3]==0x46 &&
	   buf[8]==0x57 && buf[9]==0x45 && buf[10]==0x42 && buf[11]==0x50)
	{
		fmt=IW_FORMAT_WEBP;
	}

	return fmt;
}

IW_IMPL(unsigned int) iw_get_profile_by_fmt(int fmt)
{
	unsigned int p;

	switch(fmt) {

	case IW_FORMAT_PNG:
		p = IW_PROFILE_TRANSPARENCY | IW_PROFILE_GRAYSCALE | IW_PROFILE_PALETTETRNS |
		    IW_PROFILE_GRAY1 | IW_PROFILE_GRAY2 | IW_PROFILE_GRAY4 | IW_PROFILE_16BPS |
		    IW_PROFILE_BINARYTRNS | IW_PROFILE_PAL1 | IW_PROFILE_PAL2 | IW_PROFILE_PAL4 |
		    IW_PROFILE_PAL8;
		break;

	case IW_FORMAT_BMP:
		p = IW_PROFILE_ALWAYSSRGB | IW_PROFILE_PAL1 | IW_PROFILE_PAL4 | IW_PROFILE_PAL8;
		break;

	case IW_FORMAT_JPEG:
		p = IW_PROFILE_GRAYSCALE | IW_PROFILE_ALWAYSSRGB;
		break;

	case IW_FORMAT_TIFF:
		p = IW_PROFILE_TRANSPARENCY | IW_PROFILE_GRAYSCALE | IW_PROFILE_GRAY1 |
		    IW_PROFILE_GRAY4 | IW_PROFILE_16BPS | IW_PROFILE_PAL4 | IW_PROFILE_PAL8;
		break;

	case IW_FORMAT_MIFF:
		p = IW_PROFILE_TRANSPARENCY | IW_PROFILE_GRAYSCALE | IW_PROFILE_ALWAYSLINEAR |
		    IW_PROFILE_HDRI;
		break;

	case IW_FORMAT_WEBP:
		p = IW_PROFILE_ALWAYSSRGB;
#if IW_WEBP_SUPPORT_TRANSPARENCY
		p |= IW_PROFILE_TRANSPARENCY;
#endif
		break;

	default:
		p = IW_PROFILE_ALWAYSSRGB;
	}

	return p;
}
