# New plugin anatomy

The most recent plugin actually added (alias_entries) touched 9 files; that inventory is this
checklist. A plugin not in a config entry is never loaded - the entry makes it exist.

## 1. Plugin source

`ldap/servers/plugins/<name>/<name>.c` (+ `.h` if needed), standard copyright header. Minimal
shape: `slapi-plugin.h`, one `static Slapi_PluginDesc`, one exported init whose name must
equal the entry's `nsslapd-plugininitfunc`:

```c
int myplugin_init(Slapi_PBlock *pb)
{
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_03) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_<HOOK>_FN, (void *)my_callback) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PLUGIN_NAME, "myplugin_init - Failed to register.\n");
        return -1;
    }
    return 0;
}
```

Hook-slot choice, the 24 valid `nsslapd-plugintype` strings (an unknown string rejects the
entry outright), betxn registration, and lifecycle: docs/agents/plugins.md.

## 2. Makefile.am (2 edits; 3 with a private header)

Append `lib<name>-plugin.la` to the existing `serverplugin_LTLIBRARIES` assignment; add the
stanza; add any `.h` to `dist_noinst_HEADERS`:

```make
lib<name>_plugin_la_SOURCES = ldap/servers/plugins/<name>/<name>.c
lib<name>_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
lib<name>_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
lib<name>_plugin_la_DEPENDENCIES = libslapd.la
lib<name>_plugin_la_LDFLAGS = -avoid-version
```

Omitting `$(DSPLUGIN_CPPFLAGS)` fails the compile on `VENDOR`; `-avoid-version` lets
`nsslapd-pluginpath` be the bare `lib<name>-plugin`, no `.so` suffix. Rust plugins use a
different five-touch wiring (stub C source + cargo static lib): docs/agents/plugins.md.

## 3. DSE template entry

Add the stanza to `ldap/ldif/template-dse.ldif.in` AND `template-dse-minimal.ldif.in` (the
minimal file is read by nothing in-repo, but contributors edit both in lockstep - keep the
convention). Edit the `.in` files, never a generated `.ldif`:

```ldif
dn: cn=<name>,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: <name>
nsslapd-pluginpath: lib<name>-plugin
nsslapd-plugininitfunc: <name>_init
nsslapd-plugintype: <valid type string>
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
```

`plugin_setup` rejects entries missing `nsslapd-pluginType`, `cn`, `nsslapd-pluginInitFunc`, or `nsslapd-pluginPath`.

## 4. lib389 subclass

Add a `Plugin` subclass in `src/lib389/lib389/plugins.py`: `_plugin_properties` carrying cn,
enabled, path, initfunc, type, id, vendor, version, description; a default `dn=` in
`__init__`; extend `_create_objectclasses` / `_must_attributes` when the plugin has config
attributes. Copy a small neighbor (AliasEntriesPlugin is minimal).

## 5. Enable/restart reality and the test

`nsslapd-dynamic-plugins` defaults to off: adding or enabling a plugin entry at runtime does
nothing until restart (the server logs it), so tests enable via the lib389 subclass then
`inst.restart()`. Pytest goes under `dirsrvtests/tests/suites/plugins/` - write-test skill.

Verify: `git grep -n 'lib<name>-plugin' Makefile.am ldap/ldif/` - expect the LTLIBRARIES
list, the stanza, and both templates.
