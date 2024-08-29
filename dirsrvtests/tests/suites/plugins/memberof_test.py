# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389._constants import PLUGIN_MEMBER_OF, SUFFIX
from lib389.plugins import MemberOfPlugin

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv('DEBUGGING', False)

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX
GROUP_DN = 'cn=group,' + DEFAULT_SUFFIX

PLUGIN_TYPE = 'nsslapd-pluginType'
PLUGIN_MEMBEROF_GRP_ATTR = 'memberofgroupattr'
PLUGIN_ENABLED = 'nsslapd-pluginEnabled'

USER_RDN = "user"
USERS_CONTAINER = "ou=people,%s" % SUFFIX

GROUP_RDN = "group"
GROUPS_CONTAINER = "ou=groups,%s" % SUFFIX

def _memberof_checking_delay(inst):
    memberof = MemberOfPlugin(inst)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        # In case of deferred update then a safe delay
        # to let the deferred thread processing is 3 sec
        delay = 3
    else:
        # Else it is the same TXN, no reason to wait
        delay = 0
    return delay

def _set_memberofgroupattr_add(topology_st, values):
    topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                    [(ldap.MOD_ADD,
                                      PLUGIN_MEMBEROF_GRP_ATTR,
                                      ensure_bytes(values))])


def _get_user_rdn(ext):
    return ensure_bytes("uid=%s_%s" % (USER_RDN, ext))


def _get_user_dn(ext):
    return ensure_bytes("%s,%s" % (ensure_str(_get_user_rdn(ext)), USERS_CONTAINER))


def _get_group_rdn(ext):
    return ensure_bytes("cn=%s_%s" % (GROUP_RDN, ext))


def _get_group_dn(ext):
    return ensure_bytes("%s,%s" % (ensure_str(_get_group_rdn(ext)), GROUPS_CONTAINER))


def _create_user(topology_st, ext):
    user_dn = ensure_str(_get_user_dn(ext))
    topology_st.standalone.add_s(Entry((user_dn, {
        'objectclass': 'top extensibleObject'.split(),
        'uid': ensure_str(_get_user_rdn(ext))
    })))
    log.info("Create user %s" % user_dn)
    return ensure_bytes(user_dn)


def _delete_user(topology_st, ext):
    user_dn = ensure_str(_get_user_dn(ext))
    topology_st.standalone.delete_s(user_dn)
    log.info("Delete user %s" % user_dn)


def _create_group(topology_st, ext):
    group_dn = ensure_str(_get_group_dn(ext))
    topology_st.standalone.add_s(Entry((group_dn, {
        'objectclass': 'top groupOfNames groupOfUniqueNames extensibleObject'.split(),
        'ou': ensure_str(_get_group_rdn(ext))
    })))
    log.info("Create group %s" % group_dn)
    return ensure_bytes(group_dn)


def _delete_group(topology_st, ext):
    group_dn = ensure_str(_get_group_dn(ext))
    topology_st.standalone.delete_s(group_dn)
    log.info("Delete group %s" % group_dn)


def _check_memberattr(topology_st, entry, memberattr, value):
    log.info("Check %s.%s = %s" % (entry, memberattr, value))
    entry = topology_st.standalone.getEntry(ensure_str(entry), ldap.SCOPE_BASE, '(objectclass=*)', [memberattr])
    if not entry.hasAttr(ensure_str(memberattr)):
        return False

    found = False
    for val in entry.getValues(ensure_str(memberattr)):
        log.info("%s: %s" % (memberattr, ensure_str(val)))
        if ensure_str(value.lower()) == ensure_str(val.lower()):
            found = True
            break
    return found


def _check_memberof(topology_st, member, group):
    log.info("Lookup memberof from %s" % member)
    entry = topology_st.standalone.getEntry(ensure_str(member), ldap.SCOPE_BASE, '(objectclass=*)', ['memberof'])
    if not entry.hasAttr('memberof'):
        return False

    found = False
    for val in entry.getValues('memberof'):
        log.info("memberof: %s" % ensure_str(val))
        if ensure_str(group.lower()) == ensure_str(val.lower()):
            found = True
            log.info("--> membership verified")
            break
    return found


def test_betxnpostoperation_replace(topology_st):
    """Test modify the memberof plugin operation to use the new type

    :id: d222af17-17a6-48a0-8f22-a38306726a91
    :setup: Standalone instance
    :steps:
        1. Set plugin type to betxnpostoperation
        2. Check is was changed
    :expectedresults:
        1. Success
        2. Success
    """

    topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                    [(ldap.MOD_REPLACE,
                                      PLUGIN_TYPE,
                                      b'betxnpostoperation')])
    topology_st.standalone.restart()
    ent = topology_st.standalone.getEntry(MEMBEROF_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)", [PLUGIN_TYPE])
    assert ent.hasAttr(PLUGIN_TYPE)
    assert ent.getValue(PLUGIN_TYPE) == b'betxnpostoperation'


def test_memberofgroupattr_add(topology_st):
    """Check multiple grouping attributes supported

    :id: d222af17-17a6-48a0-8f22-a38306726a92
    :setup: Standalone instance
    :steps:
        1. Add memberofgroupattr - 'uniqueMember'
        2. Check we have 'uniqueMember' and 'member' values
    :expectedresults:
        1. Success
        2. Success
    """

    _set_memberofgroupattr_add(topology_st, 'uniqueMember')
    ent = topology_st.standalone.getEntry(MEMBEROF_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)",
                                          [PLUGIN_MEMBEROF_GRP_ATTR])
    assert ent.hasAttr(PLUGIN_MEMBEROF_GRP_ATTR)
    assert b'member'.lower() in [x.lower() for x in ent.getValues(PLUGIN_MEMBEROF_GRP_ATTR)]
    assert b'uniqueMember'.lower() in [x.lower() for x in ent.getValues(PLUGIN_MEMBEROF_GRP_ATTR)]


def test_enable(topology_st):
    """Check the plug-in is started

    :id: d222af17-17a6-48a0-8f22-a38306726a93
    :setup: Standalone instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
    :expectedresults:
        1. Success
        2. Server should start and plugin should be on
    """

    log.info("Enable MemberOf plugin")
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_st.standalone.restart()
    ent = topology_st.standalone.getEntry(MEMBEROF_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)", [PLUGIN_ENABLED])
    assert ent.hasAttr(PLUGIN_ENABLED)
    assert ent.getValue(PLUGIN_ENABLED).lower() == b'on'


def test_member_add(topology_st):
    """MemberOf attribute should be successfully added to both the users

    :id: d222af17-17a6-48a0-8f22-a38306726a94
    :setup: Standalone instance
    :steps:
        1. Create user and groups
        2. Add the users as members to the groups
        3. Check the membership
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_st.standalone.restart()

    memofenh1 = _create_user(topology_st, 'memofenh1')
    memofenh2 = _create_user(topology_st, 'memofenh2')

    memofegrp1 = _create_group(topology_st, 'memofegrp1')
    memofegrp2 = _create_group(topology_st, 'memofegrp2')

    mods = [(ldap.MOD_ADD, 'member', memofenh1), (ldap.MOD_ADD, 'uniqueMember', memofenh2)]
    log.info("Update %s is memberof %s (member)" % (memofenh1, memofegrp1))
    log.info("Update %s is memberof %s (uniqueMember)" % (memofenh2, memofegrp1))
    topology_st.standalone.modify_s(ensure_str(memofegrp1), mods)

    log.info("Update %s is memberof %s (member)" % (memofenh1, memofegrp2))
    log.info("Update %s is memberof %s (uniqueMember)" % (memofenh2, memofegrp2))
    topology_st.standalone.modify_s(ensure_str(memofegrp2), mods)

    time.sleep(delay)
    # assert enh1 is member of grp1 and grp2
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is member of grp1 and grp2
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


def test_member_delete_gr1(topology_st):
    """Partial removal of memberofgroupattr: removing member attribute from Group1

    :id: d222af17-17a6-48a0-8f22-a38306726a95
    :setup: Standalone instance
    :steps:
        1. Delete a member: enh1 in grp1
        2. Check the states of the members were changed accordingly
    :expectedresults:
        1. Success
        2. Success
    """

    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    log.info("Update %s is no longer memberof %s (member)" % (memofenh1, memofegrp1))
    mods = [(ldap.MOD_DELETE, 'member', memofenh1)]
    topology_st.standalone.modify_s(ensure_str(memofegrp1), mods)

    time.sleep(delay)
    # assert enh1 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is member of grp1 and  is member of grp2
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


def test_member_delete_gr2(topology_st):
    """Partial removal of memberofgroupattr: removing uniqueMember attribute from Group2

    :id: d222af17-17a6-48a0-8f22-a38306726a96
    :setup: Standalone instance
    :steps:
        1. Delete a uniqueMember: enh2 in grp2
        2. Check the states of the members were changed accordingly
    :expectedresults:
        1. Success
        2. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')

    log.info("Update %s is no longer memberof %s (uniqueMember)" % (memofenh1, memofegrp1))
    mods = [(ldap.MOD_DELETE, 'uniqueMember', memofenh2)]
    topology_st.standalone.modify_s(ensure_str(memofegrp2), mods)

    time.sleep(delay)
    # assert enh1 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is member of grp1 and  is NOT member of grp2
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


def test_member_delete_all(topology_st):
    """Complete removal of memberofgroupattr

    :id: d222af17-17a6-48a0-8f22-a38306726a97
    :setup: Standalone instance
    :steps:
        1. Delete the rest of the members
        2. Check the states of the members were changed accordingly
    :expectedresults:
        1. Success
        2. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')

    log.info("Update %s is no longer memberof %s (uniqueMember)" % (memofenh2, memofegrp1))
    mods = [(ldap.MOD_DELETE, 'uniqueMember', memofenh2)]
    topology_st.standalone.modify_s(ensure_str(memofegrp1), mods)

    log.info("Update %s is no longer memberof %s (member)" % (memofenh1, memofegrp2))
    mods = [(ldap.MOD_DELETE, 'member', memofenh1)]
    topology_st.standalone.modify_s(ensure_str(memofegrp2), mods)

    time.sleep(delay)
    # assert enh1 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is member of grp1 and  is NOT member of grp2
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


def test_member_after_restart(topology_st):
    """MemberOf attribute should be present on both the users

    :id: d222af17-17a6-48a0-8f22-a38306726a98
    :setup: Standalone instance
    :steps:
        1. Add a couple of members to the groups
        2. Restart the instance
        3. Check the states of the members were changed accordingly
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')

    mods = [(ldap.MOD_ADD, 'member', memofenh1)]
    log.info("Update %s is memberof %s (member)" % (memofenh1, memofegrp1))
    topology_st.standalone.modify_s(ensure_str(memofegrp1), mods)

    mods = [(ldap.MOD_ADD, 'uniqueMember', memofenh2)]
    log.info("Update %s is memberof %s (uniqueMember)" % (memofenh2, memofegrp2))
    topology_st.standalone.modify_s(ensure_str(memofegrp2), mods)

    time.sleep(delay)
    # assert enh1 is member of grp1 and  is NOT member of grp2
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)

    log.info("Remove uniqueMember as a memberofgrpattr")
    topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                    [(ldap.MOD_DELETE,
                                      PLUGIN_MEMBEROF_GRP_ATTR,
                                      [b'uniqueMember'])])
    topology_st.standalone.restart()

    log.info("Assert that this change of configuration did change the already set values")
    # assert enh1 is member of grp1 and  is NOT member of grp2
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)

    _set_memberofgroupattr_add(topology_st, 'uniqueMember')
    topology_st.standalone.restart()


def test_memberofgroupattr_uid(topology_st):
    """MemberOf attribute should not be added to the user since memberuid is not a DN syntax attribute

    :id: d222af17-17a6-48a0-8f22-a38306726a99
    :setup: Standalone instance
    :steps:
        1. Try to add memberUid to the group
    :expectedresults:
        1. It should fail with Unwilling to perform error
    """

    try:
        _set_memberofgroupattr_add(topology_st, 'memberUid')
        log.error("Setting 'memberUid' as memberofgroupattr should be rejected")
        assert False
    except ldap.UNWILLING_TO_PERFORM:
        log.error("Setting 'memberUid' as memberofgroupattr is rejected (expected)")
        assert True


def test_member_add_duplicate_usr1(topology_st):
    """Duplicate member attribute to groups

    :id: d222af17-17a6-48a0-8f22-a38306726a10
    :setup: Standalone instance
    :steps:
        1. Try to add a member: enh1 which already exists
    :expectedresults:
        1. It should fail with Type of value exists error
    """

    memofenh1 = _get_user_dn('memofenh1')
    memofegrp1 = _get_group_dn('memofegrp1')

    # assert enh1 is member of grp1
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)

    mods = [(ldap.MOD_ADD, 'member', memofenh1)]
    log.info("Try %s is memberof %s (member)" % (memofenh1, memofegrp1))
    try:
        topology_st.standalone.modify_s(ensure_str(memofegrp1), mods)
        log.error(
            "Should not be allowed to add %s member of %s (because it was already member)" % (memofenh1, memofegrp1))
        assert False
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.error("%s already member of %s --> fail (expected)" % (memofenh1, memofegrp1))
        assert True


def test_member_add_duplicate_usr2(topology_st):
    """Duplicate uniqueMember attributes to groups

    :id: d222af17-17a6-48a0-8f22-a38306726a11
    :setup: Standalone instance
    :steps:
        1. Try to add a uniqueMember: enh2 which already exists
    :expectedresults:
        1. It should fail with Type of value exists error
    """

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')

    log.info("Check initial status")
    # assert enh1 is member of grp1 and  is NOT member of grp2
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)

    mods = [(ldap.MOD_ADD, 'uniqueMember', memofenh2)]
    log.info("Try %s is memberof %s (member)" % (memofenh2, memofegrp2))
    try:
        topology_st.standalone.modify_s(ensure_str(memofegrp2), mods)
        log.error(
            "Should not be allowed to add %s member of %s (because it was already member)" % (memofenh2, memofegrp2))
        assert False
    except ldap.TYPE_OR_VALUE_EXISTS:
        log.error("%s already member of %s --> fail (expected)" % (memofenh2, memofegrp2))
        assert True

    log.info("Check final status")
    # assert enh1 is member of grp1 and  is NOT member of grp2
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


#def test_memberof_MultiGrpAttr_012(topology_st):
#    """
#    MemberURL attritbute should reflect the modrdn changes in the group.
#
#    This test has been covered in MODRDN test suite
#
#    At the beginning:
#        memofenh1 is memberof memofegrp1
#        memofenh2 is memberof memofegrp2
#
#    At the end
#        memofenh1 is memberof memofegrp1
#        memofenh2 is memberof memofegrp2
#    """
#    pass


#def test_memberof_MultiGrpAttr_013(topology_st):
#    """
#    MemberURL attritbute should reflect the modrdn changes in the group.
#
#    This test has been covered in MODRDN test suite
#
#    At the beginning:
#        memofenh1 is memberof memofegrp1
#        memofenh2 is memberof memofegrp2
#
#    At the end
#        memofenh1 is memberof memofegrp1
#        memofenh2 is memberof memofegrp2
#    """
#    pass


def test_member_uniquemember_same_user(topology_st):
    """Check the situation when both member and uniqueMember
    pointing to the same user

    :id: d222af17-17a6-48a0-8f22-a38306726a13
    :setup: Standalone instance, grp3,
            enh1 is member of
            - grp1 (member)
            - not grp2
            enh2 is member of
            - not grp1
            - grp2 (uniquemember)
    :steps:
        1. Add member: enh1 and uniqueMember: enh1 to grp3
        2. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (member uniquemember)
        3. Delete member: enh1 from grp3
        4. Add member: enh2 to grp3
        5. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
        6. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')

    log.info("Check initial status")
    # assert enh1 is member of grp1 and  is NOT member of grp2
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)

    memofegrp3 = _create_group(topology_st, 'memofegrp3')

    mods = [(ldap.MOD_ADD, 'member', memofenh1), (ldap.MOD_ADD, 'uniqueMember', memofenh1)]
    log.info("Update %s is memberof %s (member)" % (memofenh1, memofegrp3))
    log.info("Update %s is memberof %s (uniqueMember)" % (memofenh1, memofegrp3))
    topology_st.standalone.modify_s(ensure_str(memofegrp3), mods)

    time.sleep(delay)
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (member uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)

    mods = [(ldap.MOD_DELETE, 'member', memofenh1)]
    log.info("Update %s is not memberof %s (member)" % (memofenh1, memofegrp3))
    topology_st.standalone.modify_s(ensure_str(memofegrp3), mods)

    mods = [(ldap.MOD_ADD, 'member', memofenh2)]
    log.info("Update %s is memberof %s (member)" % (memofenh2, memofegrp3))
    topology_st.standalone.modify_s(ensure_str(memofegrp3), mods)

    time.sleep(delay)
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)

    ent = topology_st.standalone.getEntry(ensure_str(memofegrp3), ldap.SCOPE_BASE, "(objectclass=*)", ['member', 'uniqueMember'])
    assert ent.hasAttr('member')
    assert ensure_bytes(memofenh1) not in ent.getValues('member')
    assert ensure_bytes(memofenh2) in ent.getValues('member')
    assert ent.hasAttr('uniqueMember')
    assert ensure_bytes(memofenh1) in ent.getValues('uniqueMember')
    assert ensure_bytes(memofenh2) not in ent.getValues('uniqueMember')

    log.info("Checking final status")
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)


def test_member_not_exists(topology_st):
    """Check the situation when we add non-existing users to member attribute

    :id: d222af17-17a6-48a0-8f22-a38306726a14
    :setup: Standalone instance, grp015,
            enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)
            enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
    :steps:
        1. Add member: dummy1 and uniqueMember: dummy2 to grp015
        2. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp015
        3. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp015
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')
    dummy1 = _get_user_dn('dummy1')
    dummy2 = _get_user_dn('dummy2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')

    log.info("Checking Initial status")
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)

    memofegrp015 = _create_group(topology_st, 'memofegrp015')

    mods = [(ldap.MOD_ADD, 'member', dummy1), (ldap.MOD_ADD, 'uniqueMember', dummy2)]
    log.info("Update %s is memberof %s (member)" % (dummy1, memofegrp015))
    log.info("Update %s is memberof %s (uniqueMember)" % (dummy2, memofegrp015))
    topology_st.standalone.modify_s(ensure_str(memofegrp015), mods)

    ent = topology_st.standalone.getEntry(ensure_str(memofegrp015), ldap.SCOPE_BASE, "(objectclass=*)", ['member', 'uniqueMember'])
    assert ent.hasAttr('member')
    assert ensure_bytes(dummy1) in ent.getValues('member')
    assert ensure_bytes(dummy2) not in ent.getValues('member')
    assert ent.hasAttr('uniqueMember')
    assert ensure_bytes(dummy1) not in ent.getValues('uniqueMember')
    assert ensure_bytes(dummy2) in ent.getValues('uniqueMember')

    time.sleep(delay)
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)


def test_member_not_exists_complex(topology_st):
    """Check the situation when we modify non-existing users member attribute

    :id: d222af17-17a6-48a0-8f22-a38306726a15
    :setup: Standalone instance,
            enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)
            - not grp015
            enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
            - not grp015
    :steps:
        1. Add member: enh1 and uniqueMember: enh1 to grp016
        2. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp15
           - grp16 (member uniquemember)
        3. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp15
           - not grp16
        4. Add member: dummy1 and uniqueMember: dummy2 to grp016
        5. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp15
           - grp16 (member uniquemember)
        6. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp15
           - not grp16
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')
    dummy1 = _get_user_dn('dummy1')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')
    memofegrp015 = _get_group_dn('memofegrp015')

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)

    memofegrp016 = _create_group(topology_st, 'memofegrp016')

    mods = [(ldap.MOD_ADD, 'member', memofenh1), (ldap.MOD_ADD, 'uniqueMember', memofenh1)]
    log.info("Update %s is memberof %s (member)" % (memofenh1, memofegrp016))
    log.info("Update %s is memberof %s (uniqueMember)" % (memofenh1, memofegrp016))
    topology_st.standalone.modify_s(ensure_str(memofegrp016), mods)

    time.sleep(delay)
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)

    mods = [(ldap.MOD_ADD, 'member', dummy1), ]
    log.info("Update %s is memberof %s (member)" % (dummy1, memofegrp016))
    topology_st.standalone.modify_s(ensure_str(memofegrp016), mods)

    ent = topology_st.standalone.getEntry(ensure_str(memofegrp016), ldap.SCOPE_BASE, "(objectclass=*)", ['member', 'uniqueMember'])
    assert ent.hasAttr('member')
    assert ensure_bytes(dummy1) in ent.getValues('member')
    assert ent.hasAttr('uniqueMember')
    assert ensure_bytes(dummy1) not in ent.getValues('uniqueMember')

    mods = [(ldap.MOD_ADD, 'uniqueMember', dummy1), ]
    log.info("Update %s is memberof %s (uniqueMember)" % (dummy1, memofegrp016))
    topology_st.standalone.modify_s(ensure_str(memofegrp016), mods)

    ent = topology_st.standalone.getEntry(ensure_str(memofegrp016), ldap.SCOPE_BASE, "(objectclass=*)", ['member', 'uniqueMember'])
    assert ent.hasAttr('member')
    assert ensure_bytes(dummy1) in ent.getValues('member')
    assert ent.hasAttr('uniqueMember')
    assert ensure_bytes(dummy1) in ent.getValues('uniqueMember')

    time.sleep(delay)
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)


def test_complex_group_scenario_1(topology_st):
    """Check the situation when user1 and user2 are memberof grp017
    user2 is member of grp017 but not with a memberof attribute (memberUid)

    :id: d222af17-17a6-48a0-8f22-a38306726a16
    :setup: Standalone instance, grp017,
            enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)
            - not grp015
            - grp016 (member uniquemember)
            enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
            - not grp015
            - not grp016
    :steps:
        1. Create user1 as grp17 (member)
        2. Create user2 as grp17 (uniqueMember)
        3. Create user3 as grp17 (memberuid) (not memberof attribute)
        4. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp15
           - grp16 (member uniquemember)
           - not grp17
        5. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp15
           - not grp16
           - not grp17
        6. Assert user1 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - grp17 (member)
        7. Assert user2 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - grp17 (uniqueMember)
        8. Assert user3 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - NOT grp17 (memberuid)
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')
    memofegrp015 = _get_group_dn('memofegrp015')
    memofegrp016 = _get_group_dn('memofegrp016')

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)

    #
    # create user1
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (member)
    #
    # create user2
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (uniqueMember)
    #
    # create user3
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (memberuid) (not memberof attribute)
    memofuser1 = _create_user(topology_st, 'memofuser1')
    memofuser2 = _create_user(topology_st, 'memofuser2')
    memofuser3 = _create_user(topology_st, 'memofuser3')
    memofegrp017 = _create_group(topology_st, 'memofegrp017')

    mods = [(ldap.MOD_ADD, 'member', memofuser1), (ldap.MOD_ADD, 'uniqueMember', memofuser2),
            (ldap.MOD_ADD, 'memberuid', memofuser3)]
    log.info("Update %s is memberof %s (member)" % (memofuser1, memofegrp017))
    log.info("Update %s is memberof %s (uniqueMember)" % (memofuser2, memofegrp017))
    log.info("Update %s is memberof %s (memberuid)" % (memofuser3, memofegrp017))
    topology_st.standalone.modify_s(ensure_str(memofegrp017), mods)

    time.sleep(delay)
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp17
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp017)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp17
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp017)

    # assert user1 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (member)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp016)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp017)

    # assert user2 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (uniqueMember)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp016)
    assert _check_memberof(topology_st, member=memofuser2, group=memofegrp017)

    # assert user3 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - NOT grp17 (memberuid)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp017)


def test_complex_group_scenario_2(topology_st):
    """Check the situation when user1 and user2 are memberof grp018
    user2 is member of grp018 but not with a memberof attribute (memberUid)

    :id: d222af17-17a6-48a0-8f22-a38306726a17
    :setup: Standalone instance, grp018,
            enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)
            - not grp015
            - grp016 (member uniquemember)
            - not grp17
            enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
            - not grp015
            - not grp016
            - not grp017
            user1 is member of
            - not grp1
            - not grp2
            - not grp3
            - not grp015
            - not grp016
            - grp017 (member)
            user2 is member of
            - not grp1
            - not grp2
            - not grp3
            - not grp015
            - not grp016
            - grp017 (uniquemember)
            user3 is member of
            - not grp1
            - not grp2
            - not grp3
            - not grp015
            - not grp016
            - not grp017 (memberuid)
    :steps:
        1. Add user1 as a member of grp18 (member, uniquemember)
        2. Assert user1 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - grp17 (member)
           - grp18 (member, uniquemember)
        3. Delete user1 member/uniquemember attributes from grp018
        4. Assert user1 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - grp17 (member)
           - NOT grp18 (memberUid)
        5. Delete user1, user2, user3, grp17 entries
        6. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp15
           - grp16 (member uniquemember)
           - not grp018
        7. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp15
           - not grp16
           - not grp018
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')
    memofuser1 = _get_user_dn('memofuser1')
    memofuser2 = _get_user_dn('memofuser2')
    memofuser3 = _get_user_dn('memofuser3')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')
    memofegrp015 = _get_group_dn('memofegrp015')
    memofegrp016 = _get_group_dn('memofegrp016')
    memofegrp017 = _get_group_dn('memofegrp017')

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp17
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp017)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp17
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp017)

    # assert user1 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (member)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp016)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp017)

    # assert user2 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (uniqueMember)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp016)
    assert _check_memberof(topology_st, member=memofuser2, group=memofegrp017)

    # assert user3 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - NOT grp17 (memberuid)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp017)

    #
    # Create a group grp018 with user1 member/uniquemember
    memofegrp018 = _create_group(topology_st, 'memofegrp018')

    mods = [(ldap.MOD_ADD, 'member', memofuser1), (ldap.MOD_ADD, 'uniqueMember', memofuser1),
            (ldap.MOD_ADD, 'memberuid', memofuser1)]
    log.info("Update %s is memberof %s (member)" % (memofuser1, memofegrp017))
    log.info("Update %s is memberof %s (uniqueMember)" % (memofuser1, memofegrp017))
    log.info("Update %s is memberof %s (memberuid)" % (memofuser1, memofegrp017))
    topology_st.standalone.modify_s(ensure_str(memofegrp018), mods)

    time.sleep(delay)
    # assert user1 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (member)
    #       - grp18 (member, uniquemember)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp016)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp017)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp018)

    mods = [(ldap.MOD_DELETE, 'member', memofuser1), (ldap.MOD_DELETE, 'uniqueMember', memofuser1)]
    log.info("Update %s is no longer memberof %s (member)" % (memofuser1, memofegrp018))
    log.info("Update %s is no longer memberof %s (uniqueMember)" % (memofuser1, memofegrp018))
    topology_st.standalone.modify_s(ensure_str(memofegrp018), mods)

    time.sleep(delay)
    # assert user1 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - grp17 (member)
    #       - NOT grp18 (memberUid)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp016)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp017)
    assert not _check_memberof(topology_st, member=memofuser1, group=memofegrp018)

    # DEL user1, user2, user3, grp17
    topology_st.standalone.delete_s(ensure_str(memofuser1))
    topology_st.standalone.delete_s(ensure_str(memofuser2))
    topology_st.standalone.delete_s(ensure_str(memofuser3))
    topology_st.standalone.delete_s(ensure_str(memofegrp017))

    time.sleep(delay)
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)


def test_complex_group_scenario_3(topology_st):
    """Test a complex memberOf case:
    Add user2 to grp19_2,
    Add user3 to grp19_3,
    Add grp19_2 and grp_19_3 to grp19_1

    :id: d222af17-17a6-48a0-8f22-a38306726a18
    :setup: Standalone instance,
            enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)
            - not grp015
            - grp016 (member uniquemember)
            - not grp018
            enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
            - not grp015
            - not grp016
            - not grp018
    :steps:
        1. Create user2 and user3
        2. Create a group grp019_2 with user2 member
        3. Create a group grp019_3 with user3 member
        4. Create a group grp019_1 with memofegrp019_2, memofegrp019_3 member
        5. Assert memofegrp019_1 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - not grp018
           - not grp19_1
           - not grp019_2
           - not grp019_3

        6. Assert memofegrp019_2 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - not grp018
           - grp19_1
           - not grp019_2
           - not grp019_3
        7. Assert memofegrp019_3 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - not grp018
           - grp19_1
           - not grp019_2
           - not grp019_3
        8. Assert memofuser2 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - not grp018
           - grp19_1
           - grp019_2
           - not grp019_3
        9. Assert memofuser3 is member of
           - not grp1
           - not grp2
           - not grp3
           - not grp15
           - not grp16
           - not grp018
           - grp19_1
           - not grp019_2
           - grp019_3
        10. Delete user2, user3, and all grp19* entries
        11. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp15
           - grp16 (member uniquemember)
           - not grp018
        12. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp15
           - not grp16
           - not grp018
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
        12. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')
    memofegrp015 = _get_group_dn('memofegrp015')
    memofegrp016 = _get_group_dn('memofegrp016')
    memofegrp018 = _get_group_dn('memofegrp018')

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)

    memofuser2 = _create_user(topology_st, 'memofuser2')
    memofuser3 = _create_user(topology_st, 'memofuser3')

    # Create a group grp019_2 with user2 member
    memofegrp019_2 = _create_group(topology_st, 'memofegrp019_2')
    mods = [(ldap.MOD_ADD, 'member', memofuser2)]
    topology_st.standalone.modify_s(ensure_str(memofegrp019_2), mods)

    # Create a group grp019_3 with user3 member
    memofegrp019_3 = _create_group(topology_st, 'memofegrp019_3')
    mods = [(ldap.MOD_ADD, 'member', memofuser3)]
    topology_st.standalone.modify_s(ensure_str(memofegrp019_3), mods)

    mods = [(ldap.MOD_ADD, 'objectClass', b'inetUser')]
    topology_st.standalone.modify_s(ensure_str(memofegrp019_2), mods)
    topology_st.standalone.modify_s(ensure_str(memofegrp019_3), mods)

    # Create a group grp019_1 with memofegrp019_2, memofegrp019_3 member
    memofegrp019_1 = _create_group(topology_st, 'memofegrp019_1')
    mods = [(ldap.MOD_ADD, 'member', memofegrp019_2), (ldap.MOD_ADD, 'member', memofegrp019_3)]
    topology_st.standalone.modify_s(ensure_str(memofegrp019_1), mods)

    time.sleep(delay)
    # assert memofegrp019_1 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - not grp19_1
    #       - not grp019_2
    #       - not grp019_3
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp019_1)
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp019_2)
    assert not _check_memberof(topology_st, member=memofegrp019_1, group=memofegrp019_3)

    # assert memofegrp019_2 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - grp19_1
    #       - not grp019_2
    #       - not grp019_3
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp018)
    assert _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp019_1)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp019_2)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp019_3)

    # assert memofegrp019_3 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - grp19_1
    #       - not grp019_2
    #       - not grp019_3
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp018)
    assert _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp019_1)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp019_2)
    assert not _check_memberof(topology_st, member=memofegrp019_2, group=memofegrp019_3)

    # assert memofuser2 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - grp19_1
    #       - grp019_2
    #       - not grp019_3
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp018)
    assert _check_memberof(topology_st, member=memofuser2, group=memofegrp019_1)
    assert _check_memberof(topology_st, member=memofuser2, group=memofegrp019_2)
    assert not _check_memberof(topology_st, member=memofuser2, group=memofegrp019_3)

    # assert memofuser3 is member of
    #       - not grp1
    #       - not grp2
    #       - not grp3
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - grp19_1
    #       - not grp019_2
    #       - grp019_3
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp2)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp018)
    assert _check_memberof(topology_st, member=memofuser3, group=memofegrp019_1)
    assert not _check_memberof(topology_st, member=memofuser3, group=memofegrp019_2)
    assert _check_memberof(topology_st, member=memofuser3, group=memofegrp019_3)

    # DEL user2, user3, grp19*
    topology_st.standalone.delete_s(ensure_str(memofuser2))
    topology_st.standalone.delete_s(ensure_str(memofuser3))
    topology_st.standalone.delete_s(ensure_str(memofegrp019_1))
    topology_st.standalone.delete_s(ensure_str(memofegrp019_2))
    topology_st.standalone.delete_s(ensure_str(memofegrp019_3))

    time.sleep(delay)
    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)


def test_complex_group_scenario_4(topology_st):
    """Test a complex memberOf case:
    Add user1 and grp[1-5]
    Add user1 member of grp[1-4]
    Add grp[1-4] member of grp5
    Check user1 is member of grp[1-5]

    :id: d223af17-17a6-48a0-8f22-a38306726a19
    :setup: Standalone instance,
            enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)
            - not grp015
            - grp016 (member uniquemember)
            - not grp018
           enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
            - not grp015
            - not grp016
            - not grp018
    :steps:
        1. Create user1
        2. Create grp[1-5] that can be inetUser (having memberof)
        3. Add user1 to grp[1-4] (uniqueMember)
        4. Create grp5 with grp[1-4] as member
        5. Assert user1 is a member grp[1-5]
        6. Delete user1 and all grp20 entries
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')
    memofegrp015 = _get_group_dn('memofegrp015')
    memofegrp016 = _get_group_dn('memofegrp016')
    memofegrp018 = _get_group_dn('memofegrp018')

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)

    # create user1
    memofuser1 = _create_user(topology_st, 'memofuser1')

    # create grp[1-5] that can be inetUser (having memberof)
    memofegrp020_1 = _create_group(topology_st, 'memofegrp020_1')
    memofegrp020_2 = _create_group(topology_st, 'memofegrp020_2')
    memofegrp020_3 = _create_group(topology_st, 'memofegrp020_3')
    memofegrp020_4 = _create_group(topology_st, 'memofegrp020_4')
    memofegrp020_5 = _create_group(topology_st, 'memofegrp020_5')
    mods = [(ldap.MOD_ADD, 'objectClass', b'inetUser')]
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5]:
        topology_st.standalone.modify_s(ensure_str(grp), mods)

    # add user1 to grp[1-4] (uniqueMember)
    mods = [(ldap.MOD_ADD, 'uniqueMember', memofuser1)]
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        topology_st.standalone.modify_s(ensure_str(grp), mods)

    # create grp5 with grp[1-4] as member
    mods = []
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        mods.append((ldap.MOD_ADD, 'member', grp))
    topology_st.standalone.modify_s(ensure_str(memofegrp020_5), mods)

    time.sleep(delay)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_1)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_2)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_3)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_4)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_5)

    # DEL user1, grp20*
    topology_st.standalone.delete_s(ensure_str(memofuser1))
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5]:
        topology_st.standalone.delete_s(ensure_str(grp))


def test_complex_group_scenario_5(topology_st):
    """Test a complex memberOf case:
    Add user[1-4] and Grp[1-4]
    Add userX as uniquemember of GrpX
    Add Grp5
        Grp[1-4] as members of Grp5
        user1 as member of Grp5
    Check that user1 is member of Grp1 and Grp5
    Check that user* are members of Grp5

    :id: d222af17-17a6-48a0-8f22-a38306726a20
    :setup: Standalone instance,
            enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)
            - not grp015
            - grp016 (member uniquemember)
            - not grp018
           enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
            - not grp015
            - not grp016
            - not grp018
    :steps:
        1. Create user1-4
        2. Create grp[1-4] that can be inetUser (having memberof)
        3. Add userX (uniquemember) to grpX
        4. Create grp5 with grp[1-4] as member + user1
        5. Assert user[1-4] are member of grp20_5
        6. Assert userX is uniqueMember of grpX
        7. Check that user[1-4] is only 'uniqueMember' of the grp20_[1-4]
        8. Check that grp20_[1-4] are only 'member' of grp20_5
        9. Check that user1 are only 'member' of grp20_5
        10. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp15
           - grp16 (member uniquemember)
           - not grp018
           - not grp20*
        11. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp15
           - not grp16
           - not grp018
           - not grp20*
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')
    memofegrp015 = _get_group_dn('memofegrp015')
    memofegrp016 = _get_group_dn('memofegrp016')
    memofegrp018 = _get_group_dn('memofegrp018')

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)

    # create user1-4
    memofuser1 = _create_user(topology_st, 'memofuser1')
    memofuser2 = _create_user(topology_st, 'memofuser2')
    memofuser3 = _create_user(topology_st, 'memofuser3')
    memofuser4 = _create_user(topology_st, 'memofuser4')

    # create grp[1-4] that can be inetUser (having memberof)
    # add userX (uniquemember) to grpX
    memofegrp020_1 = _create_group(topology_st, 'memofegrp020_1')
    memofegrp020_2 = _create_group(topology_st, 'memofegrp020_2')
    memofegrp020_3 = _create_group(topology_st, 'memofegrp020_3')
    memofegrp020_4 = _create_group(topology_st, 'memofegrp020_4')
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        mods = [(ldap.MOD_ADD, 'objectClass', b'inetUser'), (ldap.MOD_ADD, 'uniqueMember', x[1])]
        topology_st.standalone.modify_s(ensure_str(x[0]), mods)

    # create grp5 with grp[1-4] as member + user1
    memofegrp020_5 = _create_group(topology_st, 'memofegrp020_5')
    mods = [(ldap.MOD_ADD, 'member', memofuser1)]
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        mods.append((ldap.MOD_ADD, 'member', grp))
    topology_st.standalone.modify_s(ensure_str(memofegrp020_5), mods)

    time.sleep(delay)
    # assert user[1-4] are member of grp20_5
    for user in [memofuser1, memofuser2, memofuser3, memofuser4]:
        assert _check_memberof(topology_st, member=user, group=memofegrp020_5)

    # assert userX is uniqueMember of grpX
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_1)
    assert _check_memberof(topology_st, member=memofuser2, group=memofegrp020_2)
    assert _check_memberof(topology_st, member=memofuser3, group=memofegrp020_3)
    assert _check_memberof(topology_st, member=memofuser4, group=memofegrp020_4)

    # check that user[1-4] is only 'uniqueMember' of the grp20_[1-4]
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        assert _check_memberattr(topology_st, x[0], 'uniqueMember', x[1])
        assert not _check_memberattr(topology_st, x[0], 'member', x[1])
    # check that grp20_[1-4] are only 'member' of grp20_5
    # check that user1 are only 'member' of grp20_5
    for x in [memofuser1, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberattr(topology_st, memofegrp020_5, 'member', x)
        assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', x)

    for user in [memofuser2, memofuser3, memofuser4]:
        assert not _check_memberattr(topology_st, memofegrp020_5, 'member', user)
        assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', user)

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    #       - not grp20*
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_2)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_4)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_5)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - not grp20*
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_1)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_2)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_4)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_5)


def test_complex_group_scenario_6(topology_st):
    """Test a complex memberOf case:
    add userX as member/uniqueMember of GrpX
    add Grp5 as uniquemember of GrpX (this create a loop)

    :id: d222af17-17a6-48a0-8f22-a38306726a21
    :setup: Standalone instance
            enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)
            - not grp15
            - grp16 (member uniquemember)
            - not grp018
            - not grp20*
            enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
            - not grp15
            - not grp16
            - not grp018
            - not grp20*
            user1 is member of grp20_5
            userX is uniquemember of grp20_X
            grp[1-4] are member of grp20_5
    :steps:
        1. Add user[1-4] (member) to grp020_[1-4]
        2. Check that user[1-4] are 'member' and 'uniqueMember' of the grp20_[1-4]
        3. Add Grp[1-4] (uniqueMember) to grp5
        4. Assert user[1-4] are member of grp20_[1-4]
        5. Assert that all groups are members of each others because Grp5 is member of all grp20_[1-4]
        6. Assert user[1-5] is uniqueMember of grp[1-5]
        7. Assert enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp15
           - grp16 (member uniquemember)
           - not grp018
           - not grp20*
        8. Assert enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp15
           - not grp16
           - not grp018
           - not grp20*
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')
    memofegrp015 = _get_group_dn('memofegrp015')
    memofegrp016 = _get_group_dn('memofegrp016')
    memofegrp018 = _get_group_dn('memofegrp018')

    memofuser1 = _get_user_dn('memofuser1')
    memofuser2 = _get_user_dn('memofuser2')
    memofuser3 = _get_user_dn('memofuser3')
    memofuser4 = _get_user_dn('memofuser4')

    memofegrp020_1 = _get_group_dn('memofegrp020_1')
    memofegrp020_2 = _get_group_dn('memofegrp020_2')
    memofegrp020_3 = _get_group_dn('memofegrp020_3')
    memofegrp020_4 = _get_group_dn('memofegrp020_4')
    memofegrp020_5 = _get_group_dn('memofegrp020_5')

    # assert user[1-4] are member of grp20_5
    for user in [memofuser1, memofuser2, memofuser3, memofuser4]:
        assert _check_memberof(topology_st, member=user, group=memofegrp020_5)

    # assert userX is member of grpX
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_1)
    assert _check_memberof(topology_st, member=memofuser2, group=memofegrp020_2)
    assert _check_memberof(topology_st, member=memofuser3, group=memofegrp020_3)
    assert _check_memberof(topology_st, member=memofuser4, group=memofegrp020_4)

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    #       - not grp20*
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_2)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_4)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_5)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - not grp20*
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_1)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_2)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_4)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_5)

    # check that user[1-4] is only 'uniqueMember' of the grp20_[1-4]
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        assert _check_memberattr(topology_st, x[0], 'uniqueMember', x[1])
        assert not _check_memberattr(topology_st, x[0], 'member', x[1])

    # check that grp20_[1-4] are only 'member' of grp20_5
    # check that user1 is only 'member' of grp20_5
    for x in [memofuser1, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberattr(topology_st, memofegrp020_5, 'member', x)
        assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', x)

    # check that user2-4 are neither 'member' nor 'uniquemember' of grp20_5
    for user in [memofuser2, memofuser3, memofuser4]:
        assert not _check_memberattr(topology_st, memofegrp020_5, 'member', user)
        assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', user)

    # add userX (member) to grpX
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        mods = [(ldap.MOD_ADD, 'member', x[1])]
        topology_st.standalone.modify_s(ensure_str(x[0]), mods)

    time.sleep(delay)
    # check that user[1-4] are 'member' and 'uniqueMember' of the grp20_[1-4]
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        assert _check_memberattr(topology_st, x[0], 'uniqueMember', x[1])
        assert _check_memberattr(topology_st, x[0], 'member', x[1])

    # add Grp[1-4] (uniqueMember) to grp5
    # it creates a membership loop !!!
    mods = [(ldap.MOD_ADD, 'uniqueMember', memofegrp020_5)]
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        topology_st.standalone.modify_s(ensure_str(grp), mods)

    time.sleep(5)
    # assert user[1-4] are member of grp20_[1-4]
    for user in [memofuser1, memofuser2, memofuser3, memofuser4]:
        assert _check_memberof(topology_st, member=user, group=memofegrp020_5)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_4)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_3)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_2)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_1)

    # assert that all groups are members of each others because Grp5
    # is member of all grp20_[1-4]
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        for owner in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
            if grp == owner:
                # no member of itself
                assert not _check_memberof(topology_st, member=grp, group=owner)
            else:
                assert _check_memberof(topology_st, member=grp, group=owner)
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberof(topology_st, member=grp, group=memofegrp020_5)

    # assert userX is uniqueMember of grpX
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_1)
    assert _check_memberof(topology_st, member=memofuser2, group=memofegrp020_2)
    assert _check_memberof(topology_st, member=memofuser3, group=memofegrp020_3)
    assert _check_memberof(topology_st, member=memofuser4, group=memofegrp020_4)
    assert _check_memberof(topology_st, member=memofuser4, group=memofegrp020_5)

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    #       - not grp20*
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_2)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_4)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_5)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - not grp20*
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_1)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_2)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_4)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_5)


def verify_post_023(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4):
    """
          /----member ---> G1 ---uniqueMember -------\
         /                                            V
    G5 ------------------------>member ----------  --->U1
         |
         |----member ---> G2 ---member/uniqueMember -> U2
         |<--uniquemember-/
         |
         |----member ---> G3 ---member/uniqueMember -> U3
         |<--uniquemember-/
         |----member ---> G4 ---member/uniqueMember -> U4
         |<--uniquemember-/
    """
    for x in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberattr(topology_st, memofegrp020_5, 'member', x)
        assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', x)
    for x in [memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert not _check_memberattr(topology_st, x, 'member', memofegrp020_5)
        assert _check_memberattr(topology_st, x, 'uniqueMember', memofegrp020_5)
    # check that user[1-4] is only 'uniqueMember' of the grp20_[1-4]
    for x in [(memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        assert _check_memberattr(topology_st, x[0], 'uniqueMember', x[1])
        assert _check_memberattr(topology_st, x[0], 'member', x[1])
    assert _check_memberattr(topology_st, memofegrp020_1, 'uniqueMember', memofuser1)
    assert not _check_memberattr(topology_st, memofegrp020_1, 'member', memofuser1)
    assert not _check_memberattr(topology_st, memofegrp020_1, 'uniqueMember', memofegrp020_5)
    assert not _check_memberattr(topology_st, memofegrp020_1, 'member', memofegrp020_5)

    for x in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofuser1, memofuser2, memofuser3,
              memofuser4]:
        assert _check_memberof(topology_st, member=x, group=memofegrp020_5)
    for x in [memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberof(topology_st, member=memofegrp020_5, group=x)

    for user in [memofuser1, memofuser2, memofuser3, memofuser4]:
        assert _check_memberof(topology_st, member=user, group=memofegrp020_5)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_4)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_3)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_2)
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5]:
        assert _check_memberof(topology_st, member=memofuser1, group=grp)


def test_complex_group_scenario_7(topology_st):
    """Check the user removal from the complex membership topology

    :id: d222af17-17a6-48a0-8f22-a38306726a22
    :setup: Standalone instance,
           enh1 is member of
           - grp1 (member)
           - not grp2
           - grp3 (uniquemember)
           - not grp15
           - grp16 (member uniquemember)
           - not grp018
           - not grp20*
            enh2 is member of
           - not grp1
           - grp2 (uniquemember)
           - grp3 (member)
           - not grp15
           - not grp16
           - not grp018
           - not grp20*
        grp[1-4] are member of grp20_5
        user1 is member (member) of group_5
        grp5 is uniqueMember of grp20_[1-4]
        user[1-4] is member/uniquemember of grp20_[1-4]
    :steps:
        1. Delete user1 as 'member' of grp20_1
        2. Delete grp020_5 as 'uniqueMember' of grp20_1
        3. Check the result membership
    :expectedresults:
        1. Success
        2. Success
        3. The result should be like this

            ::

                      /----member ---> G1 ---uniqueMember -------\
                     /                                            V
                G5 ------------------------>member ----------  --->U1
                     |
                     |----member ---> G2 ---member/uniqueMember -> U2
                     |<--uniquemember-/
                     |
                     |----member ---> G3 ---member/uniqueMember -> U3
                     |<--uniquemember-/
                     |----member ---> G4 ---member/uniqueMember -> U4
                     |<--uniquemember-/

    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    memofegrp3 = _get_group_dn('memofegrp3')
    memofegrp015 = _get_group_dn('memofegrp015')
    memofegrp016 = _get_group_dn('memofegrp016')
    memofegrp018 = _get_group_dn('memofegrp018')

    memofuser1 = _get_user_dn('memofuser1')
    memofuser2 = _get_user_dn('memofuser2')
    memofuser3 = _get_user_dn('memofuser3')
    memofuser4 = _get_user_dn('memofuser4')

    memofegrp020_1 = _get_group_dn('memofegrp020_1')
    memofegrp020_2 = _get_group_dn('memofegrp020_2')
    memofegrp020_3 = _get_group_dn('memofegrp020_3')
    memofegrp020_4 = _get_group_dn('memofegrp020_4')
    memofegrp020_5 = _get_group_dn('memofegrp020_5')

    # assert user[1-4] are member of grp20_[1-4]
    for user in [memofuser1, memofuser2, memofuser3, memofuser4]:
        assert _check_memberof(topology_st, member=user, group=memofegrp020_5)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_4)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_3)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_2)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_1)

    # assert that all groups are members of each others because Grp5
    # is member of all grp20_[1-4]
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        for owner in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
            if grp == owner:
                # no member of itself
                assert not _check_memberof(topology_st, member=grp, group=owner)
            else:
                assert _check_memberof(topology_st, member=grp, group=owner)
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberof(topology_st, member=grp, group=memofegrp020_5)

    # assert userX is uniqueMember of grpX
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_1)
    assert _check_memberof(topology_st, member=memofuser2, group=memofegrp020_2)
    assert _check_memberof(topology_st, member=memofuser3, group=memofegrp020_3)
    assert _check_memberof(topology_st, member=memofuser4, group=memofegrp020_4)
    assert _check_memberof(topology_st, member=memofuser4, group=memofegrp020_5)

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    #       - not grp15
    #       - grp16 (member uniquemember)
    #       - not grp018
    #       - not grp20*
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp015)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_2)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_3)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_4)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp020_5)

    # assert enh2 is member of
    #       - not grp1
    #       - grp2 (uniquemember)
    #       - grp3 (member)
    #       - not grp15
    #       - not grp16
    #       - not grp018
    #       - not grp20*
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp015)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp016)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp018)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_1)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_2)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_3)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_4)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp020_5)

    # check that user[1-4] is only 'uniqueMember' of the grp20_[1-4]
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        assert _check_memberattr(topology_st, x[0], 'uniqueMember', x[1])
        assert _check_memberattr(topology_st, x[0], 'member', x[1])

    # check that grp20_[1-4] are 'uniqueMember' and 'member' of grp20_5
    # check that user1 is only 'member' of grp20_5
    for x in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberattr(topology_st, memofegrp020_5, 'member', x)
        assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', x)
    assert _check_memberattr(topology_st, memofegrp020_5, 'member', memofuser1)
    assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', memofuser1)

    # DEL user1 as 'member' of grp20_1
    mods = [(ldap.MOD_DELETE, 'member', memofuser1)]
    topology_st.standalone.modify_s(ensure_str(memofegrp020_1), mods)

    mods = [(ldap.MOD_DELETE, 'uniqueMember', memofegrp020_5)]
    topology_st.standalone.modify_s(ensure_str(memofegrp020_1), mods)

    """
          /----member ---> G1 ---uniqueMember -------\
         /                                            V
    G5 ------------------------>member ----------  --->U1
         | 
         |----member ---> G2 ---member/uniqueMember -> U2
         |<--uniquemember-/
         |
         |----member ---> G3 ---member/uniqueMember -> U3
         |<--uniquemember-/                              
         |----member ---> G4 ---member/uniqueMember -> U4
         |<--uniquemember-/  
    """
    time.sleep(delay)
    verify_post_023(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4)


def verify_post_024(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4):
    """
          /----member ---> G1 ---member/uniqueMember -\
         /                                             V
    G5 ------------------------>member ----------  --->U1
         |
         |----member ---> G2 ---member/uniqueMember -> U2
         |<--uniquemember-/
         |
         |----member ---> G3 ---member/uniqueMember -> U3
         |<--uniquemember-/
         |----member ---> G4 ---member/uniqueMember -> U4
         |<--uniquemember-/
    """
    for x in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberattr(topology_st, memofegrp020_5, 'member', x)
        assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', x)
    for x in [memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert not _check_memberattr(topology_st, x, 'member', memofegrp020_5)
        assert _check_memberattr(topology_st, x, 'uniqueMember', memofegrp020_5)
    # check that user[1-4] is only 'uniqueMember' of the grp20_[1-4]
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        assert _check_memberattr(topology_st, x[0], 'uniqueMember', x[1])
        assert _check_memberattr(topology_st, x[0], 'member', x[1])
    assert not _check_memberattr(topology_st, memofegrp020_1, 'uniqueMember', memofegrp020_5)
    assert not _check_memberattr(topology_st, memofegrp020_1, 'member', memofegrp020_5)

    for x in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofuser1, memofuser2, memofuser3,
              memofuser4]:
        assert _check_memberof(topology_st, member=x, group=memofegrp020_5)
    for x in [memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberof(topology_st, member=memofegrp020_5, group=x)

    for user in [memofuser1, memofuser2, memofuser3, memofuser4]:
        assert _check_memberof(topology_st, member=user, group=memofegrp020_5)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_4)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_3)
        assert _check_memberof(topology_st, member=user, group=memofegrp020_2)
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5]:
        assert _check_memberof(topology_st, member=memofuser1, group=grp)


def test_complex_group_scenario_8(topology_st):
    """Check the user add operation to the complex membership topology

    :id: d222af17-17a6-48a0-8f22-a38306726a23
    :setup: Standalone instance,

        ::

                  /----member ---> G1 ---uniqueMember -------\
                 /                                            V
            G5 ------------------------>member ----------  --->U1
                 |
                 |----member ---> G2 ---member/uniqueMember -> U2
                 |<--uniquemember-/
                 |
                 |----member ---> G3 ---member/uniqueMember -> U3
                 |<--uniquemember-/
                 |----member ---> G4 ---member/uniqueMember -> U4
                 |<--uniquemember-/

    :steps:
        1. Add user1 to grp020_1
        2. Check the result membership
    :expectedresults:
        1. Success
        2. The result should be like this

            ::

                      /----member ---> G1 ---member/uniqueMember -\
                     /                                             V
                G5 ------------------------>member ----------  --->U1
                     |
                     |----member ---> G2 ---member/uniqueMember -> U2
                     |<--uniquemember-/
                     |
                     |----member ---> G3 ---member/uniqueMember -> U3
                     |<--uniquemember-/
                     |----member ---> G4 ---member/uniqueMember -> U4
                     |<--uniquemember-/

    """
    delay = _memberof_checking_delay(topology_st.standalone)

    memofuser1 = _get_user_dn('memofuser1')
    memofuser2 = _get_user_dn('memofuser2')
    memofuser3 = _get_user_dn('memofuser3')
    memofuser4 = _get_user_dn('memofuser4')

    memofegrp020_1 = _get_group_dn('memofegrp020_1')
    memofegrp020_2 = _get_group_dn('memofegrp020_2')
    memofegrp020_3 = _get_group_dn('memofegrp020_3')
    memofegrp020_4 = _get_group_dn('memofegrp020_4')
    memofegrp020_5 = _get_group_dn('memofegrp020_5')
    verify_post_023(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4)

    # ADD user1 as 'member' of grp20_1
    mods = [(ldap.MOD_ADD, 'member', memofuser1)]
    topology_st.standalone.modify_s(ensure_str(memofegrp020_1), mods)

    time.sleep(delay)
    verify_post_024(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4)


def verify_post_025(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4):
    """
          /----member ---> G1
         /
    G5 ------------------------>member ----------  --->U1
         |
         |----member ---> G2
         |----member ---> G3
         |----member ---> G4

    """
    for x in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert _check_memberattr(topology_st, memofegrp020_5, 'member', x)
        assert not _check_memberattr(topology_st, memofegrp020_5, 'uniqueMember', x)
    for x in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        assert not _check_memberattr(topology_st, x, 'member', memofegrp020_5)
        assert not _check_memberattr(topology_st, x, 'uniqueMember', memofegrp020_5)
    # check that user[1-4] is only 'uniqueMember' of the grp20_[1-4]
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        assert not _check_memberattr(topology_st, x[0], 'uniqueMember', x[1])
        assert not _check_memberattr(topology_st, x[0], 'member', x[1])

    for x in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofuser1]:
        assert _check_memberof(topology_st, member=x, group=memofegrp020_5)
    for x in [memofuser2, memofuser3, memofuser4]:
        assert not _check_memberof(topology_st, member=x, group=memofegrp020_5)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_5)
    for user in [memofuser1, memofuser2, memofuser3, memofuser4]:
        for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4]:
            assert not _check_memberof(topology_st, member=user, group=grp)


def test_complex_group_scenario_9(topology_st):
    """Check the massive user deletion from the complex membership topology

    :id: d222af17-17a6-48a0-8f22-a38306726a24
    :setup: Standalone instance,

        ::

                  /----member ---> G1 ---member/uniqueMember -\
                 /                                             V
            G5 ------------------------>member ----------  --->U1
                 |
                 |----member ---> G2 ---member/uniqueMember -> U2
                 |<--uniquemember-/
                 |
                 |----member ---> G3 ---member/uniqueMember -> U3
                 |<--uniquemember-/
                 |----member ---> G4 ---member/uniqueMember -> U4
                 |<--uniquemember-/

    :steps:
        1. Delete user[1-5] as 'member' and 'uniqueMember' from grp20_[1-5]
        2. Check the result membership
    :expectedresults:
        1. Success
        2. The result should be like this

            ::

                      /----member ---> G1
                     /
                G5 ------------------------>member ----------  --->U1
                     |
                     |----member ---> G2
                     |----member ---> G3
                     |----member ---> G4

    """
    memberof = MemberOfPlugin(topology_st.standalone)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0

    memofuser1 = _get_user_dn('memofuser1')
    memofuser2 = _get_user_dn('memofuser2')
    memofuser3 = _get_user_dn('memofuser3')
    memofuser4 = _get_user_dn('memofuser4')

    memofegrp020_1 = _get_group_dn('memofegrp020_1')
    memofegrp020_2 = _get_group_dn('memofegrp020_2')
    memofegrp020_3 = _get_group_dn('memofegrp020_3')
    memofegrp020_4 = _get_group_dn('memofegrp020_4')
    memofegrp020_5 = _get_group_dn('memofegrp020_5')
    verify_post_024(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4)

    # ADD inet
    # for user in [memofuser1, memofuser2, memofuser3, memofuser4]:
    #    mods = [(ldap.MOD_ADD, 'objectClass', 'inetUser')]
    #    topology_st.standalone.modify_s(user, mods)
    for x in [(memofegrp020_1, memofuser1),
              (memofegrp020_2, memofuser2),
              (memofegrp020_3, memofuser3),
              (memofegrp020_4, memofuser4)]:
        mods = [(ldap.MOD_DELETE, 'member', x[1]),
                (ldap.MOD_DELETE, 'uniqueMember', x[1])]
        topology_st.standalone.modify_s(ensure_str(x[0]), mods)
    """
          /----member ---> G1 
         /                                             
    G5 ------------------------>member ----------  --->U1
         | 
         |----member ---> G2
         |<--uniquemember-/
         |
         |----member ---> G3
         |<--uniquemember-/                              
         |----member ---> G4
         |<--uniquemember-/  
    """

    for x in [memofegrp020_2, memofegrp020_3, memofegrp020_4]:
        mods = [(ldap.MOD_DELETE, 'uniqueMember', memofegrp020_5)]
        topology_st.standalone.modify_s(ensure_str(x), mods)
    """
          /----member ---> G1 
         /                                             
    G5 ------------------------>member ----------  --->U1
         | 
         |----member ---> G2
         |----member ---> G3                            
         |----member ---> G4

    """

    time.sleep(delay)
    verify_post_025(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4)

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
def test_memberof_auto_add_oc(topology_st):
    """Test the auto add objectclass (OC) feature. The plugin should add a predefined
    objectclass that will allow memberOf to be added to an entry.

    :id: d222af17-17a6-48a0-8f22-a38306726a25
    :setup: Standalone instance
    :steps:
        1. Enable dynamic plugins
        2. Enable memberOf plugin
        3. Test that the default add OC works.
        4. Add a group that already includes one user
        5. Assert memberOf on user1
        6. Delete user1 and the group
        7. Test invalid value (config validation)
        8. Add valid objectclass
        9. Add two users
        10. Add a group that already includes one user
        11. Add a user to the group
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
    """

    memberof = MemberOfPlugin(topology_st.standalone)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0

    # enable dynamic plugins
    try:
        topology_st.standalone.modify_s(DN_CONFIG,
                                        [(ldap.MOD_REPLACE,
                                          'nsslapd-dynamic-plugins',
                                          b'on')])
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugins! ' + e.message['desc'])
        assert False

    # Enable the plugin
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)

    # Test that the default add OC works.

    try:
        topology_st.standalone.add_s(Entry((USER1_DN,
                                            {'objectclass': 'top',
                                             'objectclass': 'person',
                                             'objectclass': 'organizationalPerson',
                                             'objectclass': 'inetorgperson',
                                             'sn': 'last',
                                             'cn': 'full',
                                             'givenname': 'user1',
                                             'uid': 'user1'
                                             })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    # Add a group(that already includes one user
    try:
        topology_st.standalone.add_s(Entry((GROUP_DN,
                                            {'objectclass': 'top',
                                             'objectclass': 'groupOfNames',
                                             'cn': 'group',
                                             'member': USER1_DN
                                             })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add group entry, error: ' + e.message['desc'])
        assert False

    time.sleep(delay)
    # Assert memberOf on user1
    _check_memberof(topology_st, USER1_DN, GROUP_DN)

    # Reset for the next test ....
    topology_st.standalone.delete_s(USER1_DN)
    topology_st.standalone.delete_s(GROUP_DN)

    # Test invalid value (config validation)
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    try:
        topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                        [(ldap.MOD_REPLACE,
                                          'memberofAutoAddOC',
                                          b'invalid123')])
        log.fatal('Incorrectly added invalid objectclass!')
        assert False
    except ldap.UNWILLING_TO_PERFORM:
        log.info('Correctly rejected invalid objectclass')
    except ldap.LDAPError as e:
        ldap.error('Unexpected error adding invalid objectclass - error: ' + e.message['desc'])
        assert False


    # Add valid objectclass
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    try:
        topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                        [(ldap.MOD_REPLACE,
                                          'memberofAutoAddOC',
                                          b'inetuser')])
    except ldap.LDAPError as e:
        log.fatal('Failed to configure memberOf plugin: error ' + e.message['desc'])
        assert False

    # Add two users
    try:
        topology_st.standalone.add_s(Entry((USER1_DN,
                                            {'objectclass': 'top',
                                             'objectclass': 'person',
                                             'objectclass': 'organizationalPerson',
                                             'objectclass': 'inetorgperson',
                                             'sn': 'last',
                                             'cn': 'full',
                                             'givenname': 'user1',
                                             'uid': 'user1'
                                             })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add user1 entry, error: ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN,
                                            {'objectclass': 'top',
                                             'objectclass': 'person',
                                             'objectclass': 'organizationalPerson',
                                             'objectclass': 'inetorgperson',
                                             'sn': 'last',
                                             'cn': 'full',
                                             'givenname': 'user2',
                                             'uid': 'user2'
                                             })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add user2 entry, error: ' + e.message['desc'])
        assert False

    # Add a group(that already includes one user
    try:
        topology_st.standalone.add_s(Entry((GROUP_DN,
                                            {'objectclass': 'top',
                                             'objectclass': 'groupOfNames',
                                             'cn': 'group',
                                             'member': USER1_DN
                                             })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add group entry, error: ' + e.message['desc'])
        assert False

    # Add a user to the group
    try:
        topology_st.standalone.modify_s(GROUP_DN,
                                        [(ldap.MOD_ADD,
                                          'member',
                                          ensure_bytes(USER2_DN))])
    except ldap.LDAPError as e:
        log.fatal('Failed to add user2 to group: error ' + e.message['desc'])
        assert False

    log.info('Test complete.')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
