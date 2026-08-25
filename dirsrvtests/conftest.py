# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import subprocess
import logging
import pytest
import shutil
import glob
import ldap
import sys
import os
import gzip

from .report import getReport
from .sanitizers import SANITIZERS, find_library
from lib389 import DirSrv
from lib389.paths import Paths
from enum import Enum

if "WEBUI" in os.environ:
    from slugify import slugify
    from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'lib'))

pkgs = ['389-ds-base', 'nss', 'nspr', 'openldap', 'cyrus-sasl']
p = Paths()
log = logging.getLogger(__name__)


class FIPSState(Enum):
    ENABLED = 'enabled'
    DISABLED = 'disabled'
    NOT_AVAILABLE = 'not_available'

    def __unicode__(self):
        return self.value

    def __str__(self):
        return self.value


def get_rpm_version(pkg):
    try:
        result = subprocess.check_output(['rpm', '-q', '--queryformat',
                                          '%{VERSION}-%{RELEASE}', pkg])
    except:
        result = b"not installed"

    return result.decode('utf-8')


def is_fips():
    # Are we running in FIPS mode?
    if not os.path.exists('/proc/sys/crypto/fips_enabled'):
        return FIPSState.NOT_AVAILABLE
    state = None
    with open('/proc/sys/crypto/fips_enabled', 'r') as f:
        state = f.readline().strip()
    if state == '1':
        return FIPSState.ENABLED
    else:
        return FIPSState.DISABLED


def pytest_addoption(parser):
    parser.addoption(
        "--sanitizer",
        action="store",
        default=None,
        choices=["lsan", "tsan"],
        help="Inject a runtime sanitizer into ns-slapd via LD_PRELOAD. "
             "Requires host setup: see dirsrvtests/sanitizers/README.md"
    )


@pytest.fixture(autouse=True, scope="session")
def sanitizer_setup(request):
    """Inject LSan/TSan into ns-slapd via LD_PRELOAD when --sanitizer is given.

    For systemd: writes a drop-in conf to dirsrv@<inst>.service.d/ so that
    Environment=LD_PRELOAD and sanitizer options are picked up on start.

    For non-systemd (containers, prefix builds): sets the variables on
    os.environ and routes through the ASAN code path in DirSrv.start()
    so they reach the ns-slapd subprocess.

    Collects sanitizer reports after each stop and cleans up at session end.
    """
    name = request.config.getoption("--sanitizer")
    if name is None:
        yield None
        return

    config = SANITIZERS[name]
    library = find_library(config['lib_glob'])
    if not library:
        pytest.skip("%s not found (glob: %s). Install: dnf install %s"
                    % (name.upper(), config['lib_glob'], config['package']))

    log.info("Sanitizer %s enabled (library: %s)", name, library)
    log.info("Reports will appear in pytest output and "
             "{run_dir}/ns-slapd-{instance}.%s.<pid> files. "
             "No reports? Check: sysctl fs.suid_dumpable=1, setenforce 0", name)

    # Monkeypatch DirSrv.start() and stop() to inject the sanitizer library.
    dropin_files = {}  # {dropin_path: dropin_dir} for systemd cleanup
    _orig_start = DirSrv.start
    _orig_stop = DirSrv.stop

    def _patched_start(self, timeout=120, post_open=True):
        log_path = os.path.join(
            self.ds_paths.run_dir, "ns-slapd-%s.%s" % (self.serverid, name))

        sanitizer_env = {
            'LD_PRELOAD': library,
            config['env_var']: '%s:log_path=%s' % (config['options'], log_path),
        }

        if self.with_systemd_running():
            # Use a systemd drop-in — this overrides the jemalloc LD_PRELOAD
            # from custom.conf (drop-ins are applied alphabetically and
            # sanitizer.conf sorts after custom.conf).
            dropin_dir = '/etc/systemd/system/dirsrv@%s.service.d' % self.serverid
            dropin_path = os.path.join(dropin_dir, 'sanitizer.conf')
            if dropin_path not in dropin_files:
                try:
                    os.makedirs(dropin_dir, exist_ok=True)
                    with open(dropin_path, 'w') as f:
                        f.write("[Service]\n")
                        for k, v in sanitizer_env.items():
                            f.write("Environment=%s=%s\n" % (k, v))
                    subprocess.check_call(['systemctl', 'daemon-reload'])
                    dropin_files[dropin_path] = dropin_dir
                except (PermissionError, subprocess.CalledProcessError) as e:
                    log.error("Cannot configure systemd drop-in for %s: %s",
                              self.serverid, e)
        else:
            # For non-systemd (containers, prefix builds): put sanitizer
            # vars into os.environ and force the ASAN code path in
            # DirSrv.start() so that env.update(os.environ) picks them up.
            # This avoids adding test-only hooks to the production lib389.
            saved_asan = self.ds_paths.asan_enabled
            self.ds_paths.asan_enabled = True
            os.environ.update(sanitizer_env)
            try:
                _orig_start(self, timeout, post_open)
            finally:
                self.ds_paths.asan_enabled = saved_asan
                for key in sanitizer_env:
                    os.environ.pop(key, None)
            return

        _orig_start(self, timeout, post_open)

    def _patched_stop(self, timeout=120):
        try:
            _orig_stop(self, timeout)
        finally:
            pattern = os.path.join(
                self.ds_paths.run_dir,
                "ns-slapd-%s.%s*" % (self.serverid, name))
            for report_path in sorted(glob.glob(pattern)):
                try:
                    with open(report_path, 'r', errors='replace') as f:
                        content = f.read().strip()
                    if content and ('ERROR:' in content or 'SUMMARY:' in content):
                        log.warning("Sanitizer report (%s):\n%s",
                                    os.path.basename(report_path), content)
                except OSError:
                    pass

    DirSrv.start = _patched_start
    DirSrv.stop = _patched_stop

    yield name

    # Teardown: restore originals and clean up.
    DirSrv.start = _orig_start
    DirSrv.stop = _orig_stop

    for dropin_path, dropin_dir in dropin_files.items():
        try:
            os.remove(dropin_path)
            # Remove the directory only if we left it empty
            if not os.listdir(dropin_dir):
                os.rmdir(dropin_dir)
        except OSError as e:
            log.warning("Cleanup failed for %s: %s", dropin_path, e)
    if dropin_files:
        try:
            subprocess.check_call(['systemctl', 'daemon-reload'])
        except subprocess.CalledProcessError:
            pass


@pytest.fixture(autouse=True)
def _environment(request):
    if "_metadata" in dir(request.config):
        for pkg in pkgs:
            request.config._metadata[pkg] = get_rpm_version(pkg)
        request.config._metadata['FIPS'] = is_fips()


def pytest_cmdline_main(config):
    logging.basicConfig(level=logging.DEBUG)


def pytest_report_header(config):
    header = ""
    for pkg in pkgs:
        header += "%s: %s\n" % (pkg, get_rpm_version(pkg))
    header += "FIPS: %s" % is_fips()
    sanitizer = config.getoption("--sanitizer", default=None)
    if sanitizer:
        header += "\nSanitizer: %s (runtime injection via LD_PRELOAD)" % sanitizer
    return header


@pytest.fixture(scope="function", autouse=True)
def log_test_name_to_journald(request):
    if p.with_systemd:
        def log_current_test():
            subprocess.Popen("echo $PYTEST_CURRENT_TEST | systemd-cat -t pytest", stdin=subprocess.PIPE, shell=True)

        log_current_test()
        request.addfinalizer(log_current_test)
        return log_test_name_to_journald


@pytest.fixture(scope="function", autouse=True)
def rotate_xsan_logs(request):
    # Do we have a pytest-html installed?
    pytest_html = request.config.pluginmanager.getplugin('html')
    if pytest_html is not None:
        # We have it installed, but let's check if we actually use it (--html=report.html)
        pytest_htmlpath = request.config.getoption('htmlpath')
        sanitizer_active = p.asan_enabled or request.config.getoption('--sanitizer', default=None)
        if sanitizer_active and pytest_htmlpath is not None:
            # A sanitizer is active and an HTML report was requested,
            # rotate the logs so that only relevant logs are attached to the case in the report.
            xsan_logs_dir = f'{p.run_dir}/bak'
            if not os.path.exists(xsan_logs_dir):
                os.mkdir(xsan_logs_dir)
            else:
                for f in glob.glob(f'{p.run_dir}/ns-slapd-*san*'):
                    shutil.move(f, xsan_logs_dir)
            return rotate_xsan_logs


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    pytest_html = item.config.pluginmanager.getplugin('html')
    outcome = yield
    report = outcome.get_result()
    extra = getattr(report, 'extra', [])
    if report.when == 'call' and pytest_html is not None:
        pytest_htmlpath = item.config.getoption('htmlpath')
        if pytest_htmlpath is not None:
            for f in glob.glob(f'{p.run_dir}/ns-slapd-*san*'):
                with open(f) as asan_report:
                    text = asan_report.read()
                    extra.append(pytest_html.extras.text(text, name=os.path.basename(f)))
            for f in glob.glob(f'{p.log_dir.split("/slapd",1)[0]}/*/*'):
                if not os.path.isfile(f):
                    continue
                if f.endswith('gz'):
                    with gzip.open(f, 'rb') as dirsrv_log:
                        text = dirsrv_log.read()
                        log_name = os.path.basename(f)
                        instance_name = os.path.basename(os.path.dirname(f)).split("slapd-",1)[1]
                        extra.append(pytest_html.extras.text(text, name=f"{instance_name}-{log_name}"))
                elif 'rotationinfo' not in f:
                    with open(f, errors='ignore') as dirsrv_log:
                        text = dirsrv_log.read()
                        log_name = os.path.basename(f)
                        instance_name = os.path.basename(os.path.dirname(f)).split("slapd-",1)[1]
                        extra.append(pytest_html.extras.text(text, name=f"{instance_name}-{log_name}"))
            report.extras = extra

    # Make a screenshot if WebUI test fails
    if call.when == "call" and "WEBUI" in os.environ:
        if call.excinfo is not None and "page" in item.funcargs:
            page = item.funcargs["page"]
            screenshot_dir = Path(".playwright-screenshots")
            screenshot_dir.mkdir(exist_ok=True)
            page.screenshot(path=str(screenshot_dir / f"{slugify(item.nodeid)}.png"))


def pytest_exception_interact(node, call, report):
    if report.failed:
        # call.excinfo contains an ExceptionInfo instance
        if call.excinfo.type is ldap.SERVER_DOWN:
            report.sections.extend(getReport())
