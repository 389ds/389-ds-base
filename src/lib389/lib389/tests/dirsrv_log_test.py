# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from lib389._constants import *
from lib389.utils import ensure_bytes, ensure_str
from lib389 import DirSrv, Entry
import pytest
import time
import shutil
import datetime
from dateutil.tz import tzoffset

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    standalone = DirSrv(verbose=False)
    standalone.log.debug("Instance allocated")
    args = {
            SER_PORT: INSTANCE_PORT,
            # SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    standalone.allocate(args)
    if standalone.exists():
        standalone.delete()
    standalone.create()
    standalone.open()
    standalone.config.set('nsslapd-accesslog-logbuffering', 'off')

    def fin():
        standalone.delete()
    request.addfinalizer(fin)
    # We have to wait for time to elapse for the access log to be flushed.

    return TopologyStandalone(standalone)


def test_access_log_rotation(topology):
    """
    Check we can parse rotated logs as well as active log.
    """

    # Artificially rotate the log.
    lpath = topology.standalone.ds_access_log._get_log_path()
    shutil.copyfile(lpath, lpath + ensure_bytes('.20160515-104822'))
    # check we have the right number of lines.
    access_lines = topology.standalone.ds_access_log.readlines_archive()
    assert(len(access_lines) > 0)
    access_lines = topology.standalone.ds_access_log.match_archive('.*fd=.*')
    assert(len(access_lines) > 0)


def test_access_log(topology):
    """Check the parsing of the access log"""
    access_lines = topology.standalone.ds_access_log.readlines()
    assert(len(access_lines) > 0)
    access_lines = topology.standalone.ds_access_log.match('.*fd=.*')
    assert(len(access_lines) > 0)
    # Test the line parser in a basic way.
    assert(
        topology.standalone.ds_access_log.parse_line('[27/Apr/2016:12:49:49.726093186 +1000] conn=1 fd=64 slot=64 connection from ::1 to ::1') ==
        {
            'slot': '64', 'remote': '::1', 'action': 'CONNECT', 'timestamp': '[27/Apr/2016:12:49:49.726093186 +1000]', 'fd': '64', 'conn': '1', 'local': '::1',
            'datetime': datetime.datetime(2016, 4, 27, 12, 0, 0, 726093, tzinfo=tzoffset(None, 36000))
        }
    )
    assert(
        topology.standalone.ds_access_log.parse_line('[27/Apr/2016:12:49:49.727235997 +1000] conn=1 op=2 SRCH base="cn=config" scope=0 filter="(objectClass=*)" attrs="nsslapd-instancedir nsslapd-errorlog nsslapd-accesslog nsslapd-auditlog nsslapd-certdir nsslapd-schemadir nsslapd-bakdir nsslapd-ldifdir"') ==  # noqa
        {
            'rem': 'base="cn=config" scope=0 filter="(objectClass=*)" attrs="nsslapd-instancedir nsslapd-errorlog nsslapd-accesslog nsslapd-auditlog nsslapd-certdir nsslapd-schemadir nsslapd-bakdir nsslapd-ldifdir"',  # noqa
            'action': 'SRCH', 'timestamp': '[27/Apr/2016:12:49:49.727235997 +1000]', 'conn': '1', 'op': '2',
            'datetime': datetime.datetime(2016, 4, 27, 12, 0, 0, 727235, tzinfo=tzoffset(None, 36000))
        }
    )
    assert(
        topology.standalone.ds_access_log.parse_line('[27/Apr/2016:12:49:49.736297002 +1000] conn=1 op=4 fd=64 closed - U1') ==
        {
            'status': 'U1', 'fd': '64', 'action': 'DISCONNECT', 'timestamp': '[27/Apr/2016:12:49:49.736297002 +1000]', 'conn': '1', 'op': '4',
            'datetime': datetime.datetime(2016, 4, 27, 12, 0, 0, 736297, tzinfo=tzoffset(None, 36000))
        }
    )
    assert(
        topology.standalone.ds_access_log.parse_line('[27/Apr/2016:12:49:49.736297002 -1000] conn=1 op=4 fd=64 closed - U1') ==
        {
            'status': 'U1', 'fd': '64', 'action': 'DISCONNECT', 'timestamp': '[27/Apr/2016:12:49:49.736297002 -1000]', 'conn': '1', 'op': '4',
            'datetime': datetime.datetime(2016, 4, 27, 12, 0, 0, 736297, tzinfo=tzoffset(None, -36000))
        }
    )


def test_error_log(topology):
    """Check the parsing of the error log"""
    # No need to sleep, it's not buffered.
    error_lines = topology.standalone.ds_error_log.readlines()
    assert(len(error_lines) > 0)
    error_lines = topology.standalone.ds_error_log.match('.*started.*')
    assert(len(error_lines) > 0)

    assert(
        topology.standalone.ds_error_log.parse_line('[27/Apr/2016:13:46:35.775670167 +1000] slapd started.  Listening on All Interfaces port 54321 for LDAP requests') ==  # noqa
        {
            'timestamp': '[27/Apr/2016:13:46:35.775670167 +1000]', 'message': 'slapd started.  Listening on All Interfaces port 54321 for LDAP requests',
            'datetime': datetime.datetime(2016, 4, 27, 13, 0, 0, 775670, tzinfo=tzoffset(None, 36000))
        }
    )


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -vv %s" % CURRENT_FILE)
