#!/bin/bash

DATE=`date +%Y%m%d`
# use a real tag name here
VERSION=1.3.0.5
PKGNAME=389-ds-base
TAG=${TAG:-$PKGNAME-$VERSION}
URL="http://git.fedorahosted.org/git/?p=389/ds.git;a=snapshot;h=$TAG;sf=tgz"
SRCNAME=$PKGNAME-$VERSION

wget -O $SRCNAME.tar.gz "$URL"

echo convert tgz format to tar.bz2 format

gunzip $PKGNAME-$VERSION.tar.gz
bzip2 $PKGNAME-$VERSION.tar
