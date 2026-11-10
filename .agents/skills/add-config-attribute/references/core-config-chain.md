# Core cn=config chain: the full edit table

Exemplars verified in source: `nsslapd-maxcontrolsperop` (int) and `nsslapd-thread-pool-stats`
(on/off, startup-only). Copy one end to end. Do not follow the "ADDING A NEW VALUE" comment
at the top of libglobs.c - it omits the schema file and names a template that does not exist.

| # | File | Edit | What it looks like |
|---|------|------|--------------------|
| 1 | ldap/servers/slapd/slap.h | default macro pair | `#define SLAPD_DEFAULT_MAXCONTROLS_PER_OP 10` and `#define SLAPD_DEFAULT_MAXCONTROLS_PER_OP_STR "10"` |
| 2 | ldap/servers/slapd/slap.h | attribute-name constant | `#define CONFIG_MAXCONTROLS_PER_OP_ATTRIBUTE "nsslapd-maxcontrolsperop"` |
| 3 | ldap/servers/slapd/slap.h | member on `_slapdFrontendConfig` | `slapi_int_t maxcontrols_per_op;` (`slapi_onoff_t` for booleans, `char *` for strings) |
| 4 | ldap/servers/slapd/libglobs.c | `ConfigList[]` row | see below |
| 5 | ldap/servers/slapd/libglobs.c | default in `FrontendConfig_init()` | `cfg->maxcontrols_per_op = SLAPD_DEFAULT_MAXCONTROLS_PER_OP;` |
| 6 | libglobs.c + proto-slap.h | setter/getter + declarations | `int config_set_x(const char *attrname, char *value, char *errorbuf, int apply)` and `config_get_x()`; the two proto-slap.h declaration blocks are NOT adjacent - edit both |
| 7 | ldap/servers/slapd/configdse.c | startup-only attributes only | append `"cn=config:" CONFIG_X_ATTRIBUTE` to `requires_restart[]` |
| 8 | ldap/schema/01core389.ldif | one `attributeTypes:` line | appended at the end of the attributeTypes block, next free OID under `2.16.840.1.113730.3.1.` |

## The ConfigList[] row (on/off exemplar)

```c
{CONFIG_THREAD_POOL_STATS_ATTRIBUTE, config_set_thread_pool_stats,
 NULL, 0,                                  /* logsetfunc, whichlog: log attrs only */
 (void **)&global_slapdFrontendConfig.thread_pool_stats,
 CONFIG_ON_OFF, (ConfigGetFunc)config_get_thread_pool_stats,
 &init_thread_pool_stats, NULL},           /* initvalue, geninitfunc */
```

Field order: attr name, setfunc, logsetfunc, whichlog, address of struct member, type,
getfunc, initvalue, geninitfunc. Int attributes pass `SLAPD_DEFAULT_X_STR` as initvalue;
on/off attributes pass the address of a static `slapi_onoff_t init_x` (declare it beside the
other `init_*` globals in libglobs.c and assign it in `FrontendConfig_init()`).

## Contracts the row commits you to

- **Missing row = invisible attribute.** Rows are hashed into `confighash` once, inside
  `FrontendConfig_init()`, before dse.ldif is read. An unknown name gets
  LDAP_NO_SUCH_ATTRIBUTE from `config_set`, surfaced to the client as err=53. A row with
  neither setfunc nor logsetfunc is read-only: the modify is silently ignored apart from an
  error-log line.
- **Two-pass apply.** `modify_config_dse` runs the whole mod list twice: pass 0 with
  `apply=0` (validation only), pass 1 with `apply=1`. The setter must reject bad values on
  pass 0 and touch the struct only on pass 1, under the config write lock:
  validate -> `if (!apply) return retVal;` -> `CFG_LOCK_WRITE` / assign / `CFG_UNLOCK_WRITE`.
  A few shipped setters apply on pass 0 anyway - do not copy those.
- **The `_STR` twin / initvalue is the delete path.** A valueless mod_delete calls the setter
  with the row's initvalue (or geninitfunc result) to reset the compiled-in default. If both
  are NULL the delete fails with LDAP_UNWILLING_TO_PERFORM. That reset-as-string is the whole
  reason the `_STR` macro exists.
- **Reads bypass your getter.** For rows with a non-NULL member address, cn=config reads copy
  the value straight from memory under the read lock; the getter runs for a cn=config read
  only when the address is NULL. Put no required logic in either path.
- **Getter ownership varies.** Most `config_get_*` returning `char *` return strdup'd memory
  the caller frees with `slapi_ch_free_string`, but at least two
  (`config_get_allowed_sasl_mechs`, `config_get_default_naming_context`) hand back the live
  internal pointer - freeing it corrupts config. Read the body of whichever getter you copy.
- `requires_restart[]` feeds the synthesized read-time `nsslapd-requiresrestart`
  attribute and the "will not take effect until the server is restarted" notice that
  `postop_modify_config_dse` writes into the modify result text. A listed attribute is
  still modifiable and the modify still returns success; enforcement of "restart to take
  effect" is on you (typically: read the struct member only at startup).
