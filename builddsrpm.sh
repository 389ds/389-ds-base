#!/bin/sh -vx
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

flavor=$1

rootdir=`pwd`

if [ ! $flavor ] ; then
	echo "Error: $0 <flavor>"
	echo "flavor is either redhat or fedora"
	echo "use redhat to create a redhat branded DS or use fedora"
	echo "for the fedora branded DS"
	exit 1
fi

mkdirs SOURCES BUILD SRPMS RPMS
cd SOURCES

# check out files from this CVS repo
CVSNAME=ldapserver
# change HEAD to a real static tag when available
CVSTAG=HEAD

echo "Checking out source code . . ."
cvs export -r $CVSTAG $CVSNAME > /dev/null 2>&1

echo "Creating the spec file $flavor-ds.spec . . ."
cd $CVSNAME ; make $flavor-ds.spec ; cp $flavor-ds.spec $rootdir ; cd $rootdir/SOURCES

echo "Get version from spec file . . ."
VERSION=`grep \^Version $rootdir/$flavor-ds.spec | awk '{print $2}'`

echo "Building tarball . . ."
mv $CVSNAME $flavor-ds-$VERSION
tar cfh - $flavor-ds-$VERSION | gzip > $flavor-ds-$VERSION.tar.gz
rm -rf $flavor-ds-$VERSION
cd $rootdir

macrosfile=/tmp/macros.$$
trap "rm -f $macrosfile" 0 1 2 3 15

echo "%_topdir	$rootdir" > $macrosfile

echo "Executing rpmbuild . . ."
rpmbuild --macros=$macrosfile -ba $flavor-ds.spec
echo "Finished doing rpmbuild $flavor-ds.spec"
