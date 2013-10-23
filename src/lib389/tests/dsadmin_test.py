from nose import *
from nose.tools import *

import config
from config import log
from config import *

import ldap
import time
import sys
import lib389
from lib389 import DirSrv, Entry
from lib389 import NoSuchEntryError
from lib389 import utils
from lib389.tools import DirSrvTools
from subprocess import Popen


conn = None
added_entries = None
added_backends = None

def harn_nolog():
    conn.config.loglevel([lib389.LOG_DEFAULT])
    conn.config.loglevel([lib389.LOG_DEFAULT], level='access')


def setup():
    global conn
    conn = DirSrv(**config.auth)
    conn.verbose = True
    conn.added_entries = []
    conn.added_backends = set(['o=mockbe2'])
    conn.added_replicas = []
    harn_nolog()
    
def setup_backend():
    global conn
    suffix = 'o=addressbook6'
    backend = 'addressbook6db'

    #create backend and suffix
    backendEntry, dummy = conn.backend.add(suffix, benamebase=backend)
    suffixEntry = conn.backend.setup_mt(suffix, backend)


def teardown():
    global conn
    conn.rebind()
    drop_added_entries(conn)
    
def drop_added_entries(conn):    
    while conn.added_entries:
        try:
            e = conn.added_entries.pop()
            log.info("removing entries %r" % conn.added_backends)
            conn.delete_s(e)
        except ldap.NOT_ALLOWED_ON_NONLEAF:
            log.error("Entry is not a leaf: %r" % e)
        except ldap.NO_SUCH_OBJECT:
            log.error("Cannot remove entry: %r" % e)

    log.info("removing backends %r" % conn.added_backends)
    for suffix in conn.added_backends:
        try:
            drop_backend(conn, suffix)
        except:
            log.exception("error removing %r" % suffix)
    for r in conn.added_replicas:
        try:
            drop_backend(conn, suffix=None, bename=r)
        except:
            log.exception("error removing %r" % r)


def drop_backend(conn, suffix, bename=None, maxnum=50):
    if not bename:
        bename = [x.dn for x in conn.getBackendsForSuffix(suffix)]
    
    if not bename:
        return None
        
    assert bename, "Missing bename for %r" % suffix
    if not hasattr(bename, '__iter__'):
        bename = [','.join(['cn=%s' % bename, lib389.DN_LDBM])]
    for be in bename:
        log.debug("removing entry from %r" % be)
        leaves = [x.dn for x in conn.search_s(
            be, ldap.SCOPE_SUBTREE, '(objectclass=*)', ['cn'])]
        # start deleting the leaves - which have the max number of ","
        leaves.sort(key=lambda x: x.count(","))
        while leaves and maxnum:
            # to avoid infinite loops
            # limit the iterations
            maxnum -= 1
            try:
                log.debug("removing %s" % leaves[-1])
                conn.delete_s(leaves[-1])
                leaves.pop()
            except:
                leaves.insert(0, leaves.pop())

        if not maxnum:
            raise Exception("BAD")


#
# Tests
#


def addbackend_harn(conn, name, beattrs=None):
    """Create the suffix o=name and its backend."""
    suffix = "o=%s" % name
    e = Entry((suffix, {
               'objectclass': ['top', 'organization'],
               'o': [name]
               }))

    try:
        ret = conn.addSuffix(suffix, bename=name, beattrs=beattrs)
    except ldap.ALREADY_EXISTS:
        raise
    finally:
        conn.added_backends.add(suffix)

    conn.add(e)
    conn.added_entries.append(e.dn)
    
    return ret


def setupBackend_ok_test():
    "setupBackend_ok calls brooker.Backend.add"
    try:
        backendEntry, dummy = conn.backend.add('o=mockbe5', benamebase='mockbe5')
        assert backendEntry
    except ldap.ALREADY_EXISTS:
        raise
    finally:
        conn.backend.delete(benamebase='mockbe5')


@raises(ldap.ALREADY_EXISTS)
def setupBackend_double_test():
    "setupBackend_double calls brooker.Backend.add"
    backendEntry, dummy = conn.backend.add('o=mockbe3', benamebase='mockbe3')
    backendEntry, dummy = conn.backend.add('o=mockbe3', benamebase='mockbe3')


def addsuffix_test():
    # identical to getMTEntry_present_test in dsadmin_basic_test
    #addbackend_harn(conn, 'addressbook16')
    #conn.added_backends.add('o=addressbook16')
    pass


def addreplica_write_test():
    suffix = 'o=ab3'
    backend = 'ab3'
    user = {
        'binddn': 'uid=rmanager,cn=config',
        'bindpw': 'password'
    }
    replica = {
        'suffix': suffix,
        'type': lib389.MASTER_TYPE,
        'id': 124
    }
    replica.update(user)
    
    #create backend and suffix
    backendEntry, dummy = conn.backend.add(suffix, benamebase=backend)
    suffixEntry = conn.backend.setup_mt(suffix, backend)

    ret = conn.replicaSetupAll(replica)
    
    assert ret != -1, "Error in setup replica: %s" % ret


def prepare_master_replica_test():
    """prepare_master_replica -> Replica.changelog"""
    user = {
        'binddn': 'uid=rmanager,cn=config',
        'bindpw': 'password'
    }
    conn.enableReplLogging()
    e = conn.setupBindDN(**user)
    conn.added_entries.append(e.dn)

    # only for Writable
    e = conn.replica.changelog()
    conn.added_entries.append(e.dn)


@with_setup(setup_backend)
def setupAgreement_test():
    consumer = MockDirSrv()
    args = {
        'suffix': "o=addressbook6",
        #'bename': "userRoot",
        'binddn': "uid=rmanager,cn=config",
        'bindpw': "password",
        'rtype': lib389.MASTER_TYPE,
        'rid': '1234'
    }
    conn.replica.add(**args)
    conn.added_entries.append(args['binddn'])

    dn_replica = conn.setupAgreement(consumer, args)


def stop_start_test():
    # dunno why DirSrv.start|stop writes to dirsrv error-log
    conn.errlog = "/tmp/dirsrv-errlog"
    open(conn.errlog, "w").close()
    DirSrvTools.stop(conn)
    log.info("server stopped")
    DirSrvTools.start(conn)
    log.info("server start")
    time.sleep(5)
    # save and restore conn settings after restart
    tmp = conn.added_backends, conn.added_entries
    setup()
    conn.added_backends, conn.added_entries = tmp
    assert conn.search_s(
        *utils.searches['NAMINGCONTEXTS']), "Missing namingcontexts"


def setupSSL_test():
    ssl_args = {
        'dirsrv': conn,
        'secport': 22636,
        'sourcedir': None,
        'secargs': {'nsSSLPersonalitySSL': 'localhost'},
    }
    cert_dir = conn.getDseAttr('nsslapd-certdir')
    assert cert_dir, "Cannot retrieve cert dir"

    log.info("Initialize the cert store with an empty password: %r", cert_dir)
    fd_null = open('/dev/null', 'w')
    open('%s/pin.txt' % cert_dir, 'w').close()
    cmd_initialize = 'certutil -d %s -N -f %s/pin.txt' % (cert_dir, cert_dir)
    Popen(cmd_initialize.split(), stderr=fd_null)

    log.info("Creating a self-signed cert for the server in %r" % cert_dir)
    cmd_mkcert = 'certutil -d %s -S -n localhost  -t CTu,Cu,Cu  -s cn=localhost -x' % cert_dir
    Popen(cmd_mkcert.split(), stdin=open("/dev/urandom"), stderr=fd_null)

    log.info("Testing ssl configuration")
    ssl_args.update({'dirsrv': conn})
    DirSrvTools.setupSSL(**ssl_args)
