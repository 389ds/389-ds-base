# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from lib389._constants import *
from lib389 import DirSrv, Entry
import pytest
import time

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'loggingtest'


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    standalone = DirSrv(verbose=False)
    standalone.log.debug("Instance allocated")
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            # SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    standalone.allocate(args)
    if standalone.exists():
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_access_log(topology):
    """Check the parsing of the access log"""
    # We have to wait for time to elapse for the access log to be flushed.
    time.sleep(60)
    access_lines = topology.standalone.ds_access_log.readlines()
    assert(len(access_lines) > 0)
    access_lines = topology.standalone.ds_access_log.match('.*fd=.*')
    assert(len(access_lines) > 0)
    # Test the line parser in a basic way.
    assert(
        topology.standalone.ds_access_log.parse_line('[27/Apr/2016:12:49:49.726093186 +1000] conn=1 fd=64 slot=64 connection from ::1 to ::1') ==
        {'slot': '64', 'remote': '::1', 'action': 'CONNECT', 'timestamp': '[27/Apr/2016:12:49:49.726093186 +1000]', 'fd': '64', 'conn': '1', 'local': '::1'}
    )    
    assert(
        topology.standalone.ds_access_log.parse_line('[27/Apr/2016:12:49:49.727235997 +1000] conn=1 op=2 SRCH base="cn=config" scope=0 filter="(objectClass=*)" attrs="nsslapd-instancedir nsslapd-errorlog nsslapd-accesslog nsslapd-auditlog nsslapd-certdir nsslapd-schemadir nsslapd-bakdir nsslapd-ldifdir"') ==
        {'rem': 'base="cn=config" scope=0 filter="(objectClass=*)" attrs="nsslapd-instancedir nsslapd-errorlog nsslapd-accesslog nsslapd-auditlog nsslapd-certdir nsslapd-schemadir nsslapd-bakdir nsslapd-ldifdir"', 'action': 'SRCH', 'timestamp': '[27/Apr/2016:12:49:49.727235997 +1000]', 'conn': '1', 'op': '2'}
    )
    assert(
        topology.standalone.ds_access_log.parse_line('[27/Apr/2016:12:49:49.736297002 +1000] conn=1 op=4 fd=64 closed - U1') ==
        {'status': 'U1', 'fd': '64', 'action': 'DISCONNECT', 'timestamp': '[27/Apr/2016:12:49:49.736297002 +1000]', 'conn': '1', 'op': '4'}
    )

def test_error_log(topology):
    """Check the parsing of the error log"""
    # No need to sleep, it's not buffered.
    error_lines = topology.standalone.ds_error_log.readlines()
    assert(len(error_lines) > 0)
    error_lines = topology.standalone.ds_error_log.match('.*started.*')
    assert(len(error_lines) > 0)

    assert(
        topology.standalone.ds_error_log.parse_line('[27/Apr/2016:13:46:35.775670167 +1000] slapd started.  Listening on All Interfaces port 54321 for LDAP requests') ==
        {'timestamp': '[27/Apr/2016:13:46:35.775670167 +1000]', 'message': 'slapd started.  Listening on All Interfaces port 54321 for LDAP requests'}
    )


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -vv %s" % CURRENT_FILE)
