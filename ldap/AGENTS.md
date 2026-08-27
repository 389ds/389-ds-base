# Agent notes: ldap/

Read [docs/agents/c-server.md](../docs/agents/c-server.md) first, plus [plugins.md](../docs/agents/plugins.md) / [backends.md](../docs/agents/backends.md) / [replication.md](../docs/agents/replication.md) as relevant.

- SLAPI memory ownership is per-function — check the guide before allocating or freeing (`slapi_search_internal_get_entry` returns a copy you free; `slapi_search_get_entry` a borrow you must not).
- `slapi_pblock_get` returns 0 on success and hands out borrowed references for nearly every parameter.
- DSE/`cn=config` callbacks return 1 for OK and -1 for error; returning 0 blocks the change.
- Never edit `ldap/ldif/template-dse.ldif` — only the `.in` template.
- Evaluate every `back-ldbm` change for both `db-bdb/` and `db-mdb/` (`touch-backend` skill).
- A new `cn=config` attribute without a `ConfigList[]` row in `libglobs.c` is silently invisible.
