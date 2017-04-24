lib_LTLIBRARIES=libimageworsener.la
libimageworsener_la_SOURCES=src/imagew-main.c \
 src/imagew-api.c \
 src/imagew-resize.c \
 src/imagew-opt.c \
 src/imagew-allfmts.c \
 src/imagew-bmp.c \
 src/imagew-gif.c \
 src/imagew-miff.c \
 src/imagew-tiff.c \
 src/imagew-zlib.c \
 src/imagew-png.c \
 src/imagew-jpeg.c \
 src/imagew-webp.c \
 src/imagew-pnm.c \
 src/imagew-util.c
libimageworsener_la_LIBADD=-lm
libimageworsener_la_LDFLAGS=-release 1.3.1
bin_PROGRAMS=imagew
imagew_SOURCES=src/imagew-cmd.c
imagew_LDADD=libimageworsener.la
include_HEADERS=src/imagew.h

EXTRA_DIST = readme.txt technical.txt COPYING.txt changelog.txt \
 scripts/autogen.sh scripts/Makefile scripts/makerelease.sh \
 scripts/imagew2008.sln scripts/imagew2008.vcproj scripts/libimagew2008.vcproj \
 src/imagew-config.h src/imagew-internals.h \
 src/imagew.rc src/resources/imagew.ico \
 tests/runtest \
 tests/srcimg \
 tests/expected

ACLOCAL_AMFLAGS = -I m4

check-local:  # Run test suite with support for VPATH builds (e.g. make distcheck)
	mkdir -p tests
	test -e tests/expected || ln -s "$(abs_top_srcdir)"/tests/expected tests/expected
	test -e tests/srcimg || ln -s "$(abs_top_srcdir)"/tests/srcimg tests/srcimg
	cd tests && "$(abs_top_srcdir)"/tests/runtest
	rm tests/expected 2>/dev/null ; true
	rm tests/srcimg 2>/dev/null ; true

distclean-local:  # Clean up after so distcleancheck is happy
	rm -Rf tests/actual/
