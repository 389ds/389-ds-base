# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pwd
import logging
import pytest
from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv, Entry

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_DN = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'standalone'
INSTANCE_BACKUP = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)


class TopologyInstance(object):
    def __init__(self, instance):
        self.instance = instance


@pytest.fixture(scope='module')
def topology(request):
    instance = DirSrv(verbose=False)

    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    instance.allocate(args)

    if instance.exists():
        instance.delete()

    def fin():
        if instance.exists():
            instance.delete()
    request.addfinalizer(fin)

    return TopologyInstance(instance)


def _add_user(topology):
    """Add a user to the instance"""

    topology.instance.add_s(Entry((TEST_DN, {'objectclass':
                                             "top person".split(),
                                             'objectclass':
                                             "organizationalPerson",
                                             'objectclass': "inetOrgPerson",
                                             'uid': 'test',
                                             'sn': 'test',
                                             'cn': 'test'})))
    log.info('User %s was added' % TEST_DN)


def test_allocate(topology):
    """Test instance.allocate() function
    for correct state and arguments
    """

    log.info('Check instance state is allocated')
    assert topology.instance.state == DIRSRV_STATE_ALLOCATED

    log.info('Try to add user. Failure is excpected')
    with pytest.raises(Exception):
        _add_user(topology)

    if os.getuid() == 0:
        userid = DEFAULT_USER
    else:
        userid = pwd.getpwuid(os.getuid())[0]

    log.info('Verify the settings')
    assert topology.instance.host == LOCALHOST
    assert topology.instance.port == INSTANCE_PORT
    assert topology.instance.sslport is None
    assert topology.instance.binddn == DN_DM
    assert topology.instance.bindpw == PW_DM
    assert topology.instance.creation_suffix == DEFAULT_SUFFIX
    assert topology.instance.userid == userid
    assert topology.instance.serverid == INSTANCE_SERVERID
    assert topology.instance.groupid == topology.instance.userid
    assert topology.instance.backupdir == INSTANCE_BACKUP

    log.info('Check that we can change the settings of an allocated DirSrv')
    args = {SER_SERVERID_PROP: INSTANCE_SERVERID,
            SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            SER_ROOT_DN: "uid=foo"}
    topology.instance.allocate(args)

    log.info('Check instance state is allocated')
    assert topology.instance.state == DIRSRV_STATE_ALLOCATED

    log.info('Verify the settings')
    assert topology.instance.host == LOCALHOST
    assert topology.instance.port == INSTANCE_PORT
    assert topology.instance.sslport is None
    assert topology.instance.binddn == "uid=foo"
    assert topology.instance.bindpw == PW_DM
    assert topology.instance.creation_suffix == DEFAULT_SUFFIX
    assert topology.instance.userid == userid
    assert topology.instance.serverid == INSTANCE_SERVERID
    assert topology.instance.groupid == topology.instance.userid
    assert topology.instance.backupdir == INSTANCE_BACKUP

    log.info('Restore back the valid parameters and check')
    args = {SER_SERVERID_PROP: INSTANCE_SERVERID,
            SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT}
    topology.instance.allocate(args)

    log.info('Check instance state is allocated')
    assert topology.instance.state == DIRSRV_STATE_ALLOCATED

    log.info('Verify the settings')
    assert topology.instance.host == LOCALHOST
    assert topology.instance.port == INSTANCE_PORT
    assert topology.instance.sslport is None
    assert topology.instance.binddn == DN_DM
    assert topology.instance.bindpw == PW_DM
    assert topology.instance.creation_suffix == DEFAULT_SUFFIX
    assert topology.instance.userid == userid
    assert topology.instance.serverid == INSTANCE_SERVERID
    assert topology.instance.groupid == topology.instance.userid
    assert topology.instance.backupdir == INSTANCE_BACKUP


def test_create(topology):
    """Test instance creation
    Check status before and after
    Try to add a user
    """

    log.info('Check instance state is allocated')
    assert topology.instance.state == DIRSRV_STATE_ALLOCATED

    topology.instance.create()

    log.info('Check instance state is offline')
    assert topology.instance.state == DIRSRV_STATE_OFFLINE

    log.info('Try to add user. Failure is excpected')
    with pytest.raises(Exception):
        _add_user(topology)


def test_open(topology):
    """Test instance opening
    Check status before and after
    Try to add a user
    """

    log.info('Check instance state is offline')
    assert topology.instance.state == DIRSRV_STATE_OFFLINE

    topology.instance.open()

    log.info('Check instance state is online')
    assert topology.instance.state == DIRSRV_STATE_ONLINE

    log.info('Try to add user. Success is excpected')
    _add_user(topology)


def test_close(topology):
    """Test instance closing
    Check status before and after
    Try to add a user
    """

    log.info('Check instance state is online')
    assert topology.instance.state == DIRSRV_STATE_ONLINE

    topology.instance.close()

    log.info('Check instance state is offline')
    assert topology.instance.state == DIRSRV_STATE_OFFLINE

    log.info('Try to add user. Failure is excpected')
    with pytest.raises(Exception):
        _add_user(topology)


def test_delete(topology):
    """Test instance deletion
    Check status before and after
    Try to add a user
    """

    log.info('Check instance state is offline')
    assert topology.instance.state == DIRSRV_STATE_OFFLINE

    topology.instance.delete()

    log.info('Check instance state is allocated')
    assert topology.instance.state == DIRSRV_STATE_ALLOCATED

    log.info('Try to add user. Failure is excpected')
    with pytest.raises(Exception):
        _add_user(topology)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main('-s -v %s' % CURRENT_FILE)
