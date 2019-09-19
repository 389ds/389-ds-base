import subprocess
import logging
import pytest
import os

from lib389.paths import Paths
from enum import Enum

pkgs = ['389-ds-base', 'nss', 'nspr', 'openldap', 'cyrus-sasl']

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


@pytest.mark.optionalhook
def pytest_html_results_table_header(cells):
    cells.pop()


@pytest.mark.optionalhook
def pytest_html_results_table_row(report, cells):
    cells.pop()


@pytest.fixture(scope="function", autouse=True)
def log_test_name_to_journald(request):
    p = Paths()
    if p.with_systemd:
        def log_current_test():
            subprocess.Popen("echo $PYTEST_CURRENT_TEST | systemd-cat -t pytest", stdin=subprocess.PIPE, shell=True)

        log_current_test()
        request.addfinalizer(log_current_test)
        return log_test_name_to_journald
