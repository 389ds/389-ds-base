# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

from argparse import Namespace
from unittest.mock import Mock, call

import pytest
from lib389.cli_conf import backend

pytestmark = pytest.mark.tier1


@pytest.mark.parametrize("exit_code, error", [
    (0, None),
    (None, "has an unknown outcome (exit code unavailable)"),
    (1, "failed with exit code 1"),
])
def test_backend_compact_reports_task_dn(monkeypatch, exit_code, error):
    """Check task reporting when compact-db waits for the server.

    :id: 11d6cfbd-2533-40fe-8882-e512d9671048
    :setup: Mocked compaction task
    :steps:
        1. Submit a changelog compaction task with a wait timeout.
        2. Simulate successful completion, no exit code, or failure.
    :expectedresults:
        1. The task DN is logged before waiting.
        2. The completion message or error identifies the same task.
    """
    events = Mock()
    task = events.task
    task.dn = "cn=example,cn=compact db,cn=tasks,cn=config"
    task.get_exit_code.return_value = exit_code
    monkeypatch.setattr(backend, "DBCompactTask", Mock(return_value=task))

    args = Namespace(only_changelog=True, timeout=5)
    if error is None:
        backend.backend_compact(None, None, events.log, args)
    else:
        with pytest.raises(ValueError) as exc:
            backend.backend_compact(None, None, events.log, args)
        assert task.dn in str(exc.value)
        assert error in str(exc.value)

    expected = [
        call.task.create(properties={"justChangelog": "yes"}),
        call.log.info(f"Started database compaction task {task.dn}"),
        call.task.wait(timeout=5),
        call.task.get_exit_code(),
    ]
    if error is None:
        expected.append(call.log.info(f"Database compaction task {task.dn} completed successfully"))
    assert events.mock_calls == expected
