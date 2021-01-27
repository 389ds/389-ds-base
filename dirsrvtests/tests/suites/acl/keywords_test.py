# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

"""
This test script will test wrong/correct key value with ACIs.
"""

import os
import socket
import pytest

from lib389.idm.account import Anonymous
from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.domain import Domain
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.user import UserAccount

import ldap

pytestmark = pytest.mark.tier1

KEYWORDS_OU_KEY = "ou=Keywords,{}".format(DEFAULT_SUFFIX)
DNS_OU_KEY = "ou=DNS,{}".format(KEYWORDS_OU_KEY)
IP_OU_KEY = "ou=IP,{}".format(KEYWORDS_OU_KEY)
FULLIP_KEY = "uid=FULLIP_KEY,{}".format(IP_OU_KEY)
AUTHMETHOD_OU_KEY = "ou=Authmethod,{}".format(KEYWORDS_OU_KEY)
SIMPLE_1_KEY = "uid=SIMPLE_1_KEY,{}".format(AUTHMETHOD_OU_KEY)
FULLDNS_KEY = "uid=FULLDNS_KEY,{}".format(DNS_OU_KEY)
SUNDNS_KEY = "uid=SUNDNS_KEY,{}".format(DNS_OU_KEY)
NODNS_KEY = "uid=NODNS_KEY,{}".format(DNS_OU_KEY)
NETSCAPEDNS_KEY = "uid=NETSCAPEDNS_KEY,{}".format(DNS_OU_KEY)
NONE_1_KEY = "uid=NONE_1_KEY,{}".format(AUTHMETHOD_OU_KEY)
NONE_2_KEY = "uid=NONE_2_KEY,{}".format(AUTHMETHOD_OU_KEY)


NONE_ACI_KEY = f'(target = "ldap:///{AUTHMETHOD_OU_KEY}")' \
               f'(targetattr="*")(version 3.0; aci "Authmethod aci"; ' \
               f'allow(all) userdn = "ldap:///{NONE_1_KEY}" and authmethod = "none" ;)'

SIMPLE_ACI_KEY = f'(target = "ldap:///{AUTHMETHOD_OU_KEY}")' \
                 f'(targetattr="*")(version 3.0; aci "Authmethod aci"; ' \
                 f'allow(all) userdn = "ldap:///{SIMPLE_1_KEY}" and authmethod = "simple" ;)'


def _add_aci(topo, name):
    """
    This function will add ACI to  DEFAULT_SUFFIX
    """
    Domain(topo.standalone, DEFAULT_SUFFIX).add("aci", name)


def test_user_binds_with_a_password_and_can_access_the_data(topo, add_user, aci_of_user):
    """User binds with a password and can access the data as per the ACI.

    :id: f6c4b6f0-7ac4-11e8-a517-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    _add_aci(topo, NONE_ACI_KEY)
    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NONE_1_KEY).bind(PW_DM)
    # Perform Operation
    OrganizationalUnit(conn, AUTHMETHOD_OU_KEY).replace("seeAlso", "cn=1")


def test_user_binds_with_a_bad_password_and_cannot_access_the_data(topo, add_user, aci_of_user):
    """User binds with a BAD password and cannot access the data .

    :id: 0397744e-7ac5-11e8-bfb1-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # User binds with a bad password and cannot access the data
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        UserAccount(topo.standalone, NONE_1_KEY).bind("")


def test_anonymous_user_cannot_access_the_data(topo, add_user, aci_of_user):
    """Anonymous user cannot access the data

    :id: 0821a55c-7ac5-11e8-b214-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    _add_aci(topo, NONE_ACI_KEY)

    # Create a new connection for this test.
    conn = Anonymous(topo.standalone).bind()
    # Perform Operation
    org = OrganizationalUnit(conn, AUTHMETHOD_OU_KEY)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")


def test_authenticated_but_has_no_rigth_on_the_data(topo, add_user, aci_of_user):
    """User has a password. He is authenticated but has no rigth on the data.

    :id: 11be7ebe-7ac5-11e8-b754-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    _add_aci(topo, NONE_ACI_KEY)

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, SIMPLE_1_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, AUTHMETHOD_OU_KEY)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")


def test_the_bind_client_is_accessing_the_directory(topo, add_user, aci_of_user):
    """The bind rule is evaluated to be true if the client is accessing the directory as per the ACI.

    :id: 1715bfb2-7ac5-11e8-8f2c-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    _add_aci(topo, SIMPLE_ACI_KEY)

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, SIMPLE_1_KEY).bind(PW_DM)
    # Perform Operation
    OrganizationalUnit(conn, AUTHMETHOD_OU_KEY).replace("seeAlso", "cn=1")


def test_users_binds_with_a_password_and_can_access_the_data(
        topo, add_user, aci_of_user):
    """User binds with a password and can access the data as per the ACI.

    :id: 1bd01cb4-7ac5-11e8-a2f1-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    _add_aci(topo, SIMPLE_ACI_KEY)

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, SIMPLE_1_KEY).bind(PW_DM)
    # Perform Operation
    OrganizationalUnit(conn, AUTHMETHOD_OU_KEY).replace("seeAlso", "cn=1")


def test_user_binds_without_any_password_and_cannot_access_the_data(topo, add_user, aci_of_user):
    """User binds without any password and cannot access the data

    :id: 205777fa-7ac5-11e8-ba2f-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    _add_aci(topo, SIMPLE_ACI_KEY)

    # Create a new connection for this test.
    conn = Anonymous(topo.standalone).bind()
    # Perform Operation
    org = OrganizationalUnit(conn, AUTHMETHOD_OU_KEY)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")


def test_user_can_access_the_data_when_connecting_from_any_machine(
        topo, add_user, aci_of_user
):
    """User can access the data when connecting from any machine as per the ACI.

    :id: 28cbc008-7ac5-11e8-934e-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    Domain(topo.standalone, DEFAULT_SUFFIX)\
        .add("aci", f'(target ="ldap:///{DNS_OU_KEY}")'
                    f'(targetattr="*")(version 3.0; aci "DNS aci"; allow(all) '
                    f'userdn = "ldap:///{FULLDNS_KEY}" and dns = "*" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, FULLDNS_KEY).bind(PW_DM)
    # Perform Operation
    OrganizationalUnit(conn, DNS_OU_KEY).replace("seeAlso", "cn=1")


def test_user_can_access_the_data_when_connecting_from_internal_ds_network_only(
        topo, add_user, aci_of_user
):
    """User can access the data when connecting from internal ICNC network only as per the ACI.

    :id: 2cac2136-7ac5-11e8-8328-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    dns_name = socket.getfqdn()
    # Add ACI
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", [f'(target = "ldap:///{DNS_OU_KEY}")'
                    f'(targetattr="*")(version 3.0; aci "DNS aci"; '
                    f'allow(all) userdn = "ldap:///{SUNDNS_KEY}" and dns = "*redhat.com" ;)',
                    f'(target = "ldap:///{DNS_OU_KEY}")(targetattr="*")'
                    f'(version 3.0; aci "DNS aci"; allow(all) '
                    f'userdn = "ldap:///{SUNDNS_KEY}" and dns = "{dns_name}" ;)'])

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, SUNDNS_KEY).bind(PW_DM)
    # Perform Operation
    OrganizationalUnit(conn, DNS_OU_KEY).replace("seeAlso", "cn=1")


def test_user_can_access_the_data_when_connecting_from_some_network_only(
        topo, add_user, aci_of_user
):
    """User can access the data when connecting from some network only as per the ACI.

    :id: 3098512a-7ac5-11e8-af85-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    dns_name = socket.getfqdn()
    # Add ACI
    Domain(topo.standalone, DEFAULT_SUFFIX)\
        .add("aci", f'(target = "ldap:///{DNS_OU_KEY}")'
                    f'(targetattr="*")(version 3.0; aci "DNS aci"; allow(all) '
                    f'userdn = "ldap:///{NETSCAPEDNS_KEY}" '
                    f'and dns = "{dns_name}" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NETSCAPEDNS_KEY).bind(PW_DM)
    # Perform Operation
    OrganizationalUnit(conn, DNS_OU_KEY).replace("seeAlso", "cn=1")


def test_from_an_unauthorized_network(topo, add_user, aci_of_user):
    """User cannot access the data when connecting from an unauthorized network as per the ACI.

    :id: 34cf9726-7ac5-11e8-bc12-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", f'(target = "ldap:///{DNS_OU_KEY}")'
                   f'(targetattr="*")(version 3.0; aci "DNS aci"; allow(all) '
                   f'userdn = "ldap:///{NETSCAPEDNS_KEY}" and dns != "red.iplanet.com" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NETSCAPEDNS_KEY).bind(PW_DM)
    # Perform Operation
    OrganizationalUnit(conn, DNS_OU_KEY).replace("seeAlso", "cn=1")


def test_user_cannot_access_the_data_when_connecting_from_an_unauthorized_network_2(
        topo, add_user, aci_of_user):
    """User cannot access the data when connecting from an unauthorized network as per the ACI.

    :id: 396bdd44-7ac5-11e8-8014-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", f'(target = "ldap:///{DNS_OU_KEY}")'
                   f'(targetattr="*")(version 3.0; aci "DNS aci"; allow(all) '
                   f'userdn = "ldap:///{NETSCAPEDNS_KEY}" '
                   f'and dnsalias != "www.redhat.com" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NETSCAPEDNS_KEY).bind(PW_DM)
    # Perform Operation
    OrganizationalUnit(conn, DNS_OU_KEY).replace("seeAlso", "cn=1")


def test_user_cannot_access_the_data_if_not_from_a_certain_domain(topo, add_user, aci_of_user):
    """User cannot access the data if not from a certain domain as per the ACI.

    :id: 3d658972-7ac5-11e8-930f-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", f'(target = "ldap:///{DNS_OU_KEY}")(targetattr="*")'
                   f'(version 3.0; aci "DNS aci"; allow(all) '
                   f'userdn = "ldap:///{NODNS_KEY}" '
                   f'and dns = "RAP.rock.SALSA.house.COM" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NODNS_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, AUTHMETHOD_OU_KEY)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")


def test_dnsalias_keyword_test_nodns_cannot(topo, add_user, aci_of_user):
    """Dnsalias Keyword NODNS_KEY cannot assess data as per the ACI.

    :id: 41b467be-7ac5-11e8-89a3-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server
    :steps:
        1. Add test entry
        2. Add ACI
        3. User should follow ACI role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Add ACI
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", f'(target = "ldap:///{DNS_OU_KEY}")(targetattr="*")'
                   f'(version 3.0; aci "DNS aci"; allow(all) '
                   f'userdn = "ldap:///{NODNS_KEY}" and '
                   f'dnsalias = "RAP.rock.SALSA.house.COM" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NODNS_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, DNS_OU_KEY)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")

@pytest.mark.ds50378
@pytest.mark.bz1710848
@pytest.mark.parametrize("ip_addr", ['127.0.0.1', "[::1]"])
def test_user_can_access_from_ipv4_or_ipv6_address(topo, add_user, aci_of_user, ip_addr):
    """User can modify the data when accessing the server from the allowed IPv4 and IPv6 addresses

    :id: 461e761e-7ac5-11e8-9ae4-8c16451d917b
    :customerscenario: True
    :parametrized: yes
    :setup: Standalone Server
    :steps:
        1. Add ACI that has both IPv4 and IPv6
        2. Connect from one of the IPs allowed in ACI
        3. Modify an attribute
    :expectedresults:
        1. ACI should be added
        2. Conection should be successful
        3. Operation should be successful
    """
    # Add ACI that contains both IPv4 and IPv6
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add("aci", f'(target ="ldap:///{IP_OU_KEY}")(targetattr="*") '
                   f'(version 3.0; aci "IP aci"; allow(all) '
                   f'userdn = "ldap:///{FULLIP_KEY}" and (ip = "127.0.0.1" or ip = "::1");)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, FULLIP_KEY).bind(PW_DM, uri=f'ldap://{ip_addr}:{topo.standalone.port}')

    # Perform Operation
    OrganizationalUnit(conn, IP_OU_KEY).replace("seeAlso", "cn=1")


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
