# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import pytest
from lib389.topologies import topology_st as topo
from lib389.config import Encryption

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_tls_renegotiation_config(topo):
    """Test nsTLSAllowClientRenegotiation setting can be configured

    :id: 54e5be6a-201d-457c-9b65-59ced0a5069e
    :setup: Standalone instance with TLS enabled
    :steps:
        1. Enable TLS on standalone instance
        2. Set nsTLSAllowClientRenegotiation to off and verify
        3. Set nsTLSAllowClientRenegotiation to on and verify
        4. Set nsTLSAllowClientRenegotiation to invalid value and verify it's accepted
    :expectedresults:
        1. TLS is enabled successfully
        2. Setting is updated to off
        3. Setting is updated to on
        4. Setting is updated to invalid value (server accepts it)
    """

    log.info('Enabling TLS on standalone instance')
    topo.standalone.enable_tls()
    enc = Encryption(topo.standalone)

    log.info('Setting nsTLSAllowClientRenegotiation to off')
    enc.replace('nsTLSAllowClientRenegotiation', 'off')
    topo.standalone.restart()

    log.info('Verifying nsTLSAllowClientRenegotiation is set to off')
    value = enc.get_attr_val_utf8('nsTLSAllowClientRenegotiation')
    assert value == 'off', f'Expected off, got {value}'
    log.info('Successfully set nsTLSAllowClientRenegotiation to off')

    log.info('Setting nsTLSAllowClientRenegotiation to on')
    enc.replace('nsTLSAllowClientRenegotiation', 'on')
    topo.standalone.restart()

    log.info('Verifying nsTLSAllowClientRenegotiation is set to on')
    value = enc.get_attr_val_utf8('nsTLSAllowClientRenegotiation')
    assert value == 'on', f'Expected on, got {value}'
    log.info('Successfully set nsTLSAllowClientRenegotiation to on')

    log.info('Setting nsTLSAllowClientRenegotiation to invalid value')
    enc.replace('nsTLSAllowClientRenegotiation', 'invalid')
    topo.standalone.restart()

    log.info('Verifying nsTLSAllowClientRenegotiation accepts invalid value')
    value = enc.get_attr_val_utf8('nsTLSAllowClientRenegotiation')
    assert value == 'invalid', f'Expected invalid, got {value}'
    log.info('Server accepted invalid value for nsTLSAllowClientRenegotiation')


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
