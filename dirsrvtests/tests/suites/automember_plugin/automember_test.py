# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 alisha17 <anejaalisha@yahoo.com>
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
from lib389.utils import ds_is_older
from lib389._constants import *
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinition, AutoMembershipDefinitions, AutoMembershipRegexRule
from lib389._mapped_object import DSLdapObjects, DSLdapObject
from lib389 import agreement
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.group import Groups, Group
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX


# Skip on older versions
pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.7'), reason="Not implemented")]

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def automember_fixture(topo, request):

    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'testgroup'})

    automemberplugin = AutoMembershipPlugin(topo.standalone)
    automemberplugin.enable()

    topo.standalone.restart() 

    automember_prop = {
        'cn': 'testgroup_definition',
        'autoMemberScope': 'ou=People,' + DEFAULT_SUFFIX,
        'autoMemberFilter': 'objectclass=*',
        'autoMemberDefaultGroup': group.dn,
        'autoMemberGroupingAttr': 'member:dn',
    }

    automembers = AutoMembershipDefinitions(topo.standalone, "cn=Auto Membership Plugin,cn=plugins,cn=config")

    automember = automembers.create(properties=automember_prop)

    return (group, automembers, automember)


def test_automemberscope(automember_fixture, topo):
    """Test if the automember scope is valid

    :id: c3d3f250-e7fd-4441-8387-3d24c156e982
    :setup: Standalone instance, enabled Auto Membership Plugin
    :steps:
        1. Create automember with invalid cn that raises 
           UNWILLING_TO_PERFORM exception
        2. If exception raised, set scope to any cn
        3. If exception is not raised, set scope to with ou=People
    :expectedresults:
        1. Should be success
        2. Should be success
        3. Should be success
    """

    (group, automembers, automember) = automember_fixture

    automember_prop = {
        'cn': 'anyrandomcn',
        'autoMemberScope': 'ou=People,' + DEFAULT_SUFFIX,
        'autoMemberFilter': 'objectclass=*',
        'autoMemberDefaultGroup': group.dn,
        'autoMemberGroupingAttr': 'member:dn',
    }

    # depends on issue #49465
    
    # with pytest.raises(ldap.UNWILLING_TO_PERFORM):
    #     automember = automembers.create(properties=automember_prop)
    # automember.set_scope("cn=No Entry,%s" % DEFAULT_SUFFIX)

    automember.set_scope("ou=People,%s" % DEFAULT_SUFFIX)	


def test_automemberfilter(automember_fixture, topo):
    """Test if the automember filter is valid

    :id: 935c55de-52dc-4f80-b7dd-3aacd30f6df2
    :setup: Standalone instance, enabled Auto Membership Plugin
    :steps:
        1. Create automember with invalid filter that raises 
           UNWILLING_TO_PERFORM exception
        2. If exception raised, set filter to the invalid filter
        3. If exception is not raised, set filter as all objectClasses
    :expectedresults:
        1. Should be success
        2. Should be success
        3. Should be success
    """

    (group, automembers, automember) = automember_fixture

    automember_prop = {
        'cn': 'anyrandomcn',
        'autoMemberScope': 'ou=People,' + DEFAULT_SUFFIX,
        'autoMemberFilter': '(ou=People',
        'autoMemberDefaultGroup': group.dn,
        'autoMemberGroupingAttr': 'member:dn',
    }

    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        automember = automembers.create(properties=automember_prop)
        automember.set_filter("(ou=People")

    automember.set_filter("objectClass=*")


def test_adduser(automember_fixture, topo):
    """Test if member is automatically added to the group

    :id: 14f1e2f5-2162-41ab-962c-5293516baf2e
    :setup: Standalone instance, enabled Auto Membership Plugin
    :steps:
        1. Create a user
        2. Assert that the user is member of the group
    :expectedresults:
        1. Should be success
        2. Should be success
    """    

    (group, automembers, automember) = automember_fixture

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user = users.create(properties=TEST_USER_PROPERTIES)

    assert group.is_member(user.dn)
    user.delete()


@pytest.mark.skipif(ds_is_older("1.4.1.2"), reason="Not implemented")
def test_delete_default_group(automember_fixture, topo):
    """If memberof is enable and a user became member of default group
    because of automember rule then delete the default group should succeeds

    :id: 8b55d077-8851-45a2-a547-b28a7983a3c2
    :setup: Standalone instance, enabled Auto Membership Plugin
    :steps:
        1. Enable memberof plugin
        2. Create a user
        3. Assert that the user is member of the default group
        4. Delete the default group
    :expectedresults:
        1. Should be success
        2. Should be success
        3. Should be success
        4. Should be success
    """

    (group, automembers, automember) = automember_fixture

    from lib389.plugins import MemberOfPlugin
    memberof = MemberOfPlugin(topo.standalone)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        tries = 10 # to avoid any risk of transient failure
    else:
        tries = 1
    memberof.enable()
    topo.standalone.restart()
    topo.standalone.setLogLevel(65536)

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_1 = users.create_test_user(uid=1)

    try:
        assert group.is_member(user_1.dn)
        group.delete()
        # Check there is the expected message
        while tries > 0:
            error_lines = topo.standalone.ds_error_log.match('.*auto-membership-plugin - automember_update_member_value - group .default or target. does not exist .%s.$' % group.dn)
            nb_match = len(error_lines)
            log.info("len(error_lines)=%d" % nb_match)
            for i in error_lines:
                log.info(" - %s" % i)
            assert nb_match <= 1
            if (nb_match == 1):
                # we are done the test is successful
                break
            time.sleep(1)
            tries -= 1
        assert tries > 0
    finally:
        user_1.delete()
        topo.standalone.setLogLevel(0)

@pytest.mark.skipif(ds_is_older("1.4.3.3"), reason="Not implemented")
def test_no_default_group(automember_fixture, topo):
    """If memberof is enable and a user became member of default group
    and default group does not exist then an INFO should be logged

    :id: 8882972f-fb3e-4d77-9729-0235897676bc
    :setup: Standalone instance, enabled Auto Membership Plugin
    :steps:
        1. Enable memberof plugin
        2. Set errorlog level to 0 (default)
        3. delete the default group
        4. Create a user
        5. Retrieve message in log
    :expectedresults:
        1. Should be success
        2. Should be success
        3. Should be success
        4. Should be success
        5. Should be success
    """

    (group, automembers, automember) = automember_fixture

    from lib389.plugins import MemberOfPlugin
    memberof = MemberOfPlugin(topo.standalone)
    memberof.enable()
    topo.standalone.restart()
    topo.standalone.setLogLevel(0)

    # delete it if it exists
    try:
        group.get_attr_val_utf8('creatorsname')
        group.delete()
    except ldap.NO_SUCH_OBJECT:
        pass
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_1 = users.create_test_user(uid=1)

    try:
        error_lines = topo.standalone.ds_error_log.match('.*auto-membership-plugin - automember_update_member_value - group .default or target. does not exist .%s.$' % group.dn)
        assert (len(error_lines) > 0)
    finally:
        user_1.delete()
        topo.standalone.setLogLevel(0)

@pytest.mark.skipif(ds_is_older("1.4.1.2"), reason="Not implemented")
def test_delete_target_group(automember_fixture, topo):
    """If memberof is enabld and a user became member of target group
    because of automember rule then delete the target group should succeeds

    :id: bf5745e3-3de8-485d-8a68-e2fd460ce1cb
    :setup: Standalone instance, enabled Auto Membership Plugin
    :steps:
        1. Recreate the default group if it was deleted before
        2. Create a target group (using regex)
        3. Create a target group automember rule (regex)
        4. Enable memberof plugin
        5. Create a user that goes into the target group
        6. Assert that the user is member of the target group
        7. Delete the target group
        8. Check automember skipped the regex automember rule because target group did not exist
    :expectedresults:
        1. Should be success
        2. Should be success
        3. Should be success
        4. Should be success
        5. Should be success
        6. Should be success
        7. Should be success
        8. Should be success
        """

    (group, automembers, automember) = automember_fixture

    # default group that may have been deleted in previous tests
    try:
        groups = Groups(topo.standalone, DEFAULT_SUFFIX)
        group = groups.create(properties={'cn': 'testgroup'})
    except:
        pass

    # target group that will receive regex automember
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group_regex = groups.create(properties={'cn': 'testgroup_regex'})

    # regex automember definition
    automember_regex_prop = {
        'cn': 'automember regex',
        'autoMemberTargetGroup': group_regex.dn,
        'autoMemberInclusiveRegex': 'uid=.*1',
    }
    automember_regex_dn = 'cn=automember regex, %s' % automember.dn
    automember_regexes = AutoMembershipRegexRule(topo.standalone, automember_regex_dn)
    automember_regex = automember_regexes.create(properties=automember_regex_prop)

    from lib389.plugins import MemberOfPlugin
    memberof = MemberOfPlugin(topo.standalone)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    memberof.enable()

    topo.standalone.restart()
    topo.standalone.setLogLevel(65536)

    # create a user that goes into the target group but not in the default group
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_1 = users.create_test_user(uid=1)

    try:
        assert group_regex.is_member(user_1.dn)
        assert not group.is_member(user_1.dn)

        # delete that target filter group
        group_regex.delete()
        time.sleep(delay)
        error_lines = topo.standalone.ds_error_log.match('.*auto-membership-plugin - automember_update_member_value - group .default or target. does not exist .%s.$' % group_regex.dn)
        # one line for default group and one for target group
        assert (len(error_lines) == 1)
    finally:
        user_1.delete()
        topo.standalone.setLogLevel(0)
