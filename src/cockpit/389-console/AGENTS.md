# Agent notes: src/cockpit/389-console/

Read [docs/agents/ui.md](../../../docs/agents/ui.md) first; workflow: the `ui-expose-attribute` skill (`../../../.agents/skills/ui-expose-attribute/SKILL.md`).

- PatternFly 5: handler order is `(event, value)` — the old PF4 order compiles and silently stores the event object.
- Every editable field needs state `foo`, shadow `_foo`, AND a dirty-check array entry; database settings are duplicated in the parallel MDB class — edit both.
- Command execution goes through `cockpit.spawn` with literal argv; most management calls use `dsconf`, while existing flows also use `dsctl`, `dsidm`, `dscreate`, system tools, or raw `ldapsearch`. Call `log_cmd` before every spawn, but it masks only inline `--passwd=value`, `--bind-pw=value`, and `--nsslapd-rootpw=value`. Split values and every other argument are logged in plaintext, so keep other secrets out of argv.
- Never edit `cockpit_dist/` (generated); never run `npm run prettier:fix` (no config — mass reformat).
- There are no JS unit tests; `npm run build` breakage surfaces in the RPM build on every PR.
