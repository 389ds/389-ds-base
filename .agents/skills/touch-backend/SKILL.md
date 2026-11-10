---
name: touch-backend
description: >-
  Rules for modifying back-ldbm database code under the dual-backend (BDB +
  LMDB) architecture. Read BEFORE editing anything in
  ldap/servers/slapd/back-ldbm/, its db-bdb/ subdir, or its db-mdb/ subdir.
  Decides where a change belongs (shared vs per-backend), whether to mirror it
  into the other backend, and how to verify BOTH backends — including the trap
  where the BDB pytest CI job reports green having run zero tests. Triggers:
  "backend change", "bdb", "lmdb", "mdb", "dblayer", "vtable", "IDL",
  "entry cache", "import thread", "mirror to the other backend", "test both
  backends".
---

# Touching back-ldbm: the dual-backend (BDB + LMDB) rules

One backend plugin, two storage implementations. Shared semantics live in
`ldap/servers/slapd/back-ldbm/*.c`; the implementations live in `db-bdb/` and
`db-mdb/` behind a function-pointer vtable (`struct dblayer_private` in
`dblayer.h`) that is filled only by `bdb_init()` and `mdb_init()`.

## Steps

1. **Decide placement before writing code.**
   - WHAT to store/index/cache — IDL algebra, filter-to-candidate logic, index
     key construction, entry/DN caches, entryrdn/ancestorid semantics, VLV key
     building — goes ONCE in `back-ldbm/*.c`, above the abstraction.
   - HOW bytes reach the store — env/txn/cursor handling, the import thread
     pipeline, backup/restore, monitor counters, autotune, config attrs —
     lives per-backend in `db-bdb/bdb_*.c` AND `db-mdb/mdb_*.c`.
   - Semantics reference: docs/agents/backends.md.

2. **Mirror-or-NULL-check — never mirror blindly.** Evaluate every per-backend
   change against both implementations. When a fix genuinely applies to both,
   the mirrored blocks are typically identical except the `bdb_`/`dbmdb_`
   prefix and the log/function-name strings.
   - **Never diff-and-port between `bdb_import_threads.c` and
     `mdb_import_threads.c` — they implement different algorithms** (mdb has a
     WRITER work type and its own writer queue; bdb does not).
   - **Never "fix" the deliberately empty mdb maintenance entry points**:
     `dbmdb_upgradedb` returns 0, `dbmdb_verify` always succeeds, and
     `dbmdb_ldbm_upgrade` is an intentional stub.

3. **New vtable capability.** Add the typedef and slot to the private struct
   in `dblayer.h`, then either assign it in BOTH `bdb_init()` and
   `mdb_init()`, or assign it in one and NULL-check every call site (the
   existing mdb-only slots are the precedent). Assign the slot BEFORE the
   `*_fake_priv = *priv` copy at the end of each init function, or the
   fake-backend helpers miss it.
   - Verify: `git grep <slot_name> -- ldap/servers/slapd/back-ldbm` shows the
     struct member, the assignment(s), and NULL-guarded call sites — no
     unguarded caller if you assigned only one backend.

4. **Branching above the abstraction.** If shared code must behave differently
   per backend, use the existing idioms only: `dblayer_is_lmdb(be)` or the
   `li_flags` implementation-flag masks. Above the abstraction, speak only the
   opaque `dbi_*` types — no raw BDB or MDB types. Treat each new branch as a
   maintenance hazard; prefer a vtable slot when one would do.

5. **Verify BOTH backends.** Run the touched pytest suite once per backend via
   the environment's build/test skill (routing rules: verify-changes skill):
   - `NSSLAPD_DB_LIB=bdb py.test -v dirsrvtests/tests/suites/<suite>`
   - `NSSLAPD_DB_LIB=mdb py.test -v dirsrvtests/tests/suites/<suite>`
   - Unset means mdb — an env-less run is NOT a bdb run.
   - High-signal suites: indexes, import, export, backups, vlv, betxns, clu,
     config, monitor.
   - Offline check that works in any build: `dbscan -D bdb -L <db-home>` and
     the same with `-D mdb` — dbscan enters the same vtable.
   - Where relevant, round-trip an instance: `dsctl <inst> dblib bdb2mdb`,
     then `dsctl <inst> dblib cleanup`, then `dsctl <inst> dblib mdb2bdb`.

6. **STOP if you tested BDB on a default build.** A plain `./configure`
   selects the read-only BDB reader (`--with-libbdb-ro` defaults to yes), and
   that build's ns-slapd refuses to start on a bdb instance. Writable BDB
   needs `--without-libbdb-ro` plus real libdb headers on the build host, or
   the RPM path: `SKIP_AUDIT_CI=1 make -f rpm.mk BUNDLE_LIBDB=1 dist-bz2 rpms`.

7. **STOP before trusting a green BDB CI job.** The BDB pytest workflow skips
   ALL tests and still reports green when the read-only reader is present in
   the test image: it writes placeholder pytest.html/pytest.xml instead of
   running anything.
   - Verify inside the test container: `test -f /usr/lib64/dirsrv/librobdb.so`
     — if the file exists, the BDB leg ran zero tests; rebuild with
     `BUNDLE_LIBDB=1` and rerun the suite via the build/test skill.
   - **Never claim "CI is green on both backends" without having run the bdb
     leg yourself.**

8. **Add the regression test to the feature's existing suite** (indexes,
   import, ...), not a new directory. A reproducer that only makes sense on
   one backend gets the `@pytest.mark.skipif(get_default_db_lib() == ...)`
   idiom — see the write-test skill.

## Maintenance
If a step no longer matches the code or CI, update this skill in the same PR as the change that moved it.
