#!/bin/bash

VERSION=1.3.1
WINDOWS_DOCS='readme.txt technical.txt COPYING.txt'

if [ ! -f technical.txt ]
then
	echo "This script must be run from the main project directory."
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

make_windows_package win32 Release32
make_windows_package win64 Release64


