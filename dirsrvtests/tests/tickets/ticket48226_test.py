# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2

from lib389._constants import (DEFAULT_SUFFIX, REPLICA_PURGE_DELAY, REPLICA_PURGE_INTERVAL, DN_CONFIG,
                              SUFFIX, VALGRIND_LEAK_STR)

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket48226_set_purgedelay(topology_m2):
    args = {REPLICA_PURGE_DELAY: '5',
            REPLICA_PURGE_INTERVAL: '5'}
    try:
        topology_m2.ms["master1"].replica.setProperties(DEFAULT_SUFFIX, None, None, args)
    except:
        log.fatal('Failed to configure replica')
        assert False
    try:
        topology_m2.ms["master2"].replica.setProperties(DEFAULT_SUFFIX, None, None, args)
    except:
        log.fatal('Failed to configure replica')
        assert False
    topology_m2.ms["master1"].modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-auditlog-logging-enabled', 'on')])
    topology_m2.ms["master2"].modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-auditlog-logging-enabled', 'on')])
    topology_m2.ms["master1"].restart()
    topology_m2.ms["master2"].restart()


def test_ticket48226_1(topology_m2):
    name = 'test_entry'
    dn = "cn=%s,%s" % (name, SUFFIX)

    topology_m2.ms["master1"].add_s(Entry((dn, {'objectclass': "top person".split(),
                                                'sn': name,
                                                'cn': name})))

    # First do an update that is replicated
    mods = [(ldap.MOD_ADD, 'description', '5')]
    topology_m2.ms["master1"].modify_s(dn, mods)

    nbtry = 0
    while (nbtry <= 10):
        try:
            ent = topology_m2.ms["master2"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
            if ent.hasAttr('description') and ent.getValue('description') == '5':
                break
        except ldap.NO_SUCH_OBJECT:
            pass
        nbtry = nbtry + 1
        time.sleep(1)
    assert nbtry <= 10

    # Stop M2 so that it will not receive the next update
    topology_m2.ms["master2"].stop(10)

    # ADD a new value that is not replicated
    mods = [(ldap.MOD_DELETE, 'description', '5')]
    topology_m2.ms["master1"].modify_s(dn, mods)

    # Stop M1 so that it will keep del '5' that is unknown from master2
    topology_m2.ms["master1"].stop(10)

    # Get the sbin directory so we know where to replace 'ns-slapd'
    sbin_dir = topology_m2.ms["master2"].get_sbin_dir()

    # Wrap valgrind in the try-finally block to make sure it is teared down
    try:
        if not topology_m2.ms["master2"].has_asan():
            valgrind_enable(sbin_dir)

        # start M2 to do the next updates
        topology_m2.ms["master2"].start()

        # ADD 'description' by '5'
        mods = [(ldap.MOD_DELETE, 'description', '5')]
        topology_m2.ms["master2"].modify_s(dn, mods)

        # DEL 'description' by '5'
        mods = [(ldap.MOD_ADD, 'description', '5')]
        topology_m2.ms["master2"].modify_s(dn, mods)

        # sleep of purge delay so that the next update will purge the CSN_7
        time.sleep(6)

        # ADD 'description' by '6' that purge the state info
        mods = [(ldap.MOD_ADD, 'description', '6')]
        topology_m2.ms["master2"].modify_s(dn, mods)

        # Restart master1
        # topology_m2.ms["master1"].start(30)

        if not topology_m2.ms["master2"].has_asan():
            results_file = valgrind_get_results_file(topology_m2.ms["master2"])

        # Stop master2
        topology_m2.ms["master2"].stop(30)

        # Check for leak
        if not topology_m2.ms["master2"].has_asan():
            if valgrind_check_file(results_file, VALGRIND_LEAK_STR, 'csnset_dup'):
                log.info('Valgrind reported leak in csnset_dup!')
                assert False
            else:
                log.info('Valgrind is happy!')

            # Check for invalid read/write
            if valgrind_check_file(results_file, VALGRIND_INVALID_STR, 'csnset_dup'):
                log.info('Valgrind reported invalid!')
                assert False
            else:
                log.info('Valgrind is happy!')

            # Check for invalid read/write
            if valgrind_check_file(results_file, VALGRIND_INVALID_STR, 'csnset_free'):
                log.info('Valgrind reported invalid!')
                assert False
            else:
                log.info('Valgrind is happy!')
    finally:
        if not topology_m2.ms["master2"].has_asan():
            valgrind_disable(sbin_dir)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
