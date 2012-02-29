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
 rm -f src/Makefile.in Makefile.in
 rm -f src/Makefile Makefile configure config.h.in
 rm -f config.status stamp-h1 config.log config.h src/.dirstamp
 rm -f COPYING INSTALL
 rm -f config.guess config.sub ltmain.sh libtool compile
 rm -f src/*.o src/*.lo src/*.la libimageworsener.la
 rm -rf src/.deps .libs src/.libs
 rm -f imagew imagew.exe
 exit
fi

# Stop if something fails.
set -e

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
echo "Please refer to the file COPYING.txt" > COPYING
echo "Please refer to the 'Building from source' section of the file technical.txt" > INSTALL

# Create configure
echo "Running autoconf"
autoconf

# Create Makefile.in
echo "Running automake"
automake -ac

