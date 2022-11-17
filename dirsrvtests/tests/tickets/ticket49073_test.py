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
from lib389.topologies import topology_m2

from lib389._constants import (PLUGIN_MEMBER_OF, DEFAULT_SUFFIX, SUFFIX, HOST_SUPPLIER_2,
                              PORT_SUPPLIER_2)

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]

DEBUGGING = os.getenv('DEBUGGING', False)
GROUP_DN = ("cn=group," + DEFAULT_SUFFIX)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _add_group_with_members(topology_m2):
    # Create group
    try:
        topology_m2.ms["supplier1"].add_s(Entry((GROUP_DN,
                                      {'objectclass': 'top groupofnames'.split(),
                                       'cn': 'group'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add group: error ' + e.message['desc'])
        assert False

    # Add members to the group - set timeout
    log.info('Adding members to the group...')
    for idx in range(1, 5):
        try:
            MEMBER_VAL = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology_m2.ms["supplier1"].modify_s(GROUP_DN,
                                      [(ldap.MOD_ADD,
                                        'member',
                                        MEMBER_VAL)])
        except ldap.LDAPError as e:
            log.fatal('Failed to update group: member (%s) - error: %s' %
                      (MEMBER_VAL, e.message['desc']))
            assert False


def _check_memberof(supplier, presence_flag):
    # Check that members have memberof attribute on M1
    for idx in range(1, 5):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            ent = supplier.getEntry(USER_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            if presence_flag:
                assert ent.hasAttr('memberof') and ent.getValue('memberof') == GROUP_DN
            else:
                assert not ent.hasAttr('memberof')
        except ldap.LDAPError as e:
            log.fatal('Failed to retrieve user (%s): error %s' % (USER_DN, e.message['desc']))
            assert False


def _check_entry_exist(supplier, dn):
    attempt = 0
    while attempt <= 10:
        try:
            dn
            ent = supplier.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            attempt = attempt + 1
            time.sleep(1)
        except ldap.LDAPError as e:
            log.fatal('Failed to retrieve user (%s): error %s' % (dn, e.message['desc']))
            assert False
    assert attempt != 10


def test_ticket49073(topology_m2):
    """Write your replication test here.

    To access each DirSrv instance use:  topology_m2.ms["supplier1"], topology_m2.ms["supplier2"],
        ..., topology_m2.hub1, ..., topology_m2.consumer1,...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """
    topology_m2.ms["supplier1"].plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_m2.ms["supplier1"].restart(timeout=10)
    topology_m2.ms["supplier2"].plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_m2.ms["supplier2"].restart(timeout=10)

    # Configure fractional to prevent total init to send memberof
    ents = topology_m2.ms["supplier1"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    log.info('update %s to add nsDS5ReplicatedAttributeListTotal' % ents[0].dn)
    topology_m2.ms["supplier1"].modify_s(ents[0].dn,
                              [(ldap.MOD_REPLACE,
                                'nsDS5ReplicatedAttributeListTotal',
                                '(objectclass=*) $ EXCLUDE '),
                               (ldap.MOD_REPLACE,
                                'nsDS5ReplicatedAttributeList',
                                '(objectclass=*) $ EXCLUDE memberOf')])
    topology_m2.ms["supplier1"].restart(timeout=10)

    #
    #  create some users and a group
    #
    log.info('create users and group...')
    for idx in range(1, 5):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology_m2.ms["supplier1"].add_s(Entry((USER_DN,
                                          {'objectclass': 'top extensibleObject'.split(),
                                           'uid': 'member%d' % (idx)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.message['desc']))
            assert False

    _check_entry_exist(topology_m2.ms["supplier2"], "uid=member4,%s" % (DEFAULT_SUFFIX))
    _add_group_with_members(topology_m2)
    _check_entry_exist(topology_m2.ms["supplier2"], GROUP_DN)

    # Check that for regular update memberof was on both side (because plugin is enabled both)
    time.sleep(5)
    _check_memberof(topology_m2.ms["supplier1"], True)
    _check_memberof(topology_m2.ms["supplier2"], True)

    # reinit with fractional definition
    ents = topology_m2.ms["supplier1"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier1"].agreement.init(SUFFIX, HOST_SUPPLIER_2, PORT_SUPPLIER_2)
    topology_m2.ms["supplier1"].waitForReplInit(ents[0].dn)

    # Check that for total update  memberof was on both side 
    # because memberof is NOT excluded from total init
    time.sleep(5)
    _check_memberof(topology_m2.ms["supplier1"], True)
    _check_memberof(topology_m2.ms["supplier2"], True)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
