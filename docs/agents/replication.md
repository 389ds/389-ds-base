# Replication

Multi-supplier replication spans three layers: a C plugin in `ldap/servers/plugins/replication/`, CSN machinery in the slapd core, and the lib389 management API in `src/lib389/lib389/replica.py`. General plugin anatomy: [plugins.md](plugins.md).

## C plugin map

The whole plugin is one shared library, `libreplication-plugin.la`, built from the source list in the top-level `Makefile.am` (`libreplication_plugin_la_SOURCES`); there is no per-plugin Makefile in the plugin directory. The DSE init function is `replication_multisupplier_plugin_init` (ldap/servers/plugins/replication/repl5_init.c).

All files below live in `ldap/servers/plugins/replication/`:

| Concern | Files |
|---|---|
| `struct replica` — owns the RUV, CSN generator, changelog handle; tombstone reaping | `repl5_replica.c` |
| Replica config entry handlers; replica name/DN hash lookups; mapping-tree node extension | `repl5_replica_config.c`, `repl5_replica_hash.c`, `repl5_replica_dnhash.c`, `repl5_mtnode_ext.c` |
| Agreement object `Repl_Agmt`; agreement list; schedule windows; retry backoff; update-DN list | `repl5_agmt.c`, `repl5_agmtlist.c`, `repl5_schedule.c`, `repl5_backoff.c`, `repl5_updatedn_list.c` |
| RUV structure and `nsds50ruv` parse/format; pending-CSN list | `repl5_ruv.c`, `csnpl.c`, `llist.c` |
| Changelog: API and storage, cache, config entry, init and legacy upgrade, encryption | `cl5_api.c`, `cl5_clcache.c`, `cl5_config.c`, `cl5_init.c`, `cl_crypt.c` |
| Protocol arbiter (incremental vs total) vs the incremental loop itself | `repl5_protocol.c` vs `repl5_inc_protocol.c` |
| Total init: supplier-side driver thread vs BER codec for `NSDS50ReplicationEntry` | `repl5_tot_protocol.c` vs `repl5_total.c` |
| Outbound connection to a consumer | `repl5_connection.c` |
| Extended ops, replication controls, CleanAllRUV tasks | `repl_extop.c`, `repl_controls.c`, `repl_cleanallruv.c` |
| Conflict resolution (URP), glue entries, tombstones | `urp.c`, `urp_glue.c`, `urp_tombstone.c` |
| Pre/post-op callbacks and CSN handler wiring; plugin registration | `repl5_plugins.c`, `repl5_init.c` |

Traps:

- Dead files: `cl5_test.c`, `profile.c`, `repl_helper.c`, `repl5_replsupplier.c` and `test_repl_session_plugin.c` sit in the plugin directory but appear in no build rule — editing them changes nothing at runtime. `repl5.h` still declares APIs as "In repl5_replsupplier.c" and "In repl5_bos.c" (a file that does not exist), so do not infer file ownership from header comments.
- Log component vs code symbol: error logs show `NSMMReplicationPlugin`, but the code passes the globals `repl_plugin_name`, `windows_repl_plugin_name` and `repl_plugin_name_cl` (ldap/servers/plugins/replication/repl_globals.c); the literal is `REPL_PLUGIN_NAME` (ldap/servers/plugins/replication/repl_shared.h). Grep error logs for the literal, code for the variables.
- Windows/AD sync (winsync) is a parallel implementation inside the same plugin, in the `windows_*.c` files. A fix in `repl5_inc_protocol.c` does not affect winsync; `repl5_*` is a historical prefix, not a version fork.

## CSN, RUV, changelog

**CSN.** The CSN type and generator live in the core server, not the plugin: `struct csn` is `{tstamp, seqnum, rid, subseqnum}` (ldap/servers/slapd/slap.h) and new CSNs come from `csngen_new_csn` (ldap/servers/slapd/csngen.c). `csn_compare_ext` (ldap/servers/slapd/csn.c) orders on tstamp, then seqnum, then rid, then subseqnum — rid is a third-level tiebreaker, not the primary key.

**RUV.** The replica update vector is persisted in the `nsds50ruv` attribute of a tombstone entry at the suffix root whose `nsuniqueid` is all-f (`RUV_STORAGE_ENTRY_UNIQUEID`, ldap/servers/plugins/replication/repl5.h). The value grammar (documented at the top of ldap/servers/plugins/replication/repl5_ruv.c) is one `{replicageneration} <gen>` value plus one `{replica <rid>[ <url>]}[ <mincsn> <maxcsn>]` value per known supplier — the URL and the CSN pair are both optional, so a parser that requires them is wrong.

**Changelog.** The changelog is not a separate directory of files: it lives inside the instance's main database, one changelog per backend (`struct cl5DBFileHandle`, ldap/servers/plugins/replication/cl5_api.c). `changelog5_upgrade` (ldap/servers/plugins/replication/cl5_init.c) migrates a legacy `cn=changelog5,cn=config` install and removes the old config entry.

## lib389 replication traps

All in `src/lib389/lib389/replica.py` unless noted:

- `ReplicationManager.test_replication()` scales its timeout, then calls `wait_for_replication()`, which scales it again — the default 20 s becomes `20 * scale**2`. It also returns `None` while `wait_for_replication()` returns `True`: never assert on `test_replication()`'s result.
- `wait_for_replication()` works by writing a uuid into `description` on `cn=replication_managers,<suffix>` and polling the target instance. That group exists only when the topology was built through `ReplicationManager` (`_create_service_group`); otherwise the lookup raises `ldap.NO_SUCH_OBJECT` instead of reporting a replication failure. On timeout it logs the last `NSMMReplicationPlugin` error-log lines from both instances and raises a plain `Exception`.
- `Changelog` and `Changelog5` are different classes. `Changelog(instance, suffix)` targets the current per-backend `cn=changelog,<backend dn>` and raises `ValueError` on servers without the integrated changelog, a missing suffix, or no matching backend; `Changelog5` targets the legacy global `cn=changelog5,cn=config`. Older tests mostly use `Changelog5` — do not copy that into new tests.
- `Agreements(inst)` with the default basedn cannot create agreements: `Agreements._validate` (src/lib389/lib389/agreement.py) refuses to create under `cn=mapping tree,cn=config`. Use `replica.get_agreements()`, which passes `replica.dn` as the base.
- rid 65535 (`CONSUMER_REPLICAID`) is reserved for consumers and hubs: `Replica._valid_rid` rejects supplier rids `<= 0` or `>= 65535`, and `Replica.cleanRUV` skips rid `'65535'`.
- `Replicas.create()` has a side effect: after creating the replica entry it opens `Changelog(instance, <nsds5replicaroot>)` and sets the changelog max age to `7d`.

## Class map

All modules below are under `src/lib389/lib389/`. There is no `lib389/changelog.py` — the changelog classes live in `replica.py`.

| Class | Module | Role |
|---|---|---|
| `ReplicationManager` | `replica.py` | high-level topology coordinator: create/join instances, credentials, rid allocation |
| `Replicas` / `Replica` | `replica.py` | container under the mapping tree / one replica per backend; `Replicas` parses `dse.ldif` when offline |
| `RUV` | `replica.py` | parsed `nsds50ruv` value object returned by `Replica.get_ruv()` (not a string) |
| `Changelog` / `Changelog5` | `replica.py` | current per-backend vs legacy global changelog config (see traps above) |
| `ChangelogLDIF` / `ReplicaLegacy` / `NormalizedRidDict` | `replica.py` | changelog LDIF parsing; legacy API; rid-normalising dict |
| `BootstrapReplicationManager` / `ReplicationMonitor` | `replica.py` | `cn=replication manager,cn=config` bind account; status and lag reports |
| `Agreements` / `Agreement` / `WinsyncAgreement` | `agreement.py` | agreement container and entry types |
| `CleanAllRUVTask` | `tasks.py` | task under `cn=cleanallruv,cn=tasks,cn=config` |
| CLI handlers | `cli_conf/replication.py` | agreements, winsync, changelog, cleanallruv, monitor — all in one file |

## Testing replication

Use the canonical sequence from the `ReplicationManager` docstring (src/lib389/lib389/replica.py):

```python
repl = ReplicationManager(DEFAULT_SUFFIX)
repl.create_first_supplier(supplier1)
repl.join_supplier(supplier1, supplier2)
```

Verify sync with `repl.wait_for_replication(s1, s2)` or `repl.test_replication_topology(instances)`. Pytest topology fixtures (`topology_m2`, `topo.ms["supplier1"]`, ...) build exactly this and are documented in [testing.md](testing.md).

Workflow: see the write-test skill (.agents/skills/write-test/SKILL.md).
