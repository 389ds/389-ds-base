#!/bin/ksh -p
#ident "ldclt @(#)ldif02.ksh	1.1 01/04/11"

# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2006 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

# Create ldif file with incremental strings from file.
# Will create 150k entries
#

. env.ksh

echo "dn: $BaseDN
objectclass: organization
" > /tmp/ldif02.ldif

ldclt \
	-h $Host -p $Port \
	-D "$UserDN" -w "$UserPassword" \
	-q -v  -n1 \
	-e object=ofile \
	-e append,genldif=/tmp/ldif02.ldif \
	-e imagedir=../../../data/ldclt/images \
	-b $BaseDN -e rdn='cn:blob [C=INCRFROMFILE(../../../data/ldclt/names/Lastname.txt)] [D=RNDN(3;11;5)] [E=INCRNNOLOOP(1;150000;6)]'

