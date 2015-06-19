#ident "ldclt @(#)env.ksh	1.2 01/04/11"

# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2006 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

Host=localhost
Port=1389

BaseDN=o=test.com

RootDN="cn=directory manager"
RootPasswd=secret12

UserRDN="cn=test"
UserDN="$UserRDN,$BaseDN"
UserPassword=testpassword
