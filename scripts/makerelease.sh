#!/bin/bash

VERSION=0.9.8

if [ ! -f technical.txt ]
then
	echo "This script must be run from the main project directory."
	exit 1
fi

if [ ! -f configure ]
then
	echo "Please run scripts/autogen.sh first."
	exit 1
fi

mkdir -p rel

# Make the 32-bit Windows binary package.
rm -f rel/imageworsener-win32-$VERSION.zip
zip -9 -j rel/imageworsener-win32-$VERSION.zip Release/imagew.exe readme.txt technical.txt COPYING.txt

# Make the 64-bit Windows binary package.
rm -f rel/imageworsener-win64-$VERSION.zip
zip -9 -j rel/imageworsener-win64-$VERSION.zip Release64/imagew.exe readme.txt technical.txt COPYING.txt

# Make the source package.
rm -f rel/imageworsener-src-$VERSION.tar.gz

if [ ! -f Makefile ]
then
	echo "Please run ./configure first."
	exit 1
fi

make distcheck
ln imageworsener-${VERSION}.tar.gz rel/imageworsener-src-${VERSION}.tar.gz
