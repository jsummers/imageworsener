
dnl TODO: Figure out the actual minimum version that will work.
AC_PREREQ([2.63])
AC_INIT([imageworsener], [1.3.1])
dnl AC_CONFIG_SRCDIR is just any unique file: a sanity check
AC_CONFIG_SRCDIR([src/imagew.h])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

LT_INIT()

dnl Checks for programs.

AC_PROG_CC
dnl AM_PROG_CC_C_O is required by the "subdir-options" setting.
AM_PROG_CC_C_O
AM_PROG_LIBTOOL

AC_CONFIG_FILES([Makefile])
dnl "foreign" removes the need for some files (NEWS, AUTHORS, ...).
dnl "subdir-options" puts object files in subdirectories, instead of in
dnl the current directory.
AM_INIT_AUTOMAKE([foreign subdir-objects])

dnl Checks for libraries.

dnl ---------- zlib ----------
AC_ARG_WITH([zlib],
    [AS_HELP_STRING([--without-zlib], [disable ZLIB support])],
    [with_zlib=$withval],
    [with_zlib='yes'])
AC_ARG_WITH(zlib-include-dir,
 AS_HELP_STRING([--with-zlib-include-dir=DIR],
  [location of zlib headers]),,)
AC_ARG_WITH(zlib-lib-dir,
 AS_HELP_STRING([--with-zlib-lib-dir=DIR],
  [location of zlib library binary]),,)

if test "$with_zlib" != 'no'; then
 if test "x$with_zlib_include_dir" != "x"; then
dnl TODO: Is modifying CPPFLAGS and LDFLAGS the best practice?
  CPPFLAGS="-I$with_zlib_include_dir $CPPFLAGS"
 fi
 AC_CHECK_HEADERS([zlib.h])
 if test "x$with_zlib_lib_dir" != "x"; then
  LDFLAGS="-L$with_zlib_lib_dir $LDFLAGS"
 fi
 AC_CHECK_LIB(z,deflate)
fi

dnl ---------- libpng ----------
AC_ARG_WITH([png],
    [AS_HELP_STRING([--without-png], [disable PNG support])],
    [with_png=$withval],
    [with_png='yes'])
AC_ARG_WITH(png-include-dir,
 AS_HELP_STRING([--with-png-include-dir=DIR],
  [location of libpng headers]),,)
AC_ARG_WITH(png-lib-dir,
 AS_HELP_STRING([--with-png-lib-dir=DIR],
  [location of libpng library binary]),,)

if test "$with_png" != 'no'; then
 if test "x$with_png_include_dir" != "x"; then
  CPPFLAGS="-I$with_png_include_dir $CPPFLAGS"
 fi
 AC_CHECK_HEADERS([png.h])
 if test "x$with_png_lib_dir" != "x"; then
  LDFLAGS="-L$with_png_lib_dir $LDFLAGS"
 fi
 AC_CHECK_LIB(png,png_read_png)
fi

dnl ---------- libjpeg ----------
AC_ARG_WITH([jpeg],
    [AS_HELP_STRING([--without-jpeg], [disable JPEG support])],
    [with_jpeg=$withval],
    [with_jpeg='yes'])
AC_ARG_WITH(jpeg-include-dir,
 AS_HELP_STRING([--with-jpeg-include-dir=DIR],
  [location of JPEG headers]),,)
AC_ARG_WITH(jpeg-lib-dir,
 AS_HELP_STRING([--with-jpeg-lib-dir=DIR],
  [location of JPEG library binary]),,)

if test "$with_jpeg" != 'no'; then
 if test "x$with_jpeg_include_dir" != "x"; then
  CPPFLAGS="-I$with_jpeg_include_dir $CPPFLAGS"
 fi
 AC_CHECK_HEADERS([jpeglib.h])
 if test "x$with_jpeg_lib_dir" != "x"; then
  LDFLAGS="-L$with_jpeg_lib_dir $LDFLAGS"
 fi
 AC_CHECK_LIB(jpeg,jpeg_read_scanlines)
fi

dnl ---------- libwebp ----------
AC_ARG_WITH([webp],
 [AS_HELP_STRING([--without-webp], [disable WebP support])],
 [with_webp=$withval],
 [with_webp='yes'])
AC_ARG_WITH(webp-include-dir,
 AS_HELP_STRING([--with-webp-include-dir=DIR],
  [location of libwebp headers]),,)
AC_ARG_WITH(webp-lib-dir,
 AS_HELP_STRING([--with-webp-lib-dir=DIR],
  [location of libwebp library binary]),,)

if test "$with_webp" != 'no'; then
 if test "x$with_webp_include_dir" != "x"; then
  CPPFLAGS="-I$with_webp_include_dir $CPPFLAGS"
 fi
 AC_CHECK_HEADERS([webp/decode.h])
 if test "x$with_webp_lib_dir" != "x"; then
  LDFLAGS="-L$with_webp_lib_dir $LDFLAGS"
 fi
 AC_CHECK_LIB(webp,WebPGetDecoderVersion)
fi

dnl ---------------------------

AC_OUTPUT
