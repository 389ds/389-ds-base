import os
import ldap
import time
import logging
import pytest
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

DEBUGGING = False

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


USER_DN = "uid=test,ou=people,dc=example,dc=com"
GROUP_DN = "cn=group,dc=example,dc=com"
GROUP_OU = "ou=groups,dc=example,dc=com"
PEOPLE_OU = "ou=people,dc=example,dc=com"
MEP_OU = "ou=mep,dc=example,dc=com"
MEP_TEMPLATE = "cn=mep template,dc=example,dc=com"
AUTO_DN = "cn=All Users,cn=Auto Membership Plugin,cn=plugins,cn=config"
MEP_DN = "cn=MEP Definition,cn=Managed Entries,cn=plugins,cn=config"


class TopologyStandalone(object):
    """The DS Topology Class"""
    def __init__(self, standalone):
        """Init"""
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    """Create DS Deployment"""

    # Creating standalone instance ...
    if DEBUGGING:
        standalone = DirSrv(verbose=True)
    else:
        standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        """If we are debugging just stop the instances, otherwise remove
        them
        """
        if DEBUGGING:
            standalone.stop()
        else:
            standalone.delete()

    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_ticket48637(topology):
    """Test for entry cache corruption

    This requires automember and managed entry plugins to be configured.

    Then remove the group that automember would use to trigger a failure when
    adding a new entry.  Automember fails, and then managed entry also fails.

    Make sure a base search on the entry returns error 32
    """

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    #
    # Add our setup entries
    #
    try:
        topology.standalone.add_s(Entry((PEOPLE_OU, {
            'objectclass': 'top organizationalunit'.split(),
            'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('Failed to add people ou: ' + str(e))
        assert False

    try:
        topology.standalone.add_s(Entry((GROUP_OU, {
            'objectclass': 'top organizationalunit'.split(),
            'ou': 'groups'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('Failed to add groups ou: ' + str(e))
        assert False

    try:
        topology.standalone.add_s(Entry((MEP_OU, {
            'objectclass': 'top extensibleObject'.split(),
            'ou': 'mep'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add MEP ou: ' + str(e))
        assert False

    try:
        topology.standalone.add_s(Entry((MEP_TEMPLATE, {
            'objectclass': 'top mepTemplateEntry'.split(),
            'cn': 'mep template',
            'mepRDNAttr': 'cn',
            'mepStaticAttr': 'objectclass: groupofuniquenames',
            'mepMappedAttr': 'cn: $uid'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add MEP ou: ' + str(e))
        assert False

    #
    # Configure automember
    #
    try:
        topology.standalone.add_s(Entry((AUTO_DN, {
            'cn': 'All Users',
            'objectclass': ['top', 'autoMemberDefinition'],
            'autoMemberScope': 'dc=example,dc=com',
            'autoMemberFilter': 'objectclass=person',
            'autoMemberDefaultGroup': GROUP_DN,
            'autoMemberGroupingAttr': 'uniquemember:dn'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to configure automember plugin : ' + str(e))
        assert False

    #
    # Configure managed entry plugin
    #
    try:
        topology.standalone.add_s(Entry((MEP_DN, {
            'cn': 'MEP Definition',
            'objectclass': ['top', 'extensibleObject'],
            'originScope': 'ou=people,dc=example,dc=com',
            'originFilter': 'objectclass=person',
            'managedBase': 'ou=groups,dc=example,dc=com',
            'managedTemplate': MEP_TEMPLATE})))
    except ldap.LDAPError as e:
        log.fatal('Failed to configure managed entry plugin : ' + str(e))
        assert False

    #
    # Restart DS
    #
    topology.standalone.restart(timeout=30)

    #
    # Add entry that should fail since the automember group does not exist
    #
    try:
        topology.standalone.add_s(Entry((USER_DN, {
            'uid': 'test',
            'objectclass': ['top', 'person', 'extensibleObject'],
            'sn': 'test',
            'cn': 'test'})))
    except ldap.LDAPError as e:
        pass

    #
    # Search for the entry - it should not be returned
    #
    try:
        entry = topology.standalone.search_s(USER_DN, ldap.SCOPE_SUBTREE,
                                              'objectclass=*')
        if entry:
            log.fatal('Entry was incorrectly returned')
            assert False
    except ldap.NO_SUCH_OBJECT:
        pass

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
