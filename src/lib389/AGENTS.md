# Agent notes: src/lib389/

Read [docs/agents/lib389.md](../../docs/agents/lib389.md) and [docs/agents/cli.md](../../docs/agents/cli.md) first; workflow: the `add-cli-option` skill (`../../.agents/skills/add-cli-option/SKILL.md`).

- CLI handler signatures differ per tool: dsconf/dsidm use `(inst, basedn, log, args)`; dsctl/dscreate use `(inst, log, args)`.
- Hand-built create/add handlers silently drop new flags unless you add an explicit `if args.x is not None:` assignment.
- Never edit `man/*.8` (generated from argparse help) or `lib389/tests/topologies.py` (build-time copy).
- `DSLdapObject.delete()`/`.rename()` are silent no-ops while `_protected` is true (the default a subclass inherits).
- `get_attr_val()` returns bytes; its `_utf8`, `_utf8_l`, and `_int` variants return the named types. Prefer `get_attr_val_utf8_l()` for case-insensitive comparisons.
- Python 3.8 floor (CI-enforced by vermin).
