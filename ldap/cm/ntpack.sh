#!/bin/sh
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

#######################################################################
#
#   Script to pack ldapsdk on NT (uses rtpatch)
#
#   Mahesh Purswani (3/97)
#######################################################################

# Path to the rtpatch installation
RTPATCH=/rtpatch

outfile=outname
reldir=.
olddir=/nonedir
while [ $# -gt 0 ]
do
  case "$1" in
    -o)
	shift
	outfile=$1;;
    -r)
	shift
	reldir=$1;;
     *)
	echo ""
	echo "Usage: $0 [-o outfile] [-r sourcedir]"
	echo ""
	exit 1;;
  esac
  shift
done

if [ ! -d "$olddir" ] ; then
  echo "Making empty old directory $olddir"
  mkdir $olddir
fi

rolddir=`echo $olddir | sed 's#/#\\\\#g'`
rreldir=`echo $reldir | sed 's#/#\\\\#g'`

cat <<EOF > pack.txt
OLDDIR $rolddir /F
NEWDIR $rreldir /F
OUTPUT $outfile
FILE *.*
PATCHFILE
LONGNAMES
PARTIAL
SUBDIRSEARCH
NOPATHSEARCH
IGNOREMISSING
EOF

cat <<EOF > bind.txt
[General]
Platform=Console32
DirectoryPrompt=Please specify install directory (default is current directory): 
IncludeDLL=1
PatchFile=$outfile.rtp
OutputFile=$outfile.exe
EOF

# Run rtpatch
$RTPATCH/pbld-nt @pack.txt
$RTPATCH/pbind bind.txt

echo "Packed release dir = $reldir"
echo "To outfile = $outfile"
