# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest
import logging
import os

from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.service import list, get, get_dn, create, delete, modify, rename
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older, ensure_str
from lib389.idm.services import ServiceAccounts
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def create_test_service(topology_st, request):
    service_name = 'test_service'
    services = ServiceAccounts(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create test service')
    if services.exists(service_name):
        test_service = services.get(service_name)
        test_service.delete()
    else:
        test_service = services.create_test_service()

    def fin():
        log.info('Delete test service')
        if test_service.exists():
            test_service.delete()

    request.addfinalizer(fin)


@pytest.mark.skipif(ds_is_older("2.1.0"), reason="Not implemented")
def test_dsidm_service_list(topology_st, create_test_service):
    """ Test dsidm service list option

    :id: 218aa060-51e1-11ec-8a70-3497f624ea11
    :setup: Standalone instance
    :steps:
         1. Run dsidm service list option without json
         2. Check the output content is correct
         3. Run dsidm service list option with json
         4. Check the json content is correct
         5. Delete the service
         6. Check the service is not in the list with json
         7. Check the service is not in the list without json
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
         5. Success
         6. Success
         7. Success
    """

    standalone = topology_st.standalone
    args = FakeArgs()
    args.json = False
    service_value = 'test_service'
    json_list = ['type',
                 'list',
                 'items']

    log.info('Empty the log file to prevent false data to check about service')
    topology_st.logcap.flush()

    log.info('Test dsidm service list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=service_value)

    log.info('Test dsidm service list with json')
    args.json = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value=service_value)

    log.info('Delete the service')
    services = ServiceAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    testservice = services.get(service_value)
    testservice.delete()

    log.info('Test empty dsidm service list with json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value_not=service_value)

    log.info('Test empty dsidm service list without json')
    args.json = False
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value_not=service_value)


@pytest.mark.skipif(ds_is_older("2.1.0"), reason="Not implemented")
def test_dsidm_service_get_rdn(topology_st, create_test_service):
    """ Test dsidm service get option

    :id: 294ef774-51e1-11ec-a2c7-3497f624ea11
    :setup: Standalone instance
    :steps:
         1. Run dsidm get option for created service with json
         2. Check the output content is correct
         3. Run dsidm get option for created service without json
         4. Check the json content is correct
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    services = ServiceAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    testservice = services.get('test_service')

    service_content = [f'dn: {testservice.dn}',
                       f'cn: {testservice.rdn}',
                       'description: Test Service',
                       'objectClass: top',
                       'objectClass: nsAccount',
                       'objectClass: nsMemberOf']

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'nsAccount',
                    'nsMemberOf',
                    testservice.rdn,
                    'cn',
                    'description',
                    'creatorsname',
                    'cn=directory manager',
                    'modifiersname',
                    'createtimestamp',
                    'modifytimestamp',
                    'nsuniqueid',
                    'parentid',
                    'entryid',
                    'entrydn',
                    testservice.dn]

    args = FakeArgs()
    args.json = False
    args.selector = 'test_service'

    log.info('Empty the log file to prevent false data to check about service')
    topology_st.logcap.flush()

    log.info('Test dsidm service get without json')
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=service_content)

    log.info('Test dsidm service get with json')
    args.json = True
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)


@pytest.mark.bz1893667
@pytest.mark.xfail(reason="Will fail because of bz1893667")
@pytest.mark.skipif(ds_is_older("2.1.0"), reason="Not implemented")
def test_dsidm_service_get_dn(topology_st, create_test_service):
    """ Test dsidm service get_dn option

    :id: 2e4c8f98-51e1-11ec-b472-3497f624ea11
    :setup: Standalone instance
    :steps:
         1. Run dsidm service get_dn for created service
         2. Check the output content is correct
    :expectedresults:
         1. Success
         2. Success
    """

    standalone = topology_st.standalone
    services = ServiceAccounts(standalone, DEFAULT_SUFFIX)
    test_service = services.get('test_service')
    args = FakeArgs()
    args.dn = test_service.dn

    log.info('Empty the log file to prevent false data to check about service')
    topology_st.logcap.flush()

    log.info('Test dsidm service get_dn without json')
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    # check_value_in_log_and_reset(topology_st, content_list=service_content)
    # The check_value_in_log_and_reset will have to be updated accordinly after bz1893667 is fixed
    # because now I can't determine the output


@pytest.mark.skipif(ds_is_older("2.1.0"), reason="Not implemented")
def test_dsidm_service_create(topology_st):
    """ Test dsidm service create option

    :id: 338efbc6-51e1-11ec-a83a-3497f624ea11
    :setup: Standalone instance
    :steps:
         1. Run dsidm service create
         2. Check that a message is provided on creation
         3. Check that created service exists
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    service_name = 'new_service'
    output = f'Successfully created {service_name}'

    args = FakeArgs()
    args.cn = service_name
    args.description = service_name

    log.info('Test dsidm service create')
    create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that service is present')
    services = ServiceAccounts(standalone, DEFAULT_SUFFIX)
    new_service = services.get(service_name)
    assert new_service.exists()

    log.info('Clean up for next test')
    new_service.delete()


@pytest.mark.skipif(ds_is_older("2.1.0"), reason="Not implemented")
def test_dsidm_service_delete(topology_st, create_test_service):
    """ Test dsidm service delete option

    :id: 3b382a96-51e1-11ec-a1c2-3497f624ea11
    :setup: Standalone instance
    :steps:
         1. Run dsidm service delete on created service
         2. Check that a message is provided on deletion
         3. Check that service does not exist
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    services = ServiceAccounts(standalone, DEFAULT_SUFFIX)
    test_service = services.get('test_service')
    output = f'Successfully deleted {test_service.dn}'

    args = FakeArgs()
    args.dn = test_service.dn

    log.info('Test dsidm service delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that service does not exist')
    assert not test_service.exists()


@pytest.mark.skipif(ds_is_older("2.1.0"), reason="Not implemented")
def test_dsidm_service_modify(topology_st, create_test_service):
    """ Test dsidm service modify add, replace, delete option

    :id: 4023ef22-51e1-11ec-93c5-3497f624ea11
    :setup: Standalone instance
    :steps:
         1. Run dsidm service modify replace description value
         2. Run dsidm service modify add seeAlso attribute to service
         3. Run dsidm service modify delete for seeAlso attribute
    :expectedresults:
         1. description value is replaced with new text
         2. seeAlso attribute is present
         3. seeAlso attribute is deleted
    """

    standalone = topology_st.standalone
    services = ServiceAccounts(standalone, DEFAULT_SUFFIX)
    test_service = services.get('test_service')
    output = f'Successfully modified {test_service.dn}'

    args = FakeArgs()
    args.selector = 'test_service'
    args.changes = ['replace:description:Test Service Modified']

    log.info('Test dsidm service modify replace')
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Test dsidm service modify add')
    args.changes = [f'add:seeAlso:ou=services,{DEFAULT_SUFFIX}']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_service.present('seeAlso', f'ou=services,{DEFAULT_SUFFIX}')

    log.info('Test dsidm service modify delete')
    args.changes = [f'delete:seeAlso:ou=services,{DEFAULT_SUFFIX}']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert not test_service.present('seeAlso', f'ou=services,{DEFAULT_SUFFIX}')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_service_rename_keep_old_rdn(topology_st, create_test_service):
    """ Test dsidm service rename option with keep-old-rdn

    :id: 44cc6b08-51e1-11ec-89e7-3497f624ea11
    :setup: Standalone instance
    :steps:
         1. Run dsidm service rename option with keep-old-rdn
         2. Check the service does have another cn attribute with the old rdn
         3. Check the old service is deleted
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    services = ServiceAccounts(standalone, DEFAULT_SUFFIX)
    test_service = services.get('test_service')

    args = FakeArgs()
    args.selector = test_service.rdn
    args.new_name = 'my_service'
    args.keep_old_rdn = True

    log.info('Test dsidm service rename')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    my_service = services.get(args.new_name)
    output = f'Successfully renamed to {my_service.dn}'
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('my_service should have cn attribute with the old rdn')
    assert my_service.present('cn', 'test_service')
    assert my_service.get_attr_val_utf8('cn') == 'test_service'
    assert my_service.get_attr_val_utf8('description') == 'Test Service'

    log.info('Old service dn should not exist')
    assert not test_service.exists()

    log.info('Clean up')
    my_service.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_service_rename(topology_st, create_test_service):
    """ Test dsidm service rename option

    :id: 4a13ea64-51e1-11ec-b3ff-3497f624ea11
    :setup: Standalone instance
    :steps:
         1. Run dsidm service rename option on created service
         2. Check the service does not have another cn attribute with the old rdn
         3. Check the old service is deleted
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    services = ServiceAccounts(standalone, DEFAULT_SUFFIX)
    test_service = services.get('test_service')

    args = FakeArgs()
    args.selector = test_service.rdn
    args.new_name = 'my_service'
    args.keep_old_rdn = False

    log.info('Test dsidm service rename')
    args.new_name = 'my_service'
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    my_service = services.get(args.new_name)
    output = f'Successfully renamed to {my_service.dn}'
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('New service should not have cn attribute with the old rdn')
    assert not my_service.present('cn', 'test_service')
    assert my_service.get_attr_val_utf8('cn') == 'my_service'
    assert my_service.get_attr_val_utf8('description') == 'Test Service'

    log.info('Old service dn should not exist.')
    assert not test_service.exists()

    log.info('Clean up')
    my_service.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
