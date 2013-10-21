"""Brooker classes to organize ldap methods.
   Stuff is split in classes, like:
   * Replica
   * Backend
   * Suffix

   You will access this from:
   DSAdmin.backend.methodName()
"""


import config
from config import log
from config import *

import dsadmin
from dsadmin import DSAdmin, Entry
# Test harnesses
from dsadmin_test import drop_backend, addbackend_harn
from dsadmin_test import drop_added_entries

conn = None
added_entries = None
added_backends = None

MOCK_REPLICA_ID = '12'
MOCK_TESTREPLICA_DN = "cn=testReplica,cn=ldbm database,cn=plugins,cn=config"

def setup():
    # uses an existing 389 instance
    # add a suffix
    # add an agreement
    # This setup is quite verbose but to test dsadmin method we should
    # do things manually. A better solution would be to use an LDIF.
    global conn
    conn = DSAdmin(**config.auth)
    conn.verbose = True
    conn.added_entries = []
    conn.added_backends = set(['o=mockbe1'])
    conn.added_replicas = []
    """  
    # add a backend for testing ruv and agreements
    addbackend_harn(conn, 'testReplica')

    # add another backend for testing replica.add()
    addbackend_harn(conn, 'testReplicaCreation')
    """

def teardown():
    global conn
    conn.config.loglevel([dsadmin.LOG_CACHE])
    conn.config.loglevel([dsadmin.LOG_CACHE], level='access')
    
    """
    drop_added_entries(conn)
    conn.delete_s(','.join(['cn="o=testreplica"', DN_MAPPING_TREE]))
    drop_backend(conn, 'o=testreplica')
    #conn.delete_s('o=testreplica')
    """
    
def loglevel_test():
    vals = [dsadmin.LOG_CACHE, dsadmin.LOG_REPLICA, dsadmin.LOG_CONNECT]
    expected = sum(vals)
    assert conn.config.loglevel(vals) == expected
    ret = conn.config.get('nsslapd-errorlog-level') 
    assert ret == str(expected), "expected: %r got: %r" % (expected, ret)
    

def loglevel_update_test():
    vals = [dsadmin.LOG_CACHE, dsadmin.LOG_CONNECT]
    e = sum(vals)
    assert conn.config.loglevel(vals) == e
    vals = [dsadmin.LOG_REPLICA]
    ret = conn.config.loglevel(vals, update=True) 
    assert ret == (e + sum(vals)), "expected %s got %s" % (e + sum(vals), ret)


def access_loglevel_test():
    vals = [dsadmin.LOG_CACHE, dsadmin.LOG_REPLICA, dsadmin.LOG_CONNECT]
    assert conn.config.loglevel(vals, level='access') == sum(vals)

