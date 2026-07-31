# Building and CI

## Platform: Linux only

`ns-slapd` builds and runs only on Linux:

- `configure.ac`'s host switch (the `case $host` after `AC_CANONICAL_HOST`) has arms for Linux, FreeBSD, HP-UX and Solaris only; any other host falls through to an empty `platform` with no defines.
- The server includes Linux-only headers: `sys/epoll.h` and `sys/timerfd.h` in `ldap/servers/slapd/slap.h` and `ldap/servers/slapd/daemon.c`.
- lib389 resolves `defaults.inf` from hardcoded absolute Linux locations, so without an installed server or `PREFIX` every path attribute raises `IOError` (`src/lib389/lib389/paths.py` (`Paths`)).

A change can only be built, run, or verified in a Linux environment (container or
CI). Workflow: see the verify-changes skill
(`.agents/skills/verify-changes/SKILL.md`) — it routes through the environment's
build/test skill and never builds on an unprepared host.

## What CI actually runs

The build jobs run in a prebuilt `quay.io/389ds/ci-images` container (`:test` or `:fedora`); the pytest/lmdbpytest *test* jobs run on the bare runner and drive docker themselves. No image definition is tracked in this tree.

| Workflow | Trigger | Key command |
|---|---|---|
| `pytest.yml` | push (`main`, `389-ds-base-*`), PR, nightly, manual | `SKIP_AUDIT_CI=1 make -f rpm.mk dist-bz2 rpms` in the `:test` image, then one `py.test` job per suite on the BDB backend |
| `lmdbpytest.yml` | same | identical build; its test job exports `NSSLAPD_DB_LIB=mdb` to switch the backend and drops the read-only-BDB guard |
| `compile.yml` | push, PR, manual | `autoreconf -fvi && ./configure` (no flags), `make V=0`, across a compiler/flag matrix: GCC, GCC Strict, GCC Static Analyzer (`-fanalyzer`), Clang, Clang `-Weverything` |
| `cargotest.yml` | push, PR, nightly, manual | `./configure --enable-debug`, `make V=0`, then `make check-local` |
| `npm.yml` | push, PR, nightly, manual | `npx --yes audit-ci --config audit-ci.json` in `src/cockpit/389-console` |
| `codeql.yml` | PR, push (`main`), weekly, manual | bare `./configure` + `make`; analyses C/C++ and Python only, with `+security-extended` queries |
| `validate.yml` | push, PR, manual | `testimony validate` over `dirsrvtests/tests/suites`, the duplicate-`:id:` check, and `vermin --target=3.8` over both `src/lib389` and `dirsrvtests` |
| `coverity.yml` | weekly cron only | bare `./configure`; the Coverity scan action drives `make` |
| `release.yml` | tag push `389-ds-base-*`, manual | `TAG=<tag> make -f rpm.mk dist-bz2`, then a GitHub release upload |

Facts that change how you use these:

- In a container, run `git config --global --add safe.directory "$PWD"` before any `rpm.mk` target — `dist-bz2` shells out to `git ls-files`, and without the safe-directory entry the tarball is silently empty (`.github/workflows/pytest.yml` (Add GITHUB_WORKSPACE as a safe directory step); `rpm.mk` (`dist-bz2`)).
- `dist-bz2` tars `git ls-files` output, so uncommitted changes are **excluded**; `rpms` rsyncs the working tree, so uncommitted changes are **included**. Testing an uncommitted patch requires the `rpms` path (`rpm.mk` (`dist-bz2`, `local-archive`)).
- Keep `dist-bz2` in the RPM command: `rpms` does not depend on `download-cargo-dependencies` (`dist-bz2` and `srpms` do), so `make -f rpm.mk rpms` alone can fail on a clean tree (`rpm.mk` (`rpms`)).
- The npm-audit guard is GNU make `ifndef`, so only a **non-empty** `SKIP_AUDIT_CI` skips the audit — `SKIP_AUDIT_CI=` (empty) still runs it (`rpm.mk` (`install-node-modules`)).
- No workflow passes `-Werror`; compile stderr only feeds a PR-annotation matcher. A green Compile job is not a warning-free build (`.github/workflows/compile.yml` (Build step)).
- The BDB pytest job writes a placeholder report and runs zero tests when the read-only BDB library is installed in the image, and still reports green — see [backends.md](backends.md) (`.github/workflows/pytest.yml` (Run pytest in a container step)).

## Configure flags

| Flag | Default | Effect |
|---|---|---|
| `--enable-debug` | no | `-DDEBUG -DMCC_DEBUG`, `-g3 -ggdb -gdwarf-4 -O0`; cargo builds the debug profile instead of `--release`; the version reports "DEVELOPER BUILD" (`configure.ac` (`AC_ARG_ENABLE(debug)`)) |
| `--enable-asan` | no | address-sanitizer build; also empties `check-local` (see below) |
| `--enable-msan` / `--enable-tsan` / `--enable-ubsan` | no | matching `-fsanitize=` builds; `--enable-ubsan` is registered under `--enable-tsan`'s help string, so `./configure --help` never advertises it (`configure.ac` (`AC_ARG_ENABLE(ubsan)`)) |
| `--enable-cmocka` | no | compiles and runs `test_slapd` under `make check`; without it nothing under `test/` builds (`configure.ac` (`AC_ARG_ENABLE(cmocka)`)) |
| `--enable-rust-offline` | no | cargo runs `--locked --offline` against vendored crates; also empties `check-local` ([rust.md](rust.md)) |
| `--with-libbdb-ro` | **yes**, contradicting its own help text | a plain `./configure` links the read-only BDB shim from `lib/librobdb/` — consequences in [backends.md](backends.md) (`m4/db.m4` (`AC_ARG_WITH(libbdb-ro)`)) |

The shipping configuration is the RPM spec's `%configure`: `--enable-cmocka`, `--with-selinux`, `--with-systemd`, `--libexecdir=%{_libexecdir}/dirsrv`, plus `--with-libldap-r=no` on current Fedora/RHEL (`rpm/389-ds-base.spec.in` (`%configure`)). No CI job passes anything beyond `cargotest.yml`'s `--enable-debug`.

## `make check` reality

- With `--enable-cmocka`, `make check` builds and runs exactly one C binary, `test_slapd`; without the flag it still compiles the whole tree and runs zero cmocka tests (`Makefile.am` (`check_PROGRAMS`)).
- automake chains `check` into `check-local`, which runs the Rust `cargo test` suites — so `make check` covers both. CI's Rust job invokes `make check-local` directly to skip the C side (`Makefile.am` (`check-local`); `.github/workflows/cargotest.yml` (Run Rust tests step)).
- Under `--enable-asan` or `--enable-rust-offline`, `check-local` becomes an automake-generated empty rule: it exits 0 having run nothing. A green `check-local` there is not coverage (`Makefile.am` (`RUST_ENABLE_OFFLINE` / `enable_asan` conditionals around `check-local`)).
- The cmocka suite still gates every PR: the spec passes `--enable-cmocka` and its `%check` runs `make check` inside `rpmbuild` during both pytest workflows' build jobs, skipped only under asan/msan (`rpm/389-ds-base.spec.in` (`%check`)).
- A new cmocka test takes four edits: the test `.c` file, a `test_slapd_SOURCES` row in `Makefile.am`, a `cmocka_unit_test()` registration in the group's `test.c`, and a prototype in `test/test_slapd.h`. Workflow: see the write-test skill (its cmocka reference).

## The pytest matrix

- Every directory under `dirsrvtests/tests/suites/` becomes one CI job; `replication` is split into one job per `*_test.py` file (`.github/scripts/generate_matrix.py`).
- `dirsrvtests/tests/stress/`, `perf/` and `tickets/` are run by no workflow — the generator walks only `suites/`.
- The per-suite invocation, inside a privileged `:test` container with the freshly built RPMs installed:

```bash
py.test --suppress-no-test-exit-code -m "not flaky" \
    --junit-xml=pytest.xml --html=pytest.html \
    --browser=firefox --browser=chromium \
    -v dirsrvtests/tests/suites/<suite>
```

- `--suppress-no-test-exit-code` (pytest-custom_exit_code) and `--browser` (pytest-playwright) exist only in the CI image — `dirsrvtests/requirements.txt` lists neither package. Drop both flags for a local run.
- CI also exports `WEBUI`, `NSSLAPD_DB_LIB`, `DEBUG`, `PASSWD` and `GSSAPI_ACK` into the container; what the tests do with them, and the whole test-writing contract, is in [testing.md](testing.md).

## Local build (Linux only — run via the environment's build/test skill)

The autotools sequence CI verifies (`.github/workflows/cargotest.yml` (Setup and build step)) — reference material; execute it only through the environment's build/test skill or a CI-equivalent container (verify-changes skill):

```bash
autoreconf -fvi                # repo spelling: -fvi, as in autogen.sh and CI
./configure --enable-debug     # add --enable-cmocka to build test/
make V=0
make lib389
```

- `make lib389` is not a plain Python build: it first copies `dirsrvtests/lib/test389/topologies.py` over `src/lib389/lib389/tests/topologies.py`, then runs `validate_version.py` and `python3 -m build`; `make lib389-install` follows with `pip3 install . --no-deps --force-reinstall` (`Makefile.am` (`lib389`, `lib389-install`)).
- The recommended full-fidelity route is the RPM path CI uses: `SKIP_AUDIT_CI=1 make -f rpm.mk dist-bz2 rpms`, then install `dist/rpms/*.rpm` in the container, exactly as the pytest workflows do.

## Generated files — never hand-edit

| Never edit | Produced by | Edit instead |
|---|---|---|
| `configure`, `Makefile.in`, `aclocal.m4`, `config.h.in`, libtool helper scripts | `autogen.sh` → `autoreconf -fvi` | `configure.ac`, `Makefile.am`, `m4/*.m4` |
| `Makefile`, `config.h`, `config.status`, `libtool` | `./configure` | `configure.ac` / `Makefile.am` |
| `rpm/389-ds-base.spec` | `make -f rpm.mk rpmspec` (also listed in `AC_CONFIG_FILES`) | `rpm/389-ds-base.spec.in` |
| `ldap/ldif/template-*.ldif`, `ldap/admin/src/defaults.inf`, `wrappers/*` | the `fixupcmd` sed via the `%: %.in` pattern rule (`Makefile.am`) | the matching `*.in` file |
| `dberrstrs.h` | `ldap/servers/slapd/mkDBErrStrs.py` | `ldap/servers/slapd/back-ldbm/dbimpl.h` |
| `rust-slapi-private.h`, `rust-nsslapd-private.h` | cbindgen via the crate `build.rs` | the Rust crate sources ([rust.md](rust.md)) |
| `src/lib389/lib389/tests/topologies.py` | `cp` prerequisite of `make lib389` | `dirsrvtests/lib/test389/topologies.py` |
| `src/lib389/man/*.8` | ignored outputs of the argparse-manpage command in `src/lib389/build_hooks.py` (`build_manpages`), wired by `src/lib389/pyproject.toml` (`[tool.setuptools.cmdclass]`, `[tool.build_manpages]`); the directory is ignored by `.gitignore` | the argparse `help=` text under `src/lib389/cli*` |
| `src/cockpit/389-console/cockpit_dist/`, `node_modules/` | `./build.js` (esbuild) via `rpm.mk` (`build-cockpit`) | `src/cockpit/389-console/src/` ([ui.md](ui.md)) |
| `.cargo/config.toml`, `src/pkgconfig/*.pc` | `AC_CONFIG_FILES` (`configure.ac`) | the matching `*.in` file |

Lockfiles (`src/Cargo.lock`, `src/cockpit/389-console/package-lock.json`) are reviewed source artifacts: regenerate them intentionally with the repository's documented target or ecosystem tool, review the complete diff, and never edit their contents by hand. Renovate normally opens grouped, non-auto-merged update PRs (`.github/renovate.json`), but it is not the only permitted source of a lock update. The Rust update and re-vendoring workflow is in [rust.md](rust.md) (`Locks & vendoring`).

## Version invariants

- `version` in `src/lib389/pyproject.toml` must equal `RPM_VERSION` from `VERSION.sh`. `configure` hard-errors on a mismatch and prints the fix: `cd src/lib389 && python3 validate_version.py --update`. The same script gates `make lib389`, `make -f rpm.mk tarballs`, and the RPM `%build` (`configure.ac` (lib389 version sync check); `src/lib389/validate_version.py`).
- `AC_INIT([dirsrv],[1.0],...)` is a placeholder; the real version comes from `VERSION.sh`. Never "fix" `AC_INIT` (`configure.ac` (`AC_INIT`)).

| Language | Floor | Enforced by |
|---|---|---|
| Python (lib389 and dirsrvtests) | 3.8 — no `match`, no `X \| Y` unions, no bare `list[str]` annotations | `vermin --target=3.8` over both trees (`.github/workflows/validate.yml` (minimal Python version steps)) |
| Rust | declared 1.70, edition 2018; not verified, and the lock records `uuid` 1.24.0 with a declared 1.85 floor | Cargo enforces each package's manifest declaration, but CI has no 1.70 compatibility lane and dependency MSRVs can require newer Rust (`.github/workflows/cargotest.yml` (`rust-tests`)); details in [rust.md](rust.md) |
| C | none pinned — compiler default plus `_GNU_SOURCE` on Linux | nothing |
| Node | not pinned | nothing — no `.nvmrc`, no `engines` field |

## Working-tree traps

- `.gitignore` ignores `*.patch`: any patch file you write disappears from `git status` and needs `git add -f` to commit.
- Scope tree-wide searches with `git grep` / `git ls-files` — untracked build products and shadow checkouts inflate `grep -r` results and skew line numbers.
- `CLAUDE.md` and `GEMINI.md` are git symlinks to `AGENTS.md`: edit `AGENTS.md`; writing either symlink as a regular file forks the docs.
