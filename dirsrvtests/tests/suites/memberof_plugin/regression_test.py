# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import time
import ldap
from random import sample
from lib389.utils import ds_is_older, ensure_list_bytes, ensure_bytes, ensure_str
from lib389.topologies import topology_m1h1c1 as topo, topology_st, topology_m2 as topo_m2
from lib389._constants import *
from lib389.plugins import MemberOfPlugin
from lib389 import Entry
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.group import Groups, Group
from lib389.replica import ReplicationManager
from lib389.tasks import *
from lib389.idm.nscontainer import nsContainers


# Skip on older versions
pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.7'), reason="Not implemented")]

USER_CN = 'user_'
GROUP_CN = 'group1'
DEBUGGING = os.getenv('DEBUGGING', False)
SUBTREE_1 = 'cn=sub1,%s' % SUFFIX
SUBTREE_2 = 'cn=sub2,%s' % SUFFIX


if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def add_users(topo_m2, users_num, suffix):
    """Add users to the default suffix
    Return the list of added user DNs.
    """
    users_list = []
    users = UserAccounts(topo_m2.ms["supplier1"], suffix, rdn=None)
    log.info('Adding %d users' % users_num)
    for num in sample(list(range(1000)), users_num):
        num_ran = int(round(num))
        USER_NAME = 'test%05d' % num_ran
        user = users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': '%s' % num_ran,
            'gidNumber': '%s' % num_ran,
            'homeDirectory': '/home/%s' % USER_NAME,
            'mail': '%s@redhat.com' % USER_NAME,
            'userpassword': 'pass%s' % num_ran,
        })
        users_list.append(user)
    return users_list


def config_memberof(server):
    # Configure fractional to prevent total init to send memberof
    memberof = MemberOfPlugin(server)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    server.restart()
    ents = server.agreement.list(suffix=DEFAULT_SUFFIX)
    for ent in ents:
        log.info('update %s to add nsDS5ReplicatedAttributeListTotal' % ent.dn)
        server.agreement.setProperties(agmnt_dn=ents[0].dn,
                                       properties={RA_FRAC_EXCLUDE: '(objectclass=*) $ EXCLUDE memberOf',
                                                   RA_FRAC_EXCLUDE_TOTAL_UPDATE: '(objectclass=*) $ EXCLUDE '})


def send_updates_now(server):
    ents = server.agreement.list(suffix=DEFAULT_SUFFIX)
    for ent in ents:
        server.agreement.pause(ent.dn)
        server.agreement.resume(ent.dn)


def _find_memberof(server, member_dn, group_dn):
    # To get the specific server's (M1, C1 and H1) user and group
    user = UserAccount(server, member_dn)
    assert user.exists()
    group = Group(server, group_dn)
    assert group.exists()

    # test that the user entry should have memberof attribute with specified group dn value
    assert group._dn.lower() in user.get_attr_vals_utf8_l('memberOf')


@pytest.mark.bz1352121
def test_memberof_with_repl(topo):
    """Test that we allowed to enable MemberOf plugin in dedicated consumer

    :id: ef71cd7c-e792-41bf-a3c0-b3b38391cbe5
    :setup: 1 Supplier - 1 Hub - 1 Consumer
    :steps:
        1. Configure replication to EXCLUDE memberof
        2. Enable memberof plugin
        3. Create users/groups
        4. Make user_0 member of group_0
        5. Checks that user_0 is memberof group_0 on M,H,C
        6. Make group_0 member of group_1 (nest group)
        7. Checks that user_0 is memberof group_0 and group_1 on M,H,C
        8. Check group_0 is memberof group_1 on M,H,C
        9. Remove group_0 from group_1
        10. Check group_0 and user_0 are NOT memberof group_1 on M,H,C
        11. Remove user_0 from group_0
        12. Check user_0 is not memberof group_0 and group_1 on M,H,C
        13. Disable memberof on C
        14. make user_0 member of group_1
        15. Checks that user_0 is memberof group_0 on M,H but not on C
        16. Enable memberof on C
        17. Checks that user_0 is memberof group_0 on M,H but not on C
        18. Run memberof fixup task
        19. Checks that user_0 is memberof group_0 on M,H,C
    :expectedresults:
        1. Configuration should be successful
        2. Plugin should be enabled
        3. Users and groups should be created
        4. user_0 should be member of group_0
        5. user_0 should be memberof group_0 on M,H,C
        6. group_0 should be member of group_1
        7. user_0 should be memberof group_0 and group_1 on M,H,C
        8. group_0 should be memberof group_1 on M,H,C
        9. group_0 from group_1 removal should be successful
        10. group_0 and user_0 should not be memberof group_1 on M,H,C
        11. user_0 from group_0 remove should be successful
        12. user_0 should not be memberof group_0 and group_1 on M,H,C
        13. memberof should be disabled on C
        14. user_0 should be member of group_1
        15. user_0 should be memberof group_0 on M,H and should not on C
        16. Enable memberof on C should be successful
        17. user_0 should be memberof group_0 on M,H should not on C
        18. memberof fixup task should be successful
        19. user_0 should be memberof group_0 on M,H,C
    """

    M1 = topo.ms["supplier1"]
    H1 = topo.hs["hub1"]
    C1 = topo.cs["consumer1"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    # Step 1 & 2
    M1.config.enable_log('audit')
    config_memberof(M1)
    M1.restart()

    H1.config.enable_log('audit')
    config_memberof(H1)
    H1.restart()

    C1.config.enable_log('audit')
    config_memberof(C1)
    C1.restart()

    #Declare lists of users and groups
    test_users = []
    test_groups = []

    # Step 3
    # In for loop create users and add them in the user list
    # it creates user_0 to user_9 (range is fun)
    for i in range(10):
        CN = '%s%d' % (USER_CN, i)
        users = UserAccounts(M1, SUFFIX)
        user_props = TEST_USER_PROPERTIES.copy()
        user_props.update({'uid': CN, 'cn': CN, 'sn': '_%s' % CN})
        testuser = users.create(properties=user_props)
        time.sleep(2)
        test_users.append(testuser)

    # In for loop create groups and add them to the group list
    # it creates group_0 to group_2 (range is fun)
    for i in range(3):
        CN = '%s%d' % (GROUP_CN, i)
        groups = Groups(M1, SUFFIX)
        testgroup = groups.create(properties={'cn': CN})
        time.sleep(2)
        test_groups.append(testgroup)

    # Step 4
    # Now start testing by adding differnt user to differn group
    if not ds_is_older('1.3.7'):
        test_groups[0].remove('objectClass', 'nsMemberOf')

    member_dn = test_users[0].dn
    grp0_dn = test_groups[0].dn
    grp1_dn = test_groups[1].dn

    test_groups[0].add_member(member_dn)
    repl.wait_while_replication_is_progressing(M1, C1)

    # Step 5
    for i in [M1, H1, C1]:
        _find_memberof(i, member_dn, grp0_dn)

    # Step 6
    test_groups[1].add_member(test_groups[0].dn)
    repl.wait_while_replication_is_progressing(M1, C1)

    # Step 7
    for i in [grp0_dn, grp1_dn]:
        for inst in [M1, H1, C1]:
            _find_memberof(inst, member_dn, i)

    # Step 8
    for i in [M1, H1, C1]:
        _find_memberof(i, grp0_dn, grp1_dn)

    # Step 9
    test_groups[1].remove_member(test_groups[0].dn)
    time.sleep(2)

    # Step 10
    # For negative testcase, we are using assertionerror
    for inst in [M1, H1, C1]:
        for i in [grp0_dn, member_dn]:
            with pytest.raises(AssertionError):
                _find_memberof(inst, i, grp1_dn)

    # Step 11
    test_groups[0].remove_member(member_dn)
    time.sleep(2)

    # Step 12
    for inst in [M1, H1, C1]:
        for grp in [grp0_dn, grp1_dn]:
            with pytest.raises(AssertionError):
                _find_memberof(inst, member_dn, grp)

    # Step 13
    C1.plugins.disable(name=PLUGIN_MEMBER_OF)
    C1.restart()

    # Step 14
    test_groups[0].add_member(member_dn)
    repl.wait_while_replication_is_progressing(M1, C1)

    # Step 15
    for i in [M1, H1]:
        _find_memberof(i, member_dn, grp0_dn)
    with pytest.raises(AssertionError):
        _find_memberof(C1, member_dn, grp0_dn)

    # Step 16
    memberof = MemberOfPlugin(C1)
    memberof.enable()
    C1.restart()

    # Step 17
    for i in [M1, H1]:
        _find_memberof(i, member_dn, grp0_dn)
    with pytest.raises(AssertionError):
        _find_memberof(C1, member_dn, grp0_dn)

    # Step 18
    memberof.fixup(SUFFIX)
    # have to sleep instead of task.wait() because the task opens a thread and exits
    time.sleep(5)

    # Step 19
    for i in [M1, H1, C1]:
        _find_memberof(i, member_dn, grp0_dn)


@pytest.mark.skipif(ds_is_older('1.3.7'), reason="Not implemented")
def test_scheme_violation_errors_logged(topo_m2):
    """Check that ERR messages are verbose enough, if a member entry
    doesn't have the appropriate objectclass to support 'memberof' attribute

    :id: e2af0aaa-447e-4e85-a5ce-57ae66260d0b
    :setup: Standalone instance
    :steps:
        1. Enable memberofPlugin and set autoaddoc to nsMemberOf
        2. Restart the instance
        3. Add a user without nsMemberOf attribute
        4. Create a group and add the user to the group
        5. Check that user has memberOf attribute
        6. Check the error log for ".*oc_check_allowed_sv.*USER_DN.*memberOf.*not allowed.*"
           and ".*schema violation caught - repair operation.*" patterns
    :expectedresults:
        1. Should be successful
        2. Should be successful
        3. Should be successful
        4. Should be successful
        5. User should have the attribute
        6. Errors should be logged
    """

    inst = topo_m2.ms["supplier1"]
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    inst.restart()

    users = UserAccounts(inst, SUFFIX)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': USER_CN, 'cn': USER_CN, 'sn': USER_CN})
    testuser = users.create(properties=user_props)
    testuser.remove('objectclass', 'nsMemberOf')

    groups = Groups(inst, SUFFIX)
    testgroup = groups.create(properties={'cn': GROUP_CN})

    testgroup.add('member', testuser.dn)

    user_memberof_attr = testuser.get_attr_val_utf8('memberof')
    assert user_memberof_attr
    log.info('memberOf attr value - {}'.format(user_memberof_attr))

    pattern = ".*oc_check_allowed_sv.*{}.*memberOf.*not allowed.*".format(testuser.dn)
    log.info("pattern = %s" % pattern)
    assert inst.ds_error_log.match(pattern)

    pattern = ".*schema violation caught - repair operation.*"
    assert inst.ds_error_log.match(pattern)


@pytest.mark.bz1192099
def test_memberof_with_changelog_reset(topo_m2):
    """Test that replication does not break, after DS stop-start, due to changelog reset

    :id: 60c11636-55a1-4704-9e09-2c6bcc828de4
    :setup: 2 Suppliers
    :steps:
        1. On M1 and M2, Enable memberof
        2. On M1, add 999 entries allowing memberof
        3. On M1, add a group with these 999 entries as members
        4. Stop M1 in between,
           when add the group memerof is called and before it is finished the
           add, so step 4 should be executed after memberof has started and
           before the add has finished
        5. Check that replication is working fine
    :expectedresults:
        1. memberof should be enabled
        2. Entries should be added
        3. Add operation should start
        4. M1 should be stopped
        5. Replication should be working fine
    """
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]

    log.info("Configure memberof on M1 and M2")
    memberof = MemberOfPlugin(m1)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    m1.restart()

    memberof = MemberOfPlugin(m2)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    m2.restart()

    log.info("On M1, add 999 test entries allowing memberof")
    users_list = add_users(topo_m2, 999, DEFAULT_SUFFIX)

    log.info("On M1, add a group with these 999 entries as members")
    dic_of_attributes = {'cn': ensure_bytes('testgroup'),
                         'objectclass': ensure_list_bytes(['top', 'groupOfNames'])}

    for user in users_list:
        dic_of_attributes.setdefault('member', [])
        dic_of_attributes['member'].append(user.dn)

    log.info('Adding the test group using async function')
    groupdn = 'cn=testgroup,%s' % DEFAULT_SUFFIX
    m1.add(Entry((groupdn, dic_of_attributes)))

    #shutdown the server in-between adding the group
    m1.stop()

    #start the server
    m1.start()

    log.info("Check the log messages for error")
    error_msg = "ERR - NSMMReplicationPlugin - ruv_compare_ruv"
    assert not m1.ds_error_log.match(error_msg)

    log.info("Check that the replication is working fine both ways, M1 <-> M2")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication_topology(topo_m2)


def add_container(inst, dn, name, sleep=False):
    """Creates container entry"""
    conts = nsContainers(inst, dn)
    cont = conts.create(properties={'cn': name})
    if sleep:
        time.sleep(1)
    return cont


def add_member(server, cn, subtree):
    dn = subtree
    users = UserAccounts(server, dn, rdn=None)
    users.create(properties={'uid': 'test_%s' % cn,
                             'cn': "%s" % cn,
                             'sn': 'SN',
                             'description': 'member',
                             'uidNumber': '1000',
                             'gidNumber': '2000',
                             'homeDirectory': '/home/testuser'
                             })


def add_group(server, cn, subtree):
    group = Groups(server, subtree, rdn=None)
    group.create(properties={'cn': "%s" % cn,
                             'member': ['uid=test_m1,%s' % SUBTREE_1, 'uid=test_m2,%s' % SUBTREE_1],
                             'description': 'group'})


def rename_entry(server, cn, from_subtree, to_subtree):
    dn = '%s,%s' % (cn, from_subtree)
    nrdn = '%s-new' % cn
    log.fatal('Renaming user (%s): new %s' % (dn, nrdn))
    server.rename_s(dn, nrdn, newsuperior=to_subtree, delold=0)


def _find_memberof_ext(server, user_dn=None, group_dn=None, find_result=True):
    assert (server)
    assert (user_dn)
    assert (group_dn)
    ent = server.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
    found = False
    if ent.hasAttr('memberof'):

        for val in ent.getValues('memberof'):
            server.log.info("!!!!!!! %s: memberof->%s" % (user_dn, val))
            if ensure_str(val) == group_dn:
                found = True
                break

    if find_result:
        assert found
    else:
        assert (not found)


@pytest.mark.ds49161
def test_memberof_group(topology_st):
    """Test memberof does not fail if group is moved into scope

    :id: d1d276ae-6375-4ad8-9437-6a0afcbee7d2

    :setup: Single instance

    :steps:
         1. Enable memberof plugin and set memberofentryscope
         2. Restart the server
         3. Add test sub-suffixes
         4. Add test users
         5. Add test groups
         6. Check for memberof attribute added to the test users
         7. Rename the group entry
         8. Check the new name is reflected in memberof attribute of user

    :expectedresults:
         1. memberof plugin should be enabled and memberofentryscope should be set
         2. Server should be restarted
         3. Sub-suffixes should be added
         4. Test users should be added
         5. Test groups should be added
         6. memberof attribute should be present in the test users
         7. Group entry should be renamed
         8. New group name should be present in memberof attribute of user
    """

    inst = topology_st.standalone
    log.info('Enable memberof plugin and set the scope as cn=sub1,dc=example,dc=com')
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.replace('memberOfEntryScope', SUBTREE_1)
    inst.restart()

    add_container(inst, SUFFIX, 'sub1')
    add_container(inst, SUFFIX, 'sub2')
    add_member(inst, 'm1', SUBTREE_1)
    add_member(inst, 'm2', SUBTREE_1)
    add_group(inst, 'g1', SUBTREE_1)
    add_group(inst, 'g2', SUBTREE_2)

    # _check_memberof
    dn1 = '%s,%s' % ('uid=test_m1', SUBTREE_1)
    dn2 = '%s,%s' % ('uid=test_m2', SUBTREE_1)
    g1 = '%s,%s' % ('cn=g1', SUBTREE_1)
    g2 = '%s,%s' % ('cn=g2', SUBTREE_2)
    _find_memberof_ext(inst, dn1, g1, True)
    _find_memberof_ext(inst, dn2, g1, True)
    _find_memberof_ext(inst, dn1, g2, False)
    _find_memberof_ext(inst, dn2, g2, False)

    rename_entry(inst, 'cn=g2', SUBTREE_2, SUBTREE_1)

    g2n = '%s,%s' % ('cn=g2-new', SUBTREE_1)
    _find_memberof_ext(inst, dn1, g1, True)
    _find_memberof_ext(inst, dn2, g1, True)
    _find_memberof_ext(inst, dn1, g2n, True)
    _find_memberof_ext(inst, dn2, g2n, True)


def _config_memberof_entrycache_on_modrdn_failure(server):

    server.plugins.enable(name=PLUGIN_MEMBER_OF)
    peoplebase = 'ou=people,%s' % SUFFIX
    MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
    server.modify_s(MEMBEROF_PLUGIN_DN, [(ldap.MOD_REPLACE, 'memberOfAllBackends', b'on'),
                                         (ldap.MOD_REPLACE, 'memberOfEntryScope', peoplebase.encode()),
                                         (ldap.MOD_REPLACE, 'memberOfAutoAddOC', b'nsMemberOf')])


def _disable_auto_oc_memberof(server):
    MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
    server.modify_s(MEMBEROF_PLUGIN_DN,
        [(ldap.MOD_REPLACE, 'memberOfAutoAddOC', b'nsContainer')])


@pytest.mark.ds49967
def test_entrycache_on_modrdn_failure(topology_st):
    """This test checks that when a modrdn fails, the destination entry is not returned by a search
    This could happen in case the destination entry remains in the entry cache

    :id: a4d8ac0b-2448-406a-9dc2-5a72851e30b6
    :setup: Standalone Instance
    :steps:
        1. configure memberof to only scope ou=people,SUFFIX
        2. Creates 10 users
        3. Create groups0 (in peoplebase) that contain user0 and user1
        4. Check user0 and user1 have memberof=group0.dn
        5. Create group1 (OUT peoplebase) that contain user0 and user1
        6. Check user0 and user1 have NOT memberof=group1.dn
        7. Move group1 IN peoplebase and check users0 and user1 HAVE memberof=group1.dn
        8. Create group2 (OUT peoplebase) that contain user2 and user3. Group2 contains a specific description value
        9. Check user2 and user3 have NOT memberof=group2.dn
        10. configure memberof so that added objectclass does not allow 'memberof' attribute
        11. Move group2 IN peoplebase and check move failed OPERATIONS_ERROR (because memberof failed)
        12. Search all groups and check that the group, having the specific description value,
            has the original DN of group2.dn
    :expectedresults:
        1. should succeed
        2. should succeed
        3. should succeed
        4. should succeed
        5. should succeed
        6. should succeed
        7. should succeed
        8. should succeed
        9. should succeed
        10. should succeed
        11. should fail OPERATION_ERROR because memberof plugin fails to add 'memberof' to members.
        12. should succeed

    """

    # only scopes peoplebase
    _config_memberof_entrycache_on_modrdn_failure(topology_st.standalone)
    topology_st.standalone.restart(timeout=10)

    # create 10 users
    peoplebase = 'ou=people,%s' % SUFFIX
    for i in range(10):
        cn = 'user%d' % i
        dn = 'cn=%s,%s' % (cn, peoplebase)
        log.fatal('Adding user (%s): ' % dn)
        topology_st.standalone.add_s(Entry((dn, {'objectclass': ['top', 'person'],
                             'sn': 'user_%s' % cn,
                             'description': 'add on standalone'})))

    # Check that members of group0 (in the scope) have 'memberof
    group0_dn = 'cn=group_in0,%s' % peoplebase
    topology_st.standalone.add_s(Entry((group0_dn, {'objectclass': ['top', 'groupofnames'],
                             'member': [
                                   'cn=user0,%s' % peoplebase,
                                   'cn=user1,%s' % peoplebase,
                                   ],
                             'description': 'mygroup'})))

    # Check the those entries have memberof with group0
    for i in range(2):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        assert ent.hasAttr('memberof')
        found = False
        for val in ent.getValues('memberof'):
            topology_st.standalone.log.info("!!!!!!! %s: memberof->%s (vs %s)" % (user_dn, val, group0_dn.encode().lower()))
            if val.lower() == group0_dn.encode().lower():
                found = True
                break
        assert found

    # Create a group1 out of the scope
    group1_dn = 'cn=group_out1,%s' % SUFFIX
    topology_st.standalone.add_s(Entry((group1_dn, {'objectclass': ['top', 'groupofnames'],
                             'member': [
                                   'cn=user0,%s' % peoplebase,
                                   'cn=user1,%s' % peoplebase,
                                   ],
                             'description': 'mygroup'})))

    # Check the those entries have not memberof with group1
    for i in range(2):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        assert ent.hasAttr('memberof')
        found = False
        for val in ent.getValues('memberof'):
            topology_st.standalone.log.info("!!!!!!! %s: memberof->%s (vs %s)" % (user_dn, val, group1_dn.encode().lower()))
            if val.lower() == group1_dn.encode().lower():
                found = True
                break
        assert not found

    # move group1 into the scope and check user0 and user1 are memberof group1
    topology_st.standalone.rename_s(group1_dn, 'cn=group_in1', newsuperior=peoplebase, delold=0)
    new_group1_dn = 'cn=group_in1,%s' % peoplebase
    for i in range(2):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        assert ent.hasAttr('memberof')
        found = False
        for val in ent.getValues('memberof'):
            topology_st.standalone.log.info("!!!!!!! %s: memberof->%s (vs %s)" % (user_dn, val, new_group1_dn.encode().lower()))
            if val.lower() == new_group1_dn.encode().lower():
                found = True
                break
        assert found

    # Create a group2 out of the scope with a SPECIFIC description value
    entry_description = "this is to check that the entry having this description has the appropriate DN"
    group2_dn = 'cn=group_out2,%s' % SUFFIX
    topology_st.standalone.add_s(Entry((group2_dn, {'objectclass': ['top', 'groupofnames'],
                             'member': [
                                   'cn=user2,%s' % peoplebase,
                                   'cn=user3,%s' % peoplebase,
                                   ],
                             'description': entry_description})))

    # Check the those entries have not memberof with group2
    for i in (2, 3):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        assert not ent.hasAttr('memberof')

    # memberof will not add the missing objectclass
    _disable_auto_oc_memberof(topology_st.standalone)
    topology_st.standalone.restart(timeout=10)

    # move group2 into the scope and check it fails
    try:
        topology_st.standalone.rename_s(group2_dn, 'cn=group_in2', newsuperior=peoplebase, delold=0)
        topology_st.standalone.log.info("This is unexpected, modrdn should fail as the member entry have not the appropriate objectclass")
        assert False
    except ldap.OBJECT_CLASS_VIOLATION:
        pass

    # retrieve the entry having the specific description value
    # check that the entry DN is the original group2 DN
    ents = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(cn=gr*)')
    found = False
    for ent in ents:
        topology_st.standalone.log.info("retrieve: %s with desc=%s" % (ent.dn, ent.getValue('description')))
        if ent.getValue('description') == entry_description.encode():
            found = True
            assert ent.dn == group2_dn
    assert found


def _config_memberof_silent_memberof_failure(server):
    _config_memberof_entrycache_on_modrdn_failure(server)


def test_silent_memberof_failure(topology_st):
    """This test checks that if during a MODRDN, the memberof plugin fails
    then MODRDN also fails

    :id: 095aee01-581c-43dd-a241-71f9631a18bb
    :setup: Standalone Instance
    :steps:
        1. configure memberof to only scope ou=people,SUFFIX
        2. Do some cleanup and Creates 10 users
        3. Create groups0 (IN peoplebase) that contain user0 and user1
        4. Check user0 and user1 have memberof=group0.dn
        5. Create group1 (OUT peoplebase) that contain user0 and user1
        6. Check user0 and user1 have NOT memberof=group1.dn
        7. Move group1 IN peoplebase and check users0 and user1 HAVE memberof=group1.dn
        8. Create group2 (OUT peoplebase) that contain user2 and user3.
        9. Check user2 and user3 have NOT memberof=group2.dn
        10. configure memberof so that added objectclass does not allow 'memberof' attribute
        11. Move group2 IN peoplebase and check move failed OPERATIONS_ERROR (because memberof failed)
        12. Check user2 and user3 have NOT memberof=group2.dn
        13. ADD group3 (IN peoplebase) with user4 and user5 members and check add failed OPERATIONS_ERROR (because memberof failed)
        14. Check user4 and user5 have NOT memberof=group2.dn
    :expectedresults:
        1. should succeed
        2. should succeed
        3. should succeed
        4. should succeed
        5. should succeed
        6. should succeed
        7. should succeed
        8. should succeed
        9. should succeed
        10. should succeed
        11. should fail OPERATION_ERROR because memberof plugin fails to add 'memberof' to members.
        12. should succeed
        13. should fail OPERATION_ERROR because memberof plugin fails to add 'memberof' to members
        14. should succeed
    """
    # only scopes peoplebase
    _config_memberof_silent_memberof_failure(topology_st.standalone)
    topology_st.standalone.restart(timeout=10)

    # first do some cleanup
    peoplebase = 'ou=people,%s' % SUFFIX
    for i in range(10):
        cn = 'user%d' % i
        dn = 'cn=%s,%s' % (cn, peoplebase)
        topology_st.standalone.delete_s(dn)
    topology_st.standalone.delete_s('cn=group_in0,%s' % peoplebase)
    topology_st.standalone.delete_s('cn=group_in1,%s' % peoplebase)
    topology_st.standalone.delete_s('cn=group_out2,%s' % SUFFIX)

    # create 10 users
    for i in range(10):
        cn = 'user%d' % i
        dn = 'cn=%s,%s' % (cn, peoplebase)
        log.fatal('Adding user (%s): ' % dn)
        topology_st.standalone.add_s(Entry((dn, {'objectclass': ['top', 'person'],
                             'sn': 'user_%s' % cn,
                             'description': 'add on standalone'})))

    # Check that members of group0 (in the scope) have 'memberof
    group0_dn = 'cn=group_in0,%s' % peoplebase
    topology_st.standalone.add_s(Entry((group0_dn, {'objectclass': ['top', 'groupofnames'],
                             'member': [
                                   'cn=user0,%s' % peoplebase,
                                   'cn=user1,%s' % peoplebase,
                                   ],
                             'description': 'mygroup'})))

    # Check the those entries have memberof with group0
    for i in range(2):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        assert ent.hasAttr('memberof')
        found = False
        for val in ent.getValues('memberof'):
            topology_st.standalone.log.info("!!!!!!! %s: memberof->%s (vs %s)" % (user_dn, val, group0_dn.encode().lower()))
            if val.lower() == group0_dn.encode().lower():
                found = True
                break
        assert found

    # Create a group1 out of the scope
    group1_dn = 'cn=group_out1,%s' % SUFFIX
    topology_st.standalone.add_s(Entry((group1_dn, {'objectclass': ['top', 'groupofnames'],
                             'member': [
                                   'cn=user0,%s' % peoplebase,
                                   'cn=user1,%s' % peoplebase,
                                   ],
                             'description': 'mygroup'})))

    # Check the those entries have not memberof with group1
    for i in range(2):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        assert ent.hasAttr('memberof')
        found = False
        for val in ent.getValues('memberof'):
            topology_st.standalone.log.info("!!!!!!! %s: memberof->%s (vs %s)" % (user_dn, val, group1_dn.encode().lower()))
            if val.lower() == group1_dn.encode().lower():
                found = True
                break
        assert not found

    # move group1 into the scope and check user0 and user1 are memberof group1
    topology_st.standalone.rename_s(group1_dn, 'cn=group_in1', newsuperior=peoplebase, delold=0)
    new_group1_dn = 'cn=group_in1,%s' % peoplebase
    for i in range(2):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        assert ent.hasAttr('memberof')
        found = False
        for val in ent.getValues('memberof'):
            topology_st.standalone.log.info("!!!!!!! %s: memberof->%s (vs %s)" % (user_dn, val, new_group1_dn.encode().lower()))
            if val.lower() == new_group1_dn.encode().lower():
                found = True
                break
        assert found

    # Create a group2 out of the scope
    group2_dn = 'cn=group_out2,%s' % SUFFIX
    topology_st.standalone.add_s(Entry((group2_dn, {'objectclass': ['top', 'groupofnames'],
                             'member': [
                                   'cn=user2,%s' % peoplebase,
                                   'cn=user3,%s' % peoplebase,
                                   ],
                             'description': 'mygroup'})))

    # Check the those entries have not memberof with group2
    for i in (2, 3):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        assert not ent.hasAttr('memberof')

    # memberof will not add the missing objectclass
    _disable_auto_oc_memberof(topology_st.standalone)
    topology_st.standalone.restart(timeout=10)

    # move group2 into the scope and check it fails
    try:
        topology_st.standalone.rename_s(group2_dn, 'cn=group_in2', newsuperior=peoplebase, delold=0)
        topology_st.standalone.log.info("This is unexpected, modrdn should fail as the member entry have not the appropriate objectclass")
        assert False
    except ldap.OBJECT_CLASS_VIOLATION:
        pass

    # Check the those entries have not memberof
    for i in (2, 3):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        topology_st.standalone.log.info("Should assert %s has memberof is %s" % (user_dn, ent.hasAttr('memberof')))
        assert not ent.hasAttr('memberof')

    # Create a group3 in the scope
    group3_dn = 'cn=group3_in,%s' % peoplebase
    try:
        topology_st.standalone.add_s(Entry((group3_dn, {'objectclass': ['top', 'groupofnames'],
                             'member': [
                                   'cn=user4,%s' % peoplebase,
                                   'cn=user5,%s' % peoplebase,
                                   ],
                             'description': 'mygroup'})))
        topology_st.standalone.log.info("This is unexpected, ADD should fail as the member entry have not the appropriate objectclass")
        assert False
    except ldap.OBJECT_CLASS_VIOLATION:
        pass
    except ldap.OPERATIONS_ERROR:
        pass

    # Check the those entries do not have memberof
    for i in (4, 5):
        user_dn = 'cn=user%d,%s' % (i, peoplebase)
        ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
        topology_st.standalone.log.info("Should assert %s has memberof is %s" % (user_dn, ent.hasAttr('memberof')))
        assert not ent.hasAttr('memberof')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

