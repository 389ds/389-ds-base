# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest
import logging
import os

from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.posixgroup import list, get, get_dn, create, delete, modify, rename
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older, ensure_str
from lib389.idm.posixgroup import PosixGroups
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

posixgroup_name = 'test_posixgroup'

@pytest.fixture(scope="function")
def create_test_posixgroup(topology_st, request):
    posixgroups = PosixGroups(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create test posixgroup')
    if posixgroups.exists(posixgroup_name):
        test_posixgroup = posixgroups.get(posixgroup_name)
        test_posixgroup.delete()

    properties = FakeArgs()
    properties.cn = posixgroup_name
    properties.gidNumber = '3000'
    create(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, properties)
    test_posixgroup = posixgroups.get(posixgroup_name)

    def fin():
        log.info('Delete test posixgroup')
        if test_posixgroup.exists():
            test_posixgroup.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def add_description(topology_st, request):
    args = FakeArgs()
    args.selector = posixgroup_name
    args.changes = ['add:description:{}'.format(posixgroup_name)]
    modify(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_posixgroup_create(topology_st):
    """ Test dsidm posixgroup create option

    :id: 90c988df-ff98-4004-838d-2012d77c6bd7
    :setup: Standalone instance
    :steps:
        1. Run dsidm posixgroup create
        2. Check that a message is provided on creation
        3. Check that created posixgroup exists
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    output = 'Successfully created {}'.format(posixgroup_name)

    args = FakeArgs()
    args.cn = posixgroup_name
    args.gidNumber = '3000'

    log.info('Test dsidm posixgroup create')
    create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that posixgroup is present')
    posixgroups = PosixGroups(standalone, DEFAULT_SUFFIX)
    new_posixgroup = posixgroups.get(posixgroup_name)
    assert new_posixgroup.exists()
    assert new_posixgroup.present('gidNumber', '3000')

    log.info('Clean up for next test')
    new_posixgroup.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_posixgroup_delete(topology_st, create_test_posixgroup):
    """ Test dsidm posixgroup delete option

    :id: d471c8b9-6a67-4b50-a3c7-651f1b8c1283
    :setup: Standalone instance
    :steps:
        1. Run dsidm posixgroup delete on a created group
        2. Check that a message is provided on deletion
        3. Check that posixgroup does not exist
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    posixgroups = PosixGroups(standalone, DEFAULT_SUFFIX)
    test_posixgroup = posixgroups.get(posixgroup_name)
    output = 'Successfully deleted {}'.format(test_posixgroup.dn)

    args = FakeArgs()
    args.dn = test_posixgroup.dn

    log.info('Test dsidm posixgroup delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that posixgroup does not exist')
    assert not test_posixgroup.exists()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_posixgroup_modify(topology_st, create_test_posixgroup):
    """ Test dsidm posixgroup modify option

    :id: c166ed02-56cc-4e85-bf19-d63142fac711
    :setup: Standalone instance
    :steps:
        1. Run dsidm posixgroup modify add description value
        2. Run dsidm posixgroup modify replace description value
        3. Run dsidm posixgroup modify delete description value
    :expectedresults:
        1. Description value is present
        2. Description value is replaced with the new one
        3. Description value is deleted
    """

    standalone = topology_st.standalone
    posixgroups = PosixGroups(standalone, DEFAULT_SUFFIX)
    test_posixgroup = posixgroups.get(posixgroup_name)
    output = 'Successfully modified {}'.format(test_posixgroup.dn)

    args = FakeArgs()
    args.selector = posixgroup_name

    log.info('Test dsidm posixgroup modify add')
    args.changes = ['add:description:test_posixgroup_description']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_posixgroup.present('description', 'test_posixgroup_description')

    log.info('Test dsidm posixgroup modify replace')
    args.changes = ['replace:description:new_posixgroup_description']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_posixgroup.present('description', 'new_posixgroup_description')

    log.info('Test dsidm posixgroup modify delete')
    args.changes = ['delete:description:new_posixgroup_description']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert not test_posixgroup.present('description', 'new_posixgroup_description')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_posixgroup_list(topology_st, create_test_posixgroup):
    """ Test dsidm posixgroup list option

    :id: 03b72540-2c2b-405d-a918-4ca0dbddfcc5
    :setup: Standalone instance
    :steps:
        1. Run dsidm posixgroup list option without json
        2. Check the output content is correct
        3. Run dsidm posixgroup list option with json
        4. Check the output content is correct
        5. Delete the posixgroup
        6. Check the posixgroup is not in the list with json
        7. Check the posixgroup is not in the list without json
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

    log.info('Empty the log file to prevent false data to check about posixgroup')
    topology_st.logcap.flush()

    log.info('Test dsidm posixgroup list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=posixgroup_name)

    log.info('Test dsidm posixgroup list with json')
    args.json = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value=posixgroup_name)

    log.info('Delete the posixgroup')
    posixgroups = PosixGroups(standalone, DEFAULT_SUFFIX)
    test_posixgroup = posixgroups.get(posixgroup_name)
    test_posixgroup.delete()

    log.info('Test empty dsidm posixgroup list with json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value_not=posixgroup_name)

    log.info('Test empty dsidm posixgroup list without json')
    args.json = False
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value_not=posixgroup_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_posixgroup_get_dn(topology_st, create_test_posixgroup):
    """ Test dsidm posixgroup get_dn option

    :id: 2ca19f8c-6f26-4dff-9e1f-8801a748b23b
    :setup: Standalone instance
    :steps:
         1. Run dsidm group get_dn for created group without json
         2. Check the output content is correct
         3. Run dsidm group get_dn for created group with json
         4. Check the output content is correct
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    posixgroups = PosixGroups(standalone, DEFAULT_SUFFIX)
    test_posixgroup = posixgroups.get(posixgroup_name)
    args = FakeArgs()
    args.dn = test_posixgroup.dn
    args.json = False

    log.info('Empty the log file to prevent false data to check about group')
    topology_st.logcap.flush()

    log.info('Test dsidm group get_dn without json')
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=posixgroup_name)

    log.info('Test dsidm group get_dn with json')
    args.json = True
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=posixgroup_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_posixgroup_get_rdn(topology_st, create_test_posixgroup):
    """ Test dsidm group get option

    :id: 54fe642d-bbaf-480e-8bb0-a7dc50f8f594
    :setup: Standalone instance
    :steps:
         1. Run dsidm get option for created group with json
         2. Check the output content is correct
         3. Run dsidm get option for created group without json
         4. Check the json content is correct
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    posixgroups = PosixGroups(standalone, DEFAULT_SUFFIX)
    test_posixgroup = posixgroups.get(posixgroup_name)

    posixgroup_content = ['dn: {}'.format(test_posixgroup.dn),
                          'cn: {}'.format(test_posixgroup.rdn),
                          'gidNumber: 3000',
                          'objectClass: top',
                          'objectClass: groupOfNames',
                          'objectClass: posixGroup',
                          'objectClass: nsMemberOf']

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'groupOfNames',
                    'posixGroup',
                    'nsMemberOf',
                    'cn',
                    test_posixgroup.rdn,
                    'gidnumber',
                    '3000',
                    'creatorsname',
                    'cn=directory manager',
                    'modifiersname',
                    'createtimestamp',
                    'modifytimestamp',
                    'nsuniqueid',
                    'parentid',
                    'entryid',
                    'entryuuid',
                    'dsentrydn',
                    'entrydn',
                    test_posixgroup.dn]

    args = FakeArgs()
    args.json = False
    args.selector = posixgroup_name

    log.info('Empty the log file to prevent false data to check about posixgroup')
    topology_st.logcap.flush()

    log.info('Test dsidm posixgroup get without json')
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=posixgroup_content)

    log.info('Test dsidm posixgroup get with json')
    args.json = True
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_posixgroup_rename(topology_st, create_test_posixgroup, add_description):
    """ Test dsidm posixgroup rename option

    :id: 1919805e-efd6-41da-91e7-59c011546993
    :setup: Standalone instance
    :steps:
        1. Run dsidm posixgroup rename option on created posixgroup
        2. Check the group does not have another cn attribute with the old rdn
        3. Check the old group is deleted
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    posixgroups = PosixGroups(standalone, DEFAULT_SUFFIX)
    test_posixgroup = posixgroups.get(posixgroup_name)

    args = FakeArgs()
    args.selector = posixgroup_name
    args.new_name = 'new_posixgroup'
    args.keep_old_rdn = False

    log.info('Test dsidm posixgroup rename')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_posixgroup = posixgroups.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_posixgroup.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new posixgroup does not have cn attribute with the old rdn')
    assert not new_posixgroup.present('cn', posixgroup_name)
    assert new_posixgroup.get_attr_val_utf8('description') == posixgroup_name

    log.info('Verify old posixgroup dn does not exist')
    assert not test_posixgroup.exists()

    log.info('Clean up')
    new_posixgroup.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_posixgroup_rename_keep_old_rdn(topology_st, create_test_posixgroup, add_description):
    """ Test dsidm posixgroup rename option with keep-old-rdn

    :id: 80b5faf0-dd84-4853-a21a-739388c112f5
    :setup: Standalone instance
    :steps:
        1. Run dsidm posixgroup rename option on created posixgroup with keep-old-rdn
        2. Check the group has another cn attribute with the old rdn
        3. Check the old group is deleted
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    posixgroups = PosixGroups(standalone, DEFAULT_SUFFIX)
    test_posixgroup = posixgroups.get(posixgroup_name)

    args = FakeArgs()
    args.selector = posixgroup_name
    args.new_name = 'new_posixgroup'
    args.keep_old_rdn = True

    log.info('Test dsidm posixgroup rename with keep-old-rdn')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_posixgroup = posixgroups.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_posixgroup.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new posixgroup has cn attribute with the old rdn')
    assert new_posixgroup.present('cn', posixgroup_name)
    assert new_posixgroup.get_attr_val_utf8('description') == posixgroup_name

    log.info('Verify old posixgroup dn does not exist')
    assert not test_posixgroup.exists()

    log.info('Clean up')
    new_posixgroup.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)