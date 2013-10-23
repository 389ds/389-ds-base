"""Brooker classes to organize ldap methods.
   Stuff is split in classes, like:
   * Replica
   * Backend
   * Suffix

   You will access this from:
   DirSrv.backend.methodName()
"""

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
from random import randint
from lib389.brooker import Replica
from lib389 import MASTER_TYPE, DN_MAPPING_TREE, DN_CHANGELOG, DN_LDBM
# Test harnesses
from dsadmin_test import drop_backend, addbackend_harn
from dsadmin_test import drop_added_entries

conn = None
added_entries = None
added_backends = None

MOCK_REPLICA_ID = '12'
MOCK_TESTREPLICA_DN = "cn=testReplicadb,cn=ldbm database,cn=plugins,cn=config"

def setup():
    # uses an existing 389 instance
    # add a suffix
    # add an agreement
    # This setup is quite verbose but to test DirSrv method we should
    # do things manually. A better solution would be to use an LDIF.
    global conn
    conn = DirSrv(**config.auth)
    conn.verbose = True
    conn.added_entries = []
    conn.added_backends = set(['o=mockbe1'])
    conn.added_replicas = []

    # add a backend for testing ruv and agreements
    suffix = 'o=testReplica'
    backend = 'testReplicadb'
    backendEntry, dummy = conn.backend.add(suffix, benamebase=backend)
    suffixEntry = conn.backend.setup_mt(suffix, backend)


    # add another backend for testing replica.add()
    suffix = 'o=testReplicaCreation'
    backend = 'testReplicaCreationdb'
    backendEntry, dummy = conn.backend.add(suffix, benamebase=backend)
    suffixEntry = conn.backend.setup_mt(suffix, backend)


def teardown():
    global conn
    drop_added_entries(conn)
    conn.delete_s(','.join(['cn="o=testreplica"', DN_MAPPING_TREE]))
    conn.backend.delete('testReplicadb')
    
    conn.delete_s(','.join(['cn="o=testReplicaCreation"', DN_MAPPING_TREE]))
    conn.backend.delete('testReplicaCreationdb')

    
def list_test():
    ret = conn.backend.list()
    ret = [x.dn for x in ret]
    assert len(ret) >=2, "Bad result %r" % ret

    
def list_by_name_test():
    tests = [({'name': 'testreplicadb'}, MOCK_TESTREPLICA_DN)]
    for params, result in tests:
        ret = conn.backend.list(**params)
        ret = [x.dn for x in ret]
        assert result in ret, "Result was %r " % ret
    

def list_by_suffix_test():
    tests = [
        ({'suffix': 'o=testreplica'}, MOCK_TESTREPLICA_DN)
        ]

    for params, result in tests:
        ret = conn.backend.list(**params)
        ret = [x.dn for x in ret]
        assert result in ret , "Result was %r" % ret

def list_suffixes():
    tests = [ 'o=testreplica'  ]
    suffixes = conn.backend.suffixes()
    for params in tests:
        assert params in suffixes, "Missing %r in %r" % (params, suffixes)

def readonly_test():
    bename = 'testReplicadb'
    backend_dn = ','.join(('cn=' + bename, DN_LDBM))
    try:
        conn.backend.readonly(bename=bename, readonly='on')
        e = conn.getEntry(backend_dn)
        ret = e.getValue('nsslapd-readonly')
        assert ret == 'on', "Readonly value mismatch: %r " % ret 
    finally:
        conn.backend.readonly(bename=bename, readonly='off')
        e = conn.getEntry(backend_dn)
        ret = e.getValue('nsslapd-readonly')
        assert ret == 'off', "Readonly value mismatch: %r " % ret 
