# Project Architecture

## Key Files

| File | Purpose |
|------|---------|
| `autogen.sh` | Bootstraps autotools (run before `./configure`) |
| `configure.ac` | Autoconf configuration (compiler checks, optional ASAN/TSAN, BDB/LMDB, Rust) |
| `Makefile.am` | Main automake build definition |
| `VERSION.sh` | Release version consumed by configure and RPM |
| `rpm.mk` | RPM build targets |
| `src/Cargo.toml` | Rust workspace root |
| `.clang-format` | C code formatting rules |
| `.packit.yaml` | Packit downstream build automation |

## Server Source (`ldap/`)

- `ldap/servers/slapd/` — main **ns-slapd** daemon: LDAP operations, connection handling, DSE, backends under `back-ldbm/` (LMDB and BDB layers), controls, logging, SSL, password policy
- `ldap/servers/plugins/` — bundled SLAPI plugins, one directory per plugin (e.g. `memberof/`, `replication/`, `dna/`, `acl/`, `acctpolicy/`)
- `ldap/servers/snmp/` — SNMP subagent
- `ldap/schema/` — LDAP schema definitions
- `ldap/ldif/` — LDIF samples and tooling data

## lib389 Python Library (`src/lib389/`)

lib389 is the Python library for managing and testing 389 Directory Server instances. It provides:

- **`DirSrv`** class — programmatic control of server instances (start/stop/restart, LDAP operations, configuration)
- **IDM helpers** — `lib389.idm.user`, `lib389.idm.group`, `lib389.idm.organizationalunit`, etc.
- **Replication** — `Replicas`, `Changelog5`, `ReplicationManager`
- **Plugins** — `DNAPlugin`, `MemberOfPlugin`, `RetroChangelogPlugin`, etc.
- **Tasks** — import/export, reindex, fixup tasks
- **Topology fixtures** — `topology_st` (standalone), `topology_m2` (2 suppliers), `topology_m3`, `topology_m4`, etc.
- **CLI tools** — `dsconf`, `dsctl`, `dsidm`, `dscreate`, `openldap_to_ds`
- **Instance creation** — INF-file-based setup via `dscreate` and `SetupDs`

Key paths within lib389:

| Path | Contents |
|------|----------|
| `lib389/__init__.py` | Core `DirSrv` class, constants, utilities |
| `lib389/topologies.py` | Pytest fixtures for test topologies |
| `lib389/replica.py` | Replication, changelog classes |
| `lib389/plugins.py` | Plugin configuration classes |
| `lib389/idm/` | Identity management (users, groups, accounts, roles) |
| `lib389/instance/setup.py` | Instance creation and setup |
| `lib389/paths.py` | Path resolution for installed instances |
| `lib389/cli_conf/` | CLI subcommand implementations |
| `lib389/tests/` | lib389 internal unit tests |
| `pyproject.toml` | Package metadata and dependencies |

## Cockpit Web UI (`src/cockpit/389-console/`)

React-based web administration console. Uses PatternFly components. Built with npm/webpack.

## SLAPI Plugin Architecture

Plugins are C shared libraries under `ldap/servers/plugins/`, one directory per plugin. Each plugin:

1. Includes `slapi-plugin.h`
2. Defines a `Slapi_PluginDesc` struct with name, vendor, version, description
3. Implements an `_init` function that registers callbacks via `slapi_pblock_set()`:
   - Pre/post operation callbacks (`SLAPI_PLUGIN_PRE_MODIFY_FN`, `SLAPI_PLUGIN_POST_ADD_FN`, etc.)
   - Start/close functions
   - Backend transaction variants (betxn)
4. Optionally registers additional plugin types via `slapi_register_plugin()`

API documentation is generated via Doxygen from `docs/slapi.doxy.in` and the header `ldap/servers/slapd/slapi-plugin.h`.

## Coding Conventions

### C code

- Follow `.clang-format` for formatting
- Use SLAPI API functions for memory management (`slapi_ch_strdup`, `slapi_ch_free_string`, etc.)
- Log via `slapi_log_err()` with appropriate severity levels
- Plugin entry points follow the `pluginname_operation_callback` naming pattern

### Python code (tests and lib389)

- Copyright header block at the top of every file
- Use `logging.getLogger(__name__)` for log output
- Module-level `pytestmark` for tier assignment
- Structured docstrings with `:id:`, `:setup:`, `:steps:`, `:expectedresults:`
- Prefer `f-strings` for string formatting in new code
- Use `ensure_bytes()` / `ensure_str()` from `lib389.utils` for LDAP value encoding

### Naming conventions

- Test files: `<feature>_test.py` (in suites)
- Test functions: `test_<descriptive_name>`
- Constants: `UPPER_SNAKE_CASE`
- Fixtures: `lowercase_with_underscores`
