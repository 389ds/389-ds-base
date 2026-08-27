# Adding a new CLI subcommand noun

A new noun (`dsconf <inst> mynoun ...`) is a new module plus a two-line registration
in the entry script. Handler arity per tool: SKILL.md step 1.

## Registration — two lines in the entry script

The real pattern from `src/lib389/cli/dsconf` (dsctl and dsidm are identical in shape,
with `cli_ctl/` and `cli_idm/` packages):

```python
from lib389.cli_conf import backend as cli_backend    # 1. import the module
...
cli_backend.create_parser(subparsers)                 # 2. register its parsers
```

Known irregularities — do not copy them: `directory_manager` exports `create_parsers`
(plural) and is called that way; `dsidm`'s user module takes an extra `user_type=`
kwarg. A dsconf plugin noun registers inside `cli_conf/plugin.py`'s `create_parser`,
not in the entry script. dscreate has no package of its own — its four handlers live
in `cli_ctl/instance.py`, and a new dscreate `.inf` option must additionally be
registered in `lib389/instance/options.py`: `parse_inf_config` iterates only
registered option keys and silently ignores unknown ones.

## Module skeleton (`cli_conf/<noun>.py`)

```python
arg_to_attr = {'my_flag': 'nsslapd-my-attribute'}

def mynoun_set(inst, basedn, log, args):   # dsconf arity — see SKILL.md step 1
    thing = MyNoun(inst)
    generic_object_edit(thing, log, args, arg_to_attr)

def create_parser(subparsers):
    noun = subparsers.add_parser('mynoun', help='...', formatter_class=CustomHelpFormatter)
    subcommands = noun.add_subparsers(help='action')
    set_p = subcommands.add_parser('set', help='...', formatter_class=CustomHelpFormatter)
    set_p.set_defaults(func=mynoun_set)
    set_p.add_argument('--my-flag', help='...')
```

For a plugin: create `cli_conf/plugins/<name>.py` with an `arg_to_attr` and a
`create_parser` that calls `add_generic_plugin_parsers(subcommands, <PluginClass>)` —
that yields `show`/`enable`/`disable`/`status` for free (a matching `Plugin` subclass
in `lib389/plugins.py` is required).

## When the noun needs a lib389 wrapper class

A noun that manages an LDAP subtree needs a `DSLdapObject`/`DSLdapObjects` pair in the
matching `src/lib389/lib389/` module (exemplar: `EncryptionModule`/`EncryptionModules`
in `config.py`). Full class-attribute recipe: docs/agents/lib389.md. Two traps:

- **`_create_objectclasses` (singular class: objectClass values written on create) vs
  `_objectclasses` (plural class: search-filter terms) — different names for different
  jobs, easy to swap.** An empty `_create_objectclasses` fails create with a bare
  `AssertionError`, not an LDAP error.
- **`_protected` defaults to True; without `_protected = False` the object's
  `delete()` and `rename()` are SILENT no-ops** — no exception, no log line.

## Do not

- Do not create or edit a man page. The lib389 `build_py` hook generates the ignored
  `src/lib389/man/` outputs from argparse help; they are build products, not tracked
  sources.
- Do not pick a `cli_idm/` module name outside dsidm's `BASEDN_RDNS` keys when the
  noun manages a default container (references/handler-patterns.md, dsidm couplings).

Add tests for the new noun under `dirsrvtests/tests/suites/clu/` (SKILL.md step 8).
