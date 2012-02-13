#!/bin/bash

if [ ! -f technical.txt ]
then
	echo "This script must be run from the main project directory."
	 exit 1
fi

if [ "$1" = "clean" ]
then
 rm -rf autom4te.cache m4
 rm -f autoscan.log aclocal.m4 depcomp missing install-sh 
 rm -f src/Makefile.am src/Makefile.in Makefile.am Makefile.in
 rm -f src/Makefile Makefile configure config.h.in
 rm -f config.status stamp-h1 config.log config.h src/.dirstamp
 rm -f AUTHORS ChangeLog COPYING INSTALL NEWS README
 rm -f config.guess config.sub ltmain.sh libtool compile
 rm -f src/*.o src/*.lo src/*.la libimageworsener.la
 rm -rf src/.deps .libs src/.libs
 rm -f imagew imagew.exe
 exit
fi

# Stop if something fails.
set -e

#echo "Writing Makefile.am"
#cat << EOF > Makefile.am
#SUBDIRS=src
#EOF

echo "Writing Makefile.am"
cat << EOF > Makefile.am
lib_LTLIBRARIES=libimageworsener.la
libimageworsener_la_SOURCES=src/imagew-api.c src/imagew-gif.c \
 src/imagew-miff.c src/imagew-resize.c src/imagew-webp.c src/imagew-bmp.c \
 src/imagew-jpeg.c src/imagew-opt.c src/imagew-tiff.c src/imagew-zlib.c \
 src/imagew-main.c src/imagew-png.c src/imagew-util.c
libimageworsener_la_LDFLAGS=-release 0.9.7
bin_PROGRAMS=imagew
imagew_SOURCES=src/imagew-cmd.c
imagew_LDADD=libimageworsener.la
include_HEADERS=src/imagew.h
EOF

# From Makefile.am and configure.ac, create aclocal.m4
echo "Running aclocal"
mkdir -p m4
aclocal -I m4

echo "Running libtoolize"
# force=replace files; copy=don't symlink
libtoolize --force --copy

# Create config.h.in
echo "Running autoheader"
autoheader

# Create dummy files that autoconf thinks are important
touch COPYING
touch AUTHORS
touch ChangeLog
touch NEWS
touch README
#touch INSTALL

# Create configure
echo "Running autoconf"
autoconf

# Create Makefile.in
echo "Running automake"
automake -ac

