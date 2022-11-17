# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
from lib389.topologies import topology_st as topo
from lib389.nss_ssl import NssSsl

log = logging.getLogger(__name__)


def test_tls_command_returns_error_text(topo):
    """CLI commands that called certutil should return the error text from
    certutil when something goes wrong, and not the system error code number.

    :id: 7f0c28d0-6e13-4ca4-bec2-4586d56b73f6
    :setup: Standalone Instance
    :steps:
        1. Issue invalid "generate key and cert" command, and error text is returned
        2. Issue invalid "delete cert" command, and error text is returned
        3. Issue invalid "import ca cert" command, and error text is returned
        4. Issue invalid "import server cert" command, and error text is returned
        5. Issue invalid "import key and server cert" command, and error text is returned
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    # dsctl localhost tls generate-server-cert-csr -s "bad"
    tls = NssSsl(dirsrv=topo.standalone)
    try:
        tls.create_rsa_key_and_csr([], "bad")
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'improperly formatted name' in str(e)

   # dsctl localhost tls remove-cert
    try:
        tls.del_cert("bad")
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'could not find certificate named' in str(e)

    # dsctl localhost tls import-ca
    try:
        invalid_file = topo.standalone.confdir + '/dse.ldif'
        tls.add_cert(nickname="bad", input_file=invalid_file)
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'error converting ascii to binary' in str(e)

    # dsctl localhost tls import-server-cert
    try:
        invalid_file = topo.standalone.confdir + '/dse.ldif'
        tls.import_rsa_crt(crt=invalid_file)
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'error converting ascii to binary' in str(e)

    # dsctl localhost tls import-server-key-cert
    try:
        invalid_file = topo.standalone.confdir + '/dse.ldif'
        tls.add_server_key_and_cert(invalid_file,  invalid_file)
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'unable to load private key' in str(e)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

