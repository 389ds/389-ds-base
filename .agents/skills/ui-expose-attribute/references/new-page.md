# Adding a New Page to the Cockpit Console

Read SKILL.md first — all state, i18n, spawn, and build conventions apply here too.

## New component file

- Create the component under `src/cockpit/389-console/src/lib/<area>/` (top-level tab
  components live directly in `src/`). Pages are ES6 class components with
  `this.state` and explicit `.bind(this)`; hooks appear only in newer leaf components.
- No build-config edit is needed: esbuild bundles everything reachable by import from
  the single entry point.
- `.tsx` is allowed, and it is imported **with a `.jsx` extension** (esbuild rewrites
  TS extensions), e.g.
  `import { PwpFixupTasks } from "./lib/database/pwpFixupTasks.jsx";`
- **Never add a copyright header** — `.jsx`/`.tsx` files in this tree carry none.

## Wiring into a tab's tree (the normal case)

Second-level pages are TreeView nodes plus a render switch in the owning tab component
(`server.jsx`, `database.jsx`, `monitor.jsx`). Three edits in that one file:

1. `import { MyPage } from "./lib/<area>/myPage.jsx";`
2. Add a node object to the tree data:
   `{ name: _("My Page"), icon: <SomeIcon />, id: "mypage-config" }`
   (optionally with `children: [...]` and `defaultExpanded: true`).
3. Add a branch to the render switch:
   `} else if (this.state.node_name === "mypage-config") { ... = (<MyPage ... />); }`
   passing what sibling pages get — typically `serverId`, `addNotification`,
   `enableTree`, and any preloaded `attrs`.

Security is the worked example: `security.jsx` is imported by `server.jsx` and
rendered from the Server tree's `security-config` node — it is not a top-level tab.

## New top-level tab (rare)

Import the component in `ds.jsx` and add a `<Tab eventKey={N} title={...}>` inside the
existing `<Tabs isFilled>` block, passing `addNotification`, `serverId`,
`wasActiveList`, and `key={this.state.serverId}` (the key remounts the subtree when
the selected instance changes).

## Plugin pages (their own registration path)

A plugin config page is a new file under `src/lib/plugins/`, an import in
`plugins.jsx`, and an entry in the `selectPlugins` object there:
`myPlugin: { name: "...", icon: ..., component: (<MyPlugin ... />) }`.
Plugins without an entry fall back to the generic `PluginTable`.

## Verify

- The UI build passes (verification routing: SKILL.md step 6).
- The node appears in the tree, the switch renders it, and Save behaves per SKILL.md.
- Consider a Playwright test under `dirsrvtests/tests/suites/webui/<area>/`.
