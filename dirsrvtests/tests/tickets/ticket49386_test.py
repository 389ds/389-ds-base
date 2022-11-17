# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
import time
from lib389.utils import *
from lib389.topologies import topology_st as topo
from lib389._constants import *
from lib389.config import Config
from lib389 import Entry

pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.7'), reason="Not implemented")]

USER_CN='user_'
GROUP_CN='group_'

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def add_user(server, no, desc='dummy', sleep=True):
    cn = '%s%d' % (USER_CN, no)
    dn = 'cn=%s,ou=people,%s' % (cn, SUFFIX)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'inetuser'],
                             'sn': ['_%s' % cn],
                             'description': [desc]})))
    if sleep:
        time.sleep(2)

def add_group(server, nr, sleep=True):
    cn = '%s%d' % (GROUP_CN, nr)
    dn = 'cn=%s,ou=groups,%s' % (cn, SUFFIX)
    server.add_s(Entry((dn, {'objectclass': ['top', 'groupofnames'],
                             'description': 'group %d' % nr})))
    if sleep:
        time.sleep(2)
        
def update_member(server, member_dn, group_dn, op, sleep=True):
    mod = [(op, 'member', ensure_bytes(member_dn))]
    server.modify_s(group_dn, mod)
    if sleep:
        time.sleep(2)
        
def config_memberof(server):

    server.plugins.enable(name=PLUGIN_MEMBER_OF)
    MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
    server.modify_s(MEMBEROF_PLUGIN_DN, [(ldap.MOD_REPLACE,
                                          'memberOfAllBackends',
                                          b'on'),
                                          (ldap.MOD_REPLACE, 'memberOfAutoAddOC', b'nsMemberOf')])


def _find_memberof(server, member_dn, group_dn, find_result=True):
    ent = server.getEntry(member_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
    found = False
    if ent.hasAttr('memberof'):

        for val in ent.getValues('memberof'):
            server.log.info("!!!!!!! %s: memberof->%s" % (member_dn, val))
            server.log.info("!!!!!!! %s" % (val))
            server.log.info("!!!!!!! %s" % (group_dn))
            if val.lower() == ensure_bytes(group_dn.lower()):
                found = True
                break

    if find_result:
        assert (found)
    else:
        assert (not found)
        
def test_ticket49386(topo):
    """Specify a test case purpose or name here

    :id: ceb1e2b7-42cb-49f9-8ddd-bc752aa4a589
    :setup: Fill in set up configuration here
    :steps:
        1. Configure memberof
        2. Add users (user_1)
        3. Add groups (group_1)
        4. Make user_1 member of group_1
        5. Check that user_1 has the memberof attribute to group_1
        6. Enable plugin log to capture memberof modrdn callback notification
        7. Rename group_1 in itself
        8. Check that the operation was skipped by memberof
        
    :expectedresults:
        1. memberof modrdn callbackk to log notfication that the update is skipped
    """

    S1 = topo.standalone
    
    # Step 1
    config_memberof(S1)
    S1.restart()
    
    # Step 2
    for i in range(10):
        add_user(S1, i, desc='add on S1')
    
    # Step 3
    for i in range(3):
        add_group(S1, i)
    
    # Step 4
    member_dn = 'cn=%s%d,ou=people,%s' % (USER_CN,  1, SUFFIX)
    group_parent_dn = 'ou=groups,%s' % (SUFFIX)
    group_rdn = 'cn=%s%d' % (GROUP_CN, 1)
    group_dn  = '%s,%s' % (group_rdn, group_parent_dn)
    update_member(S1, member_dn, group_dn, ldap.MOD_ADD, sleep=False)
    
    # Step 5
    _find_memberof(S1, member_dn, group_dn, find_result=True)
    
    # Step 6
    S1.config.loglevel(vals=[LOG_PLUGIN, LOG_DEFAULT], service='error')
    
    # Step 7
    S1.rename_s(group_dn, group_rdn, newsuperior=group_parent_dn, delold=0)
    
    # Step 8
    time.sleep(2) # should not be useful..
    found = False
    for i in S1.ds_error_log.match('.*Skip modrdn operation because src/dst identical.*'):
        log.info('memberof log found: %s' % i)
        found = True
    assert(found)
    
    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

