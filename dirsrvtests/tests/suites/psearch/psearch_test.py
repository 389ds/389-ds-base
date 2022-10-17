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
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
