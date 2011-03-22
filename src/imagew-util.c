// imagew-util.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// This file is mainly for portability wrappers, and any code that
// may require unusual header files (malloc.h, strsafe.h).

#include "imagew-config.h"

#ifdef IW_WINDOWS
#include <tchar.h>
#endif

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


void iw_free(void *mem)
{
	if(mem) free(mem);
}

void *iw_malloc_lowlevel(size_t n)
{
	return malloc(n);
}

void *iw_realloc_lowlevel(void *m, size_t n)
{
	return realloc(m,n);
}

void *iw_strdup(const TCHAR *s)
{
	return _tcsdup(s);
}

void *iw_malloc(struct iw_context *ctx, size_t n)
{
	void *mem;

	if(n>ctx->max_malloc) {
		iw_seterror(ctx,_T("Out of memory"));
		return NULL;
	}
	mem = iw_malloc_lowlevel(n);
	if(!mem) {
		iw_seterror(ctx,_T("Out of memory"));
		return NULL;
	}
	return mem;
}

void *iw_realloc(struct iw_context *ctx, void *m, size_t n)
{
	void *mem;

	if(n>ctx->max_malloc) {
		iw_seterror(ctx,_T("Out of memory"));
		return NULL;
	}
	mem = iw_realloc_lowlevel(m,n);
	if(!mem) {
		iw_seterror(ctx,_T("Out of memory"));
		return NULL;
	}
	return mem;
}

// Allocate a large block of memory, presumably for image data.
// Use this if integer overflow is a possibility when multiplying
// two factors together.
void *iw_malloc_large(struct iw_context *ctx, size_t n1, size_t n2)
{
	if(n1 > ctx->max_malloc/n2) {
		iw_seterror(ctx,_T("Image too large to process"));
		return NULL;
	}
	return iw_malloc(ctx,n1*n2);
}

void iw_vsnprintf(TCHAR *buf, size_t buflen, const TCHAR *fmt, va_list ap)
{
#ifdef IW_WINDOWS
	StringCchVPrintf(buf,buflen,fmt,ap);
#else
	vsnprintf(buf,buflen,fmt,ap);
	buf[buflen-1]='\0';
#endif
}

void iw_snprintf(TCHAR *buf, size_t buflen, const TCHAR *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iw_vsnprintf(buf,buflen,fmt,ap);
	va_end(ap);
}

void iw_util_set_random_seed(int s)
{
	srand((unsigned int)s);
}

void iw_util_randomize(void)
{
	srand((unsigned int)time(NULL));
}
