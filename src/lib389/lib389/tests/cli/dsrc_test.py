# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest
import logging
import ldap
import sys

from lib389.cli_base.dsrc import dsrc_to_ldap

MAJOR, MINOR, _, _, _ = sys.version_info
if MAJOR >= 3:
    import configparser

log = logging.getLogger(__name__)

def write_inf(content):
    with open('/tmp/dsrc_test.inf', 'w') as f:
        f.write(content)

@pytest.mark.skipif(sys.version_info < (3,0), reason="requires python3")
def test_dsrc_single_section():
    # Write out inf.
    write_inf("""
[localhost]
    """)
    # Parse it and assert the content
    with pytest.raises(configparser.NoOptionError):
        i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)

    write_inf("""
[localhost]
uri = ldaps://localhost:636
    """)
    # Parse it and assert the content
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)
    assert(i == {
            'uri': 'ldaps://localhost:636',
            'basedn': None,
            'binddn': None,
            'saslmech': None,
            'tls_cacertdir': None,
            'tls_cert': None,
            'tls_key': None,
            'tls_reqcert': ldap.OPT_X_TLS_HARD,
            'starttls': False}
        )

    write_inf("""
[localhost]
uri = ldaps://localhost:636
basedn = dc=example,dc=com
binddn = cn=Directory Manager
starttls = true
    """)
    # Parse it and assert the content
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)
    assert(i == {
            'uri': 'ldaps://localhost:636',
            'basedn': 'dc=example,dc=com',
            'binddn': 'cn=Directory Manager',
            'saslmech': None,
            'tls_cacertdir': None,
            'tls_cert': None,
            'tls_key': None,
            'tls_reqcert': ldap.OPT_X_TLS_HARD,
            'starttls': True}
        )

@pytest.mark.skipif(sys.version_info < (3,0), reason="requires python3")
def test_dsrc_two_section():
    write_inf("""
[localhost]
uri = ldaps://localhost:636

[localhost2]
uri = ldaps://localhost:6362
    """)
    # Parse it and assert the content
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)
    assert(i == {
            'uri': 'ldaps://localhost:636',
            'basedn': None,
            'binddn': None,
            'saslmech': None,
            'tls_cacertdir': None,
            'tls_cert': None,
            'tls_key': None,
            'tls_reqcert': ldap.OPT_X_TLS_HARD,
            'starttls': False}
        )

    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost2', log)
    assert(i == {
            'uri': 'ldaps://localhost:6362',
            'basedn': None,
            'binddn': None,
            'saslmech': None,
            'tls_cacertdir': None,
            'tls_cert': None,
            'tls_key': None,
            'tls_reqcert': ldap.OPT_X_TLS_HARD,
            'starttls': False}
        )

    # Section doesn't exist
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost3', log)
    assert(i is None)

@pytest.mark.skipif(sys.version_info < (3,0), reason="requires python3")
def test_dsrc_reqcert():
    write_inf("""
[localhost]
uri = ldaps://localhost:636
tls_reqcert = invalid
    """)
    # Parse it and assert the content
    with pytest.raises(Exception):
        i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)

    write_inf("""
[localhost]
uri = ldaps://localhost:636
tls_reqcert = hard
    """)
    # Parse it and assert the content
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)
    assert(i['tls_reqcert'] == ldap.OPT_X_TLS_HARD)

    write_inf("""
[localhost]
uri = ldaps://localhost:636
tls_reqcert = allow
    """)
    # Parse it and assert the content
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)
    assert(i['tls_reqcert'] == ldap.OPT_X_TLS_ALLOW)

    write_inf("""
[localhost]
uri = ldaps://localhost:636
tls_reqcert = never
    """)
    # Parse it and assert the content
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)
    assert(i['tls_reqcert'] == ldap.OPT_X_TLS_NEVER)

@pytest.mark.skipif(sys.version_info < (3,0), reason="requires python3")
def test_dsrc_saslmech():
    write_inf("""
[localhost]
uri = ldaps://localhost:636
saslmech = INVALID_MECH
    """)
    # Parse it and assert the content
    with pytest.raises(Exception):
        i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)

    write_inf("""
[localhost]
uri = ldaps://localhost:636
saslmech = EXTERNAL
    """)
    # Parse it and assert the content
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)
    assert(i['saslmech'] == 'EXTERNAL')

    write_inf("""
[localhost]
uri = ldaps://localhost:636
saslmech = PLAIN
    """)
    # Parse it and assert the content
    i = dsrc_to_ldap('/tmp/dsrc_test.inf', 'localhost', log)
    assert(i['saslmech'] == 'PLAIN')
