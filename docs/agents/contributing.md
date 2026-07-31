# Contributing

Commit shape, copyright headers, formatting, and PR rules for 389-ds-base.
Build and CI mechanics: [building.md](building.md). Test requirements:
[testing.md](testing.md). Cockpit UI conventions: [ui.md](ui.md).

## Commit message format

Subject: `Issue NNNN - short summary` — the GitHub issue number, a spaced
hyphen, then the summary. Never type a `(#PR)` suffix: GitHub appends it at
squash-merge, and the PR number does not exist when you commit.

Body: a `Bug Description:` paragraph (what is broken and why) followed by a
`Fix Description:` paragraph (what the change does). A simple change may use a
single `Description:` paragraph instead.

Trailers, each on its own line after the body:

| Trailer | Rule |
|---|---|
| `Fixes: <full issue URL>` | use when the commit fully resolves the issue |
| `Relates: <full issue URL>` | use when the issue stays open |
| `Reviewed by:` | added by humans at merge time — do not write it yourself |
| `Assisted by: <tool name>` | the established AI-attribution trailer |

`Signed-off-by:` (DCO) is not the convention in this repo — do not add it.

Canonical example:

```text
Issue 7529 - Fix WebUI local policy availability test

Description: Update the local password policy WebUI test to match the
current table and modal workflow. Check empty and populated table states,
open the Create New Local Policy modal, and verify the edit action only
when editable policies are present.

Fixes: https://github.com/389ds/389-ds-base/issues/7529
```

Workflow: see the commit-and-pr skill (`.agents/skills/commit-and-pr/SKILL.md`).

## Copyright headers

New `.c`/`.h` files open with exactly this block, current year
(`ldap/servers/slapd/threadpool_stats.c`):

```c
/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/
```

New `.py` files use the `#` form; test files under `dirsrvtests/` end the
block with a trailing bare `#`
(`dirsrvtests/tests/stress/backend/range_deadlock_test.py`):

```python
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
```

| File kind | Rule | Example |
|---|---|---|
| New `.c` / `.h` | 7-line `/** ... **/` block, Red Hat only | `ldap/servers/slapd/threadpool_stats.c` |
| Legacy `.c` / `.h` | keep existing Sun/Netscape lines — never strip them | `ldap/servers/slapd/back-ldbm/back-ldbm.h` |
| Test `.py` under `dirsrvtests/` | `#` block ending with a bare `#` | `dirsrvtests/tests/stress/backend/range_deadlock_test.py` |
| lib389 `.py` | same `#` block, blank line instead of the bare `#` | `src/lib389/lib389/cli_ctl/threadpool.py` |
| `.jsx` / `.tsx` (Cockpit UI) | no header — start at the imports | `src/cockpit/389-console/src/lib/database/pwpFixupTasks.tsx` |

## Formatting

`.clang-format` at the repo root is Mozilla-derived: 4-space indent,
`ColumnLimit: 0` (no line-length wrapping), `SortIncludes: false` (never
reorder includes). No CI job enforces formatting for any language, so match
the style of the file you are editing and do not run repo-wide formatters. In
particular there is no prettier config — never run `npm run prettier:fix`
([ui.md](ui.md)). Python style follows the existing files, gated only by the
vermin Python-version floor ([building.md](building.md)).

## Pull requests

- Target `main`.
- Treat agent references as part of the implementation: if code, CI, or an
  interface changes behavior described under `docs/agents/` or
  `.agents/skills/`, update every affected guide or skill in the same change.
  Keep technical claims checkable with `path (symbol)` citations.
- Every bugfix carries a regression test and every feature carries tests —
  see [testing.md](testing.md).
- The repo squash-merges. Keep one logical commit per PR: amend and
  force-push instead of stacking fixup commits.
- Report security issues through GitHub security advisories, never public
  issues.
- Verify the change before pushing via the verify-changes skill
  (`.agents/skills/verify-changes/SKILL.md`) — it routes through the
  environment's build/test skill and never builds on an unprepared host.
