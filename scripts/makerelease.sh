#!/bin/bash

VERSION=1.3.1

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

# Make the source package.
rm -f rel/imageworsener-$VERSION.tar.gz

if [ ! -f Makefile ]
then
	echo "Please run ./configure first."
	exit 1
fi

make distcheck
mv imageworsener-${VERSION}.tar.gz rel/
