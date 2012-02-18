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


# Make a list of all the files that go in the source distribution.
#
# Before autotools, this was easy. Just use git-archive.
#git archive --format=tar "--prefix=imageworsener-${VERSION}/" master | gzip > rel/imageworsener-src-$VERSION.tar.gz


(
 # I need a list of all the exportable file, but I don't know how to get git
 # to give me such a list.
 # (Other than using git-archive to create an archive, reading it to get a list
 # of the files, then deleting it.)
 # For now, use grep to ignore files I know I don't want.
 git ls-tree -r --name-only --full-tree master | grep -v '\.git'

 # Include autotools generated files.
 ls m4/* AUTHORS COPYING ChangeLog INSTALL Makefile.am Makefile.in \
NEWS README aclocal.m4 compile config.guess config.h.in config.sub \
configure depcomp install-sh ltmain.sh missing

) > rel/filelist

tar --transform 's,^,imageworsener-'${VERSION}'/,' -cvz --files-from rel/filelist -f rel/imageworsener-src-$VERSION.tar.gz


