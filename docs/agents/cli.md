# CLI Tools (dsconf / dsctl / dsidm / dscreate)

The entry scripts are plain executable Python files under `src/lib389/cli/`; handlers live in `src/lib389/lib389/cli_conf/` (dsconf), `cli_ctl/` (dsctl and dscreate), `cli_idm/` (dsidm), with shared helpers in `cli_base/`. The underlying object API is [lib389.md](lib389.md).

## Handler signatures differ per tool

There is no single CLI handler signature. Each entry script dispatches `args.func(...)` with a fixed argument list — the wrong arity is a `TypeError` at dispatch time:

| Tool | Dispatch (entry script) | Handler signature | Handler package |
|---|---|---|---|
| `dsconf` | `args.func(inst, None, log, args)` | `(inst, basedn, log, args)` — `basedn` is **always literal `None`** | `lib389/cli_conf/` |
| `dsidm` | `args.func(inst, basedn, log, args)` | `(inst, basedn, log, args)` — real basedn | `lib389/cli_idm/` |
| `dsctl` | `args.func(inst, log, args)` | `(inst, log, args)` — three args | `lib389/cli_ctl/` |
| `dscreate` | `args.func(inst, log, args)` | `(inst, log, args)` — three args | `lib389/cli_ctl/instance.py` |

- The scripts exit 1 only when a handler returns **exactly `False`** — `None`, `0`, `[]`, `''` all count as success. Signal failure by raising (a `ValueError` is formatted for the user), not by return value.
- `dsconf`/`dsidm` open an LDAP connection before dispatch (`connect_instance`).
  `dsctl` normally only calls `inst.local_simple_allocate(...)`, so its handlers
  operate on local files and services. **`dsctl healthcheck` is the exception**:
  `health_check_run` calls `connect_instance` itself, then catches only
  `ldap.SERVER_DOWN` to retain the pre-allocated local instance and run the
  offline-capable checks; checks that require a running server are skipped with
  explicit messages.
- dsconf handlers must still declare and forward `basedn` even though the script always passes `None`; config objects derive their own base.

## The silent-drop trap

An `add_argument` plus an `arg_to_attr` entry is only sufficient for map-driven handlers. Hand-built `create`/`add` handlers construct their `properties`/`replace_list` dict explicitly and **drop any new flag** without an explicit `if args.x is not None:` assignment — the flag parses, sets `args.x`, and silently does nothing. The asymmetry is systemic: `set` verbs are usually map-driven while their `create` twins are hand-built (`set_agmt` vs `add_agmt` and `set_winsync_agmt` vs `add_winsync_agmt` in `cli_conf/replication.py`; `backend_edit_vlv` vs `backend_create_vlv` in `cli_conf/backend.py`), so a flag added only to the map works on `set` and no-ops on `create`.

Routing patterns in `cli_conf/`:

| Pattern | Where | Cost of a new option |
|---|---|---|
| Shared `generic_object_*` + module `arg_to_attr` | most of `cli_conf/plugins/` (not all — see below the table), `cli_conf/plugin.py` | 1 map entry + 1 `add_argument` |
| Local `_args_to_attrs(args)` closing over a module-global map | `cli_conf/backend.py`, `chaining.py`, `replication.py` (one-arg); `cli_conf/pwpolicy.py` defines its own two-arg `_args_to_attrs(args, arg_to_attr)` reading `PwPolicyManager.arg_to_attr` from `src/lib389/lib389/pwpolicy.py` | same, plus any allowlist (below) |
| Hand-built `properties`/`replace_list` dict | handlers listed below | map entry + `add_argument` + **explicit `if args.x is not None:` line** |
| Attribute table drives both flags and setter | `cli_conf/security.py` (`SECURITY_ATTRS_MAP`, `RSA_ATTRS_MAP`; `_security_generic_set_parser` generates the flags) | 1 table entry; no parser or handler edit |
| Attribute name derived from `set_defaults` values | `cli_conf/logging.py` (`update_config` builds `nsslapd-<logtype>log`, plus `-<keyword>` when a keyword is set) | 1 subparser with `set_defaults(func=update_config, logtype=..., keyword=...)` |
| Flags generated from the lib389 class's `_must_attributes` | `cli_conf/saslmappings.py` and `cli_idm/` modules, via `populate_attr_arguments` in `cli_base/__init__.py` | add the attribute on the DSLdapObject ([lib389.md](lib389.md)) |

Hand-built handlers in the top-level `cli_conf/*.py` — every one needs the explicit assignment for a new flag:

| File | Functions |
|---|---|
| `cli_conf/backend.py` | `backend_create`, `backend_set`, `backend_create_vlv`, `backend_compact` |
| `cli_conf/backup.py` | `backup_create`, `backup_restore` |
| `cli_conf/chaining.py` | `config_set` |
| `cli_conf/replication.py` | `enable_replication`, `create_repl_manager`, `add_agmt`, `add_winsync_agmt` |
| `cli_conf/security.py` | `encryption_module_add`, `encryption_module_edit` |
| `cli_conf/schema.py` | `_get_parameters` — a shared builder; edit it, not its callers |

The table above is complete only for the top-level `cli_conf/*.py` modules. `cli_conf/plugins/` is NOT exhaustively audited: `plugins/ldappassthrough.py` is entirely hand-coded (`pta_add`/`pta_edit` write `nsslapd-pluginarg<N>` directly), and several plugin modules (`entryuuid.py`, `pwstorage.py`, `usn.py`) have no `arg_to_attr` at all — read the handler before assuming it is map-driven.

`arg_to_attr` keys are argparse *dests* (underscores), values are LDAP attribute names: `--max-concurrent` → `'max_concurrent': 'syncrepl-max-concurrent'` (`cli_conf/plugins/contentsync.py`).

## Registration

- Each resource module defines `create_parser(subparsers)`; the entry script calls it, and `cli_conf/plugin.py` (`create_parser`) fans out to every plugin module's `create_parser`.
- Each entry script builds a module-level `argparse.ArgumentParser` named exactly `parser`. **The variable name is load-bearing**: `[tool.build_manpages]` in `src/lib389/pyproject.toml` reads `object=parser` per script, and the lib389 `build_py` hook in `src/lib389/build_hooks.py` runs `build_manpages` to produce the pages from argparse help. Those outputs live under the ignored `src/lib389/man/` directory and are not tracked (the tracked pages under top-level `man/` belong to other tools) — never write, edit, or add a generated CLI page; the `help=` string IS the man page.
- New `add_parser(...)` calls take `formatter_class=CustomHelpFormatter` (from `lib389.cli_base`) — a convention, not an invariant; dozens of older calls omit it — and every leaf subparser calls `set_defaults(func=<handler>)`.

## Model-layer allowlists

- `dsconf <inst> backend config set --<x>` additionally requires the LDAP attribute to be listed in `DatabaseConfig._GLOBAL_ATTRS` or `_DB_ATTRS['bdb'|'mdb']` in `src/lib389/lib389/backend.py`; otherwise `DatabaseConfig.set` raises `ValueError("Can not update database configuration with unknown attribute: ...")`.
- `dsconf backend suffix set` goes through `BackendSuffixView` (same file), which registers a fixed set of backend and mapping-tree attributes; an unregistered attribute raises `ValueError('No mapping for attribute ...')` from `CompositeDSLdapObject._find_idx` (`src/lib389/lib389/_mapped_object.py`).
- A plain `cn=config` attribute needs **no lib389 change at all**: `dsconf <inst> config replace attr=value` parses free-form `attr=value` pairs generically (`config_replace_attr` in `cli_conf/config.py`), multi-valued included. A dedicated `--flag` is convenience only.

## Shared write helpers

The map-driven writes go through the helpers in `src/lib389/lib389/cli_conf/__init__.py`:

- `generic_object_edit(dsldap_object, log, args, arg_to_attr)` — replace/delete on an existing entry. The **literal value `delete`** (or `["delete"]`) means MOD_DELETE of that attribute, so a flag whose legitimate value could be the word "delete" misbehaves. Unchanged values are skipped; an empty modlist raises `ValueError("There is nothing to change in the ... entry")` — unless the unchanged attribute was the plugin `enabled` toggle, which logs "already enabled/disabled" and succeeds.
- `generic_object_add(dsldap_objects_class, inst, log, args, arg_to_attr, dn=..., basedn=..., props=...)` — create; drops values equal to `""`/`[""]`.
- `generic_object_add_attr` / `generic_object_del_attr` — MOD_ADD/MOD_DELETE of listed attributes via `apply_mods`.
- `_args_to_attrs` has a magic key: an argparse positional literally named `DN` is split into its rdn attribute and value and injected into the properties — which is why plugin config-entry subcommands declare `add_argument('DN', ...)` in uppercase.
- `add_generic_plugin_parsers(subparser, plugin_cls)` auto-creates `show`/`enable`/`disable`/`status` for a plugin module, stashing the class via `set_defaults(plugin_cls=...)`.

## dsidm wiring

- `dsidm` resolves the basedn **before** dispatch (`_get_basedn_arg` in `src/lib389/lib389/cli_idm/__init__.py`): it validates that the suffix entry exists, then derives the expected container rdn (`ou=People`, ...) from the **last component of the handler's module name** via `BASEDN_RDNS` (`lib389.cli_idm.user` → `user` → `ou=People`). An unknown module name silently skips the container check — placing a user handler in a differently-named module disables the validation.
- `dsidm user create` flags are generated at parser-build time from the selected class's `_must_attributes` (`populate_attr_arguments`); to add a flag, add the attribute on the lib389 class ([lib389.md](lib389.md)), not the CLI.
- Destructive verbs prompt for the exact string `Yes I am sure` (`_warn` in `cli_idm/__init__.py`) and take a `warn=True` kwarg, so in-process tests bypass the prompt with `warn=False`.

## Output and errors

- Emit through the `log` object passed to the handler: `log.info` goes to stdout, `log.error` to stderr (`setup_script_logger` in `src/lib389/lib389/cli_base/__init__.py`). JSON mode is a hand-written `if args.json:` branch per handler emitting `json.dumps(..., indent=4)`; common envelopes are `{"type": "list", "items": [...]}` and `{"type": "entry", "dn": ..., "attrs": {...}}`.
- Success messages use fixed phrasings that tests grep for — keep them verbatim: `Successfully created %s`, `Successfully deleted %s`, `Successfully changed the %s`, `Successfully modified %s` (`cli_base/__init__.py`, `cli_conf/__init__.py`).
- **Missing positionals are prompted for at runtime**, not rejected: `_get_arg` in `cli_base/__init__.py` falls back to `input()`/`getpass()`, so a non-interactive caller that omits one hangs. Always pass every positional.
- The generic `modify` verbs use the colon grammar `<add|delete|replace>:<attribute>:<value>`, split by a plain `split(":")` into exactly three parts (`_generic_modify_change_to_mod` in `cli_base/__init__.py`) — a value containing a colon fails with "Too many arguments"; `delete:attr:` (empty value) deletes the whole attribute.

## CLI tests

CLI tests live under `dirsrvtests/tests/suites/clu/`. Two conventions coexist — match the file you extend:

- **In-process**: import the handler from `lib389.cli_*` and call it with a `FakeArgs()` object. `FakeArgs.__init__` sets nothing, so set **every** attribute the handler reads, including `.json`. Assertions go through the topology's `topo.logcap` via `check_value_in_log_and_reset` (`dirsrvtests/tests/suites/clu/__init__.py`).
- **Subprocess**: run the installed binary (`/usr/sbin/dsconf <inst> -j ...`) and assert on its output.

Test authoring details (fixtures, docstring format, markers): [testing.md](testing.md).

## UI coupling

The Cockpit UI shells out with **literal `dsconf` argv strings** — e.g. `cmd.push("--enable-dynamic-lists")` in `src/cockpit/389-console/src/lib/database/databaseConfig.jsx` (`GlobalDatabaseConfig.handleSaveDBConfig`). Renaming or removing a flag or subcommand therefore breaks the UI silently; grep `src/cockpit/389-console/src/` before renaming anything. Adding a flag never breaks the UI, and exposing new options there is customary, not required — see [ui.md](ui.md).

Workflow: see the add-cli-option skill (`.agents/skills/add-cli-option/SKILL.md`).
