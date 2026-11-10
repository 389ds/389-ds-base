---
name: add-config-attribute
description: Add, rename, or wire a server config attribute end to end - a core cn=config attribute (the slap.h/libglobs.c chain) or a plugin config-entry attribute - including the schema line, defaults, upgrade migration for renames, tests, and the CLI/UI exposure decision. Use for a new nsslapd- setting, config knob, tunable, or plugin setting. The core chain is seven coordinated edits; a missed edit (especially the ConfigList row) makes the attribute silently invisible - reads return nothing and writes are ignored or rejected, with no compile error.
---

# Add or Change a Server Config Attribute

Config attributes live in two different systems with unrelated edit chains: core `cn=config`
attributes are rows in a static C table in libglobs.c; plugin attributes are plain LDAP
attributes on the plugin's own config entry. Pick the branch first.

## 1. Pick the branch

- Core `cn=config` attribute (`nsslapd-*` on the top entry) -> steps 2-5; full per-edit
  table in [references/core-config-chain.md](references/core-config-chain.md).
- Plugin config-entry attribute (`cn=<Plugin>,cn=plugins,cn=config`) -> steps 6-8; file
  inventory in [references/plugin-config.md](references/plugin-config.md).
- Entirely new plugin -> [references/new-plugin.md](references/new-plugin.md) first, then
  steps 6-8 for its attributes.
- `nsslapd-backend-*` / ldbm database attributes are a third system -> read the
  touch-backend skill first.

Config-system semantics (two-pass apply, confighash, DSE callbacks): docs/agents/c-server.md.

## 2. Core chain: seven edits, all under ldap/servers/slapd/

Copy a recent real attribute end to end - `nsslapd-maxcontrolsperop` (int) or
`nsslapd-thread-pool-stats` (on/off) are clean exemplars. The edits:

1. slap.h: `#define SLAPD_DEFAULT_X <val>` AND its `#define SLAPD_DEFAULT_X_STR "<val>"` twin.
2. slap.h: `#define CONFIG_X_ATTRIBUTE "nsslapd-x"` name constant.
3. slap.h: struct member on `_slapdFrontendConfig`.
4. libglobs.c: one `ConfigList[]` row (name constant, setter, member address, type, getter,
   `_STR` default).
5. libglobs.c: default assignment inside `FrontendConfig_init()`.
6. libglobs.c: setter + getter definitions; declarations in proto-slap.h (the setter and
   getter declaration blocks are far apart - add to both).
7. configdse.c, only if the attribute is startup-only: append `"cn=config:nsslapd-x"` to
   `requires_restart[]` (this feeds `nsslapd-requiresrestart`; it does not block the modify).

**STOP: no `ConfigList[]` row = the attribute is invisible to every read and write.** The
rows are hashed once at startup; without one, a modify gets "Unknown attribute" and a read
returns nothing - and it all compiles.

- **The setter runs TWICE per modify** (validate pass with `apply=0`, then `apply=1`).
  Validate fully, mutate nothing until apply: `if (!apply) return retVal;` before the lock.
- **The `_STR` default twin is required.** A valueless delete resets the attribute via the
  row's init value; a row with none makes the delete fail (err=53) or misbehave.
- **Never trust the "ADDING A NEW VALUE" comment block at the top of libglobs.c** - it omits
  the schema file and names a template that does not exist. Follow a recent real attribute.
- Do not put logic in the getter and expect cn=config reads to run it: rows carrying a
  struct-member address are read straight from memory.

## 3. Verify the core chain

Verify: `git grep -n 'nsslapd-x\|SLAPD_DEFAULT_X\|config_set_x' ldap/servers/slapd/` -
every file in the chain should appear: slap.h, libglobs.c (row, init, setter, getter),
proto-slap.h (twice), and configdse.c when startup-only.

## 4. Schema line

Append ONE `attributeTypes:` line at the END of the attributeTypes block in
ldap/schema/01core389.ldif (immediately before the `# objectclasses` comment), with the next
free OID under `2.16.840.1.113730.3.1.`. The block is append-ordered, not OID-ordered.
Verify: `grep -o '113730\.3\.1\.[0-9]*' ldap/schema/01core389.ldif | sort -t. -k4 -n | tail`
(ignore the legacy `9999999` debug OID) - take highest+1. This is conventional, not enforced:
`cn=config` is extensibleObject and accepts undefined attributes - add the line anyway.

## 5. CLI / UI exposure ruling

`dsconf <inst> config replace nsslapd-x=<value>` works with NO lib389 or CLI change -
lib389's Config object is a generic DSLdapObject over cn=config. Add a dedicated flag only
if UX demands it -> add-cli-option skill and docs/agents/cli.md. A Cockpit control is
customary for user-facing settings but not required -> ui-expose-attribute skill and
docs/agents/ui.md. Shipped commits go both ways; decide per attribute.

## 6. Plugin branch: define and parse

`#define` the attribute name in the plugin's own header - or its `.c` when it has no header -
never in slap.h. Parse it in the plugin's config-read path with `slapi_entry_attr_get_*`,
mirroring how that plugin reads its existing attributes (typically at init/start from the
config entry, and in its registered config-modify DSE callback when it supports live
reconfig - see docs/agents/plugins.md). Plugin attributes usually need NO schema edit -
follow that plugin's precedent (some own a `ldap/schema/60<plugin>.ldif`).
Verify: `git grep -n '<attr-name>\|<NAME_MACRO>' ldap/servers/plugins/<plugin>/` - expect
the define plus every read site (reads reference the macro, not the string).

## 7. Renaming or replacing an existing plugin attribute

**STOP: a rename without an upgrade migration silently breaks every existing install.**
Add a `static upgrade_status upgrade_<x>(void)` function in ldap/servers/slapd/upgrade.c,
call it from `upgrade_server()`, and add an upgrade test under
dirsrvtests/tests/suites/upgrade/. Exact pattern and exemplar:
[references/plugin-config.md](references/plugin-config.md).
**Never copy an attribute name out of a commit message** - take it from the header define.
Verify: `git grep -n 'upgrade_<x>' ldap/servers/slapd/upgrade.c` - definition AND call site.

## 8. Tests

Cover get/set, reject-invalid-value, and persistence across restart, under the matching
suite: dirsrvtests/tests/suites/config/ for core attributes, the plugin's own suite for
plugin attributes (docstring and fixture rules: write-test skill, docs/agents/testing.md).
Then run the verify-changes skill before committing.

## Maintenance

Update when: the `config_get_and_set` row shape or `requires_restart[]` moves in ldap/servers/slapd/, the schema OID arc changes, or upgrade functions leave upgrade.c.
Verify freshness: `git grep -n 'ConfigList\[\]' ldap/servers/slapd/libglobs.c` and `git grep -n 'upgrade_server' ldap/servers/slapd/upgrade.c` still hit.
