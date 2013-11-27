#!/bin/sh

function usage()
{
    echo "Adds patches to a specfile"
    echo ""
    echo "$0 <patchdir> <specfile>"
    echo ""
    echo "    patchdir - directory containing patches with"
    echo "               .patch extension."
    echo "    specfile - the specfile to patch."
    exit 1
}


if [ $# -ne 2 ]; then
    usage
fi

patchdir=$1
specfile=$2

# Validate our arguments.
if [ ! -d $1 ]; then
    echo "Patch directory $1 does not exist or is not a directory."
    exit 1
elif [ ! -f $2 ]; then
    echo "Specfile $2 does not exist or is not a file."
    exit 1
fi

# These keep track of our spec file substitutions.
i=1
prefix="Source0:"
prepprefix="%setup"

# Find all patches in the the patch directory.
# to the spec file.
patches=`ls ${patchdir}/*.patch 2>/dev/null`

# If no patches exist, just exit.
if [ -z "$patches" ]; then
    echo "No patches found in $patchdir."
    exit 0
fi

# Add the patches to the specfile.
for p in $patches; do
    p=`basename $p`
    echo "Adding patch to spec file - $p"
    sed -i -e "/${prefix}/a Patch${i}: ${p}" -e "/$prepprefix/a %patch${i} -p1" $specfile
    prefix="Patch${i}:"
    prepprefix="%patch${i}"
    i=$(($i+1))
done
