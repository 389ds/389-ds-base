#!/bin/ksh -p
#ident "ldclt @(#)add_incr.ksh	1.2 01/04/11"

# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2006 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

# Sequential add of entries.
# This script will add entries from 0 to 1000000 and exit. All the
# threads will share the same counter, i.e. each entry will be added
# only one time.

. env.ksh

ldclt \
	-h $Host -p $Port \
	-D "$UserDN" -w "$UserPassword" \
	-e add,person,incr,commoncounter,noloop \
	-e imagedir=../../../data/ldclt/images \
	-r0 -R1000000 \
	-I68 \
	-n5 \
	-f cn=mrXXXXXXXXX -b o=data,$BaseDN \
	-v -q
