# Contributing

For help setting up a development environment, testing the server or preparing
your first patch, see the
[full contributor guide on port389](https://www.port389.org/docs/389ds/contributing.html).
If you use AI to prepare a contribution, read the [AI contribution policy](AI_POLICY.md).

## Issues

Report suspected vulnerabilities privately through
[GitHub security advisories](https://github.com/389ds/389-ds-base/security/advisories/new).
Use the GHSA ID or assigned CVE for the fix and keep it private until coordinated disclosure.
For other bugs and feature requests, search open and closed issues in the
[issue tracker](https://github.com/389ds/389-ds-base/issues?q=is%3Aissue).
If you don't find an existing report, use the
[issue templates](https://github.com/389ds/389-ds-base/issues/new/choose).

In a bug report, give the server version and platform, the steps you took,
what you expected and what happened. If the problem is intermittent, say how
often you've seen it. Include relevant output and redact credentials and
personal data from logs and LDIF before sharing them.

For a new feature, a large patch or a series of related changes, discuss the
problem and the proposed approach on the issue before you start. If you're
stuck, tell us what you've tried and where you need help. You can also ask
on the [mailing lists](https://www.port389.org/docs/389ds/contributing.html#communicating-with-the-team).

## Pull requests

Before contributing code, complete the [Fedora Project Contributor Agreement](https://www.port389.org/docs/389ds/contributing.html#fedora-project-contributor-agreement).
Only submit work you have the right to contribute under the applicable
license. If you reuse material, identify its source and preserve its
copyright and license notices. See [LICENSE](LICENSE).

Normally target `main`. Link the issue if there is one and keep each pull request focused on one
change. Explain why you chose that approach. Include a regression test for
a bug fix and tests for new behavior.

Tell reviewers what you ran, what passed or failed and which relevant checks
you couldn't run. For example, if you changed how a `dsconf` option sets a
server attribute, read the value back to confirm the server applied it.
See the [testing guide](docs/agents/testing.md) and
[commit conventions and issue exceptions](docs/agents/contributing.md#commit-message-format).

## Reviewing contributions

Consider whether the change solves the reported problem and what it will
take to maintain it. Be specific about anything that needs work. If a
regression test passes even before the fix, tell the author how you ran it
so they can check the result.

Maintainers may ask for a smaller patch or a missing test result. If a
submission needs substantial debugging or rewriting before it can be
reviewed, a maintainer may close it with a brief explanation. The contributor
can return with the missing evidence or a revised patch. If the contributor
and reviewer disagree, either can ask another maintainer for a second opinion.

If someone repeatedly submits unverified work after feedback, maintainers
may limit further submissions to this repository.
