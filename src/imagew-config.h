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

#ifdef IW_WINDOWS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501 // 0x0501=WinXP, 0x0600=Vista
#endif
#endif

#endif // IMAGEW_CONFIG_H
