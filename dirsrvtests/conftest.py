import subprocess
import logging
import pytest
import shutil
import glob
import os

from lib389.paths import Paths
from enum import Enum


pkgs = ['389-ds-base', 'nss', 'nspr', 'openldap', 'cyrus-sasl']
p = Paths()

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
        if p.asan_enabled and pytest_htmlpath is not None:
            # ASAN is enabled and an HTML report was requested,
            # rotate the ASAN logs so that only relevant logs are attached to the case in the report.
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
        for f in glob.glob(f'{p.run_dir}/ns-slapd-*san*'):
            with open(f) as asan_report:
                text = asan_report.read()
                extra.append(pytest_html.extras.text(text, name=os.path.basename(f)))
        report.extra = extra
