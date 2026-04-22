# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import platform
import pytest

from lib389 import pid_from_file
from test389.topologies import topology_st

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _get_thread_names(pid):
    """Read thread names from /proc/<pid>/task/*/comm"""
    names = []
    task_dir = f"/proc/{pid}/task"
    if not os.path.isdir(task_dir):
        pytest.skip(f"{task_dir} not available")
    for tid in os.listdir(task_dir):
        comm_path = os.path.join(task_dir, tid, "comm")
        try:
            with open(comm_path, "r") as f:
                names.append(f.read().strip())
        except (FileNotFoundError, PermissionError):
            pass
    return names


def test_thread_names_present(topology_st):
    """Test that key threads have been named via pthread_setname_np

    :id: a95b4eca-e806-48dc-8c91-debbc3ef4053
    :setup: Standalone instance
    :steps:
        1. Get the server PID
        2. Read thread names from /proc/{pid}/task/{tid}/comm
        3. Verify expected thread names are present
    :expectedresults:
        1. PID is obtained
        2. Thread names are readable
        3. Core thread names (listener, worker, ct-list, event-q,
           housekeep) are found
    """
    inst = topology_st.standalone
    pid = str(pid_from_file(inst.ds_paths.pid_file))
    log.info(f"Server PID: {pid}")

    thread_names = _get_thread_names(pid)
    log.info(f"Found {len(thread_names)} threads: {sorted(set(thread_names))}")

    # Verify the listener thread
    assert "listener" in thread_names, \
        f"'listener' thread not found in {thread_names}"

    # Verify at least one worker thread
    worker_threads = [n for n in thread_names if n.startswith("worker-")]
    assert len(worker_threads) > 0, \
        f"No 'worker-*' threads found in {thread_names}"

    # Verify at least one connection table list thread
    ct_threads = [n for n in thread_names if n.startswith("ct-list-")]
    assert len(ct_threads) > 0, \
        f"No 'ct-list-*' threads found in {thread_names}"

    # Verify event queue thread
    assert "event-q" in thread_names, \
        f"'event-q' thread not found in {thread_names}"

    # Verify housekeeping thread
    assert "housekeep" in thread_names, \
        f"'housekeep' thread not found in {thread_names}"
