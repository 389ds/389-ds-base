# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


import io
import sys
import pytest
import time
import json
from lib389.cli_base import LogCapture, FakeArgs
from lib389.utils import *
from lib389._constants import *
from lib389.idm.nscontainer import nsContainers
from lib389.topologies import topology_m2 as topo
from lib389.cli_conf.conflicts import (list_conflicts, cmp_conflict, del_conflict, swap_conflict,
                                       convert_conflict, list_glue, del_glue, convert_glue)
from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")


def _create_container(inst, dn, name):
    """Creates container entry"""
    containers = nsContainers(inst, dn)
    container = containers.create(properties={'cn': name})
    time.sleep(1)
    return container


def _delete_container(container):
    """Deletes container entry"""
    container.delete()
    time.sleep(1)


def test_conflict_cli(topo):
    """Test manageing replication conflict entries

    :id: 800f432a-52ab-4661-ac66-a2bdd9b984d8
    :setup: two masters
    :steps:
        1. Create replication conflict entries
        2. List conflicts
        3. Compare conflict entry
        4. Delete conflict
        5. Resurrect conflict
        6. Swap conflict
        7. List glue entry
        8. Delete glue entry
        9. Convert glue entry

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
    """

    # Setup our default parameters for CLI functions
    topo.logcap = LogCapture()
    sys.stdout = io.StringIO()
    args = FakeArgs()
    args.DN = ""
    args.suffix = DEFAULT_SUFFIX
    args.json = True

    m1 = topo.ms["master1"]
    m2 = topo.ms["master2"]

    topo.pause_all_replicas()

    # Create entries
    _create_container(m1, DEFAULT_SUFFIX, 'conflict_parent1')
    _create_container(m2, DEFAULT_SUFFIX, 'conflict_parent1')
    _create_container(m1, DEFAULT_SUFFIX, 'conflict_parent2')
    _create_container(m2, DEFAULT_SUFFIX, 'conflict_parent2')
    cont_parent_m1 = _create_container(m1, DEFAULT_SUFFIX, 'conflict_parent3')
    cont_parent_m2 = _create_container(m2, DEFAULT_SUFFIX, 'conflict_parent3')
    cont_glue_m1 = _create_container(m1, DEFAULT_SUFFIX, 'conflict_parent4')
    cont_glue_m2 = _create_container(m2, DEFAULT_SUFFIX, 'conflict_parent4')

    # Create the conflicts
    _delete_container(cont_parent_m1)
    _create_container(m2, cont_parent_m2.dn, 'conflict_child1')
    _delete_container(cont_glue_m1)
    _create_container(m2, cont_glue_m2.dn, 'conflict_child2')

    # Resume replication
    topo.resume_all_replicas()
    time.sleep(5)

    # Test "list"
    list_conflicts(m2, None, topo.logcap.log, args)
    conflicts = json.loads(topo.logcap.outputs[0].getMessage())
    assert len(conflicts['items']) == 4
    conflict_1_DN = conflicts['items'][0]['dn']
    conflict_2_DN = conflicts['items'][1]['dn']
    conflict_3_DN = conflicts['items'][2]['dn']
    topo.logcap.flush()

    # Test compare
    args.DN = conflict_1_DN
    cmp_conflict(m2, None, topo.logcap.log, args)
    conflicts = json.loads(topo.logcap.outputs[0].getMessage())
    assert len(conflicts['items']) == 2
    topo.logcap.flush()

    # Test delete
    del_conflict(m2, None, topo.logcap.log, args)
    list_conflicts(m2, None, topo.logcap.log, args)
    conflicts = json.loads(topo.logcap.outputs[0].getMessage())
    assert len(conflicts['items']) == 3
    topo.logcap.flush()

    # Test swap
    args.DN = conflict_2_DN
    swap_conflict(m2, None, topo.logcap.log, args)
    list_conflicts(m2, None, topo.logcap.log, args)
    conflicts = json.loads(topo.logcap.outputs[0].getMessage())
    assert len(conflicts['items']) == 2
    topo.logcap.flush()

    # Test conflict convert
    args.DN = conflict_3_DN
    args.new_rdn = "cn=testing convert"
    convert_conflict(m2, None, topo.logcap.log, args)
    list_conflicts(m2, None, topo.logcap.log, args)
    conflicts = json.loads(topo.logcap.outputs[0].getMessage())
    assert len(conflicts['items']) == 1
    topo.logcap.flush()

    # Test list glue entries
    list_glue(m2, None, topo.logcap.log, args)
    glues = json.loads(topo.logcap.outputs[0].getMessage())
    assert len(glues['items']) == 2
    topo.logcap.flush()

    # Test delete glue entries
    args.DN = "cn=conflict_parent3,dc=example,dc=com"
    del_glue(m2, None, topo.logcap.log, args)
    list_glue(m2, None, topo.logcap.log, args)
    glues = json.loads(topo.logcap.outputs[0].getMessage())
    assert len(glues['items']) == 1
    topo.logcap.flush()

    # Test convert glue entries
    args.DN = "cn=conflict_parent4,dc=example,dc=com"
    convert_glue(m2, None, topo.logcap.log, args)
    list_glue(m2, None, topo.logcap.log, args)
    glues = json.loads(topo.logcap.outputs[0].getMessage())
    assert len(glues['items']) == 0
    topo.logcap.flush()
