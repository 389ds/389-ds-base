#!/bin/sh

# Set srcdir so VERSION.sh is able to
# determine the last git commit hash.
srcdir=`pwd`

# Source VERSION.sh to set the version
# and release environment variables.
source ./VERSION.sh

if [ "$1" = "version" ]; then
  echo $RPM_VERSION
elif [ "$1" = "release" ]; then
  echo $RPM_RELEASE
fi
