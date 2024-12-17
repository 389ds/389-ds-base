# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
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
from lib389.cli_idm.organizationalunit import get, get_dn, create, modify, delete, list, rename
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older
from lib389.idm.organizationalunit import OrganizationalUnits
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

ou_name = 'test_ou'

@pytest.fixture(scope="function")
def create_test_ou(topology_st, request):
    log.info('Create organizational unit')
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    if ous.exists(ou_name):
        test_ou = ous.get(ou_name)
        test_ou.delete()

    test_ou = ous.create(properties={
        'ou': ou_name,
        'description': 'Test OU',
    })

    def fin():
        log.info('Delete organizational unit')
        if test_ou.exists():
            test_ou.delete()

    request.addfinalizer(fin)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_organizational_unit_create(topology_st):
    """ Test dsidm organizationalunit create

    :id: 9bfd63d6-6689-436e-bad1-f18b7bcff471
    :setup: Standalone instance
    :steps:
        1. Run dsidm organizationalunit create
        2. Check that a message is provided on creation
        3. Check that created organizational unit exists
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    output = 'Successfully created {}'.format(ou_name)

    args = FakeArgs()
    args.ou = ou_name

    log.info('Test dsidm organizationalunit create')
    create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that organizational unit is present')
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    new_ou = ous.get(ou_name)
    assert new_ou.exists()

    log.info('Clean up for next test')
    new_ou.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_organizational_unit_list(topology_st, create_test_ou):
    """ Test dsidm organizationalunit list option

    :id: 14f1fb93-64e2-41f9-b3fd-d0cea3bd2ea1
    :setup: Standalone instance
    :steps:
        1. Run dsidm organizationalunit list option without json
        2. Check the output content is correct
        3. Run dsidm organizationalunit list option with json
        4. Check the output content is correct
        5. Delete the organizational unit
        6. Check the organizational unit is not in the list with json
        7. Check the organizational unit is not in the list without json
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
    json_list = ['type',
                 'list',
                 'items']

    log.info('Empty the log file to prevent false data to check about group')
    topology_st.logcap.flush()

    log.info('Test dsidm organizationalunit list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=ou_name)

    log.info('Test dsidm organizationalunit list with json')
    args.json = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=ou_name)

    log.info('Delete the organizational unit')
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    test_ou = ous.get(ou_name)
    test_ou.delete()

    log.info('Test empty dsidm organizationalunit list with json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value_not=ou_name)

    log.info('Test empty dsidm organizationalunit list without json')
    args.json = False
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value_not=ou_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_organizational_unit_modify(topology_st, create_test_ou):
    """ Test dsidm organizationalunit modify option

    :id: 20159281-ebcb-4126-aef5-0c2b24ece5df
    :setup: Standalone instance
    :steps:
        1. Run dsidm organizationalunit modify add description value
        2. Run dsidm organizationalunit modify replace description value
        3. Run dsidm organizationalunit modify delete description value
    :expectedresults:
        1. Description value is present
        2. Description value is replaced with the new one
        3. Description value is deleted
    """

    standalone = topology_st.standalone
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    test_ou = ous.get(ou_name)
    output = 'Successfully modified {}'.format(test_ou.dn)

    args = FakeArgs()
    args.selector = ou_name

    log.info('Test dsidm organizationalunit modify add')
    args.changes = ['add:description:test']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_ou.present('description', 'test')

    log.info('Test dsidm organizationalunit modify replace')
    args.changes = ['replace:description:replaced']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_ou.present('description', 'replaced')

    log.info('Test dsidm organizationalunit modify delete')
    args.changes = ['delete:description:replaced']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert not test_ou.present('description', 'replaced')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_organizational_unit_get_rdn(topology_st, create_test_ou):
    """ Test dsidm organizationalunit get option

    :id: 56840d36-fa23-4f0c-9682-ca90d9f2923e
    :setup: Standalone instance
    :steps:
         1. Run dsidm get option for created organizational unit with json
         2. Check the output content is correct
         3. Run dsidm get option for created organizational unit without json
         4. Check the json content is correct
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    test_ou = ous.get(ou_name)

    ou_content = ['dn: {}'.format(test_ou.dn),
                  'ou: {}'.format(test_ou.rdn),
                  'description: Test OU',
                  'objectClass: top',
                  'objectClass: organizationalunit']

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'organizationalunit',
                    'ou',
                    test_ou.rdn,
                    'creatorsname',
                    'cn=directory manager',
                    'modifiersname',
                    'createtimestamp',
                    'modifytimestamp',
                    'nsuniqueid',
                    'parentid',
                    'entryid',
                    'entryuuid',
                    'description',
                    'Test OU',
                    'dsentrydn',
                    'entrydn',
                    test_ou.dn]

    args = FakeArgs()
    args.json = False
    args.selector = ou_name

    log.info('Empty the log file to prevent false data to check about organizational unit')
    topology_st.logcap.flush()

    log.info('Test dsidm organizationalunit get without json')
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=ou_content)

    log.info('Test dsidm organizationalunit get with json')
    args.json = True
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_organizational_unit_rename(topology_st, create_test_ou):
    """ Test dsidm organizationalunit rename option

    :id: c1fe31c1-748d-4d8c-aa77-c770be13bfa4
    :setup: Standalone instance
    :steps:
        1. Run dsidm organizationalunit rename option on created organizational unit
        2. Check the organizational unit does not have another ou attribute with the old rdn
        3. Check the old organizational unit is deleted
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    test_ou = ous.get(ou_name)

    args = FakeArgs()
    args.selector = ou_name
    args.new_name = 'new_ou'
    args.keep_old_rdn = False

    log.info('Test dsidm organizationalunit rename')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_ou = ous.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_ou.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new organizational unit does not have ou attribute with the old rdn')
    assert not new_ou.present('ou', ou_name)
    assert new_ou.get_attr_val_utf8('description') == 'Test OU'

    log.info('Verify old organizational unit dn does not exist')
    assert not test_ou.exists()

    log.info('Clean up')
    new_ou.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_organizational_unit_rename_keep_old_rdn(topology_st, create_test_ou):
    """ Test dsidm organizationalunit rename option with keep-old-rdn

    :id: 398e2734-abc8-48ba-9dc0-9489368fa625
    :setup: Standalone instance
    :steps:
        1. Run dsidm organizationalunit rename option on created organizational unit with keep-old-rdn
        2. Check the organizational unit has another ou attribute with the old rdn
        3. Check the old organizational unit is deleted
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    test_ou = ous.get(ou_name)

    args = FakeArgs()
    args.selector = ou_name
    args.new_name = 'new_ou'
    args.keep_old_rdn = True

    log.info('Test dsidm organizationalunit rename with keep-old-rdn')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_ou = ous.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_ou.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new organizational unit has ou attribute with the old rdn')
    assert new_ou.present('ou', ou_name)
    assert new_ou.get_attr_val_utf8('description') == 'Test OU'

    log.info('Verify old organizational unit dn does not exist')
    assert not test_ou.exists()

    log.info('Clean up')
    new_ou.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_organizational_unit_get_dn(topology_st, create_test_ou):
    """ Test dsidm organizationalunit get_dn option

    :id: e2b8370e-cb75-4d71-b3c5-364a970366c7
    :setup: Standalone instance
    :steps:
         1. Run dsidm organizationalunit get_dn for a created group without json
         2. Check the output content is correct
         3. Run dsidm organizationalunit get_dn for a created group with json
         4. Check the output content is correct
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    test_ou = ous.get(ou_name)

    args = FakeArgs()
    args.dn = test_ou.dn
    args.json = False

    log.info('Empty the log file to prevent false data to check about organizational unit')
    topology_st.logcap.flush()

    log.info('Test dsidm organizationalunit get_dn without json')
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=ou_name)

    log.info('Test dsidm organizationalunit get_dn with json')
    args.json = True
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=ou_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
@pytest.mark.xfail(ds_is_older("1.4.3.16"), reason="Might fail because of bz1866294")
def test_dsidm_organizational_unit_delete(topology_st, create_test_ou):
    """ Test dsidm organizationalunit delete

    :id: 5d35a5ee-85c2-4b83-9101-938ba7732ccd
    :customerscenario: True
    :setup: Standalone instance
    :steps:
         1. Run dsidm organizationalunit delete
         2. Check the ou is deleted
    :expectedresults:
         1. Success
         2. Entry is deleted
    """

    standalone = topology_st.standalone
    ous = OrganizationalUnits(standalone, DEFAULT_SUFFIX)
    test_ou = ous.get(ou_name)
    delete_value = 'Successfully deleted {}'.format(test_ou.dn)

    args = FakeArgs()
    args.dn = test_ou.dn

    log.info('Test dsidm organizationalunit delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=delete_value)

    log.info('Check the entry is deleted')
    assert not test_ou.exists()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
