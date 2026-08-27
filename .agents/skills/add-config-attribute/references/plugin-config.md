# Plugin config-entry attributes

Plugin config is plain LDAP attributes on the plugin's entry
(`cn=<Plugin>,cn=plugins,cn=config`) - no central table, no libglobs.c. File inventory,
from the commit that added `syncrepl-max-concurrent` / `syncrepl-queue-max-size` to the
Content Synchronization plugin:

| File | Edit |
|------|------|
| `ldap/servers/plugins/<plugin>/<plugin>.h` | `#define <PLUGIN>_CFG_X "attr-name"`; plugins with no header (referint, dna) define it at the top of the `.c` |
| plugin `.c` config-read path | parse with `slapi_entry_attr_get_ref` / `_get_charray_ext` / `_get_bool` / `_get_long`, mirroring the neighboring attributes; validate before applying |
| `ldap/ldif/template-dse.ldif.in` + `template-dse-minimal.ldif.in` (lockstep) | only if a non-default value should ship on new installs; most plugin attrs ship unset |
| `src/lib389/lib389/plugins.py` | only if exposed: default in the existing subclass's `_plugin_properties`, or accessor methods on it |
| `src/lib389/lib389/cli_conf/plugins/<plugin>.py` | only if a dsconf flag is wanted: `arg_to_attr` entry + `add_argument` + `type=` validator (add-cli-option skill) |
| schema | usually nothing; when a plugin owns schema it lives in `ldap/schema/60<plugin>.ldif` - follow that plugin's precedent |

Where the read runs: at init/start from the entry in the pblock (`SLAPI_PLUGIN_CONFIG_ENTRY`
- meaningful during init only; it aliases the pre-op entry slot inside operation callbacks),
and, for plugins that support live reconfig, in DSE MODIFY callbacks registered with
`slapi_config_register_callback_plugin` - validate in the PREOP callback, apply in the
POSTOP one (memberof_config.c is the canonical pair). Locking and deep-copy rules for that
path are in docs/agents/plugins.md, as are the betxn implications: betxn plugins pick their
hook slots at init by reading `nsslapd-plugintype`, and BE_TXN_PRE callbacks can rerun many
times for one operation under deadlock retry, so config-driven behavior must be idempotent.

## Renaming or replacing an attribute: the upgrade migration

Upgrade functions live in `ldap/servers/slapd/upgrade.c` as
`static upgrade_status upgrade_<what>(void)` (freeform `<what>`, usually ending in
`_config` for attribute migrations), each called in sequence from `upgrade_server()`,
which runs at server startup from main. Return `UPGRADE_SUCCESS` / `UPGRADE_FAILURE`.

Body pattern - copy `upgrade_contentsync_max_concurrent_config`:

```c
slapi_search_get_entry(&search_pb, sdn, NULL, &plugin_entry, NULL);
/* when the old attribute is present: */
slapi_mods_add(&smods, LDAP_MOD_DELETE, old_attr, 0, NULL);
if (slapi_entry_attr_get_ref(plugin_entry, new_attr) == NULL) {
    slapi_mods_add_string(&smods, LDAP_MOD_ADD, new_attr, old_val);
}
slapi_modify_internal_pb(mod_pb);  /* then check SLAPI_PLUGIN_INTOP_RESULT */
```

The migration test goes under `dirsrvtests/tests/suites/upgrade/` (model:
`upgrade_plugin_attribute.py`): stop the server, plant the legacy attribute with `DSEldif`,
restart, assert the old attribute is gone and the new one carries the old value.

Two traps: attribute names in commit messages have been wrong before - trust only the
header define; and `nsslapd-dynamic-plugins` defaults to off, so config-entry changes take
effect at restart unless the plugin registered live-reconfig callbacks.
