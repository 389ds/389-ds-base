#!/bin/sh -vx
# --- BEGIN COPYRIGHT BLOCK ---
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
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

echo "Executing rpmbuild . . ."
rpmbuild --define "_topdir $rootdir" -ba $flavor-ds.spec
echo "Finished doing rpmbuild $flavor-ds.spec"
