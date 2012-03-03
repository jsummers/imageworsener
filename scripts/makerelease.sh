#!/bin/bash

VERSION=0.9.8
WINDOWS_DOCS='readme.txt technical.txt COPYING.txt'

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


make_windows_package() {
	local platform=$1
	local from_folder=$2

	local output_filename="rel/imageworsener-${platform}-${VERSION}.zip"
	local input_binary="${from_folder}"/imagew.exe

	rm -f "${output_filename}"
	if [ -f "${input_binary}" ]
	then
		zip -9 -j "${output_filename}" "${input_binary}" ${WINDOWS_DOCS}
	else
		echo "Skipping binary package for platform \"${platform}\" (file \"${input_binary}\" missing)."
	fi
}

make_windows_package win32 Release
make_windows_package win64 Release64


# Make the source package.
rm -f rel/imageworsener-src-$VERSION.tar.gz

if [ ! -f Makefile ]
then
	echo "Please run ./configure first."
	exit 1
fi

make distcheck
ln imageworsener-${VERSION}.tar.gz rel/imageworsener-src-${VERSION}.tar.gz
