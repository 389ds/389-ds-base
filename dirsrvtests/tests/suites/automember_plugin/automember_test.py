import logging
import pytest
import os
import ldap
from lib389.utils import ds_is_older
from lib389._constants import *
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinition, AutoMembershipDefinitions
from lib389._mapped_object import DSLdapObjects, DSLdapObject
from lib389 import agreement
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.group import Groups, Group
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX


# Skip on older versions
pytestmark = pytest.mark.skipif(ds_is_older('1.3.7'), reason="Not implemented")

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
