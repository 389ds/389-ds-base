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


def text_memberof_683241_01(topology_st):
    """
    Test Modify the memberof plugin to use the new type
    """
    topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                    [(ldap.MOD_REPLACE,
                                      PLUGIN_TYPE,
                                      b'betxnpostoperation')])
    topology_st.standalone.restart()
    ent = topology_st.standalone.getEntry(MEMBEROF_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)", [PLUGIN_TYPE])
    assert ent.hasAttr(PLUGIN_TYPE)
    assert ent.getValue(PLUGIN_TYPE) == 'betxnpostoperation'


def text_memberof_683241_01(topology_st):
    """
    Test Modify the memberof plugin to use the new type
    """
    topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                    [(ldap.MOD_REPLACE,
                                      PLUGIN_TYPE,
                                      b'betxnpostoperation')])
    topology_st.standalone.restart()
    ent = topology_st.standalone.getEntry(MEMBEROF_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)", [PLUGIN_TYPE])
    assert ent.hasAttr(PLUGIN_TYPE)
    assert ent.getValue(PLUGIN_TYPE) == 'betxnpostoperation'


def test_memberof_MultiGrpAttr_001(topology_st):
    """
    Checking multiple grouping attributes supported
    """
    _set_memberofgroupattr_add(topology_st, 'uniqueMember')
    ent = topology_st.standalone.getEntry(MEMBEROF_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)",
                                          [PLUGIN_MEMBEROF_GRP_ATTR])
    assert ent.hasAttr(PLUGIN_MEMBEROF_GRP_ATTR)
    assert b'member'.lower() in [x.lower() for x in ent.getValues(PLUGIN_MEMBEROF_GRP_ATTR)]
    assert b'uniqueMember'.lower() in [x.lower() for x in ent.getValues(PLUGIN_MEMBEROF_GRP_ATTR)]


def test_memberof_MultiGrpAttr_003(topology_st):
    """
    Check the plug-in is started
    """
    log.info("Enable MemberOf plugin")
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_st.standalone.restart()
    ent = topology_st.standalone.getEntry(MEMBEROF_PLUGIN_DN, ldap.SCOPE_BASE, "(objectclass=*)", [PLUGIN_ENABLED])
    assert ent.hasAttr(PLUGIN_ENABLED)
    assert ent.getValue(PLUGIN_ENABLED).lower() == b'on'


def test_memberof_MultiGrpAttr_004(topology_st):
    """
    MemberOf attribute should be successfully added to both the users
    """
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

    # assert enh1 is member of grp1 and grp2
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is member of grp1 and grp2
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


def test_memberof_MultiGrpAttr_005(topology_st):
    """
    Partial removal of memberofgroupattr: removing member attribute from Group1
    """
    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')
    log.info("Update %s is no longer memberof %s (member)" % (memofenh1, memofegrp1))
    mods = [(ldap.MOD_DELETE, 'member', memofenh1)]
    topology_st.standalone.modify_s(ensure_str(memofegrp1), mods)

    # assert enh1 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is member of grp1 and  is member of grp2
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


def test_memberof_MultiGrpAttr_006(topology_st):
    """
    Partial removal of memberofgroupattr: removing uniqueMember attribute from Group2
    """
    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')

    log.info("Update %s is no longer memberof %s (uniqueMember)" % (memofenh1, memofegrp1))
    mods = [(ldap.MOD_DELETE, 'uniqueMember', memofenh2)]
    topology_st.standalone.modify_s(ensure_str(memofegrp2), mods)

    # assert enh1 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is member of grp1 and  is NOT member of grp2
    assert _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


def test_memberof_MultiGrpAttr_007(topology_st):
    """
    Complete removal of memberofgroupattr
    """
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

    # assert enh1 is NOT member of grp1 and  is member of grp2
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)

    # assert enh2 is member of grp1 and  is NOT member of grp2
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh2, group=memofegrp2)


def test_memberof_MultiGrpAttr_008(topology_st):
    """
    MemberOf attribute should be present on both the users
    """
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


def test_memberof_MultiGrpAttr_009(topology_st):
    """
    MemberOf attribute should not be added to the user since memberuid is not a DN syntax attribute
    """
    try:
        _set_memberofgroupattr_add(topology_st, 'memberUid')
        log.error("Setting 'memberUid' as memberofgroupattr should be rejected")
        assert False
    except ldap.UNWILLING_TO_PERFORM:
        log.error("Setting 'memberUid' as memberofgroupattr is rejected (expected)")
        assert True


def test_memberof_MultiGrpAttr_010(topology_st):
    """
    Duplicate member attribute to groups
    """

    memofenh1 = _get_user_dn('memofenh1')
    memofenh2 = _get_user_dn('memofenh2')

    memofegrp1 = _get_group_dn('memofegrp1')
    memofegrp2 = _get_group_dn('memofegrp2')

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


def test_memberof_MultiGrpAttr_011(topology_st):
    """
    Duplicate uniqueMember attributes to groups

    At the beginning:
        memofenh1 is memberof memofegrp1
        memofenh2 is memberof memofegrp2

    At the end
        memofenh1 is memberof memofegrp1
        memofenh2 is memberof memofegrp2
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


def test_memberof_MultiGrpAttr_012(topology_st):
    """
    MemberURL attritbute should reflect the modrdn changes in the group.

    This test has been covered in MODRDN test suite

    At the beginning:
        memofenh1 is memberof memofegrp1
        memofenh2 is memberof memofegrp2

    At the end
        memofenh1 is memberof memofegrp1
        memofenh2 is memberof memofegrp2
    """
    pass


def test_memberof_MultiGrpAttr_013(topology_st):
    """
    MemberURL attritbute should reflect the modrdn changes in the group.

    This test has been covered in MODRDN test suite

    At the beginning:
        memofenh1 is memberof memofegrp1
        memofenh2 is memberof memofegrp2

    At the end
        memofenh1 is memberof memofegrp1
        memofenh2 is memberof memofegrp2
    """
    pass


def test_memberof_MultiGrpAttr_014(topology_st):
    """
    Both member and uniqueMember pointing to the same user

    At the beginning:
        enh1 is member of
            - grp1 (member)
            - not grp2

        enh2 is member of
            - not grp1
            - grp2 (uniquemember)

    At the end
        enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)

        enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)
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

    memofegrp3 = _create_group(topology_st, 'memofegrp3')

    mods = [(ldap.MOD_ADD, 'member', memofenh1), (ldap.MOD_ADD, 'uniqueMember', memofenh1)]
    log.info("Update %s is memberof %s (member)" % (memofenh1, memofegrp3))
    log.info("Update %s is memberof %s (uniqueMember)" % (memofenh1, memofegrp3))
    topology_st.standalone.modify_s(ensure_str(memofegrp3), mods)

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

    # assert enh1 is member of
    #       - grp1 (member)
    #       - not grp2
    #       - grp3 (uniquemember)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp1)
    assert not _check_memberof(topology_st, member=memofenh1, group=memofegrp2)
    assert _check_memberof(topology_st, member=memofenh1, group=memofegrp3)

    # assert enh2 is member of
    #       - not grp1
    #       - not grp2 (uniquemember)
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


def test_memberof_MultiGrpAttr_015(topology_st):
    """
    Non-existing users to member attribut

    At the beginning:
        enh1 is member of
            - grp1 (member)
            - not grp2
            - grp3 (uniquemember)

        enh2 is member of
            - not grp1
            - grp2 (uniquemember)
            - grp3 (member)

    At the end:
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
    """

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


def test_memberof_MultiGrpAttr_016(topology_st):
    """
    ldapmodify non-existing users to the member attribute

    At the beginning:
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

    At the end:
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
    """

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


def test_memberof_MultiGrpAttr_017(topology_st):
    """
    Add user1 and user2 as memberof grp017
    user2 is member of grp017 but not with a memberof attribute (memberUid)

    At the beginning:
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

    At the end:
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
    """
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


def test_memberof_MultiGrpAttr_018(topology_st):
    """
    Add user1 and user2 as memberof grp018
    user2 is member of grp018 but not with a memberof attribute (memberUid)

    At the beginning:
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

    At the end:
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
    """

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


def test_memberof_MultiGrpAttr_019(topology_st):
    """
    Add user2 to grp19_2
    Add user3 to grp19_3
    Add grp19_2 and grp_19_3 to grp19_1

    At the beginning:
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
    At the end:
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


    """

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


def test_memberof_MultiGrpAttr_020(topology_st):
    """
    Add user1 and grp[1-5]
    Add user1 member of grp[1-4]
    Add grp[1-4] member of grp5
    Check user1 is member of grp[1-5]

    At the beginning:
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

    At the end:
        Idem
    """

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

    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_1)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_2)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_3)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_4)
    assert _check_memberof(topology_st, member=memofuser1, group=memofegrp020_5)

    # DEL user1, grp20*
    topology_st.standalone.delete_s(ensure_str(memofuser1))
    for grp in [memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5]:
        topology_st.standalone.delete_s(ensure_str(grp))


def test_memberof_MultiGrpAttr_021(topology_st):
    """
    Add user[1-4] and Grp[1-4]
    Add userX as uniquemember of GrpX
    ADD Grp5
        Grp[1-4] as members of Grp5
        user1 as member of Grp5
    Check that user1 is member of Grp1 and Grp5
    check that user* are members of Grp5

    At the beginning:
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

    At the end:

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

    """

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


def test_memberof_MultiGrpAttr_022(topology_st):
    """
    add userX as member/uniqueMember of GrpX
    add Grp5 as uniquemember of GrpX (this create a loop)


    At the beginning:
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

    At the end:

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
    """

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


def test_memberof_MultiGrpAttr_023(topology_st):
    """



    At the beginning:
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

          /----member ---> G1 ---member/uniqueMember -\
         /<--uniquemember-                            V
    G5 ------------------------>member ----------  --->U1
         |
         |----member ---> G2 ---member/uniqueMember -> U2
         |<--uniquemember-/
         |
         |----member ---> G3 ---member/uniqueMember -> U3
         |<--uniquemember-/
         |----member ---> G4 ---member/uniqueMember -> U4
         |<--uniquemember-/




    At the end:
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


def test_memberof_MultiGrpAttr_024(topology_st):
    """
    At the beginning:


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

    At the end:
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


def test_memberof_MultiGrpAttr_025(topology_st):
    """
    At the beginning:


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
    At the end:
          /----member ---> G1
         /
    G5 ------------------------>member ----------  --->U1
         |
         |----member ---> G2
         |----member ---> G3
         |----member ---> G4

    """

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

    verify_post_025(topology_st, memofegrp020_1, memofegrp020_2, memofegrp020_3, memofegrp020_4, memofegrp020_5,
                    memofuser1, memofuser2, memofuser3, memofuser4)


def test_memberof_auto_add_oc(topology_st):
    """Test the auto add objectclass feature.  The plugin should add a predefined
    objectclass that will allow memberOf to be added to an entry.
    """

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
