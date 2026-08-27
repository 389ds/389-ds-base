# Project Architecture

## Repo map

| Path | What lives there |
|---|---|
| `ldap/servers/slapd/` | `ns-slapd` core: connection handling, operation dispatch, DSE/`cn=config`, schema, password policy, logging |
| `ldap/servers/slapd/back-ldbm/` | the database backend layer, with BDB and LMDB implementations underneath ([backends.md](backends.md)) |
| `ldap/servers/slapd/tools/` | only `dbscan`, `ldclt` and `pwdhash` (built from `pwenc.c`) are built (top-level `Makefile.am` (`bin_PROGRAMS`)); the other sources there have no build rule |
| `ldap/servers/plugins/` | bundled C plugins, one directory per plugin ([plugins.md](plugins.md)) |
| `ldap/servers/snmp/` | the `ldap-agent` SNMP subagent |
| `ldap/schema/` | schema as numbered LDIF files (see load order below) |
| `ldap/ldif/` | LDIF templates — the `*.in` files are the sources, the rest is generated ([building.md](building.md)) |
| `lib/` | C support libraries: `base`, `ldaputil`, `libaccess`, `libadmin`, `libsi18n`, and `librobdb` (read-only BDB shim) |
| `include/` | public and internal C headers |
| `src/lib389/` | the Python management library and the CLI entry points `dsconf`, `dsctl`, `dsidm`, `dscreate`, `dscontainer`, `openldap_to_ds` ([lib389.md](lib389.md), [cli.md](cli.md)) |
| `src/cockpit/389-console/` | the Cockpit web UI ([ui.md](ui.md)) |
| `src/librslapd/`, `src/librnsslapd/`, `src/slapd/`, `src/slapi_r_plugin/`, `src/plugins/` | the Rust workspace members (`src/Cargo.toml` (`[workspace]`)); [rust.md](rust.md) |
| `src/libsds/`, `src/svrcore/`, `src/rewriters/` | C libraries that live under `src/` despite the Rust neighbourhood |
| `dirsrvtests/` | pytest integration tests; the topology fixtures live in `dirsrvtests/lib/test389/` ([testing.md](testing.md)) |
| `test/` | cmocka C unit tests, all compiled into the single `test_slapd` binary ([building.md](building.md)) |
| `rpm/`, `.github/` | RPM spec template and packaging helpers; CI workflows ([building.md](building.md)) |

## How a request travels

There is no single accept loop. `slapd_daemon` (`ldap/servers/slapd/daemon.c`) starts an accept thread, one polling thread per connection-table list, and a worker pool sized by `nsslapd-threadnumber`. A read-ready connection is appended to the global work queue (`ldap/servers/slapd/connection.c` (`add_work_q`)); an idle worker (`connection_threadmain`) reads the request and dispatches it from the tag switch in `connection_dispatch_operation` — adding an operation type means editing that switch.

| Op | Front end | Shared handler | Backend |
|---|---|---|---|
| ADD | `do_add` (`add.c`) | `op_shared_add` (`add.c`) | `be_add` → `ldbm_back_add` |
| MODIFY | `do_modify` (`modify.c`) | `op_shared_modify` (`modify.c`) | `be_modify` → `ldbm_back_modify` |
| SEARCH | `do_search` (`search.c`) | `op_shared_search` (`opshared.c`) | `be_search` → `ldbm_back_search` |
| BIND | `do_bind` (`bind.c`) | — | `be_bind` → `ldbm_back_bind` |
| DELETE | `do_delete` (`delete.c`) | `op_shared_delete` (`delete.c`) | `be_delete` → `ldbm_back_delete` |
| MODRDN | `do_modrdn` (`modrdn.c`) | `op_shared_rename` (`modrdn.c`) | `be_modrdn` → `ldbm_back_modrdn` |
| COMPARE | `do_compare` (`compare.c`) | — | `be_compare` → `ldbm_back_compare` |
| ABANDON | `do_abandon` (`abandon.c`) | — | none — sets the target operation's `o_status` to `SLAPI_OP_STATUS_ABANDONED`; no response is sent |
| EXTENDED | `do_extended` (`extendop.c`) | — | extended-op plugin via `plugin_call_exop_plugins` |

The `be_*` names are macros over the backend vtable (`ldap/servers/slapd/slap.h`); `ldbm_back_init` (`ldap/servers/slapd/back-ldbm/init.c`) fills them for database suffixes.

Who sends the result: on success the **backend** sends it (`ldbm_back_modify` ends with `slapi_send_ldap_result`); the front end sends only its own early-error results before the backend is reached (`ldap/servers/slapd/modify.c` (`op_shared_modify`)). Operations on `cn=config`, `cn=monitor` and the root DSE never reach a database backend: `be_new_internal` (`ldap/servers/slapd/backend_manager.c`) wires them to `dse_search`/`dse_modify`/`dse_add`/`dse_delete` and hard-wires MODRDN to unwilling-to-perform — see [c-server.md](c-server.md).

## Where is X

- **Password policy** is spread across the core, not centralized: policy resolution, syntax and history in `ldap/servers/slapd/pw.c` (`new_passwdPolicy`, `check_pw_syntax`, `update_pw_history`), retry counters in `ldap/servers/slapd/pw_retry.c` (`update_pw_retry`) — and failed-bind lockout accounting runs in the **result path**, not `bind.c`: `send_ldap_result_ext` (`ldap/servers/slapd/result.c`) intercepts invalid-credentials errors and updates the retry count.
- **ACL checks** run inside the backend, not the front end: `ldbm_back_modify` (`ldap/servers/slapd/back-ldbm/ldbm_modify.c`) calls `plugin_call_acl_mods_access` after fetching the entry.
- **CSN generation** lives in the slapd core (`ldap/servers/slapd/csngen.c` (`csngen_new_csn`)), not the replication plugin — the plugin only registers per-operation handlers around it ([replication.md](replication.md)).
- **Schema** loads only `[0-9][0-9]*.ldif` files under the schema directories — a file without the two-digit prefix is silently skipped — ordered by directory (system dir before instance dir), then filename within each (`ldap/servers/slapd/schema.c` (`init_schema_dse_ext`) via `get_priority_filelist`); the last file, `99user.ldif`, is treated as the writable user schema.
- **Plugin loading**: entries under `cn=plugins,cn=config` are instantiated while `dse.ldif` is parsed (`ldap/servers/slapd/configdse.c` (`load_plugin_entry`) → `ldap/servers/slapd/plugin.c` (`plugin_setup`)), then started in dependency order (`plugin.c` (`plugin_dependency_startall`)) — see [plugins.md](plugins.md).

## The documentation map

| Doc | Answers |
|---|---|
| [architecture.md](architecture.md) | this file — where code lives, how a request flows, where X is implemented |
| [building.md](building.md) | platform limits, what CI runs, configure flags, generated files, version floors |
| [testing.md](testing.md) | the pytest contract — fixtures, markers, docstrings/`:id:`, how suites run |
| [contributing.md](contributing.md) | commit message format, PR flow, review conventions |
| [c-server.md](c-server.md) | the SLAPI C API — memory ownership, pblock, DSE callbacks, `cn=config` attributes |
| [plugins.md](plugins.md) | writing and registering plugins (C and Rust), betxn, plugin configuration |
| [backends.md](backends.md) | back-ldbm dual-backend rules — BDB vs LMDB, caches, indexes, import |
| [replication.md](replication.md) | CSN/RUV/changelog model, the replication plugin, lib389 replication API |
| [lib389.md](lib389.md) | the lib389 object model — DSLdapObject patterns and their traps |
| [cli.md](cli.md) | dsconf/dsctl/dsidm/dscreate handler signatures and wiring |
| [ui.md](ui.md) | the Cockpit console — build pipeline and edit patterns |
| [rust.md](rust.md) | the Rust workspace — crates, offline builds, MSRV |
