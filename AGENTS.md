# AGENTS.md for 389 Directory Server (389-ds-base)

This file provides guidance for AI assistants and coding agents working with this codebase.

389 Directory Server is a highly usable, fully featured, reliable and secure LDAP server implementation written primarily in C and Rust, with a Python management library (lib389) and a Cockpit-based web UI.

## Project Structure

| Path | Description |
|------|-------------|
| `ldap/` | Core LDAP server: ns-slapd daemon, bundled plugins, schema, LDIF |
| `ldap/servers/snmp` | SNMP sub-agent (ldap-agent): exposes ops/entries/entity stats via AgentX, sends server up/down traps; includes redhat-directory.mib |
| `ldap/servers/slapd/tools` | Tools: dbscan, ldclt, pwdhash (pwenc), chkvlv, eggencode, mkdep |
| `lib/` | Shared C libraries (base, ldaputil, libaccess, libadmin, libsi18n) |
| `include/` | Public and internal C headers |
| `src/` | Rust code, lib389 Python library, Cockpit web UI, svrcore |
| `dirsrvtests/` | Python/pytest integration test suites |
| `test/` | C unit tests (cmocka-based) |
| `rpm/` | RPM spec templates and packaging helpers |
| `.github/` | GitHub Actions CI workflows |

## Building

Autotools-based (not CMake):

```bash
autoreconf -fiv
./configure --enable-debug --with-openldap --enable-cmocka
make
make lib389
sudo make install
sudo make lib389-install
```

## Detailed Guides

- [Project Architecture](docs/agents/architecture.md) — source layout, lib389, SLAPI plugin system, coding conventions
- [Testing](docs/agents/testing.md) — pytest structure, writing tests, docstring format, fixtures, best practices
- [Building and CI](docs/agents/building.md) — build system, RPM packaging, CI workflows
- [Contributing](docs/agents/contributing.md) — commit message format, PR guidelines, code review

## External Documentation

- [389 Directory Server Wiki](https://www.port389.org/)
- [Building Guide](https://www.port389.org/docs/389ds/development/building.html)
- [Coding Style Guide](https://www.port389.org/docs/389ds/development/coding-style.html)
- [Contributing Guide](https://www.port389.org/docs/389ds/contributing.html)
- [Test Framework Guide](https://www.port389.org/docs/389ds/FAQ/upstream-test-framework.html)
- [Writing lib389 Tests](https://www.port389.org/docs/389ds/howto/howto-write-lib389.html)
