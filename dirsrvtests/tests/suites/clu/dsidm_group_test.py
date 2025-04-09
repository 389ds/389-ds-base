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
from lib389.cli_idm.group import (list, get, get_dn, create, delete, modify, rename,
                                  members, add_member, remove_member)
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older, ensure_str
from lib389.idm.group import Groups
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


group_name = "test_group_2000"

@pytest.fixture(scope="function")
def create_test_group(topology_st, request):
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create test group')
    if groups.exists(group_name):
        test_group = groups.get(group_name)
        test_group.delete()

    properties = FakeArgs()
    properties.cn = group_name
    create(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, properties)
    test_group = groups.get(group_name)

    def fin():
        log.info('Delete test group')
        if test_group.exists():
            test_group.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def add_description(topology_st, request):
    args = FakeArgs()
    args.selector = group_name
    args.changes = ['add:description:{}'.format(group_name)]
    modify(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_create(topology_st):
    """ Test dsidm group create option

    :id: 56e31cf5-0fbf-4693-b8d3-bc6f545d7734
    :setup: Standalone instance
    :steps:
        1. Run dsidm group create
        2. Check that a message is provided on creation
        3. Check that created group exists
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    output = 'Successfully created {}'.format(group_name)

    args = FakeArgs()
    args.cn = group_name
    args.description = "my description"

    log.info('Test dsidm group create')
    create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that group is present')
    groups = Groups(standalone, DEFAULT_SUFFIX)
    new_group = groups.get(group_name)
    assert new_group.exists()
    assert new_group.present('description', 'my description')

    log.info('Clean up for next test')
    new_group.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_delete(topology_st, create_test_group):
    """ Test dsidm group delete option

    :id: d285db5d-8efd-4775-b5f4-47bf27106da6
    :setup: Standalone instance
    :steps:
        1. Run dsidm group delete on a created group
        2. Check that a message is provided on deletion
        3. Check that group does not exist
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    groups = Groups(standalone, DEFAULT_SUFFIX)
    test_group = groups.get(group_name)
    output = 'Successfully deleted {}'.format(test_group.dn)

    args = FakeArgs()
    args.dn = test_group.dn

    log.info('Test dsidm group delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that group does not exist')
    assert not test_group.exists()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_modify(topology_st, create_test_group):
    """ Test dsidm group modify option

    :id: 43611907-b477-4793-9ed6-8cf2d43ddd6b
    :setup: Standalone instance
    :steps:
        1. Run dsidm group modify add description value
        2. Run dsidm group modify replace description value
        3. Run dsidm group modify delete description value
    :expectedresults:
        1. description value is present
        2. description value is replaced with the new one
        3. description value is deleted
    """

    standalone = topology_st.standalone
    groups = Groups(standalone, DEFAULT_SUFFIX)
    test_group = groups.get(group_name)
    output = 'Successfully modified {}'.format(test_group.dn)

    args = FakeArgs()
    args.selector = group_name

    log.info('Test dsidm group modify add')
    args.changes = ['add:description:test']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_group.present('description', 'test')

    log.info('Test dsidm group modify replace')
    args.changes = ['replace:description:replaced']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_group.present('description', 'replaced')

    log.info('Test dsidm group modify delete')
    args.changes = ['delete:description:replaced']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert not test_group.present('description', 'replaced')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_list(topology_st, create_test_group):
    """ Test dsidm group list option

    :id: e10c9430-1ea5-4b81-98c0-524f4f7da0e6
    :setup: Standalone instance
    :steps:
        1. Run dsidm group list option without json
        2. Check the output content is correct
        3. Run dsidm group list option with json
        4. Check the output content is correct
        5. Delete the group
        6. Check the group is not in the list with json
        7. Check the group is not in the list without json
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

    log.info('Test dsidm group list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=group_name)

    log.info('Test dsidm group list with json')
    args.json = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value=group_name)

    log.info('Delete the group')
    groups = Groups(standalone, DEFAULT_SUFFIX)
    testgroup = groups.get(group_name)
    testgroup.delete()

    log.info('Test empty dsidm group list with json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value_not=group_name)

    log.info('Test empty dsidm group list without json')
    args.json = False
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value_not=group_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_get_dn(topology_st, create_test_group):
    """ Test dsidm group get_dn option

        :id: c1d22d33-6431-45c6-9cd1-703225d8db50
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
    groups = Groups(standalone, DEFAULT_SUFFIX)
    test_group = groups.get(group_name)
    args = FakeArgs()
    args.dn = test_group.dn
    args.json = False

    log.info('Empty the log file to prevent false data to check about group')
    topology_st.logcap.flush()

    log.info('Test dsidm group get_dn without json')
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=group_name)

    log.info('Test dsidm group get_dn with json')
    args.json = True
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=group_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_get_rdn(topology_st, create_test_group, add_description):
    """ Test dsidm group get option

    :id: 8578005b-9dbf-4aa9-bcd4-1431f44bbff7
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
    groups = Groups(standalone, DEFAULT_SUFFIX)
    test_group = groups.get(group_name)

    group_content = ['dn: {}'.format(test_group.dn),
                    'cn: {}'.format(test_group.rdn),
                    'description: {}'.format(test_group.rdn),
                    'objectClass: top',
                    'objectClass: groupOfNames',
                    'objectClass: nsMemberOf']

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'groupOfNames',
                    'nsMemberOf',
                    'cn',
                    test_group.rdn,
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
                    test_group.dn]

    args = FakeArgs()
    args.json = False
    args.selector = group_name

    log.info('Empty the log file to prevent false data to check about group')
    topology_st.logcap.flush()

    log.info('Test dsidm group get without json')
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=group_content)

    log.info('Test dsidm group get with json')
    args.json = True
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_rename(topology_st, create_test_group, add_description):
    """ Test dsidm group rename option

    :id: 9d4b4509-3adf-41b1-8e67-d23529b251b6
    :setup: Standalone instance
    :steps:
         1. Run dsidm group rename option on created group
         2. Check the group does not have another cn attribute with the old rdn
         3. Check the old group is deleted
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    groups = Groups(standalone, DEFAULT_SUFFIX)
    test_group = groups.get(group_name)

    args = FakeArgs()
    args.selector = group_name
    args.new_name = 'new_group'
    args.keep_old_rdn = False

    log.info('Test dsidm group rename')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_group = groups.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_group.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new group does not have cn attribute with the old rdn')
    assert not new_group.present('cn', group_name)
    assert new_group.get_attr_val_utf8('description') == group_name

    log.info('Verify old group dn does not exist')
    assert not test_group.exists()

    log.info('Clean up')
    new_group.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_rename_keep_old_rdn(topology_st, create_test_group, add_description):
    """ Test dsidm group rename option with keep-old-rdn

    :id: 2c0d623b-9dab-4ca3-ae32-de4332a2ed4d
    :setup: Standalone instance
    :steps:
         1. Run dsidm group rename option on created group with keep-old-rdn
         2. Check the group has another cn attribute with the old rdn
         3. Check the old group is deleted
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    groups = Groups(standalone, DEFAULT_SUFFIX)
    test_group = groups.get(group_name)

    args = FakeArgs()
    args.selector = group_name
    args.new_name = 'new_group'
    args.keep_old_rdn = True

    log.info('Test dsidm group rename with keep-old-rdn')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_group = groups.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_group.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new group has cn attribute with the old rdn')
    assert new_group.present('cn', group_name)
    assert new_group.get_attr_val_utf8('description') == group_name

    log.info('Verify old group dn does not exist')
    assert not test_group.exists()

    log.info('Clean up')
    new_group.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_group_members_add_remove(topology_st, create_test_group):
    """ Test dsidm group members, add_member and remove_members options

    :id: c0839a0a-5519-4762-927c-6cb2accff2fe
    :setup: Standalone instance
    :steps:
        1. Show members of a group using dsidm group members
        2. Add member to the group using dsidm group add_member
        3. Verify the added member is associated with the group using dsidm group member
        4. Remove the member from the group using dsidm group remove_member
        5. Verify the member is no longer associated with the group using dsidm group member
    :expectedresults:
        1. Group has no members
        2. Member is successfully added
        3. Shows previously added member
        4. Member is successfully removed
        5. Group has no members
    """

    standalone = topology_st.standalone

    member = 'uid=new_member'
    output_no_member = 'No members to display'
    output_with_member = 'dn: {}'.format(member)
    output_add_member = 'added member: {}'.format(member)
    output_remove_member = 'removed member: {}'.format(member)

    args = FakeArgs()
    args.cn = group_name
    args.json = False

    log.info('Test dsidm group members to show no associated members')
    members(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_no_member)

    log.info('Test dsidm group add_member')
    args.dn = member
    add_member(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_add_member)

    log.info('Verify the added member is associated with the group')
    members(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_with_member)

    # Test json
    args.json = True
    members(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    assert len(json_result['members']) == 1
    args.json = False

    log.info('Test dsidm group remove_member')
    remove_member(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output_remove_member)

    log.info('Verify the added member is no longer associated with the group')
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
    pytest.main("-s %s" % CURRENT_FILE)
