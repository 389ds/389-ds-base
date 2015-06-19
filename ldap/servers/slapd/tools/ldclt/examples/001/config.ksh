#!/bin/ksh -p
#ident "ldclt @(#)config.ksh	1.2 01/04/11"

# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2006 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

. env.ksh

ldapmodify -D"$RootDN" -w"$RootPasswd" -h$Host -p$Port <<-EOD
dn: $UserDN
changetype: add
objectclass: person
sn: test user
userpassword: $UserPassword

dn: $BaseDN
changetype: modify
add : aci
aci: (targetattr = "*") (version 3.0;acl "test user";allow (all)(userdn = "ldap:///$UserDN");)

EOD
