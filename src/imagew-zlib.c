// imagew-zlib.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#if IW_SUPPORT_ZLIB == 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iw_zlib_context {
	struct iw_context *ctx;
	z_stream strm;
};

// Create a iw_zlib_context object, and initialize it for decompression.
// Returns NULL if anything failed.
// On success, iw_zlib_inflate_end() must eventually be called to free it.
static struct iw_zlib_context* iw_zlib_inflate_init(struct iw_context *ctx)
{
	struct iw_zlib_context *zctx;
	int ret;

	zctx = iw_malloc(ctx,sizeof(struct iw_zlib_context));
	if(!zctx) return NULL;

	memset(zctx,0,sizeof(struct iw_zlib_context));
	zctx->ctx = ctx;

	ret = inflateInit(&zctx->strm);
	if(ret!=Z_OK) {
		iw_free(zctx);
		return NULL;
	}

	return zctx;
}

static void iw_zlib_inflate_end(struct iw_zlib_context *zctx)
{
	if(!zctx) return;
	inflateEnd(&zctx->strm);
	iw_free(zctx);
}

// Decompress a self-contained buffer of data.
// This function requires you to know exactly how many bytes the input should
// decompress to.
// Returns 1 on success, 0 otherwise.
static int iw_zlib_inflate_item(struct iw_zlib_context *zctx,
	iw_byte *src, size_t srclen,
	iw_byte *dst, size_t dstlen)
{
	zctx->strm.next_in = src;
	zctx->strm.avail_in = (uInt)srclen;
	zctx->strm.next_out = dst;
	zctx->strm.avail_out = (uInt)dstlen;

	inflate(&zctx->strm,Z_SYNC_FLUSH);

	// The decompressor should have filled the entire output buffer, leaving
	// 0 bytes available.
	if(zctx->strm.avail_out!=0) {
		if(zctx->strm.msg)
			iw_set_errorf(zctx->ctx,"zlib reports read error: %s",zctx->strm.msg);
		else
			iw_set_error(zctx->ctx,"zlib decompression failed");
		return 0;
	}

	return 1;
}

IW_IMPL(char*) iw_get_zlib_version_string(char *s, int s_len)
{
	const char *zv;
	zv = zlibVersion();
	iw_snprintf(s,s_len,"%s",zv);
	return s;
}

IW_IMPL(void) iw_enable_zlib(struct iw_context *ctx)
{
	static struct iw_zlib_module zlib_module = {
		iw_zlib_inflate_init,
		iw_zlib_inflate_end,
		iw_zlib_inflate_item
	};

	iw_set_zlib_module(ctx,&zlib_module);
}

#endif // IW_SUPPORT_ZLIB
