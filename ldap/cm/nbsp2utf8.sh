#!/bin/sh
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
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
