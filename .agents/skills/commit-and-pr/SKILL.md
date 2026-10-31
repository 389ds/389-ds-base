---
name: commit-and-pr
description: >-
  Commit and open a PR the 389-ds-base way — "Issue NNNN - summary" subject,
  Bug Description/Fix Description body, issue and review trailers, branch
  names, and squash-merge etiquette. Use for ANY commit, amend, or pull
  request in this repo. Also guards the repo's working-tree traps — *.patch
  files are gitignored and vanish from git status, CLAUDE.md/GEMINI.md are
  symlinks that must never be replaced with regular files, and generated files
  must never be committed. Triggers: "commit this", "git commit", "amend",
  "open a PR", "create a pull request", "push this", "commit message".
---

# Commit and PR, the 389-ds-base way

This repo squash-merges PRs, links every commit to a GitHub issue, and carries
working-tree traps that silently eat files. Follow the steps in order.

## Steps

1. **Pre-flight.** If changed code, CI, or interface behavior is described in
   `docs/agents/` or `.agents/skills/`, update every affected reference in the
   same change. Verification for the touched areas must then pass
   (verify-changes skill). Check `git status --porcelain` — and remember it lies
   about patches: **`.gitignore` ignores `*.patch`, so any .patch file you created
   is invisible to git status and will not be committed.** Use a different
   extension, or `git add -f` it only as a deliberate act.
   - **Branch name**: use a short issue description, such as
     `fix-connection-timeout`.
     Do not add `codex/` or similar prefixes.

2. **Never commit generated artifacts.** The common offenders:
   - `configure`, `Makefile.in`, `Makefile` (autotools outputs)
   - `rpm/389-ds-base.spec` (generated from `rpm/389-ds-base.spec.in`)
   - `ldap/ldif/template-dse.ldif` and other non-`.in` template outputs
   - `src/lib389/man/*.8` (generated from argparse help text)
   - `src/lib389/lib389/tests/topologies.py` (build-time copy)
   - `src/cockpit/389-console/cockpit_dist/` (bundler output)
   Full table: docs/agents/building.md. Edit the `.in`/source counterpart.
   - Verify: `git status` shows none of them staged (they only get in via
     `git add -f`, so never force-add a path from this list).

3. **Never write CLAUDE.md or GEMINI.md as regular files.** They are git
   symlinks to AGENTS.md; replacing one forks the docs. Edit AGENTS.md.
   - Verify: `git ls-files -s AGENTS.md CLAUDE.md GEMINI.md` shows mode
     `120000` for the latter two.

4. **Subject line**: `Issue <number> - <summary>` — capital I, spaced hyphen.
   - **Never append a `(#N)` suffix — GitHub adds the PR number itself at
     squash-merge.**
   - **STOP if no GitHub issue exists**: create one first; the subject needs
     its number.

5. **Body**: `Bug Description:` and `Fix Description:` paragraphs of wrapped
   prose (a single `Description:` is fine for simple or test-only changes):

   ```
   Issue 7529 - Fix WebUI local policy availability test

   Bug Description: The test no longer matches the current local password
   policy table and modal workflow, so it fails against a working UI.

   Fix Description: Check empty and populated table states, open the
   Create New Local Policy modal, and verify the edit action only when
   editable policies are present.

   Fixes: https://github.com/389ds/389-ds-base/issues/7529
   ```

6. **Trailers**:
   - Separate the trailer block from the body with a blank line and put
     each trailer on its own line, using standard Git trailer syntax.
   - `Fixes: <full GitHub issue URL>` when the commit fully resolves the
     issue, so GitHub closes it when the commit reaches the default branch.
     Use `Relates: <full GitHub issue URL>` for related or partial work that
     leaves the issue open; it does not close the issue. Choose by outcome,
     including for test-only or documentation-only commits.
   - Always use full issue URLs, not bare `#NNNN` references, so the links
     remain useful in `git log` outside GitHub.
   - Use `Reviewed-by: Name <email>` for completed human reviews. Reviewers
     or maintainers add it at merge time with the reviewer's actual name
     and email; never invent review credit or add placeholders.
   - Do not add AI attribution trailers.
   - `Signed-off-by:`/DCO is not this repo's convention; skip it.

7. **One logical commit per PR.** The repo squash-merges, so amend
   (`git commit --amend`) rather than stacking fixup commits. NEW files need
   the canonical copyright header for their filetype — per-filetype table in
   docs/agents/contributing.md.

8. **Open the PR** with title = commit subject and body = commit body:
   `gh pr create` against `main`.
   - Verify: the PR title carries no `(#N)` suffix, and the body kept the
     Bug/Fix Description sections and the issue trailer.

## Maintenance
If a step no longer matches the code or CI, update this skill in the same PR as the change that moved it.
