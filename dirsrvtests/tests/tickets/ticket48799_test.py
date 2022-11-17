# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m1c1

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def _add_custom_schema(server):
    attr_value = b"( 10.0.9.2342.19200300.100.1.1 NAME 'customManager' EQUALITY distinguishedNameMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.12 X-ORIGIN 'user defined' )"
    mod = [(ldap.MOD_ADD, 'attributeTypes', attr_value)]
    server.modify_s('cn=schema', mod)

    oc_value = b"( 1.3.6.1.4.1.4843.2.1 NAME 'customPerson' SUP inetorgperson STRUCTURAL MAY (customManager) X-ORIGIN 'user defined' )"
    mod = [(ldap.MOD_ADD, 'objectclasses', oc_value)]
    server.modify_s('cn=schema', mod)


def _create_user(server):
    server.add_s(Entry((
        "uid=testuser,ou=People,%s" % DEFAULT_SUFFIX,
        {
            'objectClass': "top account posixaccount".split(),
            'uid': 'testuser',
            'gecos': 'Test User',
            'cn': 'testuser',
            'homedirectory': '/home/testuser',
            'passwordexpirationtime': '20160710184141Z',
            'userpassword': '!',
            'uidnumber': '1111212',
            'gidnumber': '1111212',
            'loginshell': '/bin/bash'
        }
    )))


def _modify_user(server):
    mod = [
        (ldap.MOD_ADD, 'objectClass', [b'customPerson']),
        (ldap.MOD_ADD, 'sn', [b'User']),
        (ldap.MOD_ADD, 'customManager', [b'cn=manager']),
    ]
    server.modify("uid=testuser,ou=People,%s" % DEFAULT_SUFFIX, mod)


def test_ticket48799(topology_m1c1):
    """Write your replication testcase here.

    To access each DirSrv instance use:  topology_m1c1.ms["supplier1"], topology_m1c1.ms["supplier1"]2,
        ..., topology_m1c1.hub1, ..., topology_m1c1.cs["consumer1"],...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """

    # Add the new schema element.
    _add_custom_schema(topology_m1c1.ms["supplier1"])
    _add_custom_schema(topology_m1c1.cs["consumer1"])

    # Add a new user on the supplier.
    _create_user(topology_m1c1.ms["supplier1"])
    # Modify the user on the supplier.
    _modify_user(topology_m1c1.ms["supplier1"])

    # We need to wait for replication here.
    time.sleep(15)

    # Now compare the supplier vs consumer, and see if the objectClass was dropped.

    supplier_entry = topology_m1c1.ms["supplier1"].search_s("uid=testuser,ou=People,%s" % DEFAULT_SUFFIX, ldap.SCOPE_BASE,
                                                        '(objectclass=*)', ['objectClass'])
    consumer_entry = topology_m1c1.cs["consumer1"].search_s("uid=testuser,ou=People,%s" % DEFAULT_SUFFIX,
                                                            ldap.SCOPE_BASE, '(objectclass=*)', ['objectClass'])

    assert (supplier_entry == consumer_entry)

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
