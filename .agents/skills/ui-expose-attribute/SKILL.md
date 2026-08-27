---
name: ui-expose-attribute
description: >-
  Expose a server setting in the Cockpit web console (389-console) or change web
  UI behavior. Triggers: "show X in the UI", "add a field to the Server/Database
  page", "web console", anything under src/cockpit/389-console. Warning: every
  editable field needs a state value, an underscore shadow copy, and a
  dirty-check array entry, and database settings must also be duplicated in the
  parallel MDB class — omitting any one breaks Save silently: it never enables,
  stays permanently enabled, or throws a TypeError.
---

# Expose an Attribute in the Cockpit UI

UI conventions and semantics: docs/agents/ui.md. The UI shells out to dsconf, so the
CLI flag must exist first — flag/UI coupling rules: docs/agents/cli.md. Adding a whole
new page or section instead of a field: references/new-page.md.

1. **Locate the page component.** Top-level tabs (Server, Database, Replication,
   Schema, Plugins, Monitoring, LDAP Browser) route from
   `src/cockpit/389-console/src/ds.jsx` to same-named `.jsx` files; use the tab map in
   docs/agents/ui.md. Pages inside a tab are PatternFly `TreeView` nodes plus a render
   switch on `state.node_name` in the tab component. **Security is not a top tab** —
   it renders from the Server tree's `security-config` node.
   Verify: grep the page's visible title or an attribute it already shows.

2. **Make the four edits for an editable field** — mirror an existing attribute of the
   same component through every occurrence site:
   (a) register the attribute where the page loads it (a module-level `*_attrs` array,
   or the parent loader that maps `attrs[...]` into props — match the page);
   (b) add `this.state['<attr>']`;
   (c) add the `'_<attr>'` shadow copy in the same constructor/`setState` path
   (underscore = value as loaded from the server);
   (d) add the render block (Grid row + label + input).
   **STOP — also add the dirty-check array entry** (`check_attrs`, the `*_attrs`
   arrays, or `this.validationFields`, depending on the class): Save is enabled by
   comparing `state[a] !== state['_' + a]` over that array. Miss the array entry and
   Save never enables; miss the `_` shadow and Save is permanently enabled — or throws
   a TypeError in classes that call `.toString()` on both sides of the comparison.
   Verify: grep the component for an existing attribute and confirm the new one
   appears at every site the old one does.

3. **Database settings exist TWICE.**
   `src/cockpit/389-console/src/lib/database/databaseConfig.jsx` defines two parallel
   classes — `GlobalDatabaseConfig` (BDB) and `GlobalDatabaseConfigMDB` — with
   different member lists and different dirty-check arrays. **Never add a database
   setting to only one class**: it silently disappears on the other backend. The same
   duality exists in the monitor components (`DatabaseMonitor`/`DatabaseMonitorMDB`,
   `SuffixMonitor`/`SuffixMonitorMDB`).

4. **Write the save handler.** Build the dsconf argv the way the page already does —
   two idioms coexist and are not interchangeable: raw `cn=config` attributes use
   `['dsconf', '-j', 'ldapi://...', 'config', 'replace', '<attr>=' + value]`, while
   backend/plugin settings use subcommand flags (`'backend', 'config', 'set'` plus
   `cmd.push("--flag=" + value)` per changed field). Push only attributes whose `_`
   shadow differs. Spawn with
   `cockpit.spawn(cmd, { superuser: "require", err: "message" })`.
   Call `log_cmd(...)` immediately before every spawn, but **do not assume it masks
   arbitrary credentials**: it recognizes only `--passwd`, `--bind-pw`, and
   `--nsslapd-rootpw`, and its masking is safe only for an inline `--flag=value`
   token. Every other argv value — including the value after a split `--flag value`
   pair and a bare `nsslapd-rootpw=value` config assignment — is logged to the
   browser console as plaintext. Inspect the complete argv and keep passwords,
   tokens, and other secrets out of it; use an existing prompt, stdin, or protected
   file mechanism instead. Route `.fail` errors through `getApiErrorMessage(err)`
   (both helpers live in `src/lib/tools.jsx`) into `addNotification`. When reading:
   parse dsconf `-j` output with `JSON.parse`; every lib389 attribute value arrives
   as an array — read `attrs['<attr>'][0]`. Verify: the dsconf flag exists with that
   exact spelling (docs/agents/cli.md).

5. **Follow the render conventions.**
   - The element `id` must equal the state key — generic handlers read
     `event.target.id` to pick the state slot.
   - Every field carries hover documentation:
     `title={_("Plain-english description (realAttributeName).")}` on the wrapping
     `Grid` (or on the control itself).
   - i18n: file-local `const _ = cockpit.gettext;`; interpolate with
     `cockpit.format(_("... $0"), val)`.
   - **Never copy the PatternFly 4 handler order `(value, event)`** — PatternFly 5
     form controls use `(event, value)`. The PF4 order compiles and silently stores
     the SyntheticEvent object into state. Consult PF5 docs only.
   Verify: the `@patternfly/react-core` major version in
   `src/cockpit/389-console/package.json`.

6. **Verify the build via the environment's build/test skill; with no such skill,
   do not run the bundler — report the UI build as unverified (CI builds it on
   every PR).** The check the skill must run: `npm ci && npm run build` from
   `src/cockpit/389-console`. The production bundle is built on every PR (inside
   the RPM tarball step), so a broken import fails all CI even though CI runs no
   JS lint or test. `npm run eslint` is advisory. **Never run `npm run prettier:fix`** — there is no prettier config, so
   it mass-reformats with defaults that fight eslint. **Never edit `cockpit_dist/`** —
   it is a generated copy of `dist/`.

7. **Test manually.** There are no JS unit tests. Use the cockpit dev loop: symlink
   `dist/` into `~/.local/share/cockpit/389-console`, run `./buildAndRun.sh`, reload
   the browser (details in docs/agents/ui.md). Optionally add a Playwright test under
   `dirsrvtests/tests/suites/webui/<area>/` — see the write-test skill.

8. **New page, tree node, or plugin page instead of a field:** references/new-page.md.

## Maintenance

Update this skill when the PatternFly major version in package.json changes, when the
shadow-state/dirty-check pattern is refactored, or when the BDB/MDB class pairs merge.
