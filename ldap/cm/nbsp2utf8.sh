#!/bin/sh
#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Note that due to a bug in the Bourne shell on Digital UNIX 4.0, this
# script should always be invoked with one argument, e.g., like this:
#
#    ./nbsp2utf8.sh infile > outfile
#   
exec sed -e 's/&nbsp;/ /g' "$@"
