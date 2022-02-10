# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import io
import sys
import pytest
import time

from lib389.cli_conf.backend import (backend_list, backend_get, backend_set, backend_get_dn,
                                     backend_create, backend_delete, backend_export,
                                     backend_import, backend_add_index, backend_list_index,
                                     backend_set_index, backend_get_index, backend_reindex,
                                     backend_del_index, backend_attr_encrypt, get_monitor,
                                     backend_create_vlv, backend_del_vlv, backend_create_vlv_index,
                                     backend_get_vlv, backend_edit_vlv, backend_reindex_vlv,
                                     backend_list_vlv)

from lib389.cli_base import LogCapture, FakeArgs
from lib389.tests.cli import check_output
from lib389.topologies import topology_st

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

stdout = sys.__stdout__
BE_NAME = 'backendRoot'
SUFFIX = 'dc=backend,dc=test'
SUB_BE_NAME = 'subBackendRoot'
SUB_SUFFIX = 'dc=sub_suffix,dc=backend,dc=test'


@pytest.fixture(scope="function")
def create_backend(topology_st, request):
    """Create backend "dc=backend,dc=test" / backendRoot
    """
    sys.stdout = io.StringIO()

    args = FakeArgs()
    args.cn = BE_NAME
    args.be_name = BE_NAME
    args.suffix = False
    args.nsslapd_suffix = SUFFIX
    args.skip_subsuffixes = False
    args.json = False
    args.parent_suffix = False
    args.create_entries = True

    args.suffix = SUFFIX
    backend_create(topology_st.standalone, None, None, args)
    check_output("The database was successfully created")

    def fin():
        sys.stdout = io.StringIO()
        args = FakeArgs()
        args.cn = BE_NAME
        args.be_name = BE_NAME
        args.suffix = SUFFIX
        args.skip_subsuffixes = False
        args.json = False

        # Delete backend
        backend_delete(topology_st.standalone, None, None, args, warn=False)
        check_output("successfully deleted")

        # Verify it's removed
        args.suffix = False
        backend_list(topology_st.standalone, None, None, args)
        check_output("backendroot", missing=True)

    request.addfinalizer(fin)


def test_backend_cli(topology_st, create_backend):
    """Test creating, listing, getting, and deleting a backend (and subsuffix)

    :id: 8a5da6ce-c24a-4d0d-9afb-dfa44ecf4145
    :setup: Standalone instance
    :steps:
        1. List backends
        2. Get backend by suffix
        3. Get backend by DN
        4. Add subsuffix
        5. Verify subsuffix
        6. Modify subsuffix
        7. Delete subsuffix
        8. Verify subsuffix is removed
        9. Modify backend
        10. Verify modify worked
        11. Test monitor works
    :expectedresults:
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
    """
    topology_st.logcap = LogCapture()
    sys.stdout = io.StringIO()

    args = FakeArgs()
    args.cn = BE_NAME
    args.be_name = BE_NAME
    args.suffix = False
    args.nsslapd_suffix = SUFFIX
    args.skip_subsuffixes = False
    args.json = False
    args.parent_suffix = False
    args.create_entries = True

    # List backend
    backend_list(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output(SUFFIX)

    # Get backend by by name
    args.selector = BE_NAME
    backend_get(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output(BE_NAME)

    # Get backend by DN
    args.dn = 'cn=backendRoot,cn=ldbm database,cn=plugins,cn=config'
    backend_get_dn(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output(BE_NAME)

    # Add subsuffix
    args.parent_suffix = SUFFIX
    args.suffix = SUB_SUFFIX
    args.be_name = SUB_BE_NAME
    backend_create(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("The database was successfully created")

    # Verify subsuffix
    args.suffix = False
    backend_list(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output(SUB_SUFFIX)

    # Modify subsuffix
    args.enable = False
    args.disable = False
    args.add_referral = False
    args.del_referral = False
    args.cache_size = False
    args.cache_memsize = False
    args.dncache_memsize = False
    args.enable_readonly = True  # Setting nsslapd-readonly to "on"
    args.disable_readonly = False
    backend_set(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("successfully updated")

    # Verify modified worked
    args.selector = SUB_BE_NAME
    backend_get(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("nsslapd-readonly: on")

    # Delete subsuffix
    args.suffix = SUB_SUFFIX
    backend_delete(topology_st.standalone, None, topology_st.logcap.log, args, warn=False)
    check_output("successfully deleted")

    # Verify it is deleted
    args.suffix = False
    backend_list(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output(SUB_BE_NAME, missing=True)

    # Modify backend (use same args from subsuffix modify)
    args.be_name = BE_NAME
    backend_set(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("successfully updated")

    # Verify modified worked
    args.selector = BE_NAME
    backend_get(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("nsslapd-readonly: on")

    # Run database monitor
    args.suffix = SUFFIX
    get_monitor(topology_st.standalone, None, topology_st.logcap.log, args)
    check_output("entrycachetries")

    # Done!


def test_indexes(topology_st, create_backend):
    """Test creating, listing, getting, and deleting an index

    :id: 74e64fea-72c2-4fec-a1fe-bd2d72a075f5
    :setup: Standalone instance
    :steps:
        1. Add index (description)
        2. Verify index was added
        3. Modify index (Add type)
        4. Verify index was modified
        5. Modify index (Delete type)
        6. Verify index was modified
        7. Modify index (Add MR)
        8. Verify index was modified
        9. Modify index (Delete MR)
        10. Verify index was modified
        11. Reindex index
        12. Remove index
        13. Verify index was removed

    :expectedresults:
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
    """
    sys.stdout = io.StringIO()

    args = FakeArgs()
    args.cn = BE_NAME
    args.be_name = BE_NAME
    args.suffix = False
    args.nsslapd_suffix = SUFFIX
    args.attr = 'description'
    args.index_type = 'eq'
    args.matching_rule = None
    args.reindex = False
    args.json = False
    args.just_names = False
    args.add_type = None
    args.del_type = None
    args.add_mr = None
    args.del_mr = None

    # Add an index
    backend_add_index(topology_st.standalone, None, None, args)
    check_output("added index")

    # List indexes
    backend_list_index(topology_st.standalone, None, None, args)
    check_output("cn: description")

    # Modify index (Add type)
    args.add_type = ['sub']
    backend_set_index(topology_st.standalone, None, None, args)
    args.add_type = None
    check_output("successfully updated")

    # Verify type was added
    args.attr = ['description']
    backend_get_index(topology_st.standalone, None, None, args)
    check_output("nsindextype: sub")

    # Remove index type sub
    args.attr = 'description'

    args.del_type = ['sub']
    backend_set_index(topology_st.standalone, None, None, args)
    args.del_type = None
    check_output("successfully updated")

    # Verify type was removed
    args.attr = ['description']
    backend_get_index(topology_st.standalone, None, None, args)
    check_output("nsindextype: sub", missing=True)

    # Modify index (add MR)
    args.attr = 'description'
    args.add_mr = ['1.1.1.1.1.1']
    backend_set_index(topology_st.standalone, None, None, args)
    args.add_mr = None
    check_output("successfully updated")

    # Verify MR was added
    args.attr = ['description']
    backend_get_index(topology_st.standalone, None, None, args)
    check_output("nsmatchingrule: 1.1.1.1.1.1")

    # Modify index (delete MR)
    args.attr = 'description'
    args.del_mr = ['1.1.1.1.1.1']
    backend_set_index(topology_st.standalone, None, None, args)
    args.del_mr = None
    check_output("successfully updated")

    # Verify MR was added
    args.attr = ['description']
    backend_get_index(topology_st.standalone, None, None, args)
    check_output("nsmatchingrule: 1.1.1.1.1.1", missing=True)

    # Reindex index
    backend_reindex(topology_st.standalone, None, None, args)
    check_output("reindexed database")
    time.sleep(2)

    # Delete index
    backend_del_index(topology_st.standalone, None, None, args)
    check_output("deleted index")

    # Verify index was removed
    backend_list_index(topology_st.standalone, None, None, args)
    check_output("cn: description", missing=True)


def test_attr_encrypt(topology_st, create_backend):
    """Test adding/removing encrypted attrs

    :id: 887429d5-b9be-4e48-b8ca-691e7437abca
    :setup: Standalone instance
    :steps:
        1. Add encrypted attr
        2. Verify it succeeded
        3. Delete encrypted attr
        4. Verity it was removed
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success

    """
    sys.stdout = io.StringIO()
    args = FakeArgs()
    args.cn = BE_NAME
    args.be_name = BE_NAME
    args.suffix = False
    args.nsslapd_suffix = SUFFIX
    args.json = False
    args.just_names = False
    args.list = False
    args.add_attr = None
    args.del_attr = None

    # Add an encrytped attr
    args.add_attr = ['description']
    backend_attr_encrypt(topology_st.standalone, None, None, args)
    args.add_attr = None
    check_output("added encrypted attribute")

    # Verify it worked
    args.list = True
    backend_attr_encrypt(topology_st.standalone, None, None, args)
    args.list = False
    check_output("cn: description")

    # Delete encrypted attr
    args.del_attr = ['description']
    backend_attr_encrypt(topology_st.standalone, None, None, args)
    args.del_attr = None
    check_output("deleted encrypted attribute")

    # Verify it worked
    args.list = True
    backend_attr_encrypt(topology_st.standalone, None, None, args)
    args.list = False
    check_output("cn: description", missing=True)


def test_import_export(topology_st):
    BE_NAME = 'userRoot'
    EXCLUDE_SUFFIX = "ou=Groups,dc=example,dc=com"
    LDIF_NAME = "test_import_export.ldif"
    LDIF_PATH = os.path.join(topology_st.standalone.ds_paths.ldif_dir, LDIF_NAME)
    topology_st.logcap = LogCapture()
    args = FakeArgs()

    # Export the backend
    args.be_names = [BE_NAME]
    args.ldif = LDIF_NAME
    args.use_id2entry = None
    args.encrypted = None
    args.min_base64 = None
    args.no_dump_uniq_id = None
    args.replication = None
    args.not_folded = None
    args.no_seq_num = None
    args.include_suffixes = None
    args.exclude_suffixes = [EXCLUDE_SUFFIX]
    backend_export(topology_st.standalone, None, topology_st.logcap.log, args)

    # Verify export worked
    assert os.path.exists(LDIF_PATH)
    with open(LDIF_PATH, 'r') as ldif:
        for line in ldif:
            assert not line.endswith("%s\n" % EXCLUDE_SUFFIX)

    # Import the backend
    args.be_name = BE_NAME
    args.ldifs = [LDIF_NAME]
    args.chunks_size = None
    args.encrypted = None
    args.gen_uniq_id = None
    args.only_core = None
    args.include_suffixes = None
    args.exclude_suffixes = None
    backend_import(topology_st.standalone, None, topology_st.logcap.log, args)
    os.remove(LDIF_PATH)

    # Done!


def test_vlv(topology_st, create_backend):
    """Test creating, listing, getting, and deleting vlv's

    :id: b38bfcf1-f71f-4c08-9221-10f45b4c9b34
    :setup: Standalone instance
    :steps:
        1. Add VLV search and index entries
        2. Verify they are created
        3. Edit VLV search and verify change
        4. Create additional VLV indexes
        5. Verity new indedxes were created
        6. Remove VLV indexes
        7. Verify indexes were removed
        8. Reindex VLV
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success

    """
    sys.stdout = io.StringIO()
    args = FakeArgs()
    args.cn = BE_NAME
    args.be_name = BE_NAME
    args.suffix = False
    args.nsslapd_suffix = SUFFIX
    args.json = False
    args.name = "myVLVSearch"
    args.index_name = "myVLVIndex"
    args.search_base = SUFFIX
    args.search_scope = '2'
    args.search_filter = "cn=*"
    args.parent_name = args.name
    args.index = False
    args.reindex = False
    args.sort = "cn sn"
    args.just_names = False


    # Create vlv search
    backend_create_vlv(topology_st.standalone, None, None, args)
    check_output("created new VLV Search entry")

    # Verify search is present
    backend_get_vlv(topology_st.standalone, None, None, args)
    check_output("VLV Search:")

    # Create VLV index under vlvSearch
    backend_create_vlv_index(topology_st.standalone, None, None, args)
    check_output("created new VLV index entry")

    # Verify index is present
    backend_get_vlv(topology_st.standalone, None, None, args)
    check_output("VLV Index:")

    # Edit VLV Search
    args.search_base = None
    args.search_scope = '0'
    args.search_filter = None
    args.sort = None
    backend_edit_vlv(topology_st.standalone, None, None, args)
    check_output("updated VLV search entry")

    # Verify edit was successful
    backend_get_vlv(topology_st.standalone, None, None, args)
    check_output("vlvscope: 0")

    # List vlv searches
    backend_list_vlv(topology_st.standalone, None, None, args)
    check_output("vlvbase: " + SUFFIX)

    # Add another index
    args.index_name = "my2ndVLVIndex"
    args.sort = "uid givenname"
    backend_create_vlv_index(topology_st.standalone, None, None, args)
    check_output("created new VLV index entry")

    # Verify new index was created
    backend_get_vlv(topology_st.standalone, None, None, args)
    check_output("vlvsort: uid givenname")

    # Reindex VLV
    backend_reindex_vlv(topology_st.standalone, None, None, args)
    check_output("reindexed VLV indexes")
    time.sleep(2)

    # Delete VLV search and indexes
    backend_del_vlv(topology_st.standalone, None, None, args)
    check_output("deleted VLV search and its indexes")

    # List vlv searches/indexes
    backend_list_vlv(topology_st.standalone, None, None, args)
    check_output("")

    # Done!
