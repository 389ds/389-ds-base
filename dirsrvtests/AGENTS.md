# Agent notes: dirsrvtests/

Read [docs/agents/testing.md](../docs/agents/testing.md) first; workflow: the `write-test` skill (`../.agents/skills/write-test/SKILL.md`).

- Import topology fixtures from `test389.topologies`; `lib389.topologies` no longer exists.
- Every `test_*`-named function under `tests/suites/` (fixtures and helpers included) needs a `:id:` UUID unique within that directory; the gate recursively searches it as text, so never copy an ID into a comment or another docstring.
- New files follow the repository's `*_test.py` convention; pytest's defaults also collect `test_*.py`. Feature tests set `pytestmark = pytest.mark.tier1`.
- Python 3.8 floor (CI-enforced): no `match`, no `X | Y` unions, no bare `list[str]`.
- Only `tests/suites/` runs in CI; `stress/`, `perf/`, and `tickets/` do not.
- Cleanup: DEBUGGING-aware finalizers; always pass `request=` through to `create_topology`.
