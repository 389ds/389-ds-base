# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import logging
import os

from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.uniquegroup import list, get, get_dn, create, delete, modify, rename
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older, ensure_str
from lib389.idm.group import UniqueGroups
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


uniquegroup_name = "test_uniquegroup"


@pytest.fixture(scope="function")
def create_test_uniquegroup(topology_st, request):
    uniquegroups = UniqueGroups(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create test uniquegroup')
    if uniquegroups.exists(uniquegroup_name):
        test_uniquegroup = uniquegroups.get(uniquegroup_name)
        test_uniquegroup.delete()

    properties = FakeArgs()
    properties.cn = uniquegroup_name
    create(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, properties)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)

    def fin():
        log.info('Delete test group')
        if test_uniquegroup.exists():
            test_uniquegroup.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def add_description(topology_st, request):
    args = FakeArgs()
    args.selector = uniquegroup_name
    args.changes = ['add:description:{}'.format(uniquegroup_name)]
    modify(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_create(topology_st):
    """ Test dsidm uniquegroup create option

    :id: 7245d28e-f6b7-4aa5-87cb-c6276f27de65
    :setup: Standalone instance
    :steps:
        1. Run dsidm uniquegroup create
        2. Check that a message is provided on creation
        3. Check that created uniquegroup exists
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    output = 'Successfully created {}'.format(uniquegroup_name)

    args = FakeArgs()
    args.cn = uniquegroup_name

    log.info('Test dsidm uniquegroup create')
    create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that uniquegroup is present')
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    new_uniquegroup = uniquegroups.get(uniquegroup_name)
    assert new_uniquegroup.exists()

    log.info('Clean up for next test')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_delete(topology_st, create_test_uniquegroup):
    """ Test dsidm uniquegroup delete option

    :id: 87a8e42a-d194-41a6-80a2-0451b9b75cf4
    :setup: Standalone instance
    :steps:
        1. Run dsidm uniquegroup delete on a created uniquegroup
        2. Check that a message is provided on deletion
        3. Check that uniquegroup does not exist
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)
    output = 'Successfully deleted {}'.format(test_uniquegroup.dn)

    args = FakeArgs()
    args.dn = test_uniquegroup.dn

    log.info('Test dsidm uniquegroup delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that uniquegroup does not exist')
    assert not test_uniquegroup.exists()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_list(topology_st, create_test_uniquegroup):
    """ Test dsidm uniquegroup list option

    :id: c19cdbe5-3bde-4ba1-917a-dafd6b43e8b9
    :setup: Standalone instance
    :steps:
        1. Run dsidm uniquegroup list option without json
        2. Check the output content is correct
        3. Run dsidm uniquegroup list option with json
        4. Check the output content is correct
        5. Delete the uniquegroup
        6. Check the uniquegroup is not in the list with json
        7. Check the uniquegroup is not in the list without json
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

    log.info('Empty the log file to prevent false data to check about uniquegroup')
    topology_st.logcap.flush()

    log.info('Test dsidm uniquegroup list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=uniquegroup_name)

    log.info('Test dsidm uniquegroup list with json')
    args.json = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value=uniquegroup_name)

    log.info('Delete the uniquegroup')
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)
    test_uniquegroup.delete()

    log.info('Test empty dsidm uniquegroup list with json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value_not=uniquegroup_name)

    log.info('Test empty dsidm uniquegroup list without json')
    args.json = False
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value_not=uniquegroup_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_modify(topology_st, create_test_uniquegroup):
    """ Test dsidm uniquegroup modify option

    :id: ab3af3cb-0194-468e-abdd-5ed0432c8127
    :setup: Standalone instance
    :steps:
        1. Run dsidm uniquegroup modify add description value
        2. Run dsidm uniquegroup modify replace description value
        3. Run dsidm uniquegroup modify delete description value
    :expectedresults:
        1. description value is present
        2. description value is replaced with the new one
        3. description value is deleted
    """

    standalone = topology_st.standalone
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)
    output = 'Successfully modified {}'.format(test_uniquegroup.dn)

    args = FakeArgs()
    args.selector = uniquegroup_name

    log.info('Test dsidm uniquegroup modify add')
    args.changes = ['add:description:test']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_uniquegroup.present('description', 'test')

    log.info('Test dsidm uniquegroup modify replace')
    args.changes = ['replace:description:replaced']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_uniquegroup.present('description', 'replaced')

    log.info('Test dsidm uniquegroup modify delete')
    args.changes = ['delete:description:replaced']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert not test_uniquegroup.present('description', 'replaced')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_get_dn(topology_st, create_test_uniquegroup):
    """ Test dsidm uniquegroup get_dn option

    :id: 6fe4a86a-cc1a-424c-88ef-9aa341487419
    :setup: Standalone instance
    :steps:
         1. Run dsidm uniquegroup get_dn for created uniquegroup without json
         2. Check the output content is correct
         3. Run dsidm uniquegroup get_dn for created uniquegroup with json
         4. Check the output content is correct
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)
    args = FakeArgs()
    args.dn = test_uniquegroup.dn
    args.json = False

    log.info('Empty the log file to prevent false data to check about uniquegroup')
    topology_st.logcap.flush()

    log.info('Test dsidm uniquegroup get_dn without json')
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=uniquegroup_name)

    log.info('Test dsidm uniquegroup get_dn with json')
    args.json = True
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=uniquegroup_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_get_rdn(topology_st, create_test_uniquegroup, add_description):
    """ Test dsidm uniquegroup get option

    :id: dea0feda-d612-45b9-953e-a53c02055ac4
    :setup: Standalone instance
    :steps:
         1. Run dsidm uniquegroup get option for created uniquegroup with json
         2. Check the output content is correct
         3. Run dsidm uniquegroup get option for created uniquegroup without json
         4. Check the json content is correct
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)

    uniquegroup_content = ['dn: {}'.format(test_uniquegroup.dn),
                    'cn: {}'.format(test_uniquegroup.rdn),
                    'description: {}'.format(test_uniquegroup.rdn),
                    'objectClass: top',
                    'objectClass: groupOfUniqueNames',
                    'objectClass: nsMemberOf']

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'groupOfUniqueNames',
                    'nsMemberOf',
                    'cn',
                    test_uniquegroup.rdn,
                    'description',
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
                    test_uniquegroup.dn]

    args = FakeArgs()
    args.json = False
    args.selector = uniquegroup_name

    log.info('Empty the log file to prevent false data to check about uniquegroup')
    topology_st.logcap.flush()

    log.info('Test dsidm uniquegroup get without json')
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=uniquegroup_content)

    log.info('Test dsidm uniquegroup get with json')
    args.json = True
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_get_rename(topology_st, create_test_uniquegroup, add_description):
    """ Test dsidm uniquegroup rename option

    :id: 71b24ff7-65dd-4527-a129-8db034ec1b48
    :setup: Standalone instance
    :steps:
         1. Run dsidm uniquegroup rename option on created uniquegroup
         2. Check the uniquegroup does not have another cn attribute with the old rdn
         3. Check the old uniquegroup is deleted
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)

    args = FakeArgs()
    args.selector = uniquegroup_name
    args.new_name = 'new_uniquegroup'
    args.keep_old_rdn = False

    log.info('Test dsidm uniquegroup rename')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_uniquegroup = uniquegroups.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_uniquegroup.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new uniquegroup does not have cn attribute with the old rdn')
    assert not new_uniquegroup.present('cn', uniquegroup_name)
    assert new_uniquegroup.get_attr_val_utf8('description') == uniquegroup_name

    log.info('Verify old uniquegroup dn does not exist')
    assert not test_uniquegroup.exists()

    log.info('Clean up')
    new_uniquegroup.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_get_rename_keep_old_rdn(topology_st, create_test_uniquegroup, add_description):
    """ Test dsidm uniquegroup rename option with keep-old-rdn

    :id: 4a4a3734-7c57-4f85-9cfe-a3c24f7037bf
    :setup: Standalone instance
    :steps:
         1. Run dsidm uniquegroup rename option on created uniquegroup with keep-old-rdn
         2. Check the uniquegroup has another cn attribute with the old rdn
         3. Check the old uniquegroup is deleted
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)

    args = FakeArgs()
    args.selector = uniquegroup_name
    args.new_name = 'new_uniquegroup'
    args.keep_old_rdn = True

    log.info('Test dsidm uniquegroup rename')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_uniquegroup = uniquegroups.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_uniquegroup.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new uniquegroup does not have cn attribute with the old rdn')
    assert new_uniquegroup.present('cn', uniquegroup_name)
    assert new_uniquegroup.get_attr_val_utf8('description') == uniquegroup_name

    log.info('Verify old uniquegroup dn does not exist')
    assert not test_uniquegroup.exists()

    log.info('Clean up')
    new_uniquegroup.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))