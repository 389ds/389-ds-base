# back-ldbm and the Dual Backends (BDB / LMDB)

One database backend, two storage implementations: shared code in `ldap/servers/slapd/back-ldbm/`, per-implementation code in its `db-bdb/` and `db-mdb/` subdirectories. Paths below are relative to `ldap/servers/slapd/back-ldbm/` except those written from the repo root (`src/...`, `ldap/servers/plugins/...`, `m4/...`, `rpm.mk`, `configure.ac`, `.github/...`). Request flow into the backend is [c-server.md](c-server.md)'s; running suites per backend is [testing.md](testing.md)'s.

## The placement rule

If the change is about WHAT to store, index, or cache — entry/DN caches, IDL algebra, filter-to-candidates logic, index key construction, entryrdn/ancestorid semantics, VLV key building — it goes ONCE in `back-ldbm/*.c`, above the abstraction. If it is about HOW bytes reach the store — env/txn/cursor handling, file layout, import thread pipeline, backup/restore, monitor counters, autotune, config attributes — it lives per-backend in `db-bdb/bdb_*.c` and `db-mdb/mdb_*.c` and must be evaluated for both.

- Mirror-or-NULL-check, and evaluate both — never mirror blindly. A literal mirrored patch applies only where the two implementations are parallel; when a fix is mirrored it is usually byte-identical apart from the `bdb_`/`dbmdb_` prefix.
- Never diff-and-port between `db-bdb/bdb_import_threads.c` and `db-mdb/mdb_import_threads.c` — they implement different algorithms (see the import section).
- Several mdb entry points are deliberate no-ops — do not "fix" them: `db-mdb/mdb_ldif2db.c (dbmdb_upgradedb)` returns 0 ("Only new idl is supported when using mdb"), `db-mdb/mdb_verify.c (dbmdb_verify)` reports verify as meaningless and always successful, `db-mdb/mdb_upgrade.c (dbmdb_ldbm_upgrade)` is an empty stub.

## The vtable

The runtime abstraction is a function-pointer vtable, `struct dblayer_private` (`dblayer.h`), reached via `((struct ldbminfo *)be->be_database->plg_private)->li_dblayer_private`. Slots are named `dblayer_*_fn` / `instance_*_fn` (`ldbm_back_wire_import_fn` is the one exception). It is filled by exactly two functions — `db-bdb/bdb_config.c (bdb_init)` and `db-mdb/mdb_config.c (mdb_init)`; no other file assigns a slot:

```c
dblayer_private *priv = li->li_dblayer_private;
priv->dblayer_start_fn = &dbmdb_start;
priv->dblayer_close_fn = &dbmdb_close;
priv->dblayer_instance_start_fn = &dbmdb_instance_start;
/* ... */
dbmdb_fake_priv = *priv; /* assign any new slot BEFORE this copy */
```

- A new slot means: typedef + struct member in `dblayer.h`, then assign it in BOTH init functions, or assign it in one and NULL-check every call site. mdb-only slots are the precedent: `dblayer_show_stat_fn` (fallback prints "not supported", `dbimpl.c (dblayer_show_statistics)`), `dblayer_clear_vlv_cache_fn` (`vlv.c (do_vlv_update_index)`), `dblayer_idl_new_fetch_fn` (`idl_new.c (idl_new_fetch)`).
- Each init ends with `bdb_fake_priv = *priv;` / `dbmdb_fake_priv = *priv;` for the `bdb_be()`/`dbmdb_be()` fake-backend helpers — a slot assigned after that copy is invisible to them.
- The backend library is loaded by name at runtime: `dblayer.c (dbimpl_setup)` builds the string `"<plgname>_init"` from `nsslapd-backend-implement` and resolves the symbol. There is no static dispatch table.
- There is a second runtime indirection besides the vtable: `back_txn.back_special_handling_fn` (`back-ldbm.h (struct back_txn)`), set only by the mdb import (`db-mdb/mdb_import_threads.c (init_pseudo_txn)` installs `import_txn_callback`) and branched on in shared code — `idl_shim.c (idl_insert_key, idl_delete_key)`, `vlv.c`, `id2entry.c (id2entry_add_ext)`, `ldbm_entryrdn.c`. In `idl_insert_key`/`idl_delete_key` it takes priority over the old/new IDL branch; a shared-code change that misses it breaks import indexing.

## Branching above the abstraction

Above the abstraction, code speaks only the opaque `dbi_*` types from `dbimpl.h` (`dbi_env_t`, `dbi_db_t`, `dbi_txn_t`, `dbi_val_t`, `dbi_cursor_t`, ...) — never raw `DB *` / `MDB_*`. Where shared code must branch per backend it uses `dblayer.c (dblayer_is_lmdb)` or the `li_flags` masks `LI_LMDB_IMPL`/`LI_BDB_IMPL` (`back-ldbm.h`). These escape hatches exist; avoid adding new ones:

| Site | Test | Effect |
|---|---|---|
| `ldbm_modrdn.c (ldbm_back_modrdn)` | `li_flags & LI_LMDB_IMPL` | clears entry + DN caches after every modrdn |
| `index.c (index_range_read_ext)` | `(li_flags & (LI_LMDB_IMPL\|LI_BDB_IMPL)) == LI_BDB_IMPL` | `idl_new_range_fetch` vs `idl_lmdb_range_fetch` |
| `vlv_srch.c (vlvIndex_checkforindex)` | `li_flags & LI_LMDB_IMPL` | lmdb always opens the dbi |
| `ldbm_entryrdn.c (_entryrdn_open_index)` | `dblayer_is_lmdb(be)` | also opens the lmdb-only `@long-entryrdn` redirect db (`LDBM_LONG_ENTRYRDN_STR`) |
| `ldap/servers/plugins/replication/repl5_agmt.c (agmt_new_from_entry)`, `cl5_api.c (_cl5Iterate, _cl5PurgeRID)` | `dblayer_is_lmdb(be)` | changelog handling — [replication.md](replication.md) |

The `index.c` test deliberately compares against `LI_BDB_IMPL` rather than testing `& LI_LMDB_IMPL`: `li_flags` is not set during internal searches (e.g. bulk import), so a plain lmdb test would silently mean "bdb" there.

## Which backend is default

| Source | Default |
|---|---|
| `ldbm_config.c (CONFIG_BACKEND_IMPLEMENT)` — the `nsslapd-backend-implement` config-table row | `bdb` |
| `back-ldbm.h (LI_DEFAULT_IMPL_FLAG)`, comment "the default is BDB for now" | `LI_BDB_IMPL` |
| `src/lib389/lib389/_constants.py (DEFAULT_DB_LIB)` — used by `dscreate`, so new instances get | `mdb` |

The `NSSLAPD_DB_LIB` environment variable overrides the lib389 default via `src/lib389/lib389/utils.py (get_default_db_lib)`. Treat mdb as the target backend: lib389's own healthcheck lint flags BDB as deprecated (`src/lib389/lib389/lint.py (DSBLE0006)`).

## Read-only BDB reality

- A plain `./configure` selects the read-only BDB reader: the `--with-libbdb-ro` action-if-not-given branch sets it to yes — the help string claiming otherwise is wrong (`m4/db.m4`). With `--with-bundle-libdb`, `m4/db.m4` is not included at all (`configure.ac`).
- In a read-only-BDB build, `ns-slapd` refuses to start a bdb instance: `dblayer.c (backend_implement_get_libpath)` probes for the `bdbreader_bdb_open` symbol and exits telling the admin to run `dsctl <instance> dblib bdb2mdb`, unless the process runs from the command line or is exporting (`dblayer.c (not_exporting)`). BDB is dbscan/export-only in that build; `dbimpl.c (dblayer_private_open)` sets `SLAPI_TASK_RUNNING_FROM_COMMANDLINE` for exactly that bypass.
- Writable BDB needs `--without-libbdb-ro` plus system libdb headers (configure aborts without them), or the RPM path `make -f rpm.mk ... BUNDLE_LIBDB=1` (`rpm.mk`, `rpm/is-robdb-used`). The operative question is whether `/usr/include/db.h` is installed, not which OS release you are on.
- The CI "BDB Test" job's pytest step writes a placeholder `pytest.html`/`pytest.xml` and stays green when `/usr/lib64/dirsrv/librobdb.so` is present in the container (`.github/workflows/pytest.yml`) — never treat a green run as BDB coverage; build writable BDB and run the suite yourself with `NSSLAPD_DB_LIB=bdb`.
- Workflow: see the touch-backend skill (.agents/skills/touch-backend/SKILL.md).

## Caches

- Each instance has two caches, both `struct cache` (the struct has no type member): `inst_cache` (`CACHE_TYPE_ENTRY`, `struct backentry`) and `inst_dncache` (`CACHE_TYPE_DN`, `struct backdn`). The dispatching entry points — `cache_clear`, `cache_destroy_please`, `cache_set_max_size` (explicit `type` argument) and `cache_remove`, `cache_replace`, `cache_return`, `cache_add` (the object's `ep_type` tag) — branch into `entrycache_*` or `dncache_*` halves (`cache.c (cache_clear)`); changing one means editing both halves. `cache_init` is shared, with no halves. The remaining `cache_*` entry points are entry-cache-only, and DN-cache callers use the exported `dncache_*` functions directly.
- Lookups filter, they do not invalidate: `cache.c (cache_find_dn, cache_find_id, dncache_find_id)` return NULL when `ep_state & ENTRY_STATE_UNAVAILABLE`; the PINNED and LRU state bits sit deliberately outside that mask (`back-ldbm.h (ENTRY_STATE_UNAVAILABLE)`). Do not simplify the test to `ep_state != 0` — that shape hid valid LRU-queued DNs and made reindex/export reuse a released DN.
- Normal add/modify/delete paths never call `cache_clear`; they mutate via `cache_add_tentative()` then `cache_replace(old, new)`, with `ldbm_modify.c (modify_switch_entries, modify_unswitch_entries)` as the canonical replace/rollback pair. The exception is `ldbm_modrdn.c (ldbm_back_modrdn)`, which under lmdb clears both caches wholesale after every modrdn.

## Index and IDL map

All single-copy, above the abstraction:

| File | Owns |
|---|---|
| `filterindex.c` | LDAP filter → candidate-IDL evaluation |
| `index.c` | index key construction, index read / range read, add/delete |
| `idl_shim.c` | old-vs-new IDL dispatch (`idl_fetch_ext`, `idl_insert_key`, `idl_delete_key`, ...) |
| `idl.c` / `idl_new.c` | old / new IDL on-disk formats (both range fetchers live in `idl_new.c`) |
| `idl_common.c` | shared IDL primitives |
| `idl_set.c` | n-way union/intersection consumed by `filterindex.c` |

- `index.c (is_indexed)` compares its `indextype` argument by POINTER IDENTITY against the globals `indextype_PRESENCE/EQUALITY/APPROX/SUB` before falling back to `strcmp` on matching rules. Passing a literal `"eq"` makes the attribute look un-indexed.
- Long keys are handled once, in `index.c (prepare_key)`: at `li_max_key_len` the value is replaced by a hash (`ldbm_attrcrypt.c (attrcrypt_hash_large_index_key)`) behind a `#` prefix. `li_max_key_len` is `UINT_MAX` at init (`init.c (ldbm_back_init)`) and set by mdb from `mdb_env_get_maxkeysize()` (`db-mdb/mdb_layer.c`); it is read in several files, so a key-length policy change touches every reader.
- IDL cursor walks run inside a SERIALIZABLE read transaction with an explicit do-not-weaken comment in `idl_new.c (idl_new_fetch, idl_new_range_fetch)` — degree-2 isolation reintroduced a crash. The `DBI_RC_RETRY` bounded retry (`back-ldbm.h (IDL_FETCH_RETRY_COUNT)`) lives in the CALLERS — the `index.c` pair (`index_read_ext_allids`, `index_range_read_ext`) stops retrying when the transaction is not its own, while `seq.c (ldbm_back_seq)` aborts its read transaction and restarts the cursor walk. Do not weaken the isolation and do not move the retries into the fetchers.

## Import / export / reindex

Shared scaffolding is `import.c` + `import.h` (`ImportJob`, the worker structs, `import_log_notice`, `import_abort_all`); the task entry points in `ldif2ldbm.c` and `dbverify.c` are pure dispatchers into vtable slots. `archive.c (ldbm_back_archive2ldbm, ldbm_back_ldbm2archive)` is NOT a thin dispatcher — it carries substantial shared setup/teardown around the vtable calls. The only import helpers both backends share are `import.c (import_update_entry_subcount, db2ldif_is_suffix_in_ldif)`.

| Task | Dispatcher | bdb (`db-bdb/`) | mdb (`db-mdb/`) |
|---|---|---|---|
| ldif2db (import) | `ldif2ldbm.c (ldbm_back_ldif2ldbm)` | `bdb_ldif2db.c (bdb_ldif2db)` | `mdb_ldif2db.c (dbmdb_ldif2db)` |
| db2ldif (export) | `ldif2ldbm.c (ldbm_back_ldbm2ldif)` | `bdb_ldif2db.c (bdb_db2ldif)` | `mdb_ldif2db.c (dbmdb_db2ldif)` |
| db2index (reindex) | `ldif2ldbm.c (ldbm_back_ldbm2index)` | `bdb_ldif2db.c (bdb_db2index)`, hand-rolled | `mdb_ldif2db.c (dbmdb_db2index)`, delegates to the import framework |
| import engine | `import.c (import_main_offline)` | `bdb_import.c (bdb_public_bdb_import_main)` | `mdb_import.c (dbmdb_public_dbmdb_import_main)` |
| wire (bulk) import | `import.c (ldbm_back_wire_import)` | via `ldbm_back_wire_import_fn` | via `ldbm_back_wire_import_fn` |
| upgradedb | `ldif2ldbm.c (ldbm_back_upgradedb)` | `bdb_ldif2db.c (bdb_upgradedb)` | `mdb_ldif2db.c (dbmdb_upgradedb)` — returns 0 |
| dbverify | `dbverify.c (ldbm_back_dbverify)` | `db-bdb/bdb_verify.c` | `mdb_verify.c (dbmdb_verify)` — always success |

The two import-thread files are different algorithms, not ports of each other: bdb runs producer/foreman/worker threads over a shared entry FIFO, while mdb adds a dedicated `WRITER` work type (`import.h (WRITER)`) with its own writer queue. Reindex is structurally different too (see the table). This is why the placement rule says evaluate, not mirror.

## Landmines

- `dbimpl.c (dblayer_op2str)` is off by one: the `dbi_op_t` enum (`dbimpl.h`) contains `DBI_OP_MOVE_TO_FIRST` but the string array skips it, so every op from `DBI_OP_MOVE_TO_FIRST` onward prints the NEXT member's name and `DBI_OP_CLOSE` prints "INVALID DBI_OP". Do not trust logs built from it, and do not "fix" it without a maintainer ruling — log consumers may depend on the shifted strings.
- The `DBI_RC_*` comments in `dbimpl.h` are machine-parsed by `ldap/servers/slapd/mkDBErrStrs.py` into the generated `dberrstrs.h` — preserve the comment format exactly. Generated-file rules: [building.md](building.md).
