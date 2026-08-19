# Cockpit Web UI (389-console)

The web UI lives in `src/cockpit/389-console/` — written `console/` below. It
is a Cockpit plugin: React 18 class components (hooks appear only in newer
leaf components), PatternFly 5, bundled by esbuild. Commit format and headers:
[contributing.md](contributing.md).

## How the UI talks to the server

There is no REST or DBus layer. Every action shells out via `cockpit.spawn`
with a literal argv and parses stdout. The canonical read, from
`console/src/database.jsx` (`loadSuffixList`):

```jsx
const cmd = [
    "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
    "backend", "suffix", "list", "--suffix"
];
log_cmd("loadSuffixList", "Get a list of all the suffixes", cmd);
cockpit.spawn(cmd, { superuser: "require", err: "message" })
        .done(content => {
            const suffixList = JSON.parse(content);
        });
```

- Pass `{ superuser: "require", err: "message" }` to every spawn.
- Call `log_cmd()` (`console/src/lib/tools.jsx`) before the spawn, but do not
  treat it as a general secret scrubber. It safely masks only inline
  `--passwd=value`, `--bind-pw=value`, and `--nsslapd-rootpw=value` tokens;
  split option values, bare config assignments, and every unrecognized
  argument are logged in plaintext. Keep all other secrets out of argv.
- Surface errors through `getApiErrorMessage(err)`
  (`console/src/lib/tools.jsx`); it handles both JSON `{desc, info}` bodies
  and plain text.
- JSON attribute values arrive as arrays: read `attrs['nsslapd-port'][0]`.
- Writes use either `config replace attr=val` for raw `cn=config` attributes
  (`console/src/lib/server/settings.jsx` (`handleSaveRootDN`)) or subcommand
  flags such as `backend config set --lookthroughlimit=N`
  (`console/src/lib/database/databaseConfig.jsx`) — match the page you edit.
  Renaming a dsconf subcommand or flag breaks the UI silently; see
  [cli.md](cli.md).
- Exception: the LDAP-browser pages bypass dsconf and run raw
  `ldapsearch -LLL -o ldif-wrap=no -Y EXTERNAL`
  (`console/src/lib/ldap_editor/lib/utils.jsx`
  (`getBaseLevelEntryAttributes`)). `-o ldif-wrap=no` is load-bearing: the
  hand-written LDIF parser breaks on wrapped lines.

## State conventions

Every editable field needs three things in its component:

| Piece | Purpose | Miss it and... |
|---|---|---|
| `this.state['foo']` | current value; the element's `id` must equal this key | the shared `onChange` (which reads `e.target.id`) cannot route the change |
| `this.state['_foo']` | shadow of the value as loaded from the server | Save is permanently enabled, or a TypeError in the MDB config class |
| entry in the page's dirty-check attrs array | Save enables when `state[a] !== state['_' + a]` | Save never enables |

- Database settings are duplicated across parallel classes:
  `GlobalDatabaseConfig` and `GlobalDatabaseConfigMDB`
  (`console/src/lib/database/databaseConfig.jsx`); likewise
  `DatabaseMonitor` / `DatabaseMonitorMDB`
  (`console/src/lib/monitor/dbMonitor.jsx`). Add settings to BOTH classes or
  they silently disappear on the other backend. The dirty-check array is
  `check_attrs` in the first and `this.validationFields` in the MDB class.
- Every field carries hover documentation:
  `title={_("Plain-English description (realAttributeName).")}` on the
  wrapping `<Grid>` or on the control itself.
- i18n: file-local `const _ = cockpit.gettext;` plus
  `cockpit.format(_("... $0"), val)` with `$0`/`$1` placeholders.

## PatternFly

- The major version is PatternFly 5 (`console/package.json`). Consult PF5
  docs only.
- PF5 form-control handlers take `(event, value)`. The old PF4 order
  `(value, event)` compiles and silently stores the event object into state —
  a wrong-value bug, not an exception.
- Some files still import `Dropdown`-family and `Wizard` components from
  `@patternfly/react-core/deprecated` (`console/src/ds.jsx`); never mix a
  deprecated import with the same-named current export. For new typeahead or
  multi-select fields use `TypeaheadSelect`
  (`console/src/dsBasicComponents.jsx`).
- `.tsx` files exist and are imported with a `.jsx` extension (esbuild
  rewrites it): `import { PwpFixupTasks } from
  "./lib/database/pwpFixupTasks.jsx"` in `console/src/database.jsx`.

## Navigation map

Top-level tabs are one PF `<Tabs>` block in `console/src/ds.jsx`. Security is
NOT a top tab: `security.jsx` renders from the Server tab's tree
(`console/src/server.jsx`, node `security-config`).

| Tab | File under `console/src/` |
|---|---|
| Server | `server.jsx` (tree includes Security -> `security.jsx`) |
| Database | `database.jsx` |
| Replication | `replication.jsx` |
| Schema | `schema.jsx` |
| Plugins | `plugins.jsx` (pages in `lib/plugins/`) |
| Monitoring | `monitor.jsx` (pages in `lib/monitor/`) |
| LDAP Browser | `LDAPEditor.jsx` (pages in `lib/ldap_editor/`) |

Server, Database and Monitoring build second-level navigation from PF
`TreeView` node objects: adding a page means adding a `{ name, id, ... }`
node AND a matching branch in the render switch on `state.node_name`.

## Build reality

Run from `src/cockpit/389-console/` unless noted:

| Task | Command |
|---|---|
| dev build | `npm run build` (esbuild via `./build.js`) |
| watch mode | `npm run watch` or `./buildAndRun.sh` |
| lint | `npm run eslint` / `npm run stylelint` — advisory, not in CI |
| production build + stage | `make -f rpm.mk build-cockpit` (repo root; plain `make build-cockpit` fails) |

- There are no JS unit tests and no lint gate in CI, but the production build
  runs on every PR inside the RPM build — a broken import fails it. The only
  other JS gate is the dependency audit `npx audit-ci`
  (`.github/workflows/npm.yml`). Playwright webui tests live under
  `dirsrvtests/tests/suites/webui/` ([testing.md](testing.md)).
- esbuild writes `dist/`; packaging consumes `cockpit_dist/`, a generated
  copy of it — never edit `cockpit_dist/` (generated-files table:
  [building.md](building.md)).
- Dev loop: `mkdir -p ~/.local/share/cockpit`, symlink `console/dist` there
  as `389-console`, run `./buildAndRun.sh`, then just reload the browser.
- Never run `npm run prettier:fix`: there is no prettier config, so it would
  reformat the tree with defaults that fight eslint's 4-space rule.

## Landmine: the LDAPI socket path

The literal `ldapi://%2fvar%2frun%2fslapd-<serverId>.socket` is hardcoded
across many components and works only because `/var/run` is a symlink to
`/run`. Copy the existing form exactly — do not "correct" it.

Workflow: see the ui-expose-attribute skill
(`.agents/skills/ui-expose-attribute/SKILL.md`).
