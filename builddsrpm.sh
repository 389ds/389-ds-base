#!/bin/sh -v
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# --- END COPYRIGHT BLOCK ---

mkdirs() {
	for d in "$@" ; do
		if [ -d $d ]; then
			mv $d $d.deleted
			rm -rf $d.deleted &
		fi
		mkdir -p $d
	done
}

if [ ! -f $HOME/.rpmmacros ]; then
	echo "This script assumes you want to build as a non-root"
	echo "user and in a non-default place (e.g. your home dir)"
	echo "You must have a $HOME/.rpmmacros file that redefines"
	echo "_topdir e.g."
	echo "%_topdir	/home/rmeggins/ds71"
	echo "Please create that file with the above contents and"
	echo "rerun this script."
	exit 1
fi

NAME=ldapserver
VERSION=7.1
# change HEAD to a real static tag when available
CVSTAG=HEAD

mkdirs SOURCES BUILD SRPMS RPMS
cd SOURCES
rm -rf $NAME-$VERSION $NAME-$VERSION.tar.gz
echo "Checking out source code . . ."
cvs export -r $CVSTAG -d $NAME-$VERSION $NAME > /dev/null 2>&1
echo "Building tarball . . ."
tar cf - $NAME-$VERSION | gzip > $NAME-$VERSION.tar.gz
rm -rf $NAME-$VERSION
cd ..
echo "Executing rpmbuild . . ."
rpmbuild -ba $NAME.spec
echo "Finished doing rpmbuild $NAME.spec"
