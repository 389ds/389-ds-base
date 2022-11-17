# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from lib389._entry import EntryAci
from lib389.utils import *
import pytest
import os

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


rawaci0 = ('(  targetattr =  "objectclass")( target = "ldap:///cn=retrieve '
           'certificate,cn=virtual operations,cn=etc,dc=ipa,dc=example"   )('
           '   version 3.0    ;    acl "permission:Retrieve   Certificates '
           'from the CA";allow (write) groupdn = "ldap:///cn=Retrieve Certif'
           'icates from the CA,cn=permissions,cn=pbac,dc=ipa,dc=example;)')
rawaci2 = ('(targetattr = "objectclass")(target = "ldap:///cn=retrieve certi'
           'ficate,cn=virtual operations,cn=etc,dc=ipa,dc=example" )(version'
           ' 3.0; acl "permission:Retrieve Certificates from the CA" ; allow '
           '(write) groupdn = "ldap:///cn=Retrieve Certificates from the CA,'
           'cn=permissions,cn=pbac,dc=ipa,dc=example;)')
rawaci3 = ('(targetfilter ="(ou=groups)")(targetattr ="uniqueMember || member")'
           '(version 3.0; acl "Allow test aci";allow (read, search, write)'
           '(userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" '
           'and userdn="ldap:///dc=example,dc=com??sub?(manager=uid='
           'wbrown,ou=managers,dc=example,dc=com) || ldap:///dc=examp'
           'le,dc=com??subrawaci?(manager=uid=tbrown,ou=managers,dc=exampl'
           'e,dc=com)" );)')
rawaci4 = ('(  targetattr =  "objectclass")( target = "ldap:///cn=retrieve '
           'certificate,cn=virtual operations,cn=etc,dc=ipa,dc=example"   )('
           '   version   3.0  ;  acl "permission:Retrieve Certificates '
           'from the CA";allow (write) groupdn =   "ldap:///cn=Retrieve Certif'
           'icates from the CA,cn=permissions,cn=pbac,dc=ipa,dc=example;)')
rawaci5 = ('(targetfilter ="(ou=groups)")( targetattr =    "uniqueMember  ||   member")'
           '(version 3.0; acl "Allow test aci";allow (read, search, write)'
           '(userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" '
           'and userdn="ldap:///dc=example,dc=com??sub?(manager=uid='
           'wbrown,ou=managers,dc=example,dc=com)   ||  ldap:///dc=examp'
           'le,dc=com??subrawaci?(manager=uid=tbrown,ou=managers,dc=exampl'
           'e,dc=com)" );)')

acis = [rawaci0, rawaci2, rawaci3, rawaci4, rawaci5]


def test_parse_aci():
    for rawaci in acis:
        # parse the aci - there should be no exceptions
        aci_obj = EntryAci(None, rawaci)
        aci_obj.getRawAci()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -vv %s" % CURRENT_FILE)

