# Testing Guide — dirsrvtests

Integration tests are Python/pytest suites under `dirsrvtests/`. This document owns the dirsrvtests contract: layout, imports, CI gates, docstrings, topology fixtures, cleanup, markers, and environment variables. Build commands, the CI container, and the pytest job matrix belong to [building.md](building.md); the lib389 object API used inside test bodies to [lib389.md](lib389.md).

## Where tests live and how they are collected

| Path | Contents |
|------|----------|
| `dirsrvtests/tests/suites/<area>/` | Functional suites — the only tree CI runs. Each top-level suite directory becomes one CI job; `replication/` is split into one job per file ([building.md](building.md)) |
| `dirsrvtests/tests/tickets/` | Legacy per-bug regression tests (`ticket#####_test.py`); not run or gated by CI |
| `dirsrvtests/tests/stress/`, `perf/`, `longduration/` | Load, performance, and long-running tests; not run in CI |
| `dirsrvtests/tests/data/` | LDIF and TLS fixture data |
| `dirsrvtests/lib/test389/` | Test-only support package: `topologies.py`, `perftools.py` |
| `dirsrvtests/pytest.ini` | Registers the `tier0`–`tier3` markers; nothing else |
| `dirsrvtests/conftest.py` | Root hooks: `sys.path` setup, `--sanitizer` injection, failure reporting |

Name new files `<feature>_test.py` and put them under `dirsrvtests/tests/suites/<area>/`. `pytest.ini` sets no `python_files` override, so pytest's default patterns apply — a file matching neither `*_test.py` nor `test_*.py` is never collected, and the tree convention is `*_test.py`.

`pytest.ini` lives in `dirsrvtests/`, not the repo root. Always invoke pytest with a path under `dirsrvtests/` so that directory becomes the rootdir; a run from the repo root with no path loses both the ini and the conftest `sys.path` insert described next.

## Imports

Topology fixtures come from `test389.topologies`:

```python
from test389.topologies import topology_st as topo
```

- The `test389` package lives at `dirsrvtests/lib/test389/` and is never installed. The import resolves only because `dirsrvtests/conftest.py` inserts `dirsrvtests/lib` onto `sys.path` at module import. A standalone script run outside pytest does not see `test389`; `dirsrvtests/create_test.py` repeats the same insert itself for exactly that reason.
- `lib389.topologies` does not exist any more — never import it.
- The test function's parameter name must match the import alias: `import topology_st as topo` means the parameter is `topo`.

## The three CI gates

All three run in the validation workflow; which file, and the exact commands, are owned by [building.md](building.md).

| Gate | Scope | Enforces |
|------|-------|----------|
| testimony validation against `dirsrvtests/testimony.yaml` | `tests/suites/` only | every `test_*`-named function has a docstring with `:id:` |
| duplicate-id check, `dirsrvtests/check_for_duplicate_ids.py` | `tests/suites/` only | no `:id:` value appears twice |
| vermin, Python 3.8 floor | all of `dirsrvtests/` plus `src/lib389` | no `match` statements, no PEP 604 unions, no bare `list[str]` annotations |

- `:id:` is the only hard-required docstring field: `dirsrvtests/testimony.yaml` marks `id` as `required: True` and every other key optional. Write the full block anyway — the whole tree does.
- Testimony keys off the `test_` name prefix, not pytest collection: a fixture or helper named `test_*` under `dirsrvtests/tests/suites/` needs a `:id:` unique within that directory too. Prefer non-`test_` names for helpers.
- The duplicate-id check is a plain recursive text search over `dirsrvtests/tests/suites/` (`dirsrvtests/check_for_duplicate_ids.py (check_for_duplicates)`, `.github/workflows/validate.yml (Check for duplicate IDs step)`). Any `:id:` string there — in a comment, a fixture docstring, or another file — joins that directory's uniqueness namespace. Never copy a docstring, even into a comment.

## Docstring contract

Canonical shape, from `dirsrvtests/tests/suites/replication/acceptance_test.py (test_add_entry)`:

```python
def test_add_entry(topo_m4, create_entry):
    """Check that entries are replicated after add operation

    :id: 024250f1-5f7e-4f3b-a9f5-27741e6fd405
    :setup: Four suppliers replication setup, an entry
    :steps:
        1. Check entry on all other suppliers
    :expectedresults:
        1. The entry should be replicated to all suppliers
    """
```

- Field order: `:id:`, optionally `:customerscenario:` / `:parametrized:`, then `:setup:`, `:steps:`, `:expectedresults:`. Indent numbered entries under `:steps:` and `:expectedresults:` as shown (`N. text`, eight spaces).
- Steps and expected results should correspond one-to-one. That is convention, not machine-enforced — write them paired anyway.
- Mint ids with `python3 -c "import uuid; print(uuid.uuid4())"` or `python3 dirsrvtests/create_test.py -u`. Never change or reuse an existing `:id:`.

## Topology fixtures

Every fixture in `dirsrvtests/lib/test389/topologies.py` is `scope="module"`.

| Fixture | Roles | Instance access |
|---|---|---|
| `topology_st` | 1 standalone | `.standalone` |
| `topology_st_gssapi` | 1 standalone + Kerberos realm | `.standalone`, `.standalone.realm` |
| `topology_no_sample` | 1 standalone, no suffix data | `.standalone` |
| `topology_i2` | 2 standalone | `.ins["standalone1"]`, `.ins["standalone2"]` |
| `topology_i3` | 3 standalone | `.ins[...]` |
| `topology_m1` | 1 supplier | `.ms["supplier1"]` |
| `topology_m1c1` | 1 supplier + 1 consumer | `.ms[...]`, `.cs["consumer1"]` |
| `topology_m2` | 2 suppliers, meshed | `.ms["supplier1"]`, `.ms["supplier2"]` |
| `topology_m2_gssapi` | 2 suppliers + Kerberos | `.ms[...]` |
| `topology_m3` | 3 suppliers, meshed | `.ms["supplier1"]`..`.ms["supplier3"]` |
| `topology_m4` | 4 suppliers, meshed | `.ms["supplier1"]`..`.ms["supplier4"]` |
| `topology_m2c1` | 2 suppliers + 1 consumer | `.ms[...]`, `.cs["consumer1"]` |
| `topology_m2c2` | 2 suppliers + 2 consumers | `.ms[...]`, `.cs[...]` |
| `topology_m1h1c1` | 1 supplier + 1 hub + 1 consumer | `.ms[...]`, `.hs["hub1"]`, `.cs[...]` |

- `.standalone` exists only on single-instance topologies: `TopologyMain` sets it only when the sole instance is `standalone1`. On `topology_i2`, `topo.standalone` raises `AttributeError` — use `topo.ins["standalone1"]`.
- `.all_insts` maps every serverid to its instance, and iterating the topology object walks them all. Every fixture also attaches `.logcap`, which CLI tests assert against ([cli.md](cli.md)).
- Module scope means instances are created once per module, not per test: state written by one test leaks into the rest of the module. Isolation between modules comes from delete-at-create — `_create_instances` deletes any pre-existing instance before creating it.
- GSSAPI suites guard themselves with the `gssapi_ack` marker exported by `test389.topologies` (a skipif on `GSSAPI_ACK`); the GSSAPI topology fixtures do NOT skip on their own — a test using one without the marker runs and rewrites the host Kerberos configuration.
- Replication object usage (`ReplicationManager`, agreements) is owned by [replication.md](replication.md).

## Cleanup, DEBUGGING, and the watchdog

Start every test module with the standard preamble:

```python
DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)
```

Nothing exports `DEBUGGING` — define it in your own module. Truthiness trap: any non-empty string enables it, including `DEBUGGING=0`. When enabled, instances are built verbose with extra server log levels, and hand-written finalizers stop instances instead of deleting them so you can inspect state.

The library fixtures all call `create_topology(..., request=request)` (`dirsrvtests/lib/test389/topologies.py (create_topology)`), which registers a shared finalizer that stops every instance at module end and — unless `DEBUGGING` is set — runs the `cleanup_cb`, removes the ssca certificate database, and deletes the instances. It also arms a SIGALRM watchdog (default 85 minutes; on expiry it kills `ns-slapd` with TERM, then QUIT so cores are distinguishable, then raises `TimeoutError`). Adjust the timeout with `set_timeout()` from an autouse module-scoped fixture that runs before the topology fixture (`dirsrvtests/tests/suites/lib389/timeout_test.py`).

A custom fixture must either pass `request=` through to `create_topology`, or register an explicit finalizer — canonical shape from `dirsrvtests/tests/suites/replication/conftest.py (topology_m2)`:

```python
def fin():
    if DEBUGGING:
        [inst.stop() for inst in topology]
    else:
        [inst.delete() for inst in topology]
request.addfinalizer(fin)
```

Prefer `request.addfinalizer` over `yield` teardown; it is the dominant convention in this tree.

## Markers and tiers

`dirsrvtests/pytest.ini` registers exactly these markers:

```
tier0: mark a test as part of tier0
tier1: mark a test as part of tier1
tier2: mark a test as part of tier2
tier3: mark a test as part of tier3
```

- Module-level `pytestmark = pytest.mark.tier1` is the default for a new feature test.
- CI never selects by tier. Its only marker filter excludes `flaky`, so `@pytest.mark.flaky(...)` tests never run in CI; the mark is unregistered (expect a warning) and its plugin is not in `dirsrvtests/requirements.txt`.
- Backend-conditional skips use `get_default_db_lib()` from `lib389.utils` (idiom from `dirsrvtests/tests/suites/monitor/db_locks_monitor_test.py`):

```python
@pytest.mark.skipif(get_default_db_lib() == "mdb",
                    reason="Not supported over mdb")
def test_bdb_specific(topology_st):
```

## Scaffolding a new file

Workflow: see the write-test skill (.agents/skills/write-test/SKILL.md).

`dirsrvtests/create_test.py` is an optional scaffold. `python3 dirsrvtests/create_test.py -s <name>` writes `<name>_test.py` into the current directory with the `tier1` `pytestmark`, the `DEBUGGING` preamble, the topology import — or, for shapes with no predefined fixture, a custom fixture calling `create_topology(..., request=request)` — and a docstring stub carrying a fresh `:id:`; it works from any cwd, and `-u` just prints a fresh UUID. Its output must be completed: fill in the docstring steps and the test body, add the imports the test needs (for example `import ldap`), move the file under `dirsrvtests/tests/suites/<area>/`, and confirm that ID is unique within `dirsrvtests/tests/suites/`.

## Environment variables

| Variable | Effect |
|---|---|
| `DEBUGGING` | verbose instances, DEBUG logging, extra server log levels; finalizers — the library one included — stop instead of delete. Any non-empty value counts |
| `NSSLAPD_DB_LIB` | backend selection (`bdb`/`mdb`), read by `get_default_db_lib()` (`src/lib389/lib389/utils.py`); unset means `mdb` |
| `DS389_MDB_MAX_SIZE` | overrides the lmdb map size for created instances |
| `TLS_HOSTNAME_CHECK` | only an empty value disables `nsslapd-ssl-check-hostname` — same truthiness trap as `DEBUGGING` |
| `HAPROXY_TRUSTED_IP` | sets `nsslapd-haproxy-trusted-ip` on every created instance |
| `GSSAPI_ACK` | un-skips tests marked with `test389.topologies.gssapi_ack` (the GSSAPI fixtures do not skip on their own — apply the marker) |
| `WEBUI` | enables the `webui/` Playwright suite and its failure screenshots |
| `PASSWD` | root password the `webui/` suite logs in with |
| `DISK_MONITORING_ACK`, `DB_LOCKS_MONITORING_ACK`, `PAM_PTA_ACK`, `USDT_LIVE_ACK` | un-skip suites that touch host configuration |

Sanitizers are a pytest flag, not an env var: `pytest --sanitizer=lsan|tsan` (`dirsrvtests/conftest.py (pytest_addoption)`) LD_PRELOADs the sanitizer into `ns-slapd`; host setup is described in `dirsrvtests/sanitizers/README.md`.

## Suite-specific conventions

- Shared helpers live in the suite package `__init__.py` and are imported relatively — `from . import get_repl_entries` in `dirsrvtests/tests/suites/replication/acceptance_test.py`. Cross-file helper imports are normal in this tree.
- `clu/`: two conventions coexist — in-process handler calls with `FakeArgs`, and `subprocess` against the installed binaries. Match the file you are extending; details in [cli.md](cli.md).
- `webui/`: Playwright-driven, module-level skip unless `WEBUI` is set (`dirsrvtests/tests/suites/webui/login/login_test.py`); also needs `PASSWD`.
- `replication/`: its `conftest.py` redefines `topology_m2`/`topology_m3` at class scope with delete-on-teardown finalizers. A file that imports the same name from `test389.topologies` shadows that back to the module-scoped library version — which fixture you get depends on your imports.
- `acl/`: its `conftest.py` imports `topology_st as topo`, so tests in that suite use `topo` without importing it.

## Running tests

Tests require a Linux environment with 389-ds-base installed and root privileges. Run suites only through the environment's build/test skill — routing rules: the verify-changes skill (.agents/skills/verify-changes/SKILL.md); never invoke pytest on an unprepared host. CI mechanics: [building.md](building.md).

Whatever runs the suite drops the CI-only plugin flags — `--suppress-no-test-exit-code` (pytest-custom_exit_code), `--html`/`--junit-xml` (pytest-html), `--browser` (pytest-playwright) — and runs one pass per backend:

```bash
NSSLAPD_DB_LIB=bdb pytest -m "not flaky" -v dirsrvtests/tests/suites/<area>/
```

Repeat with `NSSLAPD_DB_LIB=mdb`. C unit tests (cmocka) and Rust tests run through the build system — see [building.md](building.md).
