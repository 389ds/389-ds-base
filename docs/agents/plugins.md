# Plugin Anatomy (C and Rust)

How a server plugin is found, configured, registered, made transaction-aware, and wired into the build. SLAPI memory rules, pblock write ownership, and return-code tables live in [c-server.md](c-server.md). Unqualified C paths below are relative to `ldap/servers/`.

## A plugin is a config entry

A plugin with no entry under `cn=plugins,cn=config` is never loaded. The entry must carry `objectclass: nsSlapdPlugin`; `slapd/plugin.c (plugin_setup)` rejects it when `nsslapd-pluginType`, `cn`, `nsslapd-pluginInitFunc`, or `nsslapd-pluginPath` is missing (the last two only when the init function was not supplied programmatically). An unrecognised `nsslapd-plugintype` also rejects the entry outright — ignore the stale "pass to backend" comment in `slapd/plugin.c (plugin_get_type_and_list)`. Canonical shape:

```ldif
dn: cn=whoami,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: whoami
nsslapd-pluginpath: libwhoami-plugin
nsslapd-plugininitfunc: whoami_init
nsslapd-plugintype: extendedop
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
```

- Default C-plugin entries live in `ldap/ldif/template-dse.ldif.in` — edit the `.in`, never the generated `template-dse.ldif`. Convention is to edit `ldap/ldif/template-dse-minimal.ldif.in` in lockstep, even though nothing in this repo reads the minimal file: lib389 instance setup opens only `template-dse.ldif` (`src/lib389/lib389/instance/setup.py`).
- Two substitution styles coexist in the template: `@token@` is replaced at build time by the `%: %.in` sed rule (`Makefile.am (fixupcmd)`); `%token%` (e.g. `%ds_suffix%`) survives into the installed `.ldif` and is substituted by lib389 at instance creation.
- `nsslapd-dynamic-plugins` defaults to off (`slapd/libglobs.c (FrontendConfig_init)`), and `slapd/dse.c (dse_add_plugin, dse_delete_plugin)` bail unless it is on — plugin-entry changes take effect only after a restart; the modify path logs that a restart is required.
- Plugins are load-critical by default: a plugin that fails to load aborts startup unless it is on the hardcoded non-critical allowlist, matched by name or library path — `slapd/plugin.c (plugin_load_critical)`.
- `nsslapd-pluginPrecedence` is an integer 1..99 (default `PLUGIN_DEFAULT_PRECEDENCE`); ordering otherwise comes from `nsslapd-plugin-depends-on-type` / `-depends-on-named`.

## Init function contract

The entry point is `int <name>_init(Slapi_PBlock *pb)`, where `<name>_init` must equal `nsslapd-plugininitfunc`; return 0 on success. Adapted from `plugins/whoami/whoami.c (whoami_init)`:

```c
static Slapi_PluginDesc pdesc = {"whoami", VENDOR, DS_PACKAGE_VERSION, "whoami extop"};
int
whoami_init(Slapi_PBlock *pb)
{
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_03) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)whoami_exop) != 0)
        return -1;
    return 0;
}
```

- Callbacks register via `slapi_pblock_set(pb, SLAPI_PLUGIN_*_FN, (void *)fn)`, 0 on success. The slot id is often held in a local variable — that is what the betxn swap below exploits.
- Setting `SLAPI_PLUGIN_VERSION` is convention, not enforced: `slapd/plugin.c (plugin_setup)` pre-populates the slot with `SLAPI_PLUGIN_CURRENT_VERSION` and nothing validates it afterwards. The values are the strings `"01"`/`"02"`/`"03"` (`slapd/slapi-plugin.h`).
- `VENDOR` has no `#define` anywhere — it arrives via `-DVENDOR=...` in `DS_DEFINES`, which reaches plugins only through `$(DSPLUGIN_CPPFLAGS)`; a build stanza that omits it fails to compile.
- A plugin doing internal operations must capture `slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &id)` in `_init` and pass it to internal-op calls and as the last argument of `slapi_register_plugin` (`plugins/memberof/memberof.c (memberof_postop_init)`).
- `SLAPI_PLUGIN_CONFIG_ENTRY` is a `#define` alias of `SLAPI_ENTRY_PRE_OP` (`slapd/slapi-plugin.h`). It is meaningful only during init: inside an operation callback the same slot holds the operation's pre-op entry.
- `slapi_register_plugin(plugintype, enabled, initsymbol, initfunc, name, argv, identity)` lets one config entry install several plugin types (sub-plugins); it returns 0 on success (`slapd/plugin.c`). `nsslapd-plugintype: object` means "this init only registers sub-plugins" (`plugins/usn/usn.c (usn_init)`).

## Internal operations are privileged

Internal operations do not run with the client's LDAP authorization. They are created with `OP_FLAG_INTERNAL`, their common setup sets `SLAPI_REQUESTOR_ISROOT` to 1, and the ACL dispatcher returns `LDAP_SUCCESS` without invoking the normal ACI plugins for that flag (`slapd/plugin_internal_op.c (internal_operation_new, set_common_params)`, `slapd/plugin_acl.c (plugin_call_acl_plugin, plugin_call_acl_mods_access)`). The component identity passed to an internal-op API is not a client identity: `allow_operation` uses it to find the calling plugin and apply that plugin's `nsslapd-targetSubtree` / `nsslapd-exclude-targetSubtree` configuration. That is target scoping, not client authorization, and omitting the target-subtree setting defaults it to global (`slapd/plugin_internal_op.c (allow_operation)`, `slapd/plugin.c (plugin_allow_internal_op, set_plugin_config_from_entry)`).

- Before a client-triggered callback starts an internal operation, classify and enforce its authorization model while still using the original external pblock. If the feature proxies a client-selected target or action, or returns internally read data, reproduce all access decisions the equivalent external operation would make: for example the add parent/candidate entry, modify mods, both sides of a modrdn, or search access plus per-entry/per-attribute disclosure. If it performs fixed server-maintained side effects derived from an already-authorized operation, such as memberOf or referential-integrity maintenance, require the initiating operation to pass its normal authorization and constrain the derived targets and modifications to that invariant; do not pretend the client has direct write access to those maintained attributes. Separately authorize use of any privileged feature exposed to clients. No single ACL call is a generic substitute. Use `slapi_access_allowed()` and `slapi_acl_check_mods()` where their contracts fit and fail closed; checking the new internal pblock only checks as root (`slapd/plugin_acl.c (slapi_access_allowed, slapi_acl_check_mods)`, `plugins/acct_usability/acct_usability.c (auc_pre_search)`, `plugins/memberof/memberof.c (memberof_postop_modify)`, `plugins/referint/referint.c (referint_postop_modrdn)`).
- Independently constrain the privileged operation: validate its normalized target under an explicit plugin-configured subtree; use fixed filter structure and escape every untrusted assertion value with `slapi_escape_filter_value()`; fail closed on a NULL result and free the allocated escaped string with `slapi_ch_free_string()`. Allowlist returned attributes and permitted modifications. Do not pass unchecked client base DNs, filter text, attribute names, or mods into an internal operation, and do not return internal-search data that the original client was not authorized to read (`slapd/util.c (slapi_escape_filter_value)`).

## betxn is a runtime decision

There is no compile-time betxn flag. At init time the plugin reads its own config entry and picks its hook slots — `plugins/memberof/memberof.c (memberof_postop_init)`, the idiom shared by betxn-capable C plugins:

```c
if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
    plugin_entry &&
    (plugin_type = slapi_entry_attr_get_ref(plugin_entry, "nsslapd-plugintype")) &&
    plugin_type && strstr(plugin_type, "betxn")) {
    usetxn = 1;
    delfn = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
    addfn = SLAPI_PLUGIN_BE_TXN_POST_ADD_FN;
}
```

- A second, different switch exists for `object`-type plugins: the boolean `nsslapd-pluginbetxn`, read with `slapi_entry_attr_get_bool`, decides which type string their sub-plugins register — e.g. `plugins/usn/usn.c (usn_init)` swaps `"bepostoperation"` for `"betxnpostoperation"`. What the flag switches differs per plugin; read the consumer before setting it.
- `BE_TXN_PRE_*` callbacks run inside the backend deadlock-retry loop, bounded by `RETRY_TIMES` (`slapd/back-ldbm/back-ldbm.h`), and can fire many times for one client operation (`slapd/back-ldbm/ldbm_add.c (ldbm_back_add)`). They must be idempotent and must not accumulate state.
- A betxn pre-modify callback may only append mods, never remove them: `slapd/back-ldbm/ldbm_modify.c (ldbm_back_modify)` snapshots the mod count before the callbacks and applies only the appended tail; a shrunken list is an error and applies nothing. Do removals in a `bepreoperation` callback. Pblock write-ownership rules: [c-server.md](c-server.md).
- Any non-zero return from a `BE_TXN_POST_*` callback sends the backend to its error path before the transaction commit — the client's write is rolled back. That atomicity is the point of betxn versus plain be hooks (`slapd/back-ldbm/ldbm_add.c (ldbm_back_add)`).
- At the `BE_PRE_*` call sites only a negative return means failure; a positive return means "go around the retry loop again" and can spin forever. Return `SLAPI_PLUGIN_FAILURE` (-1), never a positive LDAP code.
- `BE_PRE_*` already runs inside the backend transaction — the transaction begins before it, despite in-tree comments claiming otherwise (`slapd/back-ldbm/ldbm_modify.c (ldbm_back_modify)`).

## Hook types

Valid `nsslapd-plugintype` strings, matched case-insensitively in `slapd/plugin.c (plugin_get_type_and_list)` — anything else rejects the entry: `database`, `extendedop`, `preoperation`, `postoperation`, `matchingrule`, `syntax`, `accesscontrol`, `mmr`, `bepreoperation`, `bepostoperation`, `betxnpreoperation`, `betxnpostoperation`, `internalpreoperation`, `internalpostoperation`, `entry`, `object`, `pwdstoragescheme`, `reverpwdstoragescheme`, `vattrsp`, `ldbmentryfetchstore`, `index`, `betxnextendedop`, `preextendedop`, `postextendedop`. Each maps to `SLAPI_PLUGIN_<UPPERCASED STRING>` except:

| String | Constant |
|---|---|
| `database` | `SLAPI_PLUGIN_DATABASE` — private header, `slapd/slapi-private.h` |
| `accesscontrol` | `SLAPI_PLUGIN_ACL` |
| `object` | `SLAPI_PLUGIN_TYPE_OBJECT` (note the extra `TYPE_`) |
| `pwdstoragescheme` / `reverpwdstoragescheme` | `SLAPI_PLUGIN_PWD_STORAGE_SCHEME` / `SLAPI_PLUGIN_REVER_PWD_STORAGE_SCHEME` |
| `vattrsp` / `ldbmentryfetchstore` | `SLAPI_PLUGIN_VATTR_SP` / `SLAPI_PLUGIN_LDBM_ENTRY_FETCH_STORE` |
| `preextendedop` / `postextendedop` | `SLAPI_PLUGIN_PREEXTOPERATION` / `SLAPI_PLUGIN_POSTEXTOPERATION` |

Hook pairs a betxn-capable plugin swaps inside the `strstr` block (constants in `slapd/slapi-plugin.h`):

| Non-txn | betxn |
|---|---|
| `SLAPI_PLUGIN_PRE_{ADD,MODIFY,MODRDN}_FN` | `SLAPI_PLUGIN_BE_TXN_PRE_{ADD,MODIFY,MODRDN}_FN` |
| `SLAPI_PLUGIN_POST_{ADD,MODIFY,MODRDN,DELETE}_FN` | `SLAPI_PLUGIN_BE_TXN_POST_{ADD,MODIFY,MODRDN,DELETE}_FN` |
| type string `preoperation` | `betxnpreoperation` |
| type strings `postoperation`, `bepostoperation` | `betxnpostoperation` |

## Reaching your own config at call time

A live modify of the plugin's config entry runs its DSE callback on one worker thread while other workers are inside the operation callbacks, so a config lock is mandatory. The canonical pattern — `plugins/memberof/memberof.c (memberof_postop_del)` — is: rlock, cheap scope/scalar rejection against the shared struct, deep-copy, unlock, use only the copy, free the copy on every exit path. Declare the copy `= {0}` and free it unconditionally at the `bail:` label — freeing an all-zero struct is a no-op, which is what makes early bail-outs safe. The copy is deep and the free must match it element for element: `plugins/memberof/memberof_config.c (memberof_copy_config, memberof_free_config)` are the reference pair.

- For pre/post operation hooks dispatched through `plugin_call_plugins()` whose contract treats `SLAPI_PLUGIN_SUCCESS` as a no-op, start with `if (!slapi_plugin_running(pb)) return SLAPI_PLUGIN_SUCCESS;`. Do not copy that guard into init/start/close callbacks or hooks with a different return contract. `slapi_plugin_running()` only reads the started flag; it does not take a lock, pin the plugin, or protect plugin-owned configuration. The generic dispatcher separately brackets its callbacks with the per-plugin operation counter (`slapd/plugin.c (plugin_call_plugins, slapi_plugin_running, plugin_call_func)`).
- Extended-op handlers have a different return contract: when stopped, return `SLAPI_PLUGIN_EXTENDED_NOT_HANDLED`, not 0 — 0 is `LDAP_SUCCESS`. They are called directly by `plugin_call_exop_plugins`, outside the generic `plugin_call_func` counter bracket, so the running check itself provides no lifecycle synchronization (`slapd/plugin.c (plugin_call_exop_plugins)`, `plugins/dna/dna.c (dna_extend_exop)`).
- Threads you create yourself are invisible to that counter. Task threads are bracketed for free by the task API; a raw `PR_CreateThread` is not — `slapi_plugin_op_started`/`slapi_plugin_op_finished` (`slapd/slapi-plugin.h`) exist for exactly that.

## Build wiring (C)

Append `lib<x>-plugin.la` to `serverplugin_LTLIBRARIES`, add the stanza, and (if the plugin has a private header) add it to `dist_noinst_HEADERS` — all in the top-level `Makefile.am`:

```make
libwhoami_plugin_la_SOURCES = ldap/servers/plugins/whoami/whoami.c
libwhoami_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libwhoami_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libwhoami_plugin_la_DEPENDENCIES = libslapd.la
libwhoami_plugin_la_LDFLAGS = -avoid-version
```

`-avoid-version` is why `nsslapd-pluginpath` can be the bare `libwhoami-plugin` with no `.so` suffix. Optional plugins are gated by an automake conditional that assigns a variable, and the variable — not the `.la` — appears in `serverplugin_LTLIBRARIES`. Most plugins follow `<dir>` → `lib<dir>_plugin_la` → `lib<dir>-plugin`; the irregulars:

| Source dir | `_la` target | `nsslapd-pluginpath` |
|---|---|---|
| `plugins/uiduniq/` | `libattr_unique_plugin_la` | `libattr-unique-plugin` |
| `plugins/rever/` | `libpbe_plugin_la` | `libpbe-plugin` |
| `plugins/mep/` | `libmanagedentries_plugin_la` | `libmanagedentries-plugin` |
| `plugins/sync/` | `libcontentsync_plugin_la` | `libcontentsync-plugin` |
| `plugins/syntaxes/` | `libsyntax_plugin_la` | `libsyntax-plugin` |
| `plugins/acct_usability/` | `libacctusability_plugin_la` | `libacctusability-plugin` |
| `plugins/alias_entries/` | `libalias_entries_plugin_la` | `libalias-entries-plugin` |
| `plugins/rootdn_access/` | `librootdn_access_plugin_la` | `librootdn-access-plugin` |
| `plugins/schema_reload/` | `libschemareload_plugin_la` | `libschemareload-plugin` |
| `src/plugins/entryuuid/` (Rust) | `libentryuuid_plugin_la` | `libentryuuid-plugin` |
| `src/plugins/pwdchan/` (Rust) | `libpwdchan_plugin_la` | `libpwdchan-plugin` |

Two directories look like plugins and are never built — `slapd/test-plugins/` and `plugins/vattrsp_template/` appear nowhere in `Makefile.am`/`configure.ac`. Do not copy their conventions.

## Rust plugins

A Rust plugin is a cargo crate under `src/plugins/<name>/` with `crate-type = ["staticlib", "lib"]`, a path dependency on `slapi_r_plugin`, and an entry in the workspace member list (`src/Cargo.toml`). The C-ABI entry points come from one macro call — `slapi_r_plugin_hooks!(entryuuid, EntryUuid);` — whose first argument IS the symbol prefix: it emits `entryuuid_plugin_init`, which is what `nsslapd-plugininitfunc` must hold (`src/slapi_r_plugin/src/macros.rs (slapi_r_plugin_hooks)`). One crate can call the macro several times to host several plugins (`src/plugins/pwdchan/src/lib.rs`).

- Only these `has_*` toggles on the `SlapiPlugin3` trait are honoured by the macro: `has_betxn_pre_modify`, `has_betxn_pre_add`, `has_pwd_storage`, `has_task_handler`. `has_pre_add`, `has_post_add`, `has_pre_modify`, `has_post_modify` exist on the trait (`src/slapi_r_plugin/src/plugin.rs (SlapiPlugin3)`) but are never read — implementing them has no effect. Syntax plugins use `slapi_r_syntax_plugin_hooks!`.
- The shipped `.so` is a libtool library whose only C source is the shared stub `src/slapi_r_plugin/src/init.c` (one do-nothing function); the real code arrives through `_LIBADD = ... -l<name>`, resolving to the cargo-built static lib that the cargo rule copies into `.libs/` (`Makefile.am (libentryuuid_plugin_la_SOURCES)`).
- Wiring a new Rust plugin touches `Makefile.am` in several places: `serverplugin_LTLIBRARIES`, `noinst_LTLIBRARIES`, the `<X>_LIB` + cargo-rule block, the `lib<x>_plugin_la_*` stanza, and `EXTRA_DIST` — plus the `check-local` crate loop if `make check` should run its cargo tests. Cargo env vars and workspace rules: [rust.md](rust.md).
- Rust plugin config entries are split: the pwdchan PBKDF2 scheme entries ARE in `ldap/ldif/template-dse.ldif.in`, but `cn=entryuuid` and `cn=entryuuid_syntax` are not — they are C string literals in `slapd/fedse.c (internal_entries)` added at startup, re-created on upgrade by `slapd/upgrade.c (upgrade_143_entryuuid_exists)`; password/syntax bootstrap copies also live in `slapd/config.c (bootstrap_plugins)`.

## Beyond the C code

A complete new plugin also needs: a `Plugin` subclass in `src/lib389/lib389/plugins.py` ([lib389.md](lib389.md)); the `template-dse.ldif.in` entry (above); and a pytest under `dirsrvtests/tests/suites/plugins/` ([testing.md](testing.md)). For new plugin configuration attributes — Workflow: see the add-config-attribute skill (.agents/skills/add-config-attribute/SKILL.md).
