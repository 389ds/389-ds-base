# Contributing to 389 Directory Server

## Reporting Issues

File bugs and feature requests on [GitHub Issues](https://github.com/389ds/389-ds-base/issues). Check the backlog first to avoid duplicates. The repository provides two issue templates (both automatically labeled `needs triage`):

### Bug Report

Use this template when reporting a defect. Fill in each section:

- **Issue Description** — clear, concise summary of the bug
- **Package Version and Platform** — OS/distro, exact package version (e.g. `389-ds-base-3.2.0-6.fc42.x86_64`), browser if UI-related
- **Steps to Reproduce** — numbered steps to trigger the problem
- **Expected results** — what should have happened instead
- **Screenshots** — attach if applicable
- **Additional context** — logs, configuration, environment details

### Feature Request

Use this template when suggesting new functionality:

- **Is your feature request related to a problem?** — describe the pain point or use case
- **Describe the solution you'd like** — what the desired behavior looks like
- **Describe alternatives you've considered** — other approaches you evaluated
- **Additional context** — mockups, screenshots, or related issues

### Security Issues

Security issues should **not** be reported via GitHub issues. Use [GitHub's security advisory feature](https://github.com/389ds/389-ds-base/security/advisories) instead.

## Getting Started

1. Fork the repository on GitHub.
2. Clone your fork:
   ```bash
   git clone https://github.com/<your-username>/389-ds-base.git
   cd 389-ds-base
   ```
3. Create a branch for your changes:
   ```bash
   git checkout -b issue-NNNNN
   ```
4. Build and verify:
   ```bash
   autoreconf -fiv
   ./configure --enable-debug --with-openldap --enable-cmocka
   make
   make lib389
   sudo make install
   sudo make lib389-install
   make check
   ```
5. Commit your changes (see [Commit Messages](#commit-messages)).
6. Push to your fork and open a Pull Request.

## Pull Request Guidelines

All PRs should be submitted against the `main` branch.

PRs should include:
- **Well-documented code changes.** The commit message should explain *why* the change was made.
- **Tests.** New features and bug fixes should include test coverage.
- **Documentation updates** if the changes affect user-facing behavior.

For larger features, open an issue first so the approach can be discussed before significant implementation work.

## Commit Messages

This project uses the following commit message format. Every commit **must** reference a GitHub issue number:

```
Issue ##### - Short description of the change

Description:
Explain what the change does and why it is needed.

Relates: https://github.com/389ds/389-ds-base/issues/#####

Reviewed by: ???
Assisted by: ???
```

### Format rules

- **Subject line**: `Issue ##### - <concise summary>` (under 72 characters when possible)
- **Blank line** after the subject
- **Description**: Explain *what* and *why*, not just *how*
- **Relates/Fixes**: Link to the GitHub issue. Use `Fixes:` when the commit fully resolves the issue, `Relates:` for partial work
- **Reviewed by**: The reviewer's name or GitHub handle
- **Assisted by**: If AI tools or other contributors assisted (e.g. `Cursor`, `Claude`)

## Code Review

Once a PR is submitted, a maintainer will review it. If nobody responds within two weeks, ping a maintainer.

Keep an eye on CI results. If something fails, check the logs to see if it's related to your change.

## Communication

- [389 Directory Server Wiki](https://www.port389.org/)
- [GitHub Issues](https://github.com/389ds/389-ds-base/issues) — bugs and feature requests
- [GitHub Pull Requests](https://github.com/389ds/389-ds-base/pulls) — code contributions
- [Contributing Guide on port389.org](https://www.port389.org/docs/389ds/contributing.html) — full upstream contributing guide
