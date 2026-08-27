---
name: verify-changes
description: Verify a 389-ds-base change by routing through the environment's build/test skill — static Python gates plus, via that skill, C compile, cmocka, RPM build and pytest suites, cargo tests, and the Cockpit UI build, picking the right checks for what was touched. Use when asked to test this change, build this, run the tests, run CI locally, or check nothing regressed before a PR. Never configure, build, or run tests directly - developer setups differ (prefixes, containers, VMs) and the server builds and runs only on Linux; with no build/test skill available, run only the static gates and report everything else as unverified.
---

# Verify changes

## 1. Route through the environment's build/test skill FIRST

Look through the available skills for one that builds or tests 389-ds-base in this
environment (a container wrapper, VM scripts, a prefix-based setup — names vary per
developer).

- **Present**: use it for EVERY compile, cmocka, RPM, pytest, cargo, and UI-build
  step. The table in step 3 says WHAT CI checks for what you touched; the reference
  sections below say what each check must achieve; the skill decides HOW to run it.
- **STOP: absent — do not configure, build, or run tests.** Raw autotools, rpm,
  docker, npm, or pytest commands on an unprepared host fail or corrupt the
  developer's environment, and the server cannot be built or run outside Linux at
  all. Run only the static gates (step 2), then report which checks from step 3
  remain unverified.

## 2. Static gates (read-only, safe on any machine — always run for Python or test changes)

Run the three validate.yml gates:

1. `python3 dirsrvtests/check_for_duplicate_ids.py dirsrvtests/tests/suites`
2. `testimony validate -c dirsrvtests/testimony.yaml dirsrvtests/tests/suites` (if testimony is installed)
3. `uv tool run vermin --target=3.8 src/lib389` and `uv tool run vermin --target=3.8 dirsrvtests` — exactly how validate.yml invokes vermin (on failure CI reruns it with `-vv`; a plain `vermin` install works too)

**Never use Python 3.9+ syntax in src/lib389 or dirsrvtests** — 3.8 is the enforced floor: no `match` statements, no `X | Y` unions, no bare `list[str]` annotations.
Verify: every gate exits 0 — check with `echo $?` after each.

## 3. Which CI checks cover what you touched

| Touched | CI runs |
|---|---|
| C server or plugins (ldap/, lib/) | compile + cmocka + the affected pytest suite |
| back-ldbm (ldap/servers/slapd/back-ldbm/) | same, but the suite under BOTH backends — see the touch-backend skill and docs/agents/backends.md |
| Rust crates (src/, src/plugins/) | cargo tests + the affected pytest suite |
| lib389 or dsconf/dsctl/dsidm/dscreate | static gates + the clu suite |
| dirsrvtests | static gates + the suite you touched |
| Cockpit UI (src/cockpit/389-console/) | npm build (+ audit) |

## Reference: the CI commands, check by check

Everything below is taken from the CI workflow files (pytest.yml, lmdbpytest.yml,
compile.yml, cargotest.yml, validate.yml, npm.yml) and rpm.mk — passing them means
passing in CI. **This is documentation of what the build/test skill must achieve,
not commands to run directly on an unprepared host** (step 1). Background:
docs/agents/building.md and docs/agents/testing.md.

### C compile parity (Linux, build deps installed)

compile.yml's shape: `autoreconf -fvi && ./configure && make V=0`
compile.yml also has a GCC strict-flags matrix entry (`-Wall -Wextra -Wshadow -Wstrict-prototypes` and more), but no CI job passes `-Werror` — warnings surface only as PR annotations. Fix them anyway.
Verify: rebuild with the CI wrap `bash -c "(make V=0 2> >(tee /dev/stderr)) > log.txt"`, then `grep -i "warning:" log.txt` — no line may mention a file you touched.

### cmocka C unit tests (Linux)

`./configure --enable-cmocka && make && make check`
**A bare `./configure` proves nothing — `--enable-cmocka` defaults to off, so a bare configure compiles zero C unit tests and `make check` still exits green.** CI gets cmocka through the RPM build, whose spec passes the flag unconditionally.
`make check` also invokes `check-local` (the Rust tests, below). Under `--enable-asan` or `--enable-rust-offline`, `check-local` is an empty rule that exits 0 — a green run there is not coverage.
Verify: `ls ./test_slapd` — the single cmocka binary; if absent, no C unit test was built or run.

### Full CI-fidelity RPM + pytest path (mirrors pytest.yml)

Annotated end-to-end transcript with every flag explained: references/container-recipe.md. The stages:

1. `git config --global --add safe.directory <repo path>` FIRST — `dist-bz2` shells out to `git ls-files`, and git-as-root refusing an "untrusted" repo makes the tarball silently empty.
2. `SKIP_AUDIT_CI=1 make -f rpm.mk dist-bz2 rpms` — the exact CI build command. The `rpms` target rsyncs the working tree, so uncommitted changes ARE built; `dist-bz2` tars only `git ls-files` output (plus vendor/ and cockpit_dist/), so alone it would miss them — CI runs both, in this order.
3. Start a systemd container from quay.io/389ds/ci-images:test (`--privileged`, workspace mounted) and wait for `systemctl is-system-running`.
4. `dnf install -y dist/rpms/*rpm` inside it.
5. Run the suite matching the feature (next section): `clu` for CLI changes; backend changes run once per `NSSLAPD_DB_LIB` value (`bdb`, then `mdb`).

Verify: `ls dist/rpms/*.rpm` after stage 2 — an empty glob means the tarball trap fired.

### The pytest invocation

`py.test -m "not flaky" -v dirsrvtests/tests/suites/<suite>`

Run from the repo root so the suite path makes dirsrvtests/ the pytest rootdir (that wires up the conftest and `test389` imports). This is pytest.yml's line minus the flags only the CI image satisfies — `--suppress-no-test-exit-code` (pytest-custom_exit_code), `--html=pytest.html` (pytest-html), `--browser=firefox --browser=chromium` (pytest-playwright); none are in dirsrvtests/requirements.txt. Also drop CI's `WEBUI=1` and `GSSAPI_ACK=1` env vars — GSSAPI suites rewrite the host Kerberos realm.
Verify: `py.test --collect-only -q dirsrvtests/tests/suites/<suite> | tail -1` reports a nonzero test count — a green run that collected 0 tests proved nothing.

### Cockpit UI build

`npm ci && npm run build` from src/cockpit/389-console.
A broken import fails every PR: the RPM build inside pytest.yml bundles the UI with esbuild. The audit gate, exactly as npm.yml runs it (from src/cockpit/389-console): `npx --yes audit-ci --config audit-ci.json`
**Never run `npm run prettier:fix`** — there is no prettier config, so it would reformat the tree with defaults that fight the eslint rules.
Verify: `ls src/cockpit/389-console/dist/index.html` — the bundle landed. Conventions: docs/agents/ui.md.

### Rust tests (Linux)

cargotest.yml's sequence: `autoreconf -fvi && ./configure --enable-debug && make V=0 && make check-local`
`check-local` cargo-tests librslapd, librnsslapd and pwdchan only. Same guardrail as cmocka: under `--enable-asan` or `--enable-rust-offline` it is an empty rule that exits 0 — do not read that as a pass. Details: docs/agents/rust.md.
Verify: `make check-local 2>&1 | grep -c "test result:"` — expect at least 3; 0 means the empty-rule trap fired.

## Maintenance
If a step no longer matches the code or CI, update this skill in the same PR as the change that moved it.
