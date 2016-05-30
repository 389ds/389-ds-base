# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.utils import *


def test_normalizeDN():
    test = [
        (r'dc=example,dc=com', r'dc=example,dc=com'),
        (r'dc=example, dc=com', r'dc=example,dc=com'),
        (r'cn="dc=example,dc=com",cn=config',
         'cn=dc\\=example\\,dc\\=com,cn=config'),
    ]
    for k, v in test:
        r = normalizeDN(k)
        assert r == v, "Mismatch %r vs %r" % (r, v)


def test_escapeDNValue():
    test = [(r'"dc=example, dc=com"', r'\"dc\=example\,\ dc\=com\"')]
    for k, v in test:
        r = escapeDNValue(k)
        assert r == v, "Mismatch %r vs %r" % (r, v)


def test_escapeDNFiltValue():
    test = [(r'"dc=example, dc=com"',
             '\\22dc\\3dexample\\2c\\20dc\\3dcom\\22')]
    for k, v in test:
        r = escapeDNFiltValue(k)
        assert r == v, "Mismatch %r vs %r" % (r, v)

#
# socket related functions
#
import socket


def test_isLocalHost():
    test = [
        ('localhost', True),
        ('localhost.localdomain', True),
        (socket.gethostname(), True),
        ('www.google.it', False)]
    for k, v in test:
        r = isLocalHost(k)
        assert r == v, "Mismatch %r vs %r on %r" % (r, v, k)


def test_update_newhost_with_fqdn():
    # This test doesn't account for platform where localhost.localdomain isn't
    # the name ....
    test = [
        ({'hostname': 'localhost'}, (['localhost.localdomain', 'localhost'], True)),
        ({'hostname': 'remote'}, (['remote'], False)),
    ]
    for k, v in test:
        old = k.copy()
        expected_host, expected_r = v
        r = update_newhost_with_fqdn(k)
        assert expected_r == r, "Mismatch %r vs %r for %r" % (
            r, expected_r, old)
        assert k['hostname'] in expected_host, "Mismatch %r vs %r for %r" % (
            k['hostname'], expected_host, old)


def test_formatInfData():
    ret = formatInfData({
        'hostname': 'localhost.localdomain',
        'user-id': 'dirsrv',
        'group-id': 'dirsrv',
        'ldap-port': 12345,
        'root-dn': 'cn=directory manager',
        'root-pw': 'password',
        'server-id': 'dirsrv',
        'suffix': 'o=base1',
        'strict_hostname_checking': True,
    })
    log.info("content: %r" % ret)


def test_formatInfData_withadmin():
    instance_params = {
        'hostname': 'localhost.localdomain',
        'user-id': 'dirsrv',
        'group-id': 'dirsrv',
        'ldap-port': 12346,
        'root-dn': 'cn=directory manager',
        'root-pw': 'password',
        'server-id': 'dirsrv',
        'suffix': 'o=base1',
        'strict_hostname_checking': True,
        }
    admin_params = {
        'have_admin': True,
        'create_admin': True,
        'admin_domain': 'example.com',
        'cfgdshost': 'localhost',
        'cfgdsport': 12346,
        'cfgdsuser': 'admin',
        'cfgdspwd': 'admin'}
    instance_params.update(admin_params)
    ret = formatInfData(instance_params)
    log.info("content: %r" % ret)


def test_formatInfData_withconfigserver():
    instance_params = {
        'hostname': 'localhost.localdomain',
        'user-id': 'dirsrv',
        'group-id': 'dirsrv',
        'ldap-port': 12346,
        'root-dn': 'cn=directory manager',
        'root-pw': 'password',
        'server-id': 'dirsrv',
        'suffix': 'o=base1',
        'strict_hostname_checking': True,
        }
    admin_params = {
        'have_admin': True,
        'cfgdshost': 'localhost',
        'cfgdsport': 12346,
        'cfgdsuser': 'admin',
        'cfgdspwd': 'admin',
        'admin_domain': 'example.com'}
    instance_params.update(admin_params)
    ret = formatInfData(instance_params)
    log.info("content: %r" % ret)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
