# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import os
import time
import pytest
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM
from test389.topologies import topology_st
from lib389.idm.group import Groups
from ldap.controls.psearch import PersistentSearchControl,EntryChangeNotificationControl

pytestmark = pytest.mark.tier1

def _run_psearch(inst, msg_id):
    """Run a search with EntryChangeNotificationControl"""

    results = []
    while True:
        try:
            _, data, _, _, _, _ = inst.result4(msgid=msg_id, all=0, timeout=1.0, add_ctrls=1, add_intermediates=1,
                                               resp_ctrl_classes={EntryChangeNotificationControl.controlType:EntryChangeNotificationControl})
            # See if there are any entry changes
            for dn, entry, srv_ctrls in data:
                ecn_ctrls = filter(lambda c: c.controlType == EntryChangeNotificationControl.controlType, srv_ctrls)
                if ecn_ctrls:
                    inst.log.info('%s has changed!' % dn)
                    results.append(dn)
        except ldap.TIMEOUT:
            # There are no more results, so we timeout.
            inst.log.info('No more results')
            return results


def test_psearch(topology_st):
    """Check basic Persistent Search control functionality

    :id: 4b395ef4-c3ff-49d1-a680-b9fdffa633bd
    :setup: Standalone instance
    :steps:
        1. Run an extended search with a Persistent Search control
        2. Create a new group (could be any entry)
        3. Run an extended search with a Persistent Search control again
        4. Check that entry DN is in the result
    :expectedresults:
        1. Operation should be successful
        2. Group should be successfully created
        3. Operation should be successful
        4. Entry DN should be in the result
    """

    # Create the search control
    psc = PersistentSearchControl()
    # do a search extended with the control
    msg_id = topology_st.standalone.search_ext(base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE, attrlist=['*'], serverctrls=[psc])
    # Get the result for the message id with result4
    _run_psearch(topology_st.standalone, msg_id)
    # Change an entry / add one
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'group1', 'description': 'testgroup'})
    # Now run the result again and see what's there.
    results = _run_psearch(topology_st.standalone, msg_id)
    # assert our group is in the changeset.
    assert(group.dn == results[0])


def test_psearch_gives_back_thread_slot(topology_st, request):
    """Check that ended persistent searches don't leave their connection blocked

    :id: b32fb822-dd05-49c7-a583-6666fc617930
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-maxthreadsperconn to 2 and restart
        2. On one connection, start a persistent search, then abandon it
        3. Run a normal search on the same connection
        4. Repeat steps 2 and 3 four times
    :expectedresults:
        1. Success
        2. Success
        3. The search succeeds: the ended persistent search gave back its
           max threads per connection slot
        4. Success every time
    """
    inst = topology_st.standalone
    orig = inst.config.get_attr_val_utf8("nsslapd-maxthreadsperconn")
    inst.config.replace("nsslapd-maxthreadsperconn", "2")
    inst.restart()

    def fin():
        inst.config.replace("nsslapd-maxthreadsperconn", orig)
        inst.restart()

    request.addfinalizer(fin)

    conn = ldap.initialize(f"ldap://{inst.host}:{inst.port}")
    conn.simple_bind_s(DN_DM, PW_DM)
    for i in range(4):
        msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectClass=*)", ["dn"],
                                serverctrls=[PersistentSearchControl(changesOnly=True)])
        # Let the persistent search thread start before abandoning it
        time.sleep(1)
        conn.abandon(msgid)
        time.sleep(1)
        # A leaked slot leaves the connection at its limit: this read never happens
        res = conn.search_ext_s("", ldap.SCOPE_BASE, "(objectClass=*)", ["dn"], timeout=10)
        assert len(res) == 1, f"round {i}: root DSE search failed"
    conn.unbind_s()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
