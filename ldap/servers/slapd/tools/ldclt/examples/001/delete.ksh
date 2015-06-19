#!/bin/ksh -p
#ident "ldclt @(#)delete.ksh	1.2 01/04/11"

# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2006 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

# This script will delete random entries in the database.

. env.ksh

ldclt \
	-h $Host -p $Port \
	-D "$UserDN" -w "$UserPassword" \
	-e delete,random \
	-e imagedir=../../../data/ldclt/images \
	-r0 -R1000000 \
	-I32 \
	-n50 \
	-f cn=mrXXXXXXXXX -b o=data,$BaseDN \
	-v -q

