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

#ifndef IW_WINDOWS
#define _T(x)     x
#define _tprintf  printf
#define _tcslen   strlen
#define _tcscmp   strcmp
#define _tcsicmp  strcasecmp
#define _tcsncmp  strncmp
#define _tstoi    atoi
#define _tstof    atof
#define _tfopen   fopen
#define _tcschr   strchr
#define _tcsrchr  strrchr
#define _tcsdup   strdup
#define _tmain    main
#endif

#endif // IMAGEW_CONFIG_H
