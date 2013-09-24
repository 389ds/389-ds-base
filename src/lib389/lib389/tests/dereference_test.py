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
import ldap
import pytest
import logging

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope='module')
def topology(request):
    standalone = DirSrv(verbose=False)
    standalone.log.debug('Instance allocated')
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
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


@pytest.fixture(scope='module')
def user(topology):
    """Create user entries"""

    for i in range(0, 2):
        uentry = Entry('uid=test%s,%s' % (i, DEFAULT_SUFFIX))
        uentry.setValues('objectclass', 'top', 'extensibleobject')
        uentry.setValues('uid', 'test')
        topology.standalone.add_s(uentry)


@pytest.fixture(scope='module')
def group(topology):
    """Create a group entry"""

    gentry = Entry('cn=testgroup,%s' % DEFAULT_SUFFIX)
    gentry.setValues('objectclass', 'top', 'extensibleobject')
    gentry.setValues('cn', 'testgroup')
    for i in range(0, 2):
        gentry.setValues('uniqueMember', 'uid=test%s,%s' % (i, DEFAULT_SUFFIX))
    topology.standalone.add_s(gentry)


def test_dereference(topology, user, group):
    """Test dereference search argument formating
    Check, that result is correct
    """

    log.info('Check, that our deref Control Value is correctly formatted')
    with pytest.raises(ldap.UNAVAILABLE_CRITICAL_EXTENSION):
        result, control_response = topology.standalone.dereference(
            'uniqueMember:dn,uid;uniqueMember:dn,uid',
            filterstr='(cn=testgroup)')

    result, control_response = topology.standalone.dereference(
        'uniqueMember:cn,uid,objectclass', filterstr='(cn=testgroup)')

    log.info('Check, that the dereference search result is correct')
    assert result[0][2][0].entry == [{'derefVal':
                                      'uid=test1,dc=example,dc=com',
                                      'derefAttr': 'uniqueMember',
                                      'attrVals': [{'vals':
                                                    ['top',
                                                     'extensibleobject'],
                                                    'type': 'objectclass'},
                                                   {'vals': ['test', 'test1'],
                                                    'type': 'uid'}]}]


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main('-s -v %s' % CURRENT_FILE)
