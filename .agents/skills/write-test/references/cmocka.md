# C Unit Tests (cmocka)

cmocka covers pure in-process C logic (parsers, CSN/clock handling, data structures) in libslapd or a plugin; no server instance is involved. Everything runs in one binary, `test_slapd`. Its source list links `libslapd.la` plus selected plugin libs only — no back-ldbm source is in it, so backend code is exercised by pytest, not cmocka.

## The four required edits

A new test touches all four locations — verify each one before building:

1. **Test bodies** — new file `test/<group>/<area>/<name>.c` (`<group>` is `libslapd` or `plugins`), one `void test_<x>(void **state)` per case. `#include "../../test_slapd.h"` (relative to your directory depth), plus `<slapi-private.h>` if you call server internals.
2. **`Makefile.am`** — append the file to the `test_slapd_SOURCES` list (it sits inside `if ENABLE_CMOCKA`). A file missing from this list is never compiled.
3. **Registration** — one `cmocka_unit_test(test_<x>)` row per case in the group runner in `test/<group>/test.c` (`run_libslapd_tests()` / `run_plugin_tests()`, both called from `test/main.c`). Cases needing setup/teardown use `cmocka_unit_test_setup_teardown(fn, setup, teardown)` — the plugins group does this for NSS init.
4. **Prototype** — `void test_<x>(void **state);` in `test/test_slapd.h`. Missing it is a compile error in `test.c`.

**Missing the registration row (edit 3) still compiles cleanly and the test silently never runs.** Feature-gated tests (e.g. HIBP) wrap all three list entries in the matching conditional: `if ENABLE_HIBP` around the SOURCES line, `#ifdef ENABLE_HIBP` around the registration rows and prototypes.

## Exposing internals

To drive a static/internal server function from a test, declare it in `ldap/servers/slapd/slapi-private.h` — do not invent a new header. When the internals need a seam, add a small non-static hook next to them: the CSN clock-error tests added a `csngen_set_gettime()` clock-override hook to `csngen.c` and declared it in `slapi-private.h` (commit `b01965368` is the full 6-file exemplar).

## Build, run, CI

What the build/test skill must run (routing: verify-changes skill — never build on an unprepared host):

```bash
./configure --enable-cmocka ...           # default is OFF; needs the cmocka devel package
make check                                # builds and runs test_slapd
libtool --mode=execute gdb ./test_slapd   # to debug
```

- Without `--enable-cmocka`, `test/` is not compiled at all and `make check` still exits green having run zero cmocka tests. Configure with the flag or you have verified nothing.
- CI runs the suite on every PR: the RPM spec's `%configure` always passes `--enable-cmocka`, and `%check` runs `make check` during the RPM build of both pytest workflows (skipped only under asan/msan), catting `test-suite.log` on failure. A failing unit test therefore fails the pytest jobs at the build step.
- The highest-fidelity local run is the container RPM build — see the verify-changes skill.
