import pytest
import ldap

from lib389.tests.cli import topology as default_topology
from lib389.cli_base import LogCapture, FakeArgs
from lib389.plugins import AutoMembershipPlugin
from lib389.cli_conf.plugins import automember as automember_cli

@pytest.fixture(scope="module")
def topology(request):
    topology = default_topology(request)

    plugin = AutoMembershipPlugin(topology.standalone)
    if not plugin.exists():
        plugin.create()

    # we need to restart the server after enabling the plugin
    plugin.enable()
    topology.standalone.restart()
    topology.logcap.flush()

    return topology


def test_namenotexists_listdefinition(topology):
    """
        Test if non existent instance raises ldap.NO_SUCH_OBJECT exception 
        when showing automember definition.
    """
    args = FakeArgs()
    
    args.name = "somerandomcn"
    
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        automember_cli.list_definition(topology.standalone, None, topology.logcap.log, args)
        

def test_createdefinition(topology):
    """
        Test if new automember definition is created successfully.
    """
    args = FakeArgs()

    args.name = "sometestcn"
    args.scope = "ou=People,dc=example,dc=com"
    args.filter = "objectclass=*"
    args.groupattr = "member:dn"
    args.defaultgroup = "cn=linux-group,cn=groups,dc=example,dc=com"
    
    automember_cli.create_definition(topology.standalone, None, topology.logcap.log, args)
    assert topology.logcap.contains("Automember definition created successfully!")

    topology.logcap.flush()


def test_invalidattributes_createdefinition(topology):
    """ 
        Test if invalid attributes raise ldap.INVALID_SYNTAX exception
        when creating new automember definition.
    """
    args = FakeArgs()

    args.name = "sometestcn"
    args.scope = "someinvalidscope"
    args.filter = "objectclass=*"
    args.groupattr = "member:dn"
    args.defaultgroup = "someinvaliddefaultgroup"

    with pytest.raises(ldap.INVALID_SYNTAX):
        automember_cli.create_definition(topology.standalone, None, topology.logcap.log, args)


def test_ifnameexists_createdefinition(topology):
    """
        Test if already existing instance name raises ldap.ALREADY-EXISTS
        exception when creating new automember definition.
    """

    args = FakeArgs()

    args.name = "sometestcn"
    args.scope = "ou=People,dc=example,dc=com"
    args.filter = "objectclass=*"
    args.groupattr = "member:dn"
    args.defaultgroup = "cn=windows-group,cn=groups,dc=example,dc=com"
    
    with pytest.raises(ldap.ALREADY_EXISTS):
        automember_cli.create_definition(topology.standalone, None, topology.logcap.log, args)


def test_editdefinition(topology):
    """
        Test if automember definition is updated successfully.
    """
    args = FakeArgs()

    args.name = "sometestcn"
    args.scope = "ou=People,dc=example,dc=com"
    args.filter = "objectclass=*"
    args.groupattr = "member:dn"
    args.defaultgroup = "cn=windows-group,cn=groups,dc=example,dc=com"

    a = automember_cli.edit_definition(topology.standalone, None, topology.logcap.log, args)

    assert topology.logcap.contains("Definition updated successfully.")

    
def test_nonexistentinstance_removedefinition(topology):
    """
        Test if non existent instance definition raises NO_SUCH_OBJECT
        definition when removing automember definition.
    """
    args = FakeArgs()

    args.name = "randomcn"

    with pytest.raises(ldap.NO_SUCH_OBJECT):
        automember_cli.remove_definition(topology.standalone, None, topology.logcap.log, args)
