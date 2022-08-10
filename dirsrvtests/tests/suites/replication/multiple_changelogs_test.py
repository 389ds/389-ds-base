import ldap
import logging
import pytest
import os
import threading
import time
from lib389._constants import *
from lib389.topologies import topology_m1c1 as topo
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.domain import Domain
from lib389.backend import Backend
from lib389.replica import Replicas, ReplicationManager
from lib389.config import LDBMConfig

log = logging.getLogger(__name__)

SECOND_SUFFIX = 'dc=second_suffix'
MOD_COUNT = 50

class DoMods(threading.Thread):
    """modify the suffix entry"""
    def __init__(self, inst, task):
        """
        Initialize the thread
        """
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.name = inst.serverid
        self.task = task

    def run(self):
        """
        Start adding users
        """
        idx = 0
        conn = DirectoryManager(self.inst).bind()
        domain = Domain(conn, DEFAULT_SUFFIX)
        while idx < MOD_COUNT:
            try:
                domain.replace('description', str(idx))
            except:
                if self.task == "import":
                    # Failures are expected during an import
                    pass
                else:
                    # export, should not fail
                    log.fatal('Updates should not fail during an export')
                    assert False
            idx += 1


def test_multiple_changelogs(topo):
    """Test the multiple suffixes can be replicated with the new per backend
    changelog.

    :id: eafcdb57-4ea2-4887-a0a8-9e4d295f4f4d
    :setup: Supplier Instance, Consumer Instance
    :steps:
        1. Create s second suffix
        2. Enable replication for second backend
        3. Perform some updates on both backends and make sure replication is
           working for both backends

    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    supplier = topo.ms['supplier1']
    consumer = topo.cs['consumer1']

    # Create second suffix dc=second_backend on both replicas
    for inst in [supplier, consumer]:
        # Create the backends
        props = {'cn': 'secondRoot', 'nsslapd-suffix': SECOND_SUFFIX}
        be = Backend(inst)
        be.create(properties=props)
        be.create_sample_entries('001004002')

    # Setup replication for second suffix
    repl = ReplicationManager(SECOND_SUFFIX)
    repl.create_first_supplier(supplier)
    repl.join_consumer(supplier, consumer)

    # Test replication works for each backend
    for suffix in [DEFAULT_SUFFIX, SECOND_SUFFIX]:
        replicas = Replicas(supplier)
        replica = replicas.get(suffix)
        log.info("Testing replication for: " + suffix)
        assert replica.test_replication([consumer])


def test_multiple_changelogs_export_import(topo):
    """Test that we can export and import the replication changelog

    :id: b74fcaaf-a13f-4ee0-98f9-248b281f8700
    :setup: Supplier Instance, Consumer Instance
    :steps:
        1. Create s second suffix
        2. Enable replication for second backend
        3. Perform some updates on a backend, and export the changelog
        4. Do an export and import while the server is idle
        5. Do an import while the server is under load

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    SECOND_SUFFIX = 'dc=second_suffix'
    supplier = topo.ms['supplier1']
    consumer = topo.cs['consumer1']
    supplier.config.set('nsslapd-errorlog-level', '0')
    # Create second suffix dc=second_backend on both replicas
    for inst in [supplier, consumer]:
        # Create the backends
        props = {'cn': 'secondRoot', 'nsslapd-suffix': SECOND_SUFFIX}
        be = Backend(inst)
        try:
            be.create(properties=props)
            be.create_sample_entries('001004002')
        except ldap.UNWILLING_TO_PERFORM:
            pass

    # Setup replication for second suffix
    try:
        repl = ReplicationManager(SECOND_SUFFIX)
        repl.create_first_supplier(supplier)
        repl.join_consumer(supplier, consumer)
    except ldap.ALREADY_EXISTS:
        pass

    # Put the replica under load, and export the changelog
    replicas = Replicas(supplier)
    replica = replicas.get(DEFAULT_SUFFIX)
    doMods1 = DoMods(supplier, task="export")
    doMods1.start()
    replica.begin_task_cl2ldif()
    doMods1.join()
    replica.task_finished()

    supplier.restart()
    assert replica.test_replication([consumer])

    # While idle, do an export and import, and make sure replication still works
    log.info("Testing idle server with CL export and import...")
    ldbm_config = LDBMConfig(supplier)
    ldbm_config.set('nsslapd-readonly', 'on')  # prevent keep alive updates
    replica.begin_task_cl2ldif()
    replica.task_finished()
    replica.begin_task_ldif2cl()
    replica.task_finished()
    ldbm_config.set('nsslapd-readonly', 'off')
    assert replica.test_replication([consumer])

    # stability test, put the replica under load, import the changelog, and make
    # sure server did not crash.
    log.info("Testing busy server with CL import...")
    doMods2 = DoMods(supplier, task="import")
    doMods2.start()
    replica.begin_task_ldif2cl()
    doMods2.join()
    replica.task_finished()
    # Replication will be broken so no need to test it.  This is just make sure
    # the import works, and the server is stable
    assert supplier.status()
    assert consumer.status()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

