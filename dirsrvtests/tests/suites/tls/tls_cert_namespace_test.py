# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest

from glob import glob
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.paths import Paths

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

p = Paths()


@pytest.mark.ds50889
@pytest.mark.bz1638875
@pytest.mark.skipif(p.with_systemd == False, reason='Will not run without systemd')
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_pem_cert_in_private_namespace(topology_st):
    """Test if certificates are present in private /tmp namespace

    :id: 01bc27d0-6368-496a-9724-7fe1e8fb239b
    :setup: Standalone instance
    :steps:
         1. Create DS instance
         2. Enable TLS
         3. Check if value of PrivateTmp == yes
         4. Check if pem certificates are present in private /tmp
         5. Check if pem certificates are not present in /etc/dirsrv/instance
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
         5. Success
    """

    PEM_CHECK = ['Self-Signed-CA.pem', 'Server-Cert-Key.pem', 'Server-Cert.pem']
    PRIVATE_TMP = 'PrivateTmp=yes'

    standalone = topology_st.standalone

    log.info('Enable TLS')
    standalone.enable_tls()

    log.info('Checking PrivateTmp value')
    cmdline = ['systemctl', 'show', '-p', 'PrivateTmp', 'dirsrv@{}.service'.format(standalone.serverid)]
    log.info('Command used : %s' % format_cmd_list(cmdline))
    result = subprocess.check_output(cmdline)
    assert PRIVATE_TMP in ensure_str(result)

    log.info('Check files in private /tmp')
    cert_path = glob('/tmp/systemd-private-*-dirsrv@{}.service-*/tmp/slapd-{}/'.format(standalone.serverid,
                                                                                       standalone.serverid))
    assert os.path.exists(cert_path[0])
    for item in PEM_CHECK:
        log.info('Check that {} is present in private /tmp'.format(item))
        assert os.path.exists(cert_path[0] + item)

    log.info('Check instance cert directory')
    cert_path = '/etc/dirsrv/slapd-{}/'.format(standalone.serverid)
    assert os.path.exists(cert_path)
    for item in PEM_CHECK:
        log.info('Check that {} is not present in /etc/dirsrv/slapd-{}/ directory'.format(item, standalone.serverid))
        assert not os.path.exists(cert_path + item)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)