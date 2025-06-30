# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import ldap
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import LinkedAttributesPlugin, LinkedAttributesConfigs
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

OU_PEOPLE = f'ou=People,{DEFAULT_SUFFIX}'
LINKTYPE = 'directReport'
MANAGEDTYPE = 'manager'
MANAGER = 'manager1'
EMPLOYEE = 'employee1'
INVALID = f'uid=doNotExist,{OU_PEOPLE}'


@pytest.fixture(scope='function')
def manager(topology_st, request):
    """Fixture to create a manager entry."""
    log.info('Creating manager entry')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    if users.exists(MANAGER):
        users.get(MANAGER).delete()

    manager = users.create(properties={
        'uid': MANAGER,
        'cn': MANAGER,
        'sn': MANAGER,
        'uidNumber': '1',
        'gidNumber': '10',
        'homeDirectory': f'/home/{MANAGER}'
    })
    manager.add('objectclass', 'extensibleObject')

    def fin():
        log.info('Delete manager entry')
        if manager.exists():
            manager.delete()

    request.addfinalizer(fin)

    return manager


@pytest.fixture(scope='function')
def employee(topology_st, request):
    """Fixture to create an employee entry."""
    log.info('Creating employee entry')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    if users.exists(EMPLOYEE):
        users.get(EMPLOYEE).delete()

    employee = users.create(properties={
        'uid': EMPLOYEE,
        'cn': EMPLOYEE,
        'sn': EMPLOYEE,
        'uidNumber': '2',
        'gidNumber': '20',
        'homeDirectory': f'/home/{EMPLOYEE}'
    })
    employee.add('objectclass', 'extensibleObject')

    def fin():
        log.info('Delete employee entry')
        if employee.exists():
            employee.delete()

    request.addfinalizer(fin)

    return employee


@pytest.fixture(scope='function')
def setup_linked_attributes(topology_st, request):
    """Fixture to set up the Linked Attributes plugin."""
    log.info('Setting up Linked Attributes plugin')

    log.info('Enable Linked Attributes plugin')
    linkedattr = LinkedAttributesPlugin(topology_st.standalone)
    linkedattr.enable()
    topology_st.standalone.restart()

    log.info('Add the plugin config entry')
    la_configs = LinkedAttributesConfigs(topology_st.standalone)
    config = la_configs.create(properties={'cn': 'Manager Link',
                                  'linkType': LINKTYPE,
                                  'managedType': MANAGEDTYPE})

    def fin():
        log.info('Cleaning up Linked Attributes plugin')
        config.delete()
        linkedattr.disable()
        topology_st.standalone.restart()

    request.addfinalizer(fin)


def test_linked_attribute_after_modrdn(topology_st, setup_linked_attributes, manager, employee):
    """ Test that linked attributes are properly updated after a modrdn operation.

    :id: 11577519-6e91-428e-b547-4fe49ba29285
    :setup:
        Standalone instance with Linked Attributes plugin enabled and manager and employee entries created.
    :steps:
        1. Add linktype to manager
        2. Check managed attribute on employee
        3. Rename employee1 to employee2
        4. Modify the value of directReport to employee2
        5. Verify that the link is properly updated
        6. Rename employee2 to employee3
        7. Modify the value of directReport to employee3 by deleting and readding the link
        8. Verify that the link is properly updated
        9. Rename manager1 to manager2
        10. Verify that the link is properly updated
    :expectedresults:
        1. Linktype is added to manager
        2. Managed attribute is present on employee
        3. Employee is renamed to employee2
        4. The value of directReport is modified to employee2
        5. The link is properly updated to employee2
        6. Employee2 is renamed to employee3
        7. The value of directReport is modified to employee3 by deleting and readding the link
        8. The link is properly updated to employee3
        9. Manager is renamed to manager2
        10. The link is properly updated to manager2
    """

    log.info('Add linktype to manager')
    manager.add(LINKTYPE, employee.dn)

    log.info('Check managed attribute')
    assert employee.present(MANAGEDTYPE, manager.dn)

    log.info('Rename employee1 to employee2')
    employee.rename('uid=employee2')

    log.info('Modify the value of directReport to employee2')
    manager.replace(LINKTYPE, f'uid=employee2,{OU_PEOPLE}')

    log.info('Verify that the link is properly updated')
    assert manager.present(LINKTYPE, employee.dn)
    assert employee.present(MANAGEDTYPE, manager.dn)

    log.info('Rename employee2 to employee3')
    employee.rename('uid=employee3')

    log.info('Modify the value of directReport to employee3 by deleting and readding the link')
    manager.remove(LINKTYPE, f'uid=employee2,{OU_PEOPLE}')
    manager.add(LINKTYPE, f'uid=employee3,{OU_PEOPLE}')

    log.info('Verify that the link is properly updated')
    assert manager.present(LINKTYPE, employee.dn)
    assert employee.present(MANAGEDTYPE, manager.dn)

    log.info('Rename manager1 to manager2')
    manager.rename('uid=manager2')

    log.info('Verify that the link is properly updated')
    assert manager.present(LINKTYPE, employee.dn)
    assert employee.present(MANAGEDTYPE, manager.dn)


def test_rollback_on_failed_operation(topology_st, setup_linked_attributes, manager, employee):
    """ Test that successful changes are rolled back if made as a part of unsuccessful
    linked attribute operation.

    :id: 407c03a1-a5b1-4668-8716-ccecc27414f3
    :setup:
        Standalone instance with Linked Attributes plugin enabled and manager and employee entries created.
    :steps:
        1. Add linktype to manager with a valid employee DN and an invalid DN.
        2. Check that the link was not added to employee due to failed operation.
        3. Check that manager does not have any link.
    :expectedresults:
        1. Operation fails with UNWILLING_TO_PERFORM error.
        2. No changes are made to employee's managed attribute.
        3. Manager has no linked attribute.
    """
    log.info('Add linktype to manager')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        manager.add_many((LINKTYPE, INVALID), (LINKTYPE, employee.dn))

    log.info('Check that no changes were made since previous operation failed')
    assert not employee.present(MANAGEDTYPE, manager.dn)
    assert not manager.present(LINKTYPE, employee.dn)
    assert not manager.present(LINKTYPE, INVALID)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(f"-s {CURRENT_FILE}")
