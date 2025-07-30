# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import json
import logging
import os

from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.uniquegroup import (list, get, get_dn, create, delete, modify, rename,
                                        members, add_member, remove_member)
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older, ensure_str, is_a_dn
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
    args.full_dn = False
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

    log.info('Test full_dn option with list')
    args.full_dn = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    assert is_a_dn(json_result['items'][0])
    args.full_dn = False

    log.info('Delete the uniquegroup')
    uniquegroups = UniqueGroups(standalone, DEFAULT_SUFFIX)
    test_uniquegroup = uniquegroups.get(uniquegroup_name)
    test_uniquegroup.delete()

    log.info('Test empty dsidm uniquegroup list with json')
    topology_st.logcap.flush()
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


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_uniquegroup_members_add_remove(topology_st, create_test_uniquegroup):
    """ Test dsidm uniquegroup members, add_member and remove_members options

    :id: 4903f069-c7b7-4b80-844c-6fb4b320a5b4
    :setup: Standalone instance
    :steps:
        1. Show members of a uniquegroup using dsidm uniquegroup members
        2. Add member to the uniquegroup using dsidm uniquegroup add_member
        3. Verify the added member is associated with the uniquegroup using dsidm uniquegroup member
        4. Remove the member from the uniquegroup using dsidm uniquegroup remove_member
        5. Verify the member is no longer associated with the uniquegroup using dsidm uniquegroup member
    :expectedresults:
        1. Uniquegroup has no members
        2. Member is successfully added
        3. Shows previously added member
        4. Member is successfully removed
        5. Uniquegroup has no members
    """

    standalone = topology_st.standalone

    member = 'uid=new_member'
    output_no_member = 'No members to display'
    output_with_member = 'dn: {}'.format(member)
    output_add_member = 'added member: {}'.format(member)
    output_remove_member = 'removed member: {}'.format(member)

    args = FakeArgs()
    args.cn = uniquegroup_name
    args.json = False

    log.info('Test dsidm uniquegroup members to show no associated members')
    members(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_no_member)

    log.info('Test dsidm uniquegroup add_member')
    args.dn = member
    add_member(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_add_member)

    log.info('Verify the added member is associated with the uniquegroup')
    members(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_with_member)

    # Test json
    args.json = True
    members(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    assert len(json_result['members']) == 1
    args.json = False

    log.info('Test dsidm uniquegroup remove_member')
    remove_member(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_remove_member)

    log.info('Verify the added member is no longer associated with the uniquegroup')
    members(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_no_member)

    # Test json
    args.json = True
    members(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    assert len(json_result['members']) == 0
    args.json = False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))