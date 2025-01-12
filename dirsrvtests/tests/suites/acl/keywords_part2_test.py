# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


"""
This test script will test wrong/correct key value with ACIs.
"""
import ldap
import os
import pytest
import socket
import time
from datetime import datetime
from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.domain import Domain
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.user import UserAccount
from lib389.utils import *

pytestmark = pytest.mark.tier1

KEYWORDS_OU_KEY = "ou=Keywords,{}".format(DEFAULT_SUFFIX)
DAYOFWEEK_OU_KEY = "ou=Dayofweek,{}".format(KEYWORDS_OU_KEY)
IP_OU_KEY = "ou=IP,{}".format(KEYWORDS_OU_KEY)
TIMEOFDAY_OU_KEY = "ou=Timeofday,{}".format(KEYWORDS_OU_KEY)
EVERYDAY_KEY = "uid=EVERYDAY_KEY,{}".format(DAYOFWEEK_OU_KEY)
TODAY_KEY = "uid=TODAY_KEY,{}".format(DAYOFWEEK_OU_KEY)
NODAY_KEY = "uid=NODAY_KEY,{}".format(DAYOFWEEK_OU_KEY)
FULLIP_KEY = "uid=FULLIP_KEY,{}".format(IP_OU_KEY)
NETSCAPEIP_KEY = "uid=NETSCAPEIP_KEY,{}".format(IP_OU_KEY)
NOIP_KEY = "uid=NOIP_KEY,{}".format(IP_OU_KEY)
FULLWORKER_KEY = "uid=FULLWORKER_KEY,{}".format(TIMEOFDAY_OU_KEY)
DAYWORKER_KEY = "uid=DAYWORKER_KEY,{}".format(TIMEOFDAY_OU_KEY)
NIGHTWORKER_KEY = "uid=NIGHTWORKER_KEY,{}".format(TIMEOFDAY_OU_KEY)
NOWORKER_KEY = "uid=NOWORKER_KEY,{}".format(TIMEOFDAY_OU_KEY)


@pytest.mark.skipif(os.getuid()!=0, reason="pytest non run by root user")
def test_access_from_certain_network_only_ip(topo, add_user, aci_of_user, request):
    """
    User can access the data when connecting from certain network only as per the ACI.

    :id: 4ec38296-7ac5-11e8-9816-8c16451d917b
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
    # Turn access log buffering off to make less time consuming
    topo.standalone.config.set('nsslapd-accesslog-logbuffering', 'off')

    # Find the ip from ds logs , as we need to know the exact ip used by ds to run the instances.
    # Wait till Access Log is generated
    topo.standalone.restart()

    old_hostname = socket.gethostname()
    socket.sethostname('localhost')
    hostname = socket.gethostname()
    IP = socket.gethostbyname(hostname)

    # Add ACI
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.add("aci", f'(target = "ldap:///{IP_OU_KEY}")(targetattr=\"*\")(version 3.0; aci "IP aci"; '
                      f'allow(all)userdn = "ldap:///{NETSCAPEIP_KEY}" and (ip = "127.0.0.1" or ip = "::1" or ip = "{IP}") ;)')

    # create a new connection for the test
    new_uri = topo.standalone.ldapuri.replace(old_hostname, hostname)
    topo.standalone.ldapuri = new_uri
    conn = UserAccount(topo.standalone, NETSCAPEIP_KEY).bind(PW_DM)

    # Perform Operation
    topo.standalone.config.set('nsslapd-errorlog-level', '128')
    org = OrganizationalUnit(conn, IP_OU_KEY)
    topo.standalone.host = hostname
    org.replace("seeAlso", "cn=1")

    # remove the aci
    domain.ensure_removed("aci", f'(target = "ldap:///{IP_OU_KEY}")(targetattr=\"*\")(version 3.0; aci '
                                 f'"IP aci"; allow(all)userdn = "ldap:///{NETSCAPEIP_KEY}" and '
                                 f'(ip = "127.0.0.1" or ip = "::1" or ip = "{IP}") ;)')
    # Now add aci with new ip
    domain.add("aci", f'(target = "ldap:///{IP_OU_KEY}")(targetattr="*")(version 3.0; aci "IP aci"; '
                      f'allow(all)userdn = "ldap:///{NETSCAPEIP_KEY}" and ip = "100.1.1.1" ;)')

    # After changing  the ip user cant access data
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")

    def fin():
        log.info('Setting the hostname back to orginal')
        socket.sethostname(old_hostname)

    request.addfinalizer(fin)


@pytest.mark.skipif(os.getuid()!=0, reason="pytest non run by root user")
def test_connection_from_an_unauthorized_network(topo, add_user, aci_of_user, request):
    """
    User cannot access the data when connectin from an unauthorized network as per the ACI.

    :id: 52d1ecce-7ac5-11e8-9ad9-8c16451d917b
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
    old_hostname = socket.gethostname()
    socket.sethostname('localhost')
    hostname = socket.gethostname()

    # Add ACI
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.add("aci", f'(target = "ldap:///{IP_OU_KEY}")'
                      f'(targetattr="*")(version 3.0; aci "IP aci"; '
                      f'allow(all) userdn = "ldap:///{NETSCAPEIP_KEY}" '
                      f'and (ip != "127.0.0.1" and ip != "::1") ;)')

    # create a new connection for the test
    new_uri = topo.standalone.ldapuri.replace(old_hostname, hostname)
    topo.standalone.ldapuri = new_uri
    conn = UserAccount(topo.standalone, NETSCAPEIP_KEY).bind(PW_DM)

    # Perform Operation
    topo.standalone.config.set('nsslapd-errorlog-level', '128')
    org = OrganizationalUnit(conn, IP_OU_KEY)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")

    # Remove the ACI
    domain.ensure_removed('aci', domain.get_attr_vals('aci')[-1])
    # Add new ACI
    domain.add('aci', f'(target = "ldap:///{IP_OU_KEY}")(targetattr="*")'
                      f'(version 3.0; aci "IP aci"; allow(all) '
                      f'userdn = "ldap:///{NETSCAPEIP_KEY}" and (ip = "127.0.0.1" or ip = "::1") ;)')
    time.sleep(1)

    # now user can access data
    org.replace("seeAlso", "cn=1")

    def fin():
        log.info('Setting the hostname back to orginal')
        socket.sethostname(old_hostname)

    request.addfinalizer(fin)


def test_ip_keyword_test_noip_cannot(topo, add_user, aci_of_user):
    """
    User NoIP cannot assess the data as per the ACI.

    :id: 570bc7f6-7ac5-11e8-88c1-8c16451d917b
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
    Domain(topo.standalone,
           DEFAULT_SUFFIX).add("aci", f'(target ="ldap:///{IP_OU_KEY}")'
                                      f'(targetattr="*")(version 3.0; aci "IP aci"; allow(all) '
                                      f'userdn = "ldap:///{FULLIP_KEY}" and ip = "*" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NOIP_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, IP_OU_KEY)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")


def test_user_can_access_the_data_at_any_time(topo, add_user, aci_of_user):
    """
    User can access the data at any time as per the ACI.

    :id: 5b4da91a-7ac5-11e8-bbda-8c16451d917b
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
    Domain(topo.standalone,
           DEFAULT_SUFFIX).add("aci", f'(target = "ldap:///{TIMEOFDAY_OU_KEY}")'
                                      f'(targetattr="*")(version 3.0; aci "Timeofday aci"; '
                                      f'allow(all) userdn ="ldap:///{FULLWORKER_KEY}" and '
                                      f'(timeofday >= "0000" and timeofday <= "2359") ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, FULLWORKER_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, TIMEOFDAY_OU_KEY)
    org.replace("seeAlso", "cn=1")


def test_user_can_access_the_data_only_in_the_morning(topo, add_user, aci_of_user):
    """
    User can access the data only in the morning as per the ACI.

    :id: 5f7d380c-7ac5-11e8-8124-8c16451d917b
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
    Domain(topo.standalone,
           DEFAULT_SUFFIX).add("aci", f'(target = "ldap:///{TIMEOFDAY_OU_KEY}")'
                                      f'(targetattr="*")(version 3.0; aci "Timeofday aci"; '
                                      f'allow(all) userdn = "ldap:///{DAYWORKER_KEY}" '
                                      f'and timeofday < "1200" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, DAYWORKER_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, TIMEOFDAY_OU_KEY)
    if datetime.now().hour >= 12:
        with pytest.raises(ldap.INSUFFICIENT_ACCESS):
            org.replace("seeAlso", "cn=1")
    else:
        org.replace("seeAlso", "cn=1")


def test_user_can_access_the_data_only_in_the_afternoon(topo, add_user, aci_of_user):
    """
    User can access the data only in the afternoon as per the ACI.

    :id: 63eb5b1c-7ac5-11e8-bd46-8c16451d917b
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
    Domain(topo.standalone,
           DEFAULT_SUFFIX).add("aci", f'(target = "ldap:///{TIMEOFDAY_OU_KEY}")'
                                      f'(targetattr="*")(version 3.0; aci "Timeofday aci"; '
                                      f'allow(all) userdn = "ldap:///{NIGHTWORKER_KEY}" '
                                      f'and timeofday > \'1200\' ;)')

    # create a new connection for the test
    conn = UserAccount(topo.standalone, NIGHTWORKER_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, TIMEOFDAY_OU_KEY)
    if datetime.now().hour < 12:
        with pytest.raises(ldap.INSUFFICIENT_ACCESS):
            org.replace("seeAlso", "cn=1")
    else:
        org.replace("seeAlso", "cn=1")


def test_timeofday_keyword(topo, add_user, aci_of_user):
    """
    User NOWORKER_KEY can access the data as per the ACI after removing
    ACI it cant.

    :id: 681dd58e-7ac5-11e8-bed1-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server + an user entry
    :steps:
        1. Compute current time (in minute)
        2. Redo the 4 next steps until current time (in minute) has not changed
        3. In 2. loop: Set previous time to current time
        4. In 2. loop: Set the ACI
        5. In 2. loop: Perform operation and record if access is allowed or not
        6. In 2. loop: Compute again the current time
        7. Check that access was allowed on last operation
        8. Remove the ACI
        9. Check that access is denied

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. last operation should have been allowed
        8. Success
        9. Operation should fail with ldap.INSUFFICIENT_ACCESS
    """
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    aci = domain.get_attr_vals_utf8('aci')
    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NOWORKER_KEY).bind(PW_DM)
    org = OrganizationalUnit(conn, TIMEOFDAY_OU_KEY)
    now_1 = None
    now_2 = "".join(time.strftime("%c").split()[3].split(":"))[:4]
    # Perform the test in loop to avoid timing issues
    while now_1 != now_2:
        now_1 = now_2
        # Set ACI
        new_aci = f'(target = "ldap:///{TIMEOFDAY_OU_KEY}")' \
                  '(targetattr="*")(version 3.0; aci "Timeofday aci"; ' \
                  f'allow(all) userdn = "ldap:///{NOWORKER_KEY}" ' \
                  f'and timeofday = \'{now_1}\' ;)'
        domain.replace("aci", [*aci, new_aci])
        # Perform Operation
        try:
            org.replace("seeAlso", "cn=1")
            access_allowed = True
        except ldap.INSUFFICIENT_ACCESS:
            access_allowed = False
        # Check that time has not changed
        now_2 = "".join(time.strftime("%c").split()[3].split(":"))[:4]
    # Check that previous operation was successfull
    assert access_allowed
    # Remove ACI
    domain.replace('aci', aci)
    assert new_aci not in domain.get_attr_vals_utf8('aci')
    # after removing the ACI user cannot access the data
    time.sleep(1)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")


def test_dayofweek_keyword_test_everyday_can_access(topo, add_user, aci_of_user):
    """
    User can access the data EVERYDAY_KEY as per the ACI.

    :id: 6c5922ca-7ac5-11e8-8f01-8c16451d917b
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
    Domain(topo.standalone,
           DEFAULT_SUFFIX).add("aci", f'(target = "ldap:///{DAYOFWEEK_OU_KEY}")'
                                      f'(targetattr="*")(version 3.0; aci "Dayofweek aci"; '
                                      f'allow(all) userdn = "ldap:///{EVERYDAY_KEY}" and '
                                      f'dayofweek = "Sun, Mon, Tue, Wed, Thu, Fri, Sat" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, EVERYDAY_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, DAYOFWEEK_OU_KEY)
    org.replace("seeAlso", "cn=1")


def test_dayofweek_keyword_today_can_access(topo, add_user, aci_of_user):
    """
    User can access the data one day per week as per the ACI.

    :id: 7131dc88-7ac5-11e8-acc2-8c16451d917b
    :customerscenario: True
    :setup: Standalone Server + an user entry
    :steps:
        1. Compute current date
        2. Redo the 4 next steps until current date has not changed
        3. In 2. loop: Set previous time to current date
        4. In 2. loop: Set the ACI
        5. In 2. loop: Perform operation and record if access is allowed or not
        6. In 2. loop: Compute again the current date
        7. Check that access was allowed on last operation
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. last operation should have been allowed
    """
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    aci = domain.get_attr_vals_utf8('aci')
    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, TODAY_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, DAYOFWEEK_OU_KEY)
    today_1 = None
    today_2 = time.strftime("%c").split()[0]
    while today_1 != today_2:
        # Set ACI
        today_1 = today_2
        new_aci = f'(target = "ldap:///{DAYOFWEEK_OU_KEY}")' \
                  '(targetattr="*")(version 3.0; aci "Dayofweek aci";  ' \
                  f'allow(all) userdn = "ldap:///{TODAY_KEY}" ' \
                  f'and dayofweek = \'{today_1}\' ;)'
        domain.replace("aci", [*aci, new_aci])
        try:
            org.replace("seeAlso", "cn=1")
            access_allowed = True
        except ldap.INSUFFICIENT_ACCESS:
            access_allowed = False
        # Check that time has not changed
        today_2 = time.strftime("%c").split()[0]
    assert access_allowed


def test_user_cannot_access_the_data_at_all(topo, add_user, aci_of_user):
    """
    User cannot access the data at all as per the ACI.

    :id: 75cdac5e-7ac5-11e8-968a-8c16451d917b
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
    Domain(topo.standalone,
           DEFAULT_SUFFIX).add("aci", f'(target = "ldap:///{DAYOFWEEK_OU_KEY}")'
                                      f'(targetattr="*")(version 3.0; aci "Dayofweek aci";  '
                                      f'allow(all) userdn = "ldap:///{TODAY_KEY}" '
                                      f'and dayofweek = "$NEW_DATE" ;)')

    # Create a new connection for this test.
    conn = UserAccount(topo.standalone, NODAY_KEY).bind(PW_DM)
    # Perform Operation
    org = OrganizationalUnit(conn, DAYOFWEEK_OU_KEY)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        org.replace("seeAlso", "cn=1")


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
