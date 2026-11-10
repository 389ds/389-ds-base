---
name: write-test
description: >-
  Add or extend a pytest integration test for 389 Directory Server under
  dirsrvtests/. Use when asked to add a test, write a regression test, create a
  new test file or suite, add a CLI test, or extend the test for X. Enforces the
  hard conventions - topology fixtures imported from test389.topologies
  (lib389.topologies does not exist), one unique :id: UUID per test docstring,
  a module-level tier marker (pytestmark), DEBUGGING-aware cleanup finalizers,
  and the testimony plus duplicate-id gates CI runs on every PR. C unit tests
  (cmocka) are covered by references/cmocka.md.
---

# Write a Test

The full test-framework contract (fixture table, docstring fields, environment variables) is docs/agents/testing.md. For C unit tests, read references/cmocka.md instead of this page.

1. **Place the file.** New tests go in `dirsrvtests/tests/suites/<area>/` — pick `<area>` by the FEATURE under test, not by the C file you changed (a reindex bug in backend code is tested under `suites/indexes/`). Stress/load reproducers go under `dirsrvtests/tests/stress/<area>/`. Note: `suites/` is the only tree the per-PR pytest workflows run and the only tree the docstring gates check — `tickets/`, `stress/`, `perf/`, `longduration/` get neither. Name the file `<name>_test.py`: a file matching neither of pytest's default collection patterns is never collected, and nothing in the repo overrides them.

   Verify: your path matches `dirsrvtests/tests/suites/<area>/<name>_test.py`.

2. **Start from the canonical preamble** (used by essentially every suite file):

   ```python
   import logging
   import os
   import pytest
   from lib389._constants import *
   from test389.topologies import topology_st as topo

   pytestmark = pytest.mark.tier1
   DEBUGGING = os.getenv("DEBUGGING", default=False)
   logging.getLogger(__name__).setLevel(logging.DEBUG if DEBUGGING else logging.INFO)
   log = logging.getLogger(__name__)
   ```

   Prepend the `# --- BEGIN COPYRIGHT BLOCK ---` header copied from a neighboring suite file (keep the trailing bare `#`, update the year; canonical blocks in `docs/agents/contributing.md`). Most suites spell the log-level choice as a 4-line if/else — both forms are fine. Add `import ldap` when the test expects LDAP errors (`pytest.raises(ldap.UNWILLING_TO_PERFORM)` and friends). `tier1` is the default for feature tests; stress reproducers use `tier3`.

   **Never import `lib389.topologies` — it does not exist.** Fixtures live in `test389.topologies` (under `dirsrvtests/lib/`, put on `sys.path` by the root `dirsrvtests/conftest.py`).

   Optional scaffold: `python3 dirsrvtests/create_test.py -s <name> [-i N | -m N -b N -c N]` emits `<name>_test.py` with this preamble, the topology import (or a `create_topology(..., request=request)` fixture for custom shapes) and a docstring skeleton carrying a fresh `:id:`. The file lands in the cwd — you still move it into the suite directory, replace the placeholder docstring steps, and write the test body.

3. **Pick the topology fixture** from the table in docs/agents/testing.md — `topology_st` (standalone), `topology_i2` (2 standalones), `topology_m1`/`topology_m2`/`topology_m3`/`topology_m4` (suppliers), `topology_m1c1`/`topology_m2c2` (+ consumers), `topology_m1h1c1` (+ hub). All are module-scoped, so state written by one test leaks into the rest of the file. The test's parameter name must equal the import alias: `import topology_st as topo` means `def test_x(topo):`. `.standalone` exists only on single-instance topologies — elsewhere use `.ms["supplier1"]`, `.cs["consumer1"]`, `.hs["hub1"]`, `.ins["standalone1"]`.

   For a shape no fixture covers, `from test389.topologies import create_topology` and define a module fixture:

   ```python
   @pytest.fixture(scope="module")
   def topo(request):
       return create_topology({ReplicaRole.SUPPLIER: 1, ReplicaRole.CONSUMER: 2},
                              request=request)
   ```

   **Never call `create_topology()` without `request=request`** — you lose the finalizer AND the 85-minute SIGALRM timeout watchdog that force-stops hung ns-slapd before CI kills the job.

4. **Mint the `:id:`**: `python3 -c "import uuid; print(uuid.uuid4())"` (or `python3 dirsrvtests/create_test.py -u`). One fresh UUID per test function.

   Verify: `grep -rn "<your-uuid>" dirsrvtests/tests/suites/` matches only your new test.

   **Never copy an existing docstring — even into a comment**: the duplicate-id gate recursively searches for `:id:` tokens under `dirsrvtests/tests/suites/`, so a repeated value anywhere in that directory fails CI. Every function named `test_*` there — fixtures and helpers included — must carry its own `:id:` unique within that directory; name helpers without the `test_` prefix instead.

5. **Write the docstring.** Field order is canonical, `:id:` always first:

   ```python
   def test_example(topo):
       """One-line summary of the verified behavior

       :id: <your fresh UUID>
       :setup: Standalone instance
       :steps:
           1. Do the action under test
       :expectedresults:
           1. Success
       """
   ```

   Indent numbered entries 8 spaces as `N. text`; give every step a matching numbered result (they correspond 1:1 by convention). Optional fields (`:customerscenario: True`, `:parametrized: yes` on parametrized tests) go between `:id:` and `:setup:`.

6. **Cleanup.** The shared topology fixtures already register a finalizer (stops instances, disarms the watchdog) — with standard fixtures you only remove entries/config your test created. A hand-rolled fixture registers `request.addfinalizer(fin)` with the DEBUGGING-aware idiom, so a failed run can be inspected live:

   ```python
   def fin():
       if DEBUGGING:
           [inst.stop() for inst in topology]
       else:
           [inst.delete() for inst in topology]
   request.addfinalizer(fin)
   ```

   Any non-empty `DEBUGGING` value is truthy — `DEBUGGING=0` still enables debugging mode.

7. **Backend-conditional tests** (BDB-only or LMDB-only behavior) combine tier and skip in a `pytestmark` list:

   ```python
   from lib389.utils import get_default_db_lib
   pytestmark = [pytest.mark.tier1,
                 pytest.mark.skipif(get_default_db_lib() == "mdb",
                                    reason="Behavior under test is BDB-specific")]
   ```

8. **Run the gates before pushing**, from the repo root, exactly as CI's validate workflow does:

   ```bash
   python3 dirsrvtests/check_for_duplicate_ids.py dirsrvtests/tests/suites
   testimony validate -c dirsrvtests/testimony.yaml dirsrvtests/tests/suites
   ```

   Verify: the first prints `No duplicates found`; testimony exits 0 (`pip install testimony` if missing).

   **STOP if the duplicate-id check fails** — mint a new UUID for YOUR test; never edit someone else's `:id:`. Then run the new test via the verify-changes skill (it routes to the environment's build/test skill; never run pytest directly on an unprepared host).

## Maintenance

If a step no longer matches the code, update this skill in the same PR as the change that moved it.
