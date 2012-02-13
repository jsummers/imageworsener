// imagew-config.h
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

#ifndef IMAGEW_CONFIG_H
#define IMAGEW_CONFIG_H

// This code is duplicated in imagew.h.
#if defined(_WIN32) && !defined(__GNUC__)
#define IW_WINDOWS
#endif

#if defined(_WIN64) || defined(__x86_64__)
#define IW_64BIT
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef IW_WINDOWS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501 // 0x0501=WinXP, 0x0600=Vista
#endif
#endif

#define IW_IMPL(x) x

#ifdef HAVE_CONFIG_H
// Using autoconf

#if defined(HAVE_LIBZ) && defined(HAVE_ZLIB_H)
#define IW_SUPPORT_ZLIB 1
#else
#define IW_SUPPORT_ZLIB 0
#endif

#if defined(HAVE_LIBPNG) && defined(HAVE_PNG_H)
#define IW_SUPPORT_PNG 1
#else
#define IW_SUPPORT_PNG 0
#endif

#if defined(HAVE_LIBJPEG) && defined(HAVE_JPEGLIB_H)
#define IW_SUPPORT_JPEG 1
#else
#define IW_SUPPORT_JPEG 0
#endif

#if defined(HAVE_LIBWEBP) && defined(HAVE_WEBP_DECODE_H)
#define IW_SUPPORT_WEBP 1
#else
#define IW_SUPPORT_WEBP 0
#endif

#else
// Not using autoconf

#ifndef IW_SUPPORT_ZLIB
#define IW_SUPPORT_ZLIB 1
#endif

#ifndef IW_SUPPORT_PNG
#if IW_SUPPORT_ZLIB == 1
#define IW_SUPPORT_PNG 1
#else
#define IW_SUPPORT_PNG 0
#endif
#endif

#ifndef IW_SUPPORT_JPEG
#define IW_SUPPORT_JPEG 1
#endif
#ifndef IW_SUPPORT_WEBP
#define IW_SUPPORT_WEBP 1
#endif

#endif

#ifndef IW_WEBP_SUPPORT_TRANSPARENCY
#define IW_WEBP_SUPPORT_TRANSPARENCY 1
#endif

#endif // IMAGEW_CONFIG_H
