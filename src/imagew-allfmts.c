// imagew-allfmts.c
// Part of ImageWorsener, Copyright (c) 2011-2012 by Jason Summers.
// For more information, see the readme.txt file.

// This file is for functions that call functions in multiple optional
// modules.
//
// Nothing critical should be in this file, and nothing else in the library
// should call functions in this file.
//
// A possible reason to avoid referencing these functions in your code is
// that, if libimageworsener is statically linked, you (probably) won't have
// to link to third party libraries that you're not using.

#include "imagew-config.h"
#include "imagew.h"

IW_IMPL(int) iw_read_file_by_fmt(struct iw_context *ctx,
	struct iw_iodescr *readdescr, int fmt)
{
	int retval=0;
	int supported=0;

#if IW_SUPPORT_ZLIB
	// If any function in this file is used, there's no benefit to making
	// the caller explicitly call iw_enable_zlib(). So we'll just do it
	// automatically.
	iw_enable_zlib(ctx);
#endif

	switch(fmt) {

	case IW_FORMAT_PNG:
#if IW_SUPPORT_PNG == 1
		supported=1;
		retval = iw_read_png_file(ctx,readdescr);
#endif
		break;

	case IW_FORMAT_JPEG:
#if IW_SUPPORT_JPEG == 1
		supported=1;
		retval = iw_read_jpeg_file(ctx,readdescr);
#endif
		break;

	case IW_FORMAT_WEBP:
#if IW_SUPPORT_WEBP == 1
		supported=1;
		retval = iw_read_webp_file(ctx,readdescr);
#endif
		break;

	case IW_FORMAT_MIFF:
		supported=1;
		retval = iw_read_miff_file(ctx,readdescr);
		break;

	case IW_FORMAT_GIF:
		supported=1;
		retval = iw_read_gif_file(ctx,readdescr);
		break;

	case IW_FORMAT_BMP:
		supported=1;
		retval = iw_read_bmp_file(ctx,readdescr);
		break;

	case IW_FORMAT_TIFF:
		break;

	case IW_FORMAT_PNM:
		supported=1;
		retval = iw_read_pnm_file(ctx,readdescr);
		break;

	case IW_FORMAT_PAM:
		supported=1;
		retval = iw_read_pam_file(ctx,readdescr);
		break;

	default:
		iw_set_errorf(ctx,"Attempt to read unknown file format (%d)",fmt);
		goto done;
	}

	if(!supported) {
		const char *s;
		s = iw_get_fmt_name(fmt);
		if(!s) s="(unknown)";
		iw_set_errorf(ctx,"Reading %s files is not supported",s);
	}
done:
	return retval;
}


IW_IMPL(int) iw_write_file_by_fmt(struct iw_context *ctx,
	struct iw_iodescr *writedescr, int fmt)
{
	int retval=0;
	int supported=0;

#if IW_SUPPORT_ZLIB
	iw_enable_zlib(ctx);
#endif

	iw_set_value(ctx,IW_VAL_OUTPUT_FORMAT,fmt);

	switch(fmt) {

	case IW_FORMAT_PNG:
#if IW_SUPPORT_PNG == 1
		supported=1;
		retval = iw_write_png_file(ctx,writedescr);
#endif
		break;

	case IW_FORMAT_JPEG:
#if IW_SUPPORT_JPEG == 1
		supported=1;
		retval = iw_write_jpeg_file(ctx,writedescr);
#endif
		break;

	case IW_FORMAT_WEBP:
#if IW_SUPPORT_WEBP == 1
		supported=1;
		retval = iw_write_webp_file(ctx,writedescr);
#endif
		break;

	case IW_FORMAT_MIFF:
		supported=1;
		retval = iw_write_miff_file(ctx,writedescr);
		break;

	case IW_FORMAT_TIFF:
		supported=1;
		retval = iw_write_tiff_file(ctx,writedescr);
		break;

	case IW_FORMAT_BMP:
		supported=1;
		retval = iw_write_bmp_file(ctx,writedescr);
		break;

	case IW_FORMAT_PNM:
	case IW_FORMAT_PPM:
	case IW_FORMAT_PGM:
	case IW_FORMAT_PBM:
		supported=1;
		retval = iw_write_pnm_file(ctx,writedescr);
		break;

	case IW_FORMAT_PAM:
		supported=1;
		retval = iw_write_pam_file(ctx,writedescr);
		break;

	case IW_FORMAT_GIF:
		break;

	default:
		iw_set_errorf(ctx,"Attempt to write unknown file format (%d)",fmt);
		goto done;
	}

	if(!supported) {
		const char *s;
		s = iw_get_fmt_name(fmt);
		if(!s) s="(unknown)";
		iw_set_errorf(ctx,"Writing %s files is not supported",s);
	}

done:
	if(!retval) {
		// Just in case the error hasn't been handled yet:
		iw_set_error(ctx,"Error writing file");
	}
	return retval;
}
