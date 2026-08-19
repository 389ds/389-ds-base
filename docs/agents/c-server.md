# C Server Development (ldap/servers/slapd/)

SLAPI memory ownership, pblock discipline, return codes, threads, performance, logging, and the cn=config/DSE system.
File paths below are relative to `ldap/servers/slapd/` unless prefixed. Plugin anatomy and registration:
[plugins.md](plugins.md). back-ldbm internals: [backends.md](backends.md). Request flow and repo map:
[architecture.md](architecture.md).

## Memory: the slapi_ch_ rules

- `slapi_ch_malloc/calloc/realloc/strdup/bvdup` call `exit(1)` on OOM. Never NULL-check for allocation failure — the branch is dead (`ch_malloc.c (slapi_ch_malloc)`).
- They still return NULL/0 for bad sizes, despite the header's `returns_nonnull` annotation: `slapi_ch_malloc` and `slapi_ch_calloc` return 0 for a non-positive size; `slapi_ch_realloc` returns the original block on non-positive size and delegates to `slapi_ch_malloc` when the block is NULL. Check the result of any allocation whose size is computed (`ch_malloc.c (slapi_ch_calloc)`).
- `slapi_ch_strdup(NULL)` and `slapi_ch_bvecdup(NULL)` return NULL silently — the only non-error NULL returns in the family (`ch_malloc.c (slapi_ch_strdup)`).
- `slapi_ch_free(void **ptr)` takes the **address of** your pointer, frees `*ptr`, and NULLs it; NULL-safe at both levels. Passing the pointer itself is the classic mistake (`ch_malloc.c (slapi_ch_free)`).
- For `char *` use `slapi_ch_free_string(&s)` — it exists so the compiler type-checks the double pointer (`ch_malloc.c (slapi_ch_free_string)`).
- `slapi_ch_smprintf()` allocates through NSPR `PR_vsmprintf` but is deliberately paired with `slapi_ch_free_string` (`ch_malloc.c (slapi_ch_smprintf)`).

## Free-function arity traps

Arity is inconsistent and not guessable — check before writing a free:

- Single pointer, cannot NULL your variable: `slapi_entry_free(e)` (`entry.c (slapi_entry_free)`, which carries the comment "Should be ** so that we can NULL the ptr") and `slapi_valueset_free(vs)` (`valueset.c (slapi_valueset_free)`).
- Double pointer, NULLs your variable: `slapi_value_free(&v)`, `slapi_mods_free(&smods)`, `slapi_mod_free(&smod)`, `slapi_rdn_free(&rdn)`.
- `slapi_attr_free(&a)` takes a double pointer but does **not** NULL your variable — it copies into a local and frees the local. Set `a = NULL;` yourself (`attr.c (slapi_attr_free)`).
- `slapi_sdn_free(&sdn)` always releases the strings but frees the struct (and NULLs your pointer) only when the SDN came from `slapi_sdn_new*()`; safe on stack SDNs (`dn.c (slapi_sdn_free)`).

## Create/free pairs

| Type | Create | Free | Arity | NULLs your pointer? |
|---|---|---|---|---|
| Slapi_Entry | `slapi_entry_alloc()`+`slapi_entry_init()`, `slapi_entry_dup()` | `slapi_entry_free(e)` | single | no |
| Slapi_Attr | `slapi_attr_new()`+`slapi_attr_init()`, `slapi_attr_dup()` | `slapi_attr_free(&a)` | double | **no** |
| Slapi_Value | `slapi_value_new*()`, `slapi_value_dup()` | `slapi_value_free(&v)` | double | yes |
| Slapi_ValueSet | `slapi_valueset_new()` | `slapi_valueset_free(vs)` (struct+values); `slapi_valueset_done(vs)` values only | single | no |
| Slapi_DN | `slapi_sdn_new*()`, `slapi_sdn_dup()` | `slapi_sdn_free(&sdn)`; `slapi_sdn_done(sdn)` contents only | double | heap SDNs only |
| Slapi_RDN | `slapi_rdn_new*()` | `slapi_rdn_free(&rdn)` | double | yes |
| Slapi_Mods | `slapi_mods_new()`, or `slapi_mods_init/_byref/_passin` on a stack struct | `slapi_mods_free(&smods)`; `slapi_mods_done(smods)` for stack | double | yes |
| Slapi_Mod | `slapi_mod_new()` + `slapi_mod_init*()` | `slapi_mod_free(&smod)` / `slapi_mod_done(smod)` | double / single | yes / n/a |
| struct berval | `slapi_ch_bvdup(bv)` | `slapi_ch_bvfree(&bv)` | double | yes |
| struct berval ** | `slapi_ch_bvecdup(bvv)` | `ber_bvecfree(bvv)` | single | no |

Definitions: `entry.c`, `attr.c`, `value.c`, `valueset.c`, `dn.c`, `rdn.c`, `modutil.c`, `ch_malloc.c`.

## The two search helpers

Two near-identically named helpers have opposite ownership — the top double-free source in plugins:

- `slapi_search_internal_get_entry(sdn, attrs, &e, id)` hands back a **duplicate** (`slapi_entry_dup`); free it with `slapi_entry_free(e)` (`plugin_internal_op.c (slapi_search_internal_get_entry)`).
- `slapi_search_get_entry(&pb, sdn, attrs, &e, id)` hands back `entries[0]` itself — a **borrow** from the pblock. Never free it; release everything with `slapi_search_get_entry_done(&pb)`, which frees the results and destroys the pblock (`plugin_internal_op.c (slapi_search_get_entry_done)`).

General internal search, alloc → use → free (`plugin_internal_op.c (slapi_search_internal_get_entry)`):

```c
Slapi_PBlock *pb = slapi_pblock_new();
slapi_search_internal_set_pb(pb, base_dn, LDAP_SCOPE_BASE, "(objectclass=*)",
                             attrs, 0, NULL, NULL, plugin_id, 0);
slapi_search_internal_pb(pb);
slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
if (rc == LDAP_SUCCESS) {
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    /* entries are owned by the pblock: slapi_entry_dup() anything you keep */
}
slapi_free_search_results_internal(pb);  /* then: */ slapi_pblock_destroy(pb);
```

Both teardown calls, in that order, on every exit path.

## _charptr vs _ref getters

| Getter | Returns | Free with |
|---|---|---|
| `slapi_entry_attr_get_charptr()` | heap copy | `slapi_ch_free_string()` |
| `slapi_entry_attr_get_ref()` | pointer into the entry; dangles when the entry is freed or the attribute replaced | never free |
| `slapi_entry_attr_get_charray(_ext)()` | heap copies | `slapi_ch_array_free()` |
| `slapi_entry_attr_find/_first_attr/_next_attr` | `Slapi_Attr *` into the entry | never free; `slapi_attr_dup()` to outlive the entry |
| `slapi_value_get_string()` / `_get_berval()` | internal storage; the string may not be NUL-terminated (`value.c (slapi_value_get_string)`) | never free |
| `slapi_attr_get_type()` | internal string | never free |
| `slapi_attr_get_oid_copy()` / `_syntax_oid_copy()` | copies | `slapi_ch_free_string()` |

The names differ by one word and in-tree code mixes both in one block (`ldap/servers/plugins/usn/usn_cleanup.c`):

```c
suffix = slapi_entry_attr_get_charptr(e, "suffix");        /* free this  */
backend = (char *)slapi_entry_attr_get_ref(e, "backend");  /* never free */
```

Never-free entry getters: `slapi_entry_get_dn/_get_dn_const/_get_ndn/_get_sdn/_get_sdn_const/_get_rdn_const/_get_uniqueid` all return internal pointers. One carve-out: `slapi_entry_get_dn()`'s result may be freed only when you are replacing the DN via `slapi_entry_set_dn()` (`slapi-plugin.h (slapi_entry_get_dn)`). The adjacent setters differ: `slapi_entry_set_dn(e, dn)` **takes ownership** of `dn` (pass `slapi_ch_strdup(x)`), while `slapi_entry_set_sdn(e, sdn)` **copies** (`entry.c (slapi_entry_set_dn)`).

## Reading the pblock

- A `Slapi_PBlock` comes only from `slapi_pblock_new()`; the header forbids stack or malloc'd pblocks (`slapi-plugin.h`, `pblock.c (slapi_pblock_new)`).
- `slapi_pblock_get/set` return 0 on success and -1 for an unknown parameter id (with a debug-build assert). Consequence: `if (slapi_pblock_get(...) && x)` is an always-false dead guard — the tree contains exactly that bug at the BE_PRE_MODIFY call site, where only the callback's return value drives the failure branch (`back-ldbm/ldbm_modify.c (ldbm_back_modify)`).
- Initialize destinations to NULL/0 first: many getters return 0 while leaving `*value` untouched when the backing object is NULL (`pblock.c (slapi_pblock_get_add_entry)`).
- `SLAPI_CONN_ID` writes a `uint64_t`; passing `&connid` for an `int` corrupts the stack (`pblock.c (slapi_pblock_get_conn_id)`).

Ownership of what `slapi_pblock_get` hands back:

| Parameter | Caller owns? | Rule |
|---|---|---|
| pointer parameters, nearly all | no — reference into pblock/operation/connection | never free; re-`get` instead of caching |
| `SLAPI_CONN_DN`, `SLAPI_CONN_AUTHMETHOD` | **yes — heap copies** (`pblock.c (slapi_pblock_get_conn_dn)`, `(slapi_pblock_get_conn_authmethod)`) | `slapi_ch_free_string()` |
| `SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES` | no | `slapi_free_search_results_internal(pb)` before `slapi_pblock_destroy(pb)` |
| `slapi_err2string(rc)` | no — static string | never free |

## Writing the pblock

Almost every setter is a bare pointer assignment — it does not free, copy, or inspect the value it displaces (`pblock.c (slapi_pblock_set_modify_mods)`). The load-bearing parameters:

| Parameter | Setter frees old? | Who frees the installed value | You must free |
|---|---|---|---|
| `SLAPI_MODIFY_MODS` | no | frontend re-`get`s the slot after the op and `ldap_mods_free(mods, 1)` (`modify.c (do_modify)`) | nothing with passin/passout; a fresh array means you free the one you displaced |
| `SLAPI_ADD_ENTRY` | no | frontend — and `op_shared_add` reads the slot once and never re-reads it, so a replaced pointer is not honored. **Edit the entry in place; never free or replace it** (`add.c (op_shared_add)`) | nothing |
| `SLAPI_MODRDN_NEWRDN` | no | frontend frees whatever it finds after the op (`modrdn.c (op_shared_rename)`) | the old string you displaced |
| `SLAPI_MODRDN_NEWSUPERIOR_SDN` | no | frontend (`modrdn.c (op_shared_rename)`) | the old `Slapi_DN *` you displaced |
| `SLAPI_MODRDN_NEWSUPERIOR` (`char *`) | **yes** — frees the old SDN, builds a new one from your string (`pblock.c (slapi_pblock_set_modrdn_newsuperior)`) | the pblock | your own `char *` |
| `SLAPI_SEARCH_ENTRY_COPY` | no | the result path frees it on every path (`result.c (send_ldap_search_entry_ext)`) | free any copy already present before overwriting |
| `SLAPI_RESULT_CODE` | n/a — dereferenced and copied | n/a | nothing; `&` a local `int` is fine (`pblock.c (slapi_pblock_set_result_code)`) |
| `SLAPI_PB_RESULT_TEXT` | **yes** — strdups yours (`pblock.c (slapi_pblock_set_pb_result_text)`) | the pblock | your own buffer; a stack `char[SLAPI_DSE_RETURNTEXT_SIZE]` is correct |
| `SLAPI_RESULT_TEXT` (a different parameter) | no | operation teardown (`operation.c (operation_done)`) | the old string; prefer `slapi_set_ldap_result()`, which frees old and strdups (`plugin.c (slapi_set_ldap_result)`) |

Setting `SLAPI_TARGET_DN` frees the existing `SLAPI_TARGET_SDN` — any held `Slapi_DN *` dangles; re-`get` it (`pblock.c (slapi_pblock_set_target_dn)`). `SLAPI_TARGET_DN` and the `*_TARGET` aliases are deprecated; use `SLAPI_TARGET_SDN`.

The one in-tree write idiom for `SLAPI_MODIFY_MODS` (passin/passout, e.g. `ldap/servers/plugins/dna/dna.c`):

```c
slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
slapi_mods_init_passin(&smods, mods);           /* wrapper owns the array */
slapi_mods_add(&smods, LDAP_MOD_REPLACE, "attr", strlen(val), val);
mods = slapi_mods_get_ldapmods_passout(&smods); /* ownership back out */
slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
```

Between `slapi_mods_init_passin()` and the re-`set`, the pointer still in the pblock may dangle (appends realloc; packing can free the array) — every exit path must go through the re-`set` (`modutil.c (slapi_mods_get_ldapmods_passout)`).

On the failure path: never restore a parameter you already replaced — on a DB deadlock retry the backend reinstalls its own pre-callback snapshot (`back-ldbm/ldbm_modify.c (ldbm_back_modify)`). Set `SLAPI_RESULT_CODE` plus `SLAPI_PB_RESULT_TEXT` and return failure; failure without a result code makes the backend substitute `LDAP_OPERATIONS_ERROR` and log that you did not set one (`back-ldbm/ldbm_add.c (ldbm_back_add)`). betxn-pre callbacks may only append mods — see [plugins.md](plugins.md).

## Return codes by context

| Context | Success | Failure | Notes |
|---|---|---|---|
| Plugin operation callbacks | `SLAPI_PLUGIN_SUCCESS` 0 | `SLAPI_PLUGIN_FAILURE` -1 | non-zero from a preop vetoes the operation (`slapi-plugin.h`) |
| `BE_PRE_*` callbacks | 0 | -1 only | a **positive** return means "plugin changed things, go around the loop again" and can spin forever — never return a positive LDAP code (`back-ldbm/ldbm_add.c (ldbm_back_add)`); details in [plugins.md](plugins.md) |
| Extended-op plugins | LDAP result code (>= 0) | `SLAPI_PLUGIN_EXTENDED_SENT_RESULT` -1, `_NOT_HANDLED` -2, `_NO_BACKEND_AVAILABLE` -3 | `slapi-plugin.h` |
| Internal ops (`slapi_*_internal_pb`) | 0 | -1 | the real outcome is `SLAPI_PLUGIN_INTOP_RESULT`, an LDAP result code |
| `slapi_pblock_get/set` | 0 | -1 unknown id + debug assert | see "Reading the pblock" |
| DSE/config callbacks | `SLAPI_DSE_CALLBACK_OK` **1** | `SLAPI_DSE_CALLBACK_ERROR` **-1** | `DO_NOT_APPLY` **0** (modify only; treated as ERROR elsewhere). Inverted vs everything else — see "DSE callbacks" (`slapi-plugin.h`) |

## DNs and normalization

- Prefer `Slapi_DN` over `char *` DNs; it caches the original, normalized, and case-folded forms.
- `slapi_dn_normalize()` and `slapi_dn_normalize_to_end()` are documented-deprecated **no-ops** — they return their input unchanged (`dn.c (slapi_dn_normalize)`). `slapi_dn_normalize_case()` is **not** a no-op: its normalize half does nothing, but it still lowercases the DN in place (`dn.c (slapi_dn_ignore_case)`). Use `slapi_dn_normalize_ext()`.
- `slapi_dn_normalize_ext(src, len, &dest, &dest_len)` has a three-way return: **0** = normalized in place and `dest` aliases `src`, do not free; **1** = freshly allocated and NUL-terminated, you free it; **-1** = error. The alias path rewrites `src` and does not place the new terminator, so `src` must be a writable, NUL-terminated buffer whose original terminator remains addressable; never cast away `const` or pass a length-bounded unterminated slice. On return 0, write `dest[dest_len] = '\0'` before using it as a C string (`dn.c (slapi_dn_normalize_ext)`).
- The `byval` / `byref` / `passin` suffix is the ownership contract across the sdn and mods APIs: byval copies, byref borrows (you must outlive the holder), passin transfers ownership (`modutil.c (slapi_mods_init_passin)`).
- Never call `slapi_sdn_init*()` on an SDN from `slapi_sdn_new()`: init clears the allocated flag, so a later `slapi_sdn_free()` leaks the struct (`dn.c (slapi_sdn_new)`). The init family is private (`slapi-private.h`); plugin code uses new/free.
- DN equality is `slapi_sdn_compare()`, which compares normalized forms (`dn.c (slapi_sdn_compare)`).

## Thread safety

| Object | Safe to share across threads? |
|---|---|
| `Slapi_Counter` | **Yes** — atomic 64-bit counter; use it instead of an int + mutex (`slapi-plugin.h (Slapi_Counter)`) |
| `Slapi_PBlock` | No — no synchronization at all; one pblock per thread |
| `Slapi_Entry` | No — embeds a `Slapi_DN` by value, so the SDN race below applies to every entry |
| `Slapi_DN` | **No — not even under a read lock**: `slapi_sdn_get_dn/_get_ndn` mutate through a cast on a `const` SDN to memoize the normalized form, with no locking (`dn.c (slapi_sdn_get_dn)`) |
| `Slapi_Attr` / `Slapi_Filter` | No — duplicate into per-operation copies |
| `Slapi_Mutex` | Yes, while valid — `slapi_lock_mutex(NULL)` silently does nothing and has no return value; `slapi_unlock_mutex(NULL)` returns 0, while a successful unlock returns 1 (`slapi2runtime.c (slapi_lock_mutex, slapi_unlock_mutex)`) |
| `Slapi_RWLock` | Yes, while valid — read-lock, write-lock, and unlock all skip a NULL handle and return 0, which is also the POSIX success value; checking that return cannot detect a missing lock (`slapi2runtime.c (slapi_rwlock_rdlock, slapi_rwlock_wrlock, slapi_rwlock_unlock)`) |

- `slapi_sdn_dup(src)` calls `slapi_sdn_get_dn(src)` — duplicating a shared SDN mutates the source (`dn.c (slapi_sdn_dup)`).
- `slapi_current_time()` is deprecated and "NOT THREAD SAFE. DO NOT USE IT." — use `slapi_current_time_hr()` (`slapi-plugin.h (slapi_current_time)`).

Writing concurrent code:

- Operation callbacks run concurrently on worker threads (`connection.c (connection_threadmain)`), and a live config modify runs a plugin's DSE callback on another worker at the same time (the config-lock pattern in [plugins.md](plugins.md)). Anything outside locals and the per-operation pblock is shared state and needs a synchronization owner: `slapi_new_mutex()` / `slapi_new_rwlock()`, the `slapi_atomic_*` family (`slapi-plugin.h (slapi_atomic_incr_64)`), or `Slapi_Counter`.
- Release every lock on every exit path — early returns, `goto bail`, and error branches are where missed unlocks hide, and a leaked lock deadlocks the next worker thread that contends it.
- Nested locks need one fixed acquisition order across all call paths. betxn callbacks already run inside the backend's transaction (see [plugins.md](plugins.md)), so audit ordering across module boundaries, not just within one file.
- A change that touches threading is done only when it is provably free of data races and deadlocks; walk every new lock/unlock pair and every access to shared data before calling it complete.

## Performance

Code in the operation path runs for every LDAP request the server serves; treat added
per-operation cost as a regression and pick the most performant implementation that is
still correct. Before adding work to an operation callback, filter/index evaluation, or
a plugin pre/post hook:

- Hoist invariant work to startup, plugin start, or config-change time — never
  recompute per operation what a config callback can compute once.
- Prefer the borrowing getters over copies where the lifetime allows (`_get_ref` vs
  `_charptr` — see the getter table above), and the memoized `Slapi_DN` forms over
  re-normalizing a DN.
- Keep critical sections short. Prefer `slapi_atomic_*` / `Slapi_Counter` to a mutex
  for counters and flags, and a `Slapi_RWLock` read lock for read-mostly state.
- An internal search (`slapi_search_internal_*`) is a full extra operation with its own
  plugin chain — never add one to a per-operation path when the result can be cached
  at config time or derived from data already in hand.

## Logging

- `slapi_log_err()` is a macro in `slapi-private.h` that expands to `slapi_log_error()`; it exists only in-tree. `slapi-private.h` is not installed — the installed headers are just slapi-plugin.h, slapi_pal.h, and the two replication plugin headers (`Makefile.am (serverinc_HEADERS)`) — so out-of-tree code calls the public `slapi_log_error()` directly.
- Every format string must end with `\n`; nothing appends one.
- Level values 0-18 are topic selectors (`SLAPI_LOG_TRACE`, `SLAPI_LOG_PLUGIN`, ...) and 19-26 are severities (`SLAPI_LOG_EMERG` ... `SLAPI_LOG_ERR` 22 ... `SLAPI_LOG_DEBUG` 26); out-of-range levels are rejected (`slapi-plugin.h`).
- The subsystem argument is a per-plugin constant (e.g. `"memberof-plugin"`) defined in the plugin's header; message shape is `"<function_name> - <message>\n"`.

## The cn=config system

A frontend config attribute exists only as a row of `ConfigList[]` in `libglobs.c`. The table is hashed once by `init_config_get_and_set()`, the last statement of `FrontendConfig_init()`, which runs before bootstrap — a missing row is invisible to everything downstream (`libglobs.c (init_config_get_and_set)`).

Mandatory edits for a new attribute (exemplar: `nsslapd-maxcontrolsperop`):

| # | Edit | Where |
|---|---|---|
| 1 | `SLAPD_DEFAULT_X` **and** `SLAPD_DEFAULT_X_STR` macros (the `_STR` twin is not optional — see delete-reset below) | `slap.h` |
| 2 | `CONFIG_X_ATTRIBUTE "nsslapd-x"` name macro | `slap.h` |
| 3 | Struct member on `_slapdFrontendConfig` | `slap.h` |
| 4 | `ConfigList[]` row: name, setter, `config_var_addr`, type, getter, `_STR` default | `libglobs.c (ConfigList)` |
| 5 | Default assignment in `FrontendConfig_init()` | `libglobs.c` |
| 6 | `config_set_X()` + `config_get_X()` definitions; declarations | `libglobs.c`, `proto-slap.h` |
| 7 | Startup-only attributes: a `"cn=config:<attr>"` string in `requires_restart[]` | `configdse.c` |

Rules that are easy to get wrong:

- Every `config_set_*` runs **twice** per LDAP modify: `modify_config_dse` loops apply=0 (validation) then apply=1. Validate first and `if (!apply) return retVal;` — be side-effect-free on the validation pass (`configdse.c (modify_config_dse)`).
- Reads bypass your getter whenever `config_var_addr` is set: `config_set_entry` pass 1 reads the address directly under the config read lock; pass 2 calls getters only for rows with a NULL address. Put no logic in a getter and expect cn=config reads to run it (`libglobs.c (config_set_entry)`).
- A valueless mod-delete is a **reset to the compiled default**: `config_set` calls the setter with the row's `initvalue`/`geninitfunc`, and fails with `LDAP_UNWILLING_TO_PERFORM` when both are NULL. This is why every numeric default needs a `_STR` twin (`libglobs.c (config_set)`).
- A row with no setter is read-only: the modify is silently ignored with only an error-log line (`libglobs.c (config_set)`).
- Most `char *`-returning `config_get_*` getters return a strdup you free with `slapi_ch_free_string()`, but `config_get_allowed_sasl_mechs()` and `config_get_default_naming_context()` return the live internal pointer — freeing them corrupts config. Read the getter body before freeing (`libglobs.c (config_get_allowed_sasl_mechs)`).
- `requires_restart[]` is advisory metadata: it feeds the synthesized `nsslapd-requiresrestart` attribute and the "will not take effect until the server is restarted" notice that `postop_modify_config_dse` writes into the modify result text; a listed attribute's modify still succeeds (`configdse.c (requires_restart, postop_modify_config_dse)`).
- The schema line is conventional, not enforced: `cn=config` is `extensibleObject` (`ldap/ldif/template-dse.ldif.in`), so undefined attributes pass schema and syntax checks. Add it anyway for new `nsslapd-*` attributes: allocate the next free OID under `2.16.840.1.113730.3.1.` and append at the **end** of the attributeTypes block in `ldap/schema/01core389.ldif` — the block is append-ordered, not OID-ordered.
- Ignore the "ADDING A NEW VALUE" comment at the top of `libglobs.c` — it is stale; use the table above. No lib389 or Cockpit change is required for the attribute to be settable via `dsconf` (see [lib389.md](lib389.md), [ui.md](ui.md)).

Workflow: see the add-config-attribute skill (.agents/skills/add-config-attribute/SKILL.md).

## DSE callbacks

- Register with the public `slapi_config_register_callback(_plugin)()` or, in core code, `dse_register_callback(pdse, operation, flags, base, scope, filter, fn, fn_arg, plugin)` (`dse.c (dse_register_callback)`).
- Callback signature: `int fn(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)`. `returntext` is a caller-owned buffer of exactly `SLAPI_DSE_RETURNTEXT_SIZE` (512) bytes (`slapi-plugin.h (dseCallbackFn)`).
- Return codes are inverted (see the table above). `dse_call_callback` seeds OK and keeps the minimum across all matching callbacks with no early break: a single ERROR (or DO_NOT_APPLY) overrides every other callback's OK, and every matching callback still runs after one fails (`dse.c (dse_call_callback)`).
- Matching is against **entryBefore**: a callback fires only when operation and flags match and the registered base/scope/filter match the pre-op entry, never the post-op one (`dse.c (dse_call_callback)`).
- A callback belonging to a plugin that is not started is not invoked — the dispatcher returns ERROR naming the disabled plugin (`dse.c (dse_call_callback)`).
- `cn=config` itself is served by DSE callbacks registered in `setup_internal_backends()`: `read_config_dse` (search preop), `modify_config_dse` (modify preop/postop pair), and a delete refusal (`fedse.c (setup_internal_backends)`, `configdse.c`). Two pseudo-operations exist beyond the SLAPI ones: `DSE_OPERATION_READ` (dse.ldif parse at startup) and `DSE_OPERATION_WRITE` (dse.ldif write).
- MODRDN and abandon under `cn=config` are hard-wired to unwilling-to-perform in the internal DSE backend — a rename there can never work (`backend_manager.c (be_new_internal)`).
- Modifying an entry under `cn=plugins,cn=config` takes live effect only when `nsslapd-dynamic-plugins` is on; otherwise the server logs a notice and the change needs a restart (`dse.c (dse_modify)`).
