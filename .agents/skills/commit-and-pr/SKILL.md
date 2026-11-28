---
name: commit-and-pr
description: >-
  Prepare a local commit and draft a pull request for the contributor.
  Covers branch names, issue references, commit messages and squash merges.
  Use when committing, amending or preparing a PR.
---

# Preparing a commit and pull request

Most commits need a GitHub issue number. You can look up an existing issue
and draft text locally. The contributor opens the issue or pull request.

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

4. **Default subject line**: `Issue <number> - <summary>` — capital I, spaced hyphen.
   - **Never append a `(#N)` suffix — GitHub adds the PR number itself at
     squash-merge.**
   - If no matching issue exists, check the
     [issue exceptions](../../../docs/agents/contributing.md#commit-message-format).
     If an issue is required, ask the contributor to open one and share
     its number before committing. You can draft the issue text for them.

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
   - Omit `Fixes:` and `Relates:` if there is no GitHub issue to link.
   - `Fixes: <full GitHub issue URL>` when the commit fully resolves the
     issue, so GitHub closes it when the commit reaches the default branch.
     Use `Relates: <full GitHub issue URL>` for related or partial work;
     it does not close the issue. Choose by outcome,
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

8. **Prepare the PR draft for the contributor.** Use the commit subject as
   the title and the commit body as the starting point for the description.
   Include any [AI disclosure](../../../AI_POLICY.md#disclosing-ai-use) in
   the draft. Give the contributor the branch name and draft for a PR
   against `main` by default.

## Maintenance
If a step no longer matches the code or CI, update this skill in the same PR as the change that moved it.
