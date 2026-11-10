# Runtime Sanitizer Support for 389-ds-base Tests

Inject LeakSanitizer (LSan) or ThreadSanitizer (TSan) into `ns-slapd`
during pytest runs **without rebuilding** with `--enable-asan` or `--enable-tsan`.

Under systemd, this works by writing a drop-in to
`dirsrv@<instance>.service.d/sanitizer.conf` that sets `LD_PRELOAD` and
sanitizer options. For prefix builds and containers (no systemd), the
fixture injects the same variables directly into the `ns-slapd` subprocess.

## Quick Start

```bash
# One-time host setup (as root):
sudo dirsrvtests/sanitizers/setup_host.sh

# Run tests with leak detection:
pytest --sanitizer=lsan -xvs dirsrvtests/tests/suites/syncrepl_plugin/

# Run tests with thread sanitizer:
pytest --sanitizer=tsan -xvs dirsrvtests/tests/suites/replication/
```

## Host Prerequisites

### Packages

```bash
dnf install liblsan    # for --sanitizer=lsan
dnf install libtsan    # for --sanitizer=tsan
```

### sysctl

LSan/TSan use `ptrace` to suspend threads for leak/race scanning.
Two sysctl settings are needed:

```bash
sysctl -w fs.suid_dumpable=1          # keep process dumpable after setuid
sysctl -w kernel.yama.ptrace_scope=0  # allow same-UID ptrace
# Persist across reboots:
cat > /etc/sysctl.d/99-ds-sanitizers.conf <<'EOF'
fs.suid_dumpable = 1
kernel.yama.ptrace_scope = 0
EOF
```

`fs.suid_dumpable=1` is critical — ns-slapd starts as root and drops
to `dirsrv` via `setuid()`, which clears the dumpable flag by default.
Without this setting, the sanitizer's ptrace calls fail silently and
no reports are generated.

### SELinux

The `dirsrv_t` domain needs `ptrace` (thread scanning) and `execmem`
(sanitizer trampolines) permissions that the default policy does not grant.

The `setup_host.sh` script installs a targeted policy module
(`ds_sanitizer.te`) that adds only these two permissions:

```bash
sudo dirsrvtests/sanitizers/setup_host.sh   # compiles and loads the module
semodule -l | grep ds_sanitizer              # verify it's loaded
semodule -r ds_sanitizer                     # remove when done
```

If the module isn't enough (no reports, ns-slapd crashes on startup),
fall back to permissive mode while you collect the actual AVC denials:

```bash
setenforce 0                    # temporary, re-enable with: setenforce 1
ausearch -m avc -ts recent      # find what's being denied
```

## How It Works

1. `pytest --sanitizer=lsan` activates the `sanitizer_setup` fixture (session-scoped).
2. Before each instance starts, the fixture injects `LD_PRELOAD` and `LSAN_OPTIONS`:
   - **Under systemd:** writes a drop-in file to
     `/etc/systemd/system/dirsrv@{instance}.service.d/sanitizer.conf`
     with `Environment=LD_PRELOAD=...` directives (overrides the jemalloc
     `LD_PRELOAD` from `custom.conf` because `sanitizer.conf` sorts after it).
   - **Under containers or prefix installs:** sets the variables on
     `os.environ` and routes through the ASAN code path in `DirSrv.start()`
     so they reach the `ns-slapd` subprocess.
3. `ns-slapd` starts with the sanitizer library injected.
4. On clean shutdown, sanitizer reports are written to
   `{run_dir}/ns-slapd-{instance}.{lsan,tsan}.<pid>`.
5. The fixture collects reports and logs them to pytest output.
   The existing `*san*` glob in `conftest.py` also attaches them
   to the pytest-html report.
6. On session teardown, drop-in files are removed and `DirSrv` is restored.

## Output

Sanitizer reports appear in three places:
- **pytest console output** — logged as warnings after each instance stop
- **Report files** — `{run_dir}/ns-slapd-{instance}.{lsan,tsan}.<pid>`
- **pytest-html report** — if `--html=report.html` is used

## Troubleshooting

**No reports after test run**

Check in order:

1. SELinux denials (most common cause):
```bash
ausearch -m avc -ts recent | grep dirsrv   # look for ptrace/execmem denials
semodule -l | grep ds_sanitizer             # policy module loaded?
```
If the policy module is missing, run `setup_host.sh`. As a quick test,
try `setenforce 0` — if reports appear, the issue is SELinux.

2. sysctl prerequisites (run `setup_host.sh` if not done):
```bash
sysctl fs.suid_dumpable          # must be 1
sysctl kernel.yama.ptrace_scope  # must be 0
```

**"FATAL: StopTheWorld failed" or ptrace errors**

The sanitizer needs `ptrace` to stop threads for scanning. This requires
`fs.suid_dumpable=1` because ns-slapd drops from root to `dirsrv` via
`setuid()`, which clears the dumpable flag by default.

```bash
sudo sysctl -w fs.suid_dumpable=1
```

**Reports are empty (only thread listing, no leaks/races)**

The report filtering only shows files containing `ERROR:` or `SUMMARY:`.
Raw report files are still available at `{run_dir}/ns-slapd-*.{lsan,tsan}.*`.

## Limitations

- **Not a substitute for ASAN builds.** `LD_PRELOAD`-based LSan only detects
  memory leaks (unreachable allocations at exit). It cannot detect
  use-after-free, buffer overflows, or other memory errors — those require
  compile-time instrumentation (`--enable-asan`).
- **TSan via `LD_PRELOAD` has limitations.** Full TSan requires compile-time
  instrumentation. Runtime injection may miss some races.
- **Clean shutdown required.** LSan reports leaks at `exit()`. If `ns-slapd`
  is killed with `SIGKILL`, no report is generated.
- **Performance overhead.** LSan adds ~2x slowdown, TSan adds ~5-15x.
  Adjust test timeouts accordingly.

## Available Sanitizers

| Flag              | What it detects       | Library    | Overhead |
|-------------------|-----------------------|------------|----------|
| `--sanitizer=lsan`| Memory leaks          | liblsan.so | ~2x      |
| `--sanitizer=tsan`| Data races, deadlocks | libtsan.so | ~5-15x   |

For full ASAN/MSAN support, rebuild with `--enable-asan` / `--enable-msan`.
