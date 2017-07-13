# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

from lib389.topologies import topology_st
from lib389._mapped_object import DSLdapObject
from lib389.idm.group import Group
from lib389._constants import DEFAULT_SUFFIX


def test_exists(topology_st):
    """
    Assert that exists method returns True when entry exists, else False.
    """
    group = Group(topology_st.standalone, dn="cn=MyTestGroup,ou=Groups," + DEFAULT_SUFFIX)
    assert not group.exists()
    group.create(properties={'cn': 'MyTestGroup', 'ou': 'groups'})
    assert group.exists()
