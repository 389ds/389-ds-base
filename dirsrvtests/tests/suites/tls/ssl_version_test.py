import logging
import pytest
import os
from lib389.config import Encryption
from lib389.utils import ds_is_older
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_ssl_version_range(topo):
    """Specify a test case purpose or name here

    :id: bc400f54-3966-49c8-b640-abbf4fb2377e
    :customerscenario: True
        1. Get current default range
        2. Set sslVersionMin and verify it is applied after a restart
        3. Set sslVersionMax and verify it is applied after a restart
        4. Sanity test all the min/max versions
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    topo.standalone.enable_tls()
    enc = Encryption(topo.standalone)
    default_min = enc.get_attr_val_utf8('sslVersionMin')
    default_max = enc.get_attr_val_utf8('sslVersionMax')
    log.info(f"default min: {default_min} max: {default_max}")
    if DEBUGGING:
        topo.standalone.config.set('nsslapd-auditlog-logging-enabled',  'on')

    # Test that setting the min version is applied after a restart
    enc.replace('sslVersionMin',  default_max)
    enc.replace('sslVersionMax',  default_max)
    topo.standalone.restart()
    min = enc.get_attr_val_utf8('sslVersionMin')
    assert min == default_max

    # Test that setting the max version is applied after a restart
    enc.replace('sslVersionMin',  default_min)
    enc.replace('sslVersionMax',  default_min)
    topo.standalone.restart()
    max = enc.get_attr_val_utf8('sslVersionMax')
    assert max == default_min

    # 389-ds-base-1.4.3 == Fedora 32, 389-ds-base-1.4.4 == Fedora 33
    # Starting from Fedora 33, cryptographic protocols (TLS 1.0 and TLS 1.1) were moved to LEGACY
    # So we should not check for the policies with our DEFAULT crypro setup
    # https://fedoraproject.org/wiki/Changes/StrongCryptoSettings2
    if ds_is_older('1.4.4'):
        ssl_versions = [('sslVersionMin', ['TLS1.0', 'TLS1.1', 'TLS1.2', 'TLS1.0']),
                        ('sslVersionMax', ['TLS1.0', 'TLS1.1', 'TLS1.2'])]
    else:
        ssl_versions = [('sslVersionMin', ['TLS1.2']),
                        ('sslVersionMax', ['TLS1.2', 'TLS1.3'])]

    # Sanity test all the min/max versions
    for attr, versions in ssl_versions:
        for version in versions:
            # Test that the setting is correctly applied after a restart
            enc.replace(attr, version)
            topo.standalone.restart()
            current_val = enc.get_attr_val_utf8(attr)
            assert current_val == version


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
