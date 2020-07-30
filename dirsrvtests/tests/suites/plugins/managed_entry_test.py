# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import time
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Account, Accounts
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.group import Groups
from lib389.config import Config
from lib389.idm.organizationalunit import OrganizationalUnits, OrganizationalUnit
from lib389.plugins import MEPTemplates, MEPConfigs, ManagedEntriesPlugin, MEPTemplate
from lib389.idm.nscontainer import nsContainers
from lib389.idm.domain import Domain
from lib389.tasks import Entry
import ldap

pytestmark = pytest.mark.tier1
USER_PASSWORD = 'password'


@pytest.fixture(scope="module")
def _create_inital(topo):
    """
    Will create entries for this module
    """
    meps = MEPTemplates(topo.standalone, DEFAULT_SUFFIX)
    mep_template1 = meps.create(
        properties={'cn': 'UPG Template', 'mepRDNAttr': 'cn', 'mepStaticAttr': 'objectclass: posixGroup',
                    'mepMappedAttr': 'cn: $uid|gidNumber: $gidNumber|description: User private group for $uid'.split(
                        '|')})
    conf_mep = MEPConfigs(topo.standalone)
    conf_mep.create(properties={'cn': 'UPG Definition1', 'originScope': f'cn=Users,{DEFAULT_SUFFIX}',
                                             'originFilter': 'objectclass=posixaccount',
                                             'managedBase': f'cn=Groups,{DEFAULT_SUFFIX}',
                                             'managedTemplate': mep_template1.dn})
    container = nsContainers(topo.standalone, DEFAULT_SUFFIX)
    for cn in ['Users', 'Groups']:
        container.create(properties={'cn': cn})


def test_binddn_tracking(topo, _create_inital):
    """Test Managed Entries basic functionality

    :id: ea2ddfd4-aaec-11ea-8416-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Set nsslapd-plugin-binddn-tracking attribute under cn=config
        2. Add user
        3. Managed Entry Plugin runs against managed entries upon any update without validating
        4. verify creation of User Private Group with its time stamp value
        5. Modify the SN attribute which is not mapped with managed entry
        6. run ModRDN operation and check the User Private group
        7. Check the time stamp of UPG should be changed now
        8. Check the creatorsname should be user dn and internalCreatorsname should be plugin name
        9. Check if a managed group entry was created
    :expected results:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """
    config = Config(topo.standalone)
    # set nsslapd-plugin-binddn-tracking attribute under cn=config
    config.replace('nsslapd-plugin-binddn-tracking', 'on')
    # Add user
    user = UserAccounts(topo.standalone, f'cn=Users,{DEFAULT_SUFFIX}', rdn=None).create_test_user()
    assert user.get_attr_val_utf8('mepManagedEntry') == f'cn=test_user_1000,cn=Groups,{DEFAULT_SUFFIX}'
    entry = Account(topo.standalone, f'cn=test_user_1000,cn=Groups,{DEFAULT_SUFFIX}')
    # Managed Entry Plugin runs against managed entries upon any update without validating
    # verify creation of User Private Group with its time stamp value
    stamp1 = entry.get_attr_val_utf8('modifyTimestamp')
    user.replace('sn', 'NewSN_modified')
    stamp2 = entry.get_attr_val_utf8('modifyTimestamp')
    # Modify the SN attribute which is not mapped with managed entry
    # Check the time stamp of UPG should not be changed
    assert stamp1 == stamp2
    time.sleep(1)
    # run ModRDN operation and check the User Private group
    user.rename(new_rdn='uid=UserNewRDN', newsuperior='cn=Users,dc=example,dc=com')
    assert user.get_attr_val_utf8('mepManagedEntry') == f'cn=UserNewRDN,cn=Groups,{DEFAULT_SUFFIX}'
    entry = Account(topo.standalone, f'cn=UserNewRDN,cn=Groups,{DEFAULT_SUFFIX}')
    stamp3 = entry.get_attr_val_utf8('modifyTimestamp')
    # Check the time stamp of UPG should be changed now
    assert stamp2 != stamp3
    time.sleep(1)
    user.replace('gidNumber', '1')
    stamp4 = entry.get_attr_val_utf8('modifyTimestamp')
    assert stamp4 != stamp3
    # Check the creatorsname should be user dn and internalCreatorsname should be plugin name
    assert entry.get_attr_val_utf8('creatorsname') == 'cn=directory manager'
    assert entry.get_attr_val_utf8('internalCreatorsname') == 'cn=Managed Entries,cn=plugins,cn=config'
    assert entry.get_attr_val_utf8('modifiersname') == 'cn=directory manager'
    user.delete()
    config.replace('nsslapd-plugin-binddn-tracking', 'off')


class WithObjectClass(Account):
    def __init__(self, instance, dn=None):
        super(WithObjectClass, self).__init__(instance, dn)
        self._rdn_attribute = 'uid'
        self._create_objectclasses = ['top', 'person', 'inetorgperson']


def test_mentry01(topo, _create_inital):
    """Test Managed Entries basic functionality

    :id: 9b87493b-0493-46f9-8364-6099d0e5d806
    :setup: Standalone Instance
    :steps:
        1. Check the plug-in status
        2. Add Template and definition entry
        3. Add our org units
        4. Add users with PosixAccount ObjectClass and verify creation of User Private Group
        5. Disable the plug-in and check the status
        6. Enable the plug-in and check the status the plug-in is disabled and creation of UPG should fail
        7. Add users with PosixAccount ObjectClass and verify creation of User Private Group
        8. Add users, run ModRDN operation and check the User Private group
        9. Add users, run LDAPMODIFY to change the gidNumber and check the User Private group
        10. Checking whether creation of User Private group fails for existing group entry
        11. Checking whether adding of posixAccount objectClass to existing user creates UPG
        12. Running ModRDN operation and checking the user private groups mepManagedBy attribute
        13. Deleting mepManagedBy attribute and running ModRDN operation to check if it creates a new UPG
        14. Change the RDN of template entry, DSA Unwilling to perform error expected
        15. Change the RDN of cn=Users to cn=TestUsers and check UPG are deleted
    :expected results:
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
        13. Success
        14. Fail(Unwilling to perform )
        15. Success
    """
    # Check the plug-in status
    mana = ManagedEntriesPlugin(topo.standalone)
    assert mana.status()
    # Add Template and definition entry
    org1 = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX).create(properties={'ou': 'Users'})
    org2 = OrganizationalUnit(topo.standalone, f'ou=Groups,{DEFAULT_SUFFIX}')
    meps = MEPTemplates(topo.standalone, DEFAULT_SUFFIX)
    mep_template1 = meps.create(properties={
        'cn': 'UPG Template1',
        'mepRDNAttr': 'cn',
        'mepStaticAttr': 'objectclass: posixGroup',
        'mepMappedAttr': 'cn: $uid|gidNumber: $gidNumber|description: User private group for $uid'.split('|')})
    conf_mep = MEPConfigs(topo.standalone)
    mep_config = conf_mep.create(properties={
        'cn': 'UPG Definition2',
        'originScope': org1.dn,
        'originFilter': 'objectclass=posixaccount',
        'managedBase': org2.dn,
        'managedTemplate': mep_template1.dn})
    # Add users with PosixAccount ObjectClass and verify creation of User Private Group
    user = UserAccounts(topo.standalone, f'ou=Users,{DEFAULT_SUFFIX}', rdn=None).create_test_user()
    assert user.get_attr_val_utf8('mepManagedEntry') == f'cn=test_user_1000,ou=Groups,{DEFAULT_SUFFIX}'
    # Disable the plug-in and check the status
    mana.disable()
    user.delete()
    topo.standalone.restart()
    # Add users with PosixAccount ObjectClass when the plug-in is disabled and creation of UPG should fail
    user = UserAccounts(topo.standalone, f'ou=Users,{DEFAULT_SUFFIX}', rdn=None).create_test_user()
    assert not user.get_attr_val_utf8('mepManagedEntry')
    # Enable the plug-in and check the status
    mana.enable()
    user.delete()
    topo.standalone.restart()
    # Add users with PosixAccount ObjectClass and verify creation of User Private Group
    user = UserAccounts(topo.standalone, f'ou=Users,{DEFAULT_SUFFIX}', rdn=None).create_test_user()
    assert user.get_attr_val_utf8('mepManagedEntry') == f'cn=test_user_1000,ou=Groups,{DEFAULT_SUFFIX}'
    # Add users, run ModRDN operation and check the User Private group
    # Add users, run LDAPMODIFY to change the gidNumber and check the User Private group
    user.rename(new_rdn='uid=UserNewRDN', newsuperior='ou=Users,dc=example,dc=com')
    assert user.get_attr_val_utf8('mepManagedEntry') == f'cn=UserNewRDN,ou=Groups,{DEFAULT_SUFFIX}'
    user.replace('gidNumber', '20209')
    entry = Account(topo.standalone, f'cn=UserNewRDN,ou=Groups,{DEFAULT_SUFFIX}')
    assert entry.get_attr_val_utf8('gidNumber') == '20209'
    user.replace_many(('sn', 'new_modified_sn'), ('gidNumber', '31309'))
    assert entry.get_attr_val_utf8('gidNumber') == '31309'
    user.delete()
    # Checking whether creation of User Private group fails for existing group entry
    grp = Groups(topo.standalone, f'ou=Groups,{DEFAULT_SUFFIX}', rdn=None).create(properties={'cn': 'MENTRY_14'})
    user = UserAccounts(topo.standalone, f'ou=Users,{DEFAULT_SUFFIX}', rdn=None).create_test_user()
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        entry.status()
    user.delete()
    # Checking whether adding of posixAccount objectClass to existing user creates UPG
    # Add Users without posixAccount objectClass
    users = WithObjectClass(topo.standalone, f'uid=test_test, ou=Users,{DEFAULT_SUFFIX}')
    user_properties1 = {'uid': 'test_test', 'cn': 'test', 'sn': 'test', 'mail': 'sasa@sasa.com', 'telephoneNumber': '123'}
    user = users.create(properties=user_properties1)
    assert not user.get_attr_val_utf8('mepManagedEntry')
    # Add posixAccount objectClass
    user.replace_many(('objectclass', ['top', 'person', 'inetorgperson', 'posixAccount']),
                      ('homeDirectory', '/home/ok'),
                      ('uidNumber', '61603'), ('gidNumber', '61603'))
    assert not user.get_attr_val_utf8('mepManagedEntry')
    user = UserAccounts(topo.standalone, f'ou=Users,{DEFAULT_SUFFIX}', rdn=None).create_test_user()
    entry = Account(topo.standalone, 'cn=test_user_1000,ou=Groups,dc=example,dc=com')
    # Add inetuser objectClass
    user.replace_many(
        ('objectclass', ['top', 'account', 'posixaccount', 'inetOrgPerson',
                         'organizationalPerson', 'nsMemberOf', 'nsAccount',
                         'person', 'mepOriginEntry', 'inetuser']),
        ('memberOf', entry.dn))
    assert entry.status()
    user.delete()
    user = UserAccounts(topo.standalone, f'ou=Users,{DEFAULT_SUFFIX}', rdn=None).create_test_user()
    entry = Account(topo.standalone, 'cn=test_user_1000,ou=Groups,dc=example,dc=com')
    # Add groupofNames objectClass
    user.replace_many(
        ('objectclass', ['top', 'account', 'posixaccount', 'inetOrgPerson',
                         'organizationalPerson', 'nsMemberOf', 'nsAccount',
                         'person', 'mepOriginEntry', 'groupofNames']),
        ('memberOf', user.dn))
    assert entry.status()
    # Running ModRDN operation and checking the user private groups mepManagedBy attribute
    user.replace('mepManagedEntry', f'uid=CheckModRDN,ou=Users,{DEFAULT_SUFFIX}')
    user.rename(new_rdn='uid=UserNewRDN', newsuperior='ou=Users,dc=example,dc=com')
    assert user.get_attr_val_utf8('mepManagedEntry') == f'uid=CheckModRDN,ou=Users,{DEFAULT_SUFFIX}'
    # Deleting mepManagedBy attribute and running ModRDN operation to check if it creates a new UPG
    user.remove('mepManagedEntry', f'uid=CheckModRDN,ou=Users,{DEFAULT_SUFFIX}')
    user.rename(new_rdn='uid=UserNewRDN1', newsuperior='ou=Users,dc=example,dc=com')
    assert user.get_attr_val_utf8('mepManagedEntry') == f'cn=UserNewRDN1,ou=Groups,{DEFAULT_SUFFIX}'
    # Change the RDN of template entry, DSA Unwilling to perform error expected
    mep = MEPTemplate(topo.standalone, f'cn=UPG Template,{DEFAULT_SUFFIX}')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        mep.rename(new_rdn='cn=UPG Template2', newsuperior='dc=example,dc=com')
    # Change the RDN of cn=Users to cn=TestUsers and check UPG are deleted
    before = user.get_attr_val_utf8('mepManagedEntry')
    user.rename(new_rdn='uid=Anuj', newsuperior='ou=Users,dc=example,dc=com')
    assert user.get_attr_val_utf8('mepManagedEntry') != before


def test_managed_entry_removal(topo):
    """Check that we can't remove managed entry manually

    :id: cf9c5be5-97ef-46fc-b199-8346acf4c296
    :setup: Standalone Instance
    :steps:
        1. Enable the plugin
        2. Restart the instance
        3. Add our org units
        4. Set up config entry and template entry for the org units
        5. Add an entry that meets the MEP scope
        6. Check if a managed group entry was created
        7. Try to remove the entry while bound as Admin (non-DM)
        8. Remove the entry while bound as DM
        9. Check that the managing entry can be deleted too
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Should fail
        8. Success
        9. Success
    """

    inst = topo.standalone

    # Add ACI so we can test that non-DM user can't delete managed entry
    domain = Domain(inst, DEFAULT_SUFFIX)
    ACI_TARGET = f"(target = \"ldap:///{DEFAULT_SUFFIX}\")"
    ACI_TARGETATTR = "(targetattr = *)"
    ACI_ALLOW = "(version 3.0; acl \"Admin Access\"; allow (all) "
    ACI_SUBJECT = "(userdn = \"ldap:///anyone\");)"
    ACI_BODY = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    domain.add('aci', ACI_BODY)

    # stop the plugin, and start it
    plugin = ManagedEntriesPlugin(inst)
    plugin.disable()
    plugin.enable()

    # Add our org units
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ou_people = ous.create(properties={'ou': 'managed_people'})
    ou_groups = ous.create(properties={'ou': 'managed_groups'})

    mep_templates = MEPTemplates(inst, DEFAULT_SUFFIX)
    mep_template1 = mep_templates.create(properties={
        'cn': 'MEP template',
        'mepRDNAttr': 'cn',
        'mepStaticAttr': 'objectclass: groupOfNames|objectclass: extensibleObject'.split('|'),
        'mepMappedAttr': 'cn: $cn|uid: $cn|gidNumber: $uidNumber'.split('|')
    })
    mep_configs = MEPConfigs(inst)
    mep_configs.create(properties={'cn': 'config',
                                   'originScope': ou_people.dn,
                                   'originFilter': 'objectclass=posixAccount',
                                   'managedBase': ou_groups.dn,
                                   'managedTemplate': mep_template1.dn})
    inst.restart()

    # Add an entry that meets the MEP scope
    test_users_m1 = UserAccounts(inst, DEFAULT_SUFFIX, rdn='ou={}'.format(ou_people.rdn))
    managing_entry = test_users_m1.create_test_user(1001)
    managing_entry.reset_password(USER_PASSWORD)
    user_bound_conn = managing_entry.bind(USER_PASSWORD)

    # Get the managed entry
    managed_groups = Groups(inst, ou_groups.dn, rdn=None)
    managed_entry = managed_groups.get(managing_entry.rdn)

    # Check that the managed entry was created
    assert managed_entry.exists()

    # Try to remove the entry while bound as Admin (non-DM)
    managed_groups_user_conn = Groups(user_bound_conn, ou_groups.dn, rdn=None)
    managed_entry_user_conn = managed_groups_user_conn.get(managed_entry.rdn)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        managed_entry_user_conn.delete()
    assert managed_entry_user_conn.exists()

    # Remove the entry while bound as DM
    managed_entry.delete()
    assert not managed_entry.exists()

    # Check that the managing entry can be deleted too
    managing_entry.delete()
    assert not managing_entry.exists()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
