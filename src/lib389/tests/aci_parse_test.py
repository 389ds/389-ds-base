# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from lib389._constants import *
from lib389 import DirSrv, Entry
import pytest

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'aciparseds'
# INSTANCE_PREFIX   = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    standalone = DirSrv(verbose=False)
    standalone.log.debug("Instance allocated")
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            # SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    standalone.allocate(args)
    if standalone.exists():
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


@pytest.fixture(scope="module")
def complex_aci(topology):
    ACI_TARGET = ('(targetfilter ="(ou=groups)")(targetattr ="uniqueMember '
                  '|| member")')
    ACI_ALLOW = ('(version 3.0; acl "Allow test aci";allow (read, search, '
                 'write)')
    ACI_SUBJECT = ('(userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" '
                   'and userdn="ldap:///dc=example,dc=com??sub?(manager=uid='
                   'wbrown,ou=managers,dc=example,dc=com) || ldap:///dc=examp'
                   'le,dc=com??sub?(manager=uid=tbrown,ou=managers,dc=exampl'
                   'e,dc=com)" );)')
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    gentry = Entry('cn=testgroup,%s' % DEFAULT_SUFFIX)
    gentry.setValues('objectclass', 'top', 'extensibleobject')
    gentry.setValues('cn', 'testgroup')
    gentry.setValues('aci', ACI_BODY)

    topology.standalone.add_s(gentry)

    return ACI_BODY


def test_aci(topology, complex_aci):
    """Checks content of previously added aci"""

    acis = topology.standalone.aci.list('cn=testgroup,%s' % DEFAULT_SUFFIX)
    assert len(acis) == 1
    aci = acis[0]
    assert aci.acidata == {
        'allow': [{'values': ['read', 'search', 'write']}],
        'target': [], 'targetattr': [{'values': ['uniqueMember', 'member'],
                                      'equal': True}],
        'targattrfilters': [],
        'deny': [],
        'acl': [{'values': ['Allow test aci']}],
        'deny_raw_bindrules': [],
        'targetattrfilters': [],
        'allow_raw_bindrules': [{'values': [(
            'userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" and'
            ' userdn="ldap:///dc=example,dc=com??sub?(manager=uid=wbrown,'
            'ou=managers,dc=example,dc=com) || ldap:///dc=example,dc=com'
            '??sub?(manager=uid=tbrown,ou=managers,dc=example,dc=com)" ')]}],
        'targetfilter': [{'values': ['(ou=groups)'], 'equal': True}],
        'targetscope': [],
        'version 3.0;': [],
        'rawaci': complex_aci
    }
    assert aci.getRawAci() == complex_aci


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -vv %s" % CURRENT_FILE)
