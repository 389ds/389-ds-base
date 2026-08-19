# Container recipe — the pytest.yml path, end to end

**Reference material — not a script to run on a development machine.** Execute these
commands only through the environment's build/test skill or on a Linux host you know
is configured for docker-driven CI work (SKILL.md step 1); on anything else they fail
or corrupt the setup.

Every command in a code block is copied character-exact from .github/workflows/pytest.yml
(differences in lmdbpytest.yml are noted). CI splits this into two jobs: "Build" runs
*inside* the quay.io/389ds/ci-images:test image as a GitHub Actions container job;
"BDB Test" drives docker from the runner. Locally one systemd container serves both
stages; `podman` accepts the same flags wherever `sudo docker` appears.

## Stage 1 — start the systemd test container (run from the repo root)

    CID=$(sudo docker run -d -h server.example.com --ulimit core=-1 --cap-add=SYS_PTRACE --privileged --rm --shm-size=4gb -v ${PWD}:/workspace quay.io/389ds/ci-images:test)

Flag by flag: `-d` detached; `-h server.example.com` the hostname tests expect;
`--ulimit core=-1` unlimited core dumps; `--cap-add=SYS_PTRACE` lets debuggers and
sanitizers attach; `--privileged` needed for systemd and instance management; `--rm`
autodelete on stop; `--shm-size=4gb` large /dev/shm for the databases;
`-v ${PWD}:/workspace` mounts the checkout. The image boots systemd — wait for it
(verbatim loop; it prints failures until ready):

    until sudo docker exec $CID sh -c "systemctl is-system-running"
    do
      echo "Waiting for container to be ready..."
    done

## Stage 2 — build the RPMs

CI runs the next two command strings in its separate Build job, directly inside the
image. Locally, exec them in the container — the command strings are verbatim, the
`sudo docker exec $CID sh -c "cd /workspace && ..."` wrapping is the local adaptation:

    git config --global --add safe.directory "$GITHUB_WORKSPACE"
    SKIP_AUDIT_CI=1 make -f rpm.mk dist-bz2 rpms

- safe.directory comes FIRST (use `/workspace` for the path inside the container):
  `dist-bz2` shells out to `git ls-files`, and git-as-root refuses a repo owned by
  another user — the build "succeeds" while the tarball is silently empty.
- `SKIP_AUDIT_CI=1` skips only the `npx --yes audit-ci` step of rpm.mk's
  install-node-modules target, and only a NON-EMPTY value skips it
  (`SKIP_AUDIT_CI=` still audits — the guard is GNU make `ifndef`).
- `dist-bz2` tars `$(git ls-files)` plus vendor/ and cockpit_dist/ — uncommitted
  changes are NOT in that tarball; it also vendors the cargo dependencies.
- `rpms` rsyncs the whole working tree (minus node_modules, dist, .git, rpmbuild)
  into the source tarball it builds from, so uncommitted changes ARE built — this
  is the path that tests your patch. Alone it skips cargo vendoring, which is why
  CI always runs both targets together, in this order.

Verify: `ls dist/rpms/*.rpm` — the RPMs exist and are newer than your edit.

## Stage 3 — install the RPMs and prepare the container (verbatim)

    sudo docker exec $CID sh -c "dnf install -y dist/rpms/*rpm"
    export PASSWD=$(openssl rand -base64 32)
    sudo docker exec $CID sh -c "echo \"${PASSWD}\" | passwd --stdin root"
    sudo docker exec $CID sh -c "systemctl start dbus.service"
    sudo docker exec $CID sh -c "systemctl enable --now cockpit.socket"
    sudo docker exec $CID sh -c "mkdir -p /workspace/assets/cores && chmod 777 /workspace{,/assets{,/cores}}"
    sudo docker exec $CID sh -c "echo '/workspace/assets/cores/core.%e.%P' > /proc/sys/kernel/core_pattern"

`dist/rpms/*rpm` is relative — it resolves against the container's working directory,
the /workspace mount; if the glob misses, use `/workspace/dist/rpms/*rpm`. The root
password and cockpit.socket exist for the webui suite; the core_pattern line drops any
ns-slapd core into assets/cores/ on the host — check that directory after a crash.

## Stage 4 — run the suite: the BDB skip guard, quoted verbatim

    if sudo docker exec $CID sh -c "test -f /usr/lib64/dirsrv/librobdb.so"
    then
        echo "Tests skipped because read-only Berkeley Database is installed." > pytest.html
        echo "<?xml version="1.0" encoding="utf-8"?>'Tests skipped because read-only Berkeley Database is installed.'" > pytest.xml
    else
        sudo docker exec -e WEBUI=1 -e NSSLAPD_DB_LIB=bdb -e DEBUG=pw:api -e PASSWD="${PASSWD}" -e GSSAPI_ACK=1 $CID py.test  --suppress-no-test-exit-code  -m "not flaky" --junit-xml=pytest.xml --html=pytest.html --browser=firefox --browser=chromium -v dirsrvtests/tests/suites/${{ matrix.suite }}
    fi

Consequence of the guard: when `/usr/lib64/dirsrv/librobdb.so` (read-only BDB) is
present in the container, the "BDB Test" job writes placeholder pytest.html/pytest.xml
and reports GREEN having run ZERO tests — green does not prove the suite ran under
BDB. Verifying a backend change for real means building writable BDB and running the
suite once per backend: see the touch-backend skill. lmdbpytest.yml has no guard; its
py.test line is identical except `-e NSSLAPD_DB_LIB=mdb` and no if/else.
`${{ matrix.suite }}` is one directory name under dirsrvtests/tests/suites (CI runs
one job per directory; replication is split one job per test file).

## The py.test line, fragment by fragment — and what to drop locally

| Fragment | Why CI has it | Locally |
|---|---|---|
| `-e WEBUI=1` | enables the Cockpit/Playwright webui suite + failure screenshots | drop |
| `-e NSSLAPD_DB_LIB=bdb` | backend under test; unset defaults to mdb | keep, set explicitly |
| `-e DEBUG=pw:api` | Playwright debug logging (NOT the test tree's DEBUGGING var) | drop |
| `-e PASSWD="${PASSWD}"` | root password the webui suite logs in with | drop |
| `-e GSSAPI_ACK=1` | un-skips GSSAPI suites, which rewrite the host krb5 realm | drop |
| `--suppress-no-test-exit-code` | pytest-custom_exit_code plugin — CI image only | drop |
| `-m "not flaky"` | skips flaky-marked tests; CI's only marker filter | keep |
| `--junit-xml=pytest.xml` | JUnit report (core pytest) | optional |
| `--html=pytest.html` | pytest-html plugin — CI image only | drop |
| `--browser=firefox --browser=chromium` | pytest-playwright plugin — CI image only | drop |
| `-v` | verbose per-test output | keep |

None of the three CI-only plugins are in dirsrvtests/requirements.txt (pytest,
pytest-libfaketime, slugify). Minimal local line:

    sudo docker exec -e NSSLAPD_DB_LIB=mdb $CID py.test -m "not flaky" -v dirsrvtests/tests/suites/<suite>

Backend changes: run it twice, `-e NSSLAPD_DB_LIB=bdb` then `-e NSSLAPD_DB_LIB=mdb`.

## Teardown

    sudo docker stop $CID

(`--rm` on the run line removes the stopped container.)
