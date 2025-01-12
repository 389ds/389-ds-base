# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389._mapped_object import DSLdapObjects

pytestmark = pytest.mark.tier2
disk_monitoring_ack = pytest.mark.skipif(not os.environ.get('DISK_MONITORING_ACK', False), reason="Disk monitoring tests may damage system configuration.")

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def create_dummy_mount(topology_st, request):
    cmds = ['setenforce 0',
            'mkdir /var/log/dirsrv/slapd-{}/tmp'.format(topology_st.standalone.serverid),
            'mount -t tmpfs tmpfs /var/log/dirsrv/slapd-{}/tmp -o size=0'.format(topology_st.standalone.serverid),
            'chown dirsrv: /var/log/dirsrv/slapd-{}/tmp'.format(topology_st.standalone.serverid)]

    log.info('Create dummy mount')
    for cmd in cmds:
        log.info('Command used : %s' % cmd)
        subprocess.Popen(cmd, shell=True)

    def fin():
        cmds = ['umount /var/log/dirsrv/slapd-{}/tmp'.format(topology_st.standalone.serverid),
                'setenforce 1']

        for cmd in cmds:
            log.info('Command used : %s' % cmds)
            subprocess.Popen(cmd, shell=True)

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def change_config(topology_st):
    topology_st.standalone.config.set('nsslapd-disk-monitoring', 'on')
    topology_st.standalone.config.set('nsslapd-disk-monitoring-readonly-on-threshold', 'on')


@pytest.mark.skipif(ds_is_older("1.4.3.16"), reason="Might fail because of bz1890118")
@disk_monitoring_ack
def test_produce_division_by_zero(topology_st, create_dummy_mount, change_config):
    """Test dirsrv will not crash when division by zero occurs

    :id: 51b11093-8851-41bd-86cb-217b1a3339c7
    :customerscenario: True
    :setup: Standalone
    :steps:
        1. Turn on disk monitoring
        2. Go below the threshold
        3. Check DS is up and not entering shutdown mode
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone

    log.info('Check search works before changing the nsslapd-auditlog attribute')
    try:
        DSLdapObjects(topology_st.standalone, basedn='cn=disk space,cn=monitor').filter("(objectclass=*)", scope=0)
    except ldap.SERVER_DOWN as e:
        log.info('Test failed - dirsrv crashed')
        assert False

    log.info('Change location of nsslapd-auditlog')
    standalone.config.set('nsslapd-auditlog', '/var/log/dirsrv/slapd-{}/tmp/audit'.format(standalone.serverid))

    log.info('Check search will not fail')
    try:
        DSLdapObjects(topology_st.standalone, basedn='cn=disk space,cn=monitor').filter("(objectclass=*)", scope=0)
    except ldap.SERVER_DOWN as e:
        log.info('Test failed - dirsrv crashed')
        assert False

    log.info('If passed, run search again just in case')
    try:
        DSLdapObjects(topology_st.standalone, basedn='cn=disk space,cn=monitor').filter("(objectclass=*)", scope=0)
    except ldap.SERVER_DOWN as e:
        log.info('Test failed - dirsrv crashed')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
