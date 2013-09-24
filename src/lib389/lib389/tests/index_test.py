# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import ldap

from lib389.topologies import topology_st

from lib389.backend import Backends
from lib389.index import Indexes

def test_default_index_list(topology_st):
    indexes = Indexes(topology_st.standalone)
    # create and delete a default index.

    index = indexes.create(properties={
        'cn': 'modifytimestamp',
        'nsSystemIndex': 'false',
        'nsIndexType': 'eq'
        })
    default_index_list = indexes.list()
    found = False
    for i in default_index_list:
        if i.dn.startswith('cn=modifytimestamp'):
            found = True
    assert found
    index.delete()

    default_index_list = indexes.list()
    found = False
    for i in default_index_list:
        if i.dn.startswith('cn=modifytimestamp'):
            found = True
    assert not found

def test_backend_index(topology_st):
    backends = Backends(topology_st.standalone)
    ur_backend = backends.get('userRoot')
    ur_indexes = ur_backend.get_indexes()

    index = ur_indexes.create(properties={
        'cn': 'modifytimestamp',
        'nsSystemIndex': 'false',
        'nsIndexType': 'eq'
        })

    index_list = ur_indexes.list()
    found = False
    for i in index_list:
        if i.dn.startswith('cn=modifytimestamp'):
            found = True
    assert found
    index.delete()

    index_list = ur_indexes.list()
    found = False
    for i in index_list:
        if i.dn.startswith('cn=modifytimestamp'):
            found = True
    assert not found



