---
name: add-cli-option
description: >-
  Add or change a dsconf/dsctl/dsidm/dscreate flag, subcommand, or noun in
  src/lib389 — argparse wiring, arg_to_attr maps, lib389 allowlists, generated
  man pages, Cockpit coupling, and clu tests. Use when asked to add an option to
  dsconf, create a new dsctl subcommand, expose a server attribute in the CLI,
  or add a CLI noun. Warning: handler signatures differ per tool (dsconf/dsidm
  handlers take 4 args, dsctl/dscreate take 3 — wrong arity is a TypeError at
  dispatch), and hand-built create/add handlers silently drop new flags unless
  an explicit if-assignment is added to the handler.
---

# Add or change a CLI option (dsconf / dsctl / dsidm / dscreate)

All four tools are argparse front-ends in `src/lib389/cli/` that dispatch via
`set_defaults(func=...)` to handlers in `src/lib389/lib389/cli_conf/`, `cli_ctl/`,
and `cli_idm/`. CLI semantics: docs/agents/cli.md. Object API: docs/agents/lib389.md.

## Steps

1. **Identify the entry script and handler arity FIRST.**

   | Tool | Handler signature | Handler package | Note |
   |---|---|---|---|
   | dsconf | `(inst, basedn, log, args)` | `lib389/cli_conf/` | script always passes `basedn=None` |
   | dsidm | `(inst, basedn, log, args)` | `lib389/cli_idm/` | `basedn` is real, resolved before dispatch |
   | dsctl | `(inst, log, args)` | `lib389/cli_ctl/` | normally local-only; `healthcheck` connects, then falls back offline on `ldap.SERVER_DOWN` |
   | dscreate | `(inst, log, args)` | `lib389/cli_ctl/instance.py` | there is no `cli_create/` package |

   **STOP if you copied a handler signature from a different tool — wrong arity is a
   TypeError at dispatch, not at import time.**

   The `dsctl` entry script only pre-allocates the local instance, but
   `health_check_run` is a deliberate exception: it calls `connect_instance`
   inside the handler. If that raises `ldap.SERVER_DOWN`, it keeps the local
   instance and runs only offline-capable checks; other connection errors fail
   the command. Do not assume this exception applies to other dsctl handlers.

2. **Read the target handler end-to-end and classify its routing pattern** (full
   table: references/handler-patterns.md). The split that matters: generic
   `arg_to_attr`-driven edit vs hand-built `properties`/`replace_list` construction.
   **Guardrail: in a hand-built create/add handler, adding the argparse flag plus the
   `arg_to_attr` entry alone is a SILENT no-op — the flag parses, sets `args.x`, and
   is dropped without error. Add the explicit `if args.x is not None:` assignment
   into the properties dict. The create/set asymmetry means the same flag can work
   on `set` and no-op on `create`.**
   Verify: `git grep -n 'arg_to_attr' src/lib389/lib389/cli_conf/<module>.py`, then
   read the handler to see whether it consumes the map or references `args.x` by name.

3. Add `parser.add_argument('--my-flag', help='...')` inside the module's
   `create_parser(subparsers)` on the right sub-parser. Keep
   `formatter_class=CustomHelpFormatter` (from `lib389.cli_base`) on every
   `add_parser(...)` call and `set_defaults(func=<handler>)` on every leaf. Then add
   the `arg_to_attr` entry: key is the argparse dest (dashes become underscores),
   value is the LDAP attribute — `--dynamic-list-attr` maps as
   `'dynamic_list_attr': 'nsslapd-dynamic-lists-attr'`.

4. **Allowlists** — the lib389 model layer rejects unknown attributes on two verbs:
   - `dsconf backend config set`: the LDAP attribute must be in
     `DatabaseConfig._GLOBAL_ATTRS` or `_DB_ATTRS['bdb'|'mdb']`
     (`src/lib389/lib389/backend.py`), or `set()` raises "Can not update database
     configuration with unknown attribute".
   - `dsconf backend suffix set`: routes through `BackendSuffixView.__init__`'s
     `be_args`/`mt_args` lists; missing raises "No mapping for attribute ... in
     composite object".
   Verify: grep the LDAP attribute into those classes if your verb routes through
   them. A plain `cn=config` attribute needs no CLI change at all —
   `dsconf <inst> config replace attr=value` is free-form; a dedicated flag is
   convenience only.

5. **Never edit or create a man page for these tools.** The lib389 `build_py` hook
   in `src/lib389/build_hooks.py` invokes `build_manpages`; its
   `[tool.build_manpages]` configuration in `src/lib389/pyproject.toml` generates
   ignored outputs under `src/lib389/man/` from each entry script's argparse help.
   They are not tracked (the tracked pages under top-level `man/` belong to other
   tools), so the `help=` text you write IS the man page.

6. Exit and error semantics: the entry script exits 1 only when the handler returns
   the literal `False` (`None`/`0`/`[]` are success). Prefer `raise ValueError("...")`
   for failures — the script catches, formats, and exits 1 — never
   `log.error(...); return`. Validate flag values with a `type=` callable raising
   `argparse.ArgumentTypeError`, not post-hoc checks in the handler. Emit output via
   `log.info(...)` and add an `if args.json:` branch if the subcommand supports JSON.

7. **STOP before renaming or removing any existing flag or subcommand: the Cockpit
   UI pushes literal dsconf argv strings onto command lines, so a rename breaks the
   UI silently while adding a flag cannot.**
   Verify: `git grep -rn -- '--old-flag' src/cockpit/389-console/src/` — any hit
   means a coordinated UI change (ui-expose-attribute skill). A UI counterpart for a
   new flag is customary, not required.

8. **Test** under `dirsrvtests/tests/suites/clu/`, matching the target file's
   existing convention — in-process handler calls with `FakeArgs` vs `subprocess`
   against the installed binary. `FakeArgs.__init__` sets NOTHING: set every
   attribute the handler reads, including `args.json`. In-process tests call the
   handler with `topo.logcap.log` and assert with `check_value_in_log_and_reset(...)`
   from the suite's `__init__.py` (it flushes the capture for the next test).
   Verify: run the suite via the verify-changes skill before claiming the change works.

For a brand-new subcommand noun (new module, entry-script registration, possible
lib389 wrapper class): references/new-noun.md.

## Maintenance

Derived from the entry scripts in `src/lib389/cli/`, `lib389/cli_base/__init__.py`, and `lib389/cli_conf/__init__.py`.
If a handler moves between the hand-built list in references/handler-patterns.md and the generic helpers, update that table.
