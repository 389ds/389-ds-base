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
from lib389._constants import DEFAULT_SUFFIX, DEFAULT_BENAME

from lib389.backend import Backends

pytestmark = pytest.mark.tier2

def test_49290_range_unindexed_notes(topology_st):
    """
    Ticket 49290 had a small collection of issues - the primary issue is
    that range requests on an attribute that is unindexed was not reporting
    notes=U. This asserts that:

    * When unindexed, the attr shows notes=U
    * when indexed, the attr does not
    """

    # First, assert that modifyTimestamp does not have an index. If it does,
    # delete it.
    topology_st.standalone.config.set('nsslapd-accesslog-logbuffering', 'off')
    backends = Backends(topology_st.standalone)
    backend = backends.get(DEFAULT_BENAME)
    indexes = backend.get_indexes()

    for i in indexes.list():
        i_cn = i.get_attr_val_utf8('cn')
        if i_cn.lower() == 'modifytimestamp':
            i.delete()
            topology_st.standalone.restart()

    # Now restart the server, and perform a modifyTimestamp range operation.
    # in access, we should see notes=U (or notes=A)
    results = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(modifyTimestamp>=0)', ['nsUniqueId',])
    access_lines_unindexed = topology_st.standalone.ds_access_log.match('.*notes=U.*')
    assert len(access_lines_unindexed) == 1

    # Now add the modifyTimestamp index and run db2index. This will restart
    # the server
    indexes.create(properties={
        'cn': 'modifytimestamp',
        'nsSystemIndex': 'false',
        'nsIndexType' : 'eq',
    })
    topology_st.standalone.stop()
    assert topology_st.standalone.db2index(DEFAULT_BENAME, attrs=['modifytimestamp'] )
    topology_st.standalone.start()

    # Now run the modifyTimestamp range query again. Assert that there is no
    # notes=U/A in the log
    results = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(modifyTimestamp>=0)', ['nsUniqueId',])
    access_lines_indexed = topology_st.standalone.ds_access_log.match('.*notes=U.*')
    # Remove the old lines too.
    access_lines_final = set(access_lines_unindexed) - set(access_lines_indexed)
    # Make sure we have no unindexed notes in the log.
    assert len(access_lines_final) == 0

