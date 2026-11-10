# CLI handler routing patterns

How `args.<dest>` values become LDAP modifications differs per handler. Classify the
handler before adding a flag — the cost of a new option depends on the pattern.

## The six routing patterns

| # | Pattern | Where | New option costs |
|---|---|---|---|
| P1 | shared `generic_object_add/edit/add_attr/del_attr` helpers + module `arg_to_attr` | most of `cli_conf/plugins/` (see the caveat under the hand-built list), `cli_conf/plugin.py` | 1 map entry + 1 `add_argument` |
| P2 | local `_args_to_attrs(args)` closing over a module-global `arg_to_attr` | `cli_conf/backend.py`, `chaining.py`, `replication.py` (pwpolicy reads its map off the `PwPolicyManager` class instead) | same, plus any allowlist (SKILL.md step 4) |
| P3 | hand-built `properties` / `replace_list` dict | the handlers listed below | map entry + `add_argument` + **explicit `if args.x is not None:` line** |
| P4 | attribute table drives both flags and setter | `cli_conf/security.py` `SECURITY_ATTRS_MAP`/`RSA_ATTRS_MAP` — the parser builder loops the map calling `add_argument(f'--{opt}')` | 1 table entry; no parser or handler edit |
| P5 | attribute name derived from `set_defaults` values | `cli_conf/logging.py` — `attr = "nsslapd-" + args.logtype + "log"`, plus `"-" + args.keyword` when `keyword` is set | 1 subparser with `set_defaults(func=update_config, logtype=..., keyword=...)` + a row in the matching display map |
| P6 | flags generated from the lib389 class's attribute lists | `cli_conf/saslmappings.py`, most `cli_idm/` modules, via `populate_attr_arguments` | add the attribute to the DSLdapObject class (docs/agents/lib389.md) |

Two arities share the `_args_to_attrs` name: the module-local versions in
`backend.py`/`chaining.py`/`replication.py` take `(args)` and close over the module
global; the shared `cli_conf/__init__.py` version — and `pwpolicy.py`'s own
module-local copy — take `(args, arg_to_attr)`. Check which one the handler calls
before copying code.

## Hand-built handlers in the top-level cli_conf/*.py (complete for that level — each needs the explicit assignment)

| File | Functions |
|---|---|
| `backend.py` | `backend_create`, `backend_set`, `backend_create_vlv`, `backend_compact` |
| `backup.py` | `backup_create`, `backup_restore` |
| `chaining.py` | `config_set` |
| `replication.py` | `enable_replication`, `create_repl_manager`, `add_agmt`, `add_winsync_agmt` |
| `security.py` | `encryption_module_add`, `encryption_module_edit` |
| `schema.py` | `_get_parameters` (shared builder for attributetype/objectclass add and edit — extend the builder, not its four callers) |

Everything else in the top-level `cli_conf/*.py` is map- or table-driven. The
create/set asymmetry is systemic: `add_agmt` is hand-built while `set_agmt` is
map-driven; likewise `add_winsync_agmt` vs `set_winsync_agmt`, and
`backend_create_vlv` vs `backend_edit_vlv`. A flag wired only into the map works on
`set` and silently no-ops on `create`.

**Caveat: `cli_conf/plugins/` and `cli_idm/` are NOT exhaustively audited** — read
the handler before assuming it is map-driven. Known exceptions in
`cli_conf/plugins/`: `ldappassthrough.py` is entirely hand-coded (`pta_add`/
`pta_edit` write `nsslapd-pluginarg<N>` directly), and `entryuuid.py`,
`pwstorage.py`, `usn.py` have no `arg_to_attr` at all.

## Magic `DN` positional

The shared `_args_to_attrs` treats an argparse positional literally named `DN`
specially: the first RDN of its value is split and injected into the properties as
attribute + value. That is why plugin config-entry subcommands declare
`add_argument('DN', ...)` in uppercase. Do not name an unrelated positional `DN`.

## "delete" sentinel and empty-value semantics in the shared helpers

- `generic_object_edit`: a value equal to the literal string `delete` (or
  `["delete"]`) becomes MOD_DELETE of the whole attribute. An empty modlist raises
  `ValueError("There is nothing to change ...")` — except when `enabled` was among
  the unchanged attributes, which logs "already enabled/disabled" and succeeds.
- `generic_object_add`: values equal to `""` / `[""]` are silently dropped from the
  properties before create.
- Consequence: a flag whose legitimate value could be the word `delete` or an empty
  string misbehaves in these helpers — hand-code that handler instead.
- `generic_object_add` has a mutable default `props={}` that it mutates — always pass
  `props=` explicitly, or values leak between calls when lib389 is used as a library.

## dsidm couplings

- Container check keyed by MODULE NAME: dsidm derives the default container from the
  last component of `args.func.__module__` (`lib389.cli_idm.user` -> `ou=People`) via
  `BASEDN_RDNS` in `cli_idm/__init__.py`. An unrecognised module name silently skips
  the container-existence check — do not move handlers into differently-named modules.
- Create flags are GENERATED, not listed: `populate_attr_arguments(parser, attrs)`
  builds `--<attr>` flags from the singular class's `_must_attributes` (user path) or
  from module-level `MUST_ATTRIBUTES`/`MAY_ATTRIBUTES` lists (group path). To add a
  create flag, change the attribute lists, not the parser. Dashes in attribute names
  normalise to underscores when dests are read back; a missing `userpassword` prompts
  via getpass.
- `dsidm user` resolves `--user-type` in a pre-pass parser to pick the class pair from
  `SINGULAR_DICT`/`MANY_DICT`; adding a user type means extending `USER_TYPE_CHOICES`
  and both dicts in `cli_idm/user.py`.
- Destructive dsidm verbs prompt for the exact string `Yes I am sure`; handlers take a
  `warn=True` kwarg so tests bypass the prompt with `warn=False`.
