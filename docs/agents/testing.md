# Testing Guide

## C Unit Tests

```bash
make check
```

Uses cmocka. To debug:

```bash
libtool --mode=execute gdb /path/to/test_binary
```

## Python Integration Tests (pytest)

Tests live in `dirsrvtests/` and require lib389 to be installed. Run with pytest:

```bash
# Run a specific suite
sudo py.test -s dirsrvtests/tests/suites/basic/

# Run a specific test
sudo py.test -v dirsrvtests/tests/suites/plugins/dna_test.py::test_function_name

# Filter by tier marker
sudo py.test -m tier0 dirsrvtests/tests/suites/
```

## Test Organization

| Path | Purpose |
|------|---------|
| `dirsrvtests/tests/suites/<area>/` | Functional area tests (basic, replication, tls, plugins, clu, acl, etc.) |
| `dirsrvtests/tests/tickets/` | Regression tests for specific bug tickets (`ticket#####_test.py`) |
| `dirsrvtests/tests/stress/` | Stress and load tests |
| `dirsrvtests/conftest.py` | Session/function fixtures, sanitizer injection, report hooks |
| `dirsrvtests/pytest.ini` | Marker definitions: `tier0`, `tier1`, `tier2`, `tier3` |
| `dirsrvtests/create_test.py` | Template generator for new tests |

## Test Tier Markers

- `tier0` — critical smoke tests
- `tier1` — core functionality tests
- `tier2` — extended feature tests
- `tier3` — edge case and stress tests

Assign a tier at the module level:

```python
pytestmark = pytest.mark.tier1
```

## Test Topology Fixtures

Tests use predefined topology fixtures from `lib389.topologies`:

```python
from lib389.topologies import topology_st as topo          # Standalone
from lib389.topologies import topology_m2 as topo           # 2 suppliers
from lib389.topologies import topology_m3 as topo_m3        # 3 suppliers
from lib389.topologies import topology_m4 as topo_m4        # 4 suppliers
from lib389.topologies import topology_m2c2 as topo_m2c2    # 2 suppliers + 2 consumers
```

Access instances via `topo.standalone`, `topo.ms["supplier1"]`, `topo.ms["supplier2"]`, etc.

## Writing Tests — Docstring Format

Suite tests **must** use structured docstrings with these fields (enforced by Testimony tooling):

```python
def test_example_feature(topo):
    """Short description of what the test verifies

    :id: <unique-uuid>
    :setup: Describe the test setup (e.g. "Standalone instance" or "Two supplier replication")
    :steps:
        1. First action
        2. Second action
        3. Third action
    :expectedresults:
        1. Expected outcome of step 1
        2. Expected outcome of step 2
        3. Expected outcome of step 3
    """
```

Each test **must** have a unique `:id:` (UUID). Generate one using `dirsrvtests/create_test.py` or `python -c "import uuid; print(uuid.uuid4())"`. Steps and expected results must have matching numbered items.

## Writing Tests — Common Patterns

```python
import pytest
import ldap
import logging
import os
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.idm.user import UserAccounts, UserAccount
from lib389.plugins import MemberOfPlugin
from lib389.utils import ds_supports_new_changelog

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)
```

**Prefer modern lib389 API** over deprecated raw LDAP calls:
- Use `UserAccount`, `UserAccounts` instead of `add_s()` / `modify_s()`
- Use `inst.config.set()` instead of `modify_s('cn=config', ...)`
- Use `PwPolicyManager`, `DNAPlugin`, etc. instead of raw DN manipulation
- Use `Changelog5` / `Changelog` instead of direct changelog DN access
- Use `pytest.raises()` for expected exceptions

**Cleanup**: Use `request.addfinalizer()` or `try/finally` blocks to clean up test entries. The `DEBUGGING` env variable convention: when set, finalizers stop instances instead of deleting them.

**Conditional skipping**: Prefer `@pytest.mark.skipif()` decorators over runtime `pytest.skip()` calls inside the test body. Remember that every test must retain its unique `:id:` UUID in the docstring — never remove or change an existing `:id:` when adding skip conditions:

```python
@pytest.mark.skipif(ds_supports_new_changelog(),
                    reason="This test is for legacy changelog")
def test_legacy_changelog(topo):
    """Verify legacy changelog behavior

    :id: a1b2c3d4-e5f6-7890-abcd-ef1234567890
    :setup: Standalone instance
    :steps:
        1. ...
    :expectedresults:
        1. ...
    """
    ...
```

## Test Helpers and Shared Code

- **Do not import directly between test files.** Place shared helpers in `conftest.py` fixtures or dedicated utility modules.
- Fixtures in `conftest.py` are automatically discovered by pytest at each directory level.
- Common constants like `DEFAULT_SUFFIX`, `PASSWORD`, `DN_DM` are in `lib389._constants`.
