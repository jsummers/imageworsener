// imagew-pnm.c
// Part of ImageWorsener, Copyright (c) 2013 by Jason Summers.
// For more information, see the readme.txt file.

#include "imagew-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define IW_INCLUDE_UTIL_FUNCTIONS
#include "imagew.h"

struct iwpnmcontext {
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
};

IW_IMPL(int) iw_read_pnm_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	iw_set_error(ctx,"PNM reading not implemented");
	return 0;
}

struct iwpnmwcontext {
	struct iw_iodescr *iodescr;
	struct iw_context *ctx;
	struct iw_image *img;
};


IW_IMPL(int) iw_write_pnm_file(struct iw_context *ctx, struct iw_iodescr *iodescr)
{
	iw_set_error(ctx,"PNM writing not implemented");
	return 0;
}
