# AGENTS.md — 389 Directory Server (389-ds-base)

389 Directory Server is an LDAP server: C core (`ldap/servers/slapd/`), C plugins
(`ldap/servers/plugins/`), Rust components (workspace `src/Cargo.toml`), the lib389
Python management library (`src/lib389/`), the dsconf/dsctl/dsidm/dscreate CLI tools
(`src/lib389/cli/`), pytest integration suites (`dirsrvtests/`), and a Cockpit React
web UI (`src/cockpit/389-console/`). Autotools build, RPM packaging, GitHub Actions CI.

This file is an index. Before editing an area, read its guide in `docs/agents/`
(orientation table below). Step-by-step task recipes live in `.agents/skills/`.

## Hard invariants (these fail silently — memorize)

- **Linux only.** `ns-slapd` builds and runs only on Linux — any other host falls
  out of `configure.ac`'s host switch with an empty `platform`, and the server uses
  Linux-only headers. Build and verify only through a Linux environment — see
  [docs/agents/building.md](docs/agents/building.md).
- **`CLAUDE.md` and `GEMINI.md` are git symlinks to this file.** Only edit
  `AGENTS.md`; writing the others as regular files forks the documentation.
- **Never hand-edit generated artifacts**: `configure`, `Makefile.in`,
  `rpm/389-ds-base.spec`, `ldap/ldif/template-dse.ldif`, the ignored
  `src/lib389/man/*.8` build outputs, `src/lib389/lib389/tests/topologies.py`,
  `src/cockpit/389-console/cockpit_dist/`.
  Full table with what to edit instead: [docs/agents/building.md](docs/agents/building.md).
- **Tests import topology fixtures from `test389.topologies`**
  (`dirsrvtests/lib/test389/`); `lib389.topologies` no longer exists.
- **Every function named `test_*` under `dirsrvtests/tests/suites/` — fixtures
  and helpers included — needs a `:id:` UUID unique within that directory.** The
  gate recursively searches the directory as text, so never copy an ID into a
  comment or another docstring.
- **Python 3.8 is the CI-enforced floor** for `src/lib389` and `dirsrvtests`:
  no `match`, no `X | Y` unions, no bare `list[str]` annotations.
- **Evaluate every `back-ldbm` change against BOTH `db-bdb/` and `db-mdb/`**, and
  never trust a green "BDB test" CI job as backend coverage — it can run zero
  tests. See [docs/agents/backends.md](docs/agents/backends.md).
- **SLAPI memory ownership is per-function and not guessable** (e.g.
  `slapi_search_internal_get_entry` returns a copy you free;
  `slapi_search_get_entry` returns a borrow you must not). Read
  [docs/agents/c-server.md](docs/agents/c-server.md) before allocating or freeing in C.
- **DSE/`cn=config` callbacks invert the return convention**: OK = 1, ERROR = -1;
  returning 0 blocks the change.
- **A new dsconf flag can parse and still be silently dropped** by hand-built
  create/add handlers — read [docs/agents/cli.md](docs/agents/cli.md) first.
- **`.gitignore` ignores `*.patch`** — patch files vanish from `git status`.
- **Commit subject is `Issue NNNN - summary`; never type a `(#PR)` suffix**
  (squash-merge appends it). See [docs/agents/contributing.md](docs/agents/contributing.md).

## Orientation: read before touching

| You are about to… | Read first |
|---|---|
| Build, check CI behavior, or verify a change | [docs/agents/building.md](docs/agents/building.md) |
| Edit C in `ldap/servers/slapd/` (core, `cn=config`, DSE) | [docs/agents/c-server.md](docs/agents/c-server.md) |
| Edit or add a plugin (`ldap/servers/plugins/`, `src/plugins/`) | [docs/agents/plugins.md](docs/agents/plugins.md) |
| Edit `back-ldbm/`, `db-bdb/`, `db-mdb/` | [docs/agents/backends.md](docs/agents/backends.md) |
| Work on replication (C or tests) | [docs/agents/replication.md](docs/agents/replication.md) |
| Edit Rust (`src/*/Cargo.toml`, `src/plugins/`, FFI) | [docs/agents/rust.md](docs/agents/rust.md) |
| Edit lib389 (`src/lib389/lib389/`) | [docs/agents/lib389.md](docs/agents/lib389.md) |
| Edit the CLI tools (`src/lib389/cli*`) | [docs/agents/cli.md](docs/agents/cli.md) |
| Edit the web UI (`src/cockpit/389-console/`) | [docs/agents/ui.md](docs/agents/ui.md) |
| Write or change tests (`dirsrvtests/`) | [docs/agents/testing.md](docs/agents/testing.md) |
| Commit, add files, format code | [docs/agents/contributing.md](docs/agents/contributing.md) |
| Find where something lives / trace a request | [docs/agents/architecture.md](docs/agents/architecture.md) |

## Task skills

Step-by-step recipes with guardrails live in `.agents/skills/<name>/SKILL.md`:
`verify-changes`, `write-test`, `commit-and-pr`, `add-config-attribute`,
`add-cli-option`, `touch-backend`, `ui-expose-attribute`. Before starting a
matching task, read the skill in full and follow its numbered steps — they encode
CI-exact commands and known silent-failure traps.

## Guardrails

- Only a Linux build/test environment proves a server-side change works. Verify
  through the environment's 389-ds-base build/test skill when one exists (the
  verify-changes skill routes this); with no such skill, run only the static
  gates and report the rest as unverified — never configure, build, or run
  tests directly, as unprepared setups differ per developer.
- `ns-slapd` is heavily multi-threaded: every operation runs on a worker thread
  (`ldap/servers/slapd/connection.c (connection_threadmain)`), and plugin, DSE,
  and housekeeping callbacks run concurrently against shared state. Any change
  that touches threading or shared data must be thread-safe — no data races,
  one fixed lock order, every lock released on every exit path. Primitives and
  per-object rules: [docs/agents/c-server.md](docs/agents/c-server.md).
- Performance is critical: server code sits on the hot path of every LDAP
  operation. Write the most performant implementation that is still correct —
  no new allocations, copies, locks, or internal searches on per-operation
  paths without need. See the performance section in
  [docs/agents/c-server.md](docs/agents/c-server.md).
- In new test code prefer the modern lib389 object API over raw `add_s`-style
  calls; existing raw calls are not style precedent.
- Match the style of the file you are editing; no formatter runs in CI.
- Reference docs and skills are part of the code change: when code, CI, or an
  interface changes behavior they describe, update every affected guide or skill
  in the same PR. Docs cite `path (symbol)` so claims can be checked; when a doc
  and the code disagree, trust the code and correct the doc.
