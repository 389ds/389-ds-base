import subprocess
import logging
import pytest

pkgs = ['389-ds-base', 'nss', 'nspr', 'openldap', 'cyrus-sasl']


def get_rpm_version(pkg):
    try:
        result = subprocess.check_output(['rpm', '-q', '--queryformat',
                                          '%{VERSION}-%{RELEASE}', pkg])
    except:
        result = b"not installed"

    return result.decode('utf-8')


def is_fips():
    # Are we running in FIPS mode?
    with open('/proc/sys/crypto/fips_enabled', 'r') as f:
        return f.readline()


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
        header += pkg + ": " + get_rpm_version(pkg) + "\n"
    header += "FIPS: " + is_fips()
    return header


@pytest.mark.optionalhook
def pytest_html_results_table_header(cells):
    cells.pop()


@pytest.mark.optionalhook
def pytest_html_results_table_row(report, cells):
    cells.pop()
