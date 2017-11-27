import os
import sys
import time
import ldap
import logging
import pytest
import signal
import threading
from lib389 import DirSrv
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.idm.directorymanager import DirectoryManager

DEBUGGING = os.getenv('DEBUGGING', default=False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

MAX_CONNS = 10000000
MAX_THREADS = 20
STOP = False
HOSTNAME = DirSrvTools.getLocalhost()
PORT = 389


class TopologyStandalone(object):
    """The DS Topology Class"""
    def __init__(self, standalone):
        """Init"""
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    """Create DS Deployment"""

    # Creating standalone instance ...
    standalone = DirSrv(verbose=DEBUGGING)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SECURE_PORT] = SECUREPORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        """If we are debugging just stop the instances, otherwise remove them
        """
        if DEBUGGING:
            standalone.stop()
        else:
            standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def signalHandler(signal, frame):
    """
    handle control-C cleanly
    """
    global STOP
    STOP = True
    sys.exit(0)


def init(inst):
    """Set the idle timeout, and add sample entries
    """

    try:
        inst.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                   'nsslapd-idletimeout',
                                   '5')])
    except ldap.LDAPError as e:
        log.fatal('Failed to set idletimeout: ' + str(e))
        assert False

    try:
        inst.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                   'nsslapd-enable-nunc-stans',
                                   'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable nunc-stans: ' + str(e))
        assert False

    for idx in range(0, 9):
        user_dn = 'uid=entry%d,%s' % (idx, DEFAULT_SUFFIX)
        try:
            inst.add_s(Entry((user_dn,
                {'objectclass': ['top', 'extensibleObject'],
                 'uid': 'entry%d' % idx,
                 'cn': 'entry%d' % idx,
                 'userpassword': 'password'})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user entry (%s): %s' % (user_dn, str(e)))
            assert False

    inst.restart()


class BindOnlyConn(threading.Thread):
    """This class opens and closes connections
    """
    def __init__(self, inst):
        """Initialize the thread class with the server instance info"""
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst

    def run(self):
        """Keep opening and closing connections"""
        idx = 0
        err_count = 0
        global STOP
        while idx < MAX_CONNS and not STOP:
            try:
                conn = DirectoryManager(self.inst).bind(connOnly=True)
                conn.unbind_s()
                time.sleep(.2)
                err_count = 0
            except ldap.LDAPError as e:
                err_count += 1
                if err_count > 3:
                    log.error('BindOnlyConn exiting thread: %s' %
                          (str(e)))
                    return
                time.sleep(.4)
            idx += 1


class IdleConn(threading.Thread):
    """This class opens and closes connections
    """
    def __init__(self, inst):
        """Initialize the thread class withte server isntance info"""
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst

    def run(self):
        """Assume idleTimeout is set to less than 10 seconds
        """
        idx = 0
        err_count = 0
        global STOP
        while idx < (MAX_CONNS / 10) and not STOP:
            try:
                conn = self.inst.clone()
                conn.simple_bind_s('uid=entry0,dc=example,dc=com', 'password')
                conn.search_s('dc=example,dc=com', ldap.SCOPE_SUBTREE,
                              'uid=*')
                time.sleep(10)
                conn.search_s('dc=example,dc=com', ldap.SCOPE_SUBTREE,
                              'cn=*')
                conn.unbind_s()
                time.sleep(.2)
                err_count = 0
            except ldap.LDAPError as e:
                err_count += 1
                if err_count > 3:
                    log.error('IdleConn exiting thread: %s' %
                              (str(e)))
                    return
                time.sleep(.4)
            idx += 1


class LongConn(threading.Thread):
    """This class opens and closes connections to a specified server
    """
    def __init__(self, inst):
        """Initialize the thread class with the server instance info"""
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst

    def run(self):
        """Assume idleTimeout is set to less than 10 seconds
        """
        idx = 0
        err_count = 0
        global STOP
        while idx < MAX_CONNS and not STOP:
            try:
                conn = self.inst.clone()
                conn.search_s('dc=example,dc=com', ldap.SCOPE_SUBTREE,
                              'objectclass=*')
                conn.search_s('dc=example,dc=com', ldap.SCOPE_SUBTREE,
                              'uid=mark')
                conn.search_s('dc=example,dc=com', ldap.SCOPE_SUBTREE,
                              'cn=*')
                conn.search_s('', ldap.SCOPE_BASE, 'objectclass=*')
                conn.unbind_s()
                time.sleep(.2)
                err_count = 0
            except ldap.LDAPError as e:
                err_count += 1
                if err_count > 3:
                    log.error('LongConn search exiting thread: %s' %
                              (str(e)))
                    return
                time.sleep(.4)
            idx += 1


def test_connection_load(topology):
    """Send the server a variety of connections using many threads:
        - Open, Bind, Close
        - Open, Bind, Search, wait to trigger idletimeout, Search, Close
        - Open, Bind, Search, Search, Search, Close
    """

    # setup the control-C signal handler
    signal.signal(signal.SIGINT, signalHandler)

    # Set the config and add sample entries
    log.info('Initializing setup...')
    init(topology.standalone)

    #
    # Bind/Unbind Conn Threads
    #
    log.info('Launching Bind-Only Connection threads...')
    threads = []
    idx = 0
    while idx < MAX_THREADS:
        threads.append(BindOnlyConn(topology.standalone))
        idx += 1
    for thread in threads:
        thread.start()
        time.sleep(0.1)

    #
    # Idle Conn Threads
    #
    log.info('Launching Idle Connection threads...')
    idx = 0
    idle_threads = []
    while idx < MAX_THREADS:
        idle_threads.append(IdleConn(topology.standalone))
        idx += 1
    for thread in idle_threads:
        thread.start()
        time.sleep(0.1)

    #
    # Long Conn Threads
    #
    log.info('Launching Long Connection threads...')
    idx = 0
    long_threads = []
    while idx < MAX_THREADS:
        long_threads.append(LongConn(topology.standalone))
        idx += 1
    for thread in long_threads:
        thread.start()
        time.sleep(0.1)

    #
    # Now wait for all the threads to complete
    #
    log.info('Waiting for threads to finish...')
    while threading.active_count() > 0:
        time.sleep(1)

    log.info('Done')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

