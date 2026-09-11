# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

"""
This is the config file for keywords test scripts.

"""

import socket

import pytest

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.user import  UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnit, OrganizationalUnits
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain


@pytest.fixture(scope="module", autouse=True)
def configure_ipv4_mapped_hosts(request, topo):
    """Register the IPv4-mapped IPv6 address in /etc/hosts and remove it on teardown.

    When DS binds on :: (dual-stack), IPv4 clients appear as ::ffff:x.x.x.x
    at the socket layer. NSPR's PR_GetHostByAddr calls gethostbyaddr_r with
    AF_INET6, and glibc-2.34 (RHEL 9.x) does not auto-unmap IPv4-mapped
    addresses, so the plain IPv4 entry in /etc/hosts is not found and the
    DNS keyword ACI evaluation fails with INSUFFICIENT_ACCESS.
    Adding the ::ffff:<ip> form ensures the reverse lookup succeeds.
    """
    host = topo.standalone.host
    ip = socket.gethostbyname(host)
    mapped = f"::ffff:{ip}"

    with open("/etc/hosts") as f:
        already_present = any(
            line.split()[0] == mapped and host in line.split()
            for line in f
            if line.split() and not line.startswith('#')
        )

    if already_present:
        yield
        return

    entry = f"{mapped}   {host}\n"
    with open("/etc/hosts", "a") as f:
        f.write(entry)

    def remove_entry():
        with open("/etc/hosts") as f:
            lines = f.readlines()
        with open("/etc/hosts", "w") as f:
            f.writelines(line for line in lines if line != entry)

    request.addfinalizer(remove_entry)

    yield


@pytest.fixture(scope="function")
def aci_of_user(request, topo):
    """
    Removes and Restores ACIs after the test.
    """
    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals_utf8('aci')

    def finofaci():
        """
        Removes and Restores ACIs after the test.
        """
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.remove_all('aci')
        for aci in aci_list:
            domain.add("aci", aci)

    request.addfinalizer(finofaci)


@pytest.fixture(scope="module")
def add_user(request, topo):
    """
    This function will create user for the test and in the end entries will be deleted .
    """
    ous_origin = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou_origin = ous_origin.create(properties={'ou': 'Keywords'})

    ous_next = OrganizationalUnits(topo.standalone, ou_origin.dn)
    for ou in ['Authmethod', 'Dayofweek', 'DNS', 'IP', 'Timeofday']:
        ous_next.create(properties={'ou': ou})

    users_day_of_week = UserAccounts(topo.standalone, f"ou=Dayofweek,ou=Keywords,{DEFAULT_SUFFIX}", rdn=None)
    for user in ['EVERYDAY_KEY', 'TODAY_KEY', 'NODAY_KEY']:
        users_day_of_week.create(properties={
            'uid': user,
            'cn': user,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + user,
            'userPassword': PW_DM
        })

    users_ip = UserAccounts(topo.standalone, f"ou=IP,ou=Keywords,{DEFAULT_SUFFIX}", rdn=None)
    for user in ['FULLIP_KEY', 'NETSCAPEIP_KEY', 'NOIP_KEY']:
        users_ip.create(properties={
            'uid': user,
            'cn': user,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + user,
            'userPassword': PW_DM
        })

    users_timeof_day = UserAccounts(topo.standalone, f"ou=Timeofday,ou=Keywords,{DEFAULT_SUFFIX}", rdn=None)
    for user in ['FULLWORKER_KEY', 'DAYWORKER_KEY', 'NOWORKER_KEY', 'NIGHTWORKER_KEY']:
        users_timeof_day.create(properties={
            'uid': user,
            'cn': user,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + user,
            'userPassword': PW_DM
        })

    users_authmethod = UserAccounts(topo.standalone, f"ou=Authmethod,ou=Keywords,{DEFAULT_SUFFIX}", rdn=None)
    for user in ['NONE_1_KEY', 'NONE_2_KEY', 'SIMPLE_1_KEY']:
        users_authmethod.create(properties={
            'uid': user,
            'cn': user,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + user,
            'userPassword': PW_DM
        })

    users_dns = UserAccounts(topo.standalone, f"ou=DNS,ou=Keywords,{DEFAULT_SUFFIX}", rdn=None)
    for user in ['FULLDNS_KEY', 'SUNDNS_KEY', 'NODNS_KEY', 'NETSCAPEDNS_KEY']:
        users_dns.create(properties={
            'uid': user,
            'cn': user,
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + user,
            'userPassword': PW_DM
        })

    def fin():
        """
        Deletes entries after the test.
        """
        for user in users_day_of_week.list() + users_ip.list() + users_timeof_day.list() + \
                     users_authmethod.list() + users_dns.list():
            user.delete()

        for ou in sorted(ous_next.list(), key=lambda x: len(x.dn), reverse=True):
            ou.delete()

    request.addfinalizer(fin)
