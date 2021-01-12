# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest

from lib389.cli_conf.replication import get_repl_monitor_info
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2
from lib389.cli_base import FakeArgs
from lib389.cli_base.dsrc import dsrc_arg_concat
from lib389.cli_base import connect_instance
from lib389.replica import Replicas


pytestmark = pytest.mark.tier0

LOG_FILE = '/tmp/monitor.log'
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def set_log_file(request):
    fh = logging.FileHandler(LOG_FILE)
    fh.setLevel(logging.DEBUG)
    log.addHandler(fh)

    def fin():
        log.info('Delete files')
        os.remove(LOG_FILE)

        config = os.path.expanduser(DSRC_HOME)
        if os.path.exists(config):
            os.remove(config)

    request.addfinalizer(fin)


def check_value_in_log_and_reset(content_list, second_list=None, single_value=None, error_list=None):
    with open(LOG_FILE, 'r+') as f:
        file_content = f.read()

        for item in content_list:
            log.info('Check that "{}" is present'.format(item))
            assert item in file_content

        if second_list is not None:
            log.info('Check for "{}"'.format(second_list))
            for item in second_list:
                assert item in file_content

        if single_value is not None:
            log.info('Check for "{}"'.format(single_value))
            assert single_value in file_content

        if error_list is not None:
            log.info('Check that "{}" is not present'.format(error_list))
            for item in error_list:
                assert item not in file_content

        log.info('Reset log file')
        f.truncate(0)


@pytest.mark.ds50545
@pytest.mark.bz1739718
@pytest.mark.skipif(ds_is_older("1.4.0"), reason="Not implemented")
def test_dsconf_replication_monitor(topology_m2, set_log_file):
    """Test replication monitor that was ported from legacy tools

    :id: ce48020d-7c30-41b7-8f68-144c9cd757f6
    :setup: 2 MM topology
    :steps:
         1. Create DS instance
         2. Run replication monitor with connections option
         3. Run replication monitor with aliases option
         4. Run replication monitor with --json option
         5. Run replication monitor with .dsrc file created
         6. Run replication monitor with connections option as if using dsconf CLI
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
         5. Success
         6. Success
    """

    m1 = topology_m2.ms["master1"]
    m2 = topology_m2.ms["master2"]

    # Enable ldapi if not already done.
    for inst in [topology_m2.ms["master1"], topology_m2.ms["master2"]]:
        if not inst.can_autobind():
            # Update ns-slapd instance
            inst.config.set('nsslapd-ldapilisten', 'on')
            inst.config.set('nsslapd-ldapiautobind', 'on')
            inst.restart()
    # Ensure that updates have been sent both ways.
    replicas = Replicas(m1)
    replica = replicas.get(DEFAULT_SUFFIX)
    replica.test_replication([m2])
    replicas = Replicas(m2)
    replica = replicas.get(DEFAULT_SUFFIX)
    replica.test_replication([m1])

    alias_content = ['Supplier: M1 (' + m1.host + ':' + str(m1.port) + ')',
                     'Supplier: M2 (' + m2.host + ':' + str(m2.port) + ')']

    connection_content = 'Supplier: '+ m1.host + ':' + str(m1.port)
    content_list = ['Replica Root: dc=example,dc=com',
                    'Replica ID: 1',
                    'Replica Status: Available',
                    'Max CSN',
                    'Status For Agreement: "002" ('+ m2.host + ':' + str(m2.port) + ')',
                    'Replica Enabled: on',
                    'Update In Progress: FALSE',
                    'Last Update Start:',
                    'Last Update End:',
                    'Number Of Changes Sent:',
                    'Number Of Changes Skipped: None',
                    'Last Update Status: Error (0) Replica acquired successfully: Incremental update succeeded',
                    'Last Init Start:',
                    'Last Init End:',
                    'Last Init Status:',
                    'Reap Active: 0',
                    'Replication Status: In Synchronization',
                    'Replication Lag Time:',
                    'Supplier: ',
                     m2.host + ':' + str(m2.port),
                    'Replica Root: dc=example,dc=com',
                    'Replica ID: 2',
                    'Status For Agreement: "001" (' + m1.host + ':' + str(m1.port)+')']

    error_list = ['consumer (Unavailable)',
                  'Failed to retrieve database RUV entry from consumer']

    json_list = ['type',
                 'list',
                 'items',
                 'name',
                 m1.host + ':' + str(m1.port),
                 'data',
                 '"replica_id": "1"',
                 '"replica_root": "dc=example,dc=com"',
                 '"replica_status": "Available"',
                 'maxcsn',
                 'agmts_status',
                 'agmt-name',
                 '002',
                 'replica',
                 m2.host + ':' + str(m2.port),
                 'replica-enabled',
                 'update-in-progress',
                 'last-update-start',
                 'last-update-end',
                 'number-changes-sent',
                 'number-changes-skipped',
                 'last-update-status',
                 'Error (0) Replica acquired successfully: Incremental update succeeded',
                 'last-init-start',
                 'last-init-end',
                 'last-init-status',
                 'reap-active',
                 'replication-status',
                 'In Synchronization',
                 'replication-lag-time',
                 '"replica_id": "2"',
                 '001',
                 m1.host + ':' + str(m1.port)]

    dsrc_content = '[repl-monitor-connections]\n' \
                   'connection1 = ' + m1.host + ':' + str(m1.port) + ':' + DN_DM + ':' + PW_DM + '\n' \
                   'connection2 = ' + m2.host + ':' + str(m2.port) + ':' + DN_DM + ':' + PW_DM + '\n' \
                   '\n' \
                   '[repl-monitor-aliases]\n' \
                   'M1 = ' + m1.host + ':' + str(m1.port) + '\n' \
                   'M2 = ' + m2.host + ':' + str(m2.port)

    connections = [m1.host + ':' + str(m1.port) + ':' + DN_DM + ':' + PW_DM,
                   m2.host + ':' + str(m2.port) + ':' + DN_DM + ':' + PW_DM]

    aliases = ['M1=' + m1.host + ':' + str(m1.port),
               'M2=' + m2.host + ':' + str(m2.port)]

    args = FakeArgs()
    args.connections = connections
    args.aliases = None
    args.json = False

    log.info('Run replication monitor with connections option')
    get_repl_monitor_info(m1, DEFAULT_SUFFIX, log, args)
    check_value_in_log_and_reset(content_list, connection_content, error_list=error_list)

    log.info('Run replication monitor with aliases option')
    args.aliases = aliases
    get_repl_monitor_info(m1, DEFAULT_SUFFIX, log, args)
    check_value_in_log_and_reset(content_list, alias_content)

    log.info('Run replication monitor with --json option')
    args.aliases = None
    args.json = True
    get_repl_monitor_info(m1, DEFAULT_SUFFIX, log, args)
    check_value_in_log_and_reset(json_list)

    with open(os.path.expanduser(DSRC_HOME), 'w+') as f:
        f.write(dsrc_content)

    args.connections = None
    args.aliases = None
    args.json = False

    log.info('Run replication monitor when .dsrc file is present with content')
    get_repl_monitor_info(m1, DEFAULT_SUFFIX, log, args)
    check_value_in_log_and_reset(content_list, alias_content)
    os.remove(os.path.expanduser(DSRC_HOME))

    log.info('Run replication monitor with connections option as if using dsconf CLI')
    # Perform same test than steps 2 test but without using directly the topology instance.
    # but with an instance similar to those than dsconf cli generates:
    # step 2 args
    args.connections = connections
    args.aliases = None
    args.json = False
    # args needed to generate an instance with dsrc_arg_concat
    args.instance = 'master1'
    args.basedn = None
    args.binddn = None
    args.bindpw = None
    args.pwdfile = None
    args.prompt = False
    args.starttls = False
    dsrc_inst = dsrc_arg_concat(args, None)
    inst = connect_instance(dsrc_inst, True, args)
    get_repl_monitor_info(inst, DEFAULT_SUFFIX, log, args)
    check_value_in_log_and_reset(content_list, connection_content, error_list=error_list)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
