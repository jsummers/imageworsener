lib_LTLIBRARIES=libimageworsener.la
libimageworsener_la_SOURCES=src/imagew-main.c \
 src/imagew-api.c \
 src/imagew-resize.c \
 src/imagew-opt.c \
 src/imagew-bmp.c \
 src/imagew-gif.c \
 src/imagew-miff.c \
 src/imagew-tiff.c \
 src/imagew-zlib.c \
 src/imagew-png.c \
 src/imagew-jpeg.c \
 src/imagew-webp.c \
 src/imagew-util.c
libimageworsener_la_LDFLAGS=-release 0.9.8
bin_PROGRAMS=imagew
imagew_SOURCES=src/imagew-cmd.c
imagew_LDADD=libimageworsener.la
include_HEADERS=src/imagew.h

EXTRA_DIST = readme.txt technical.txt COPYING.txt changelog.txt \
 CMakeLists.txt \
 scripts/autogen.sh scripts/Makefile scripts/makerelease.sh \
 imagew2008.sln imagew2008.vcproj libimagew2008.vcproj \
 src/imagew-config.h src/imagew-internals.h \
 src/imagew.rc src/resources/imagew.ico \
 tests/runtest \
 tests/srcimg \
 tests/expected

