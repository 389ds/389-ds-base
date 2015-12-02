# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Brooker classes to organize ldap methods.
   Stuff is split in classes, like:
   * Replica
   * Backend
   * Suffix

   You will access this from:
   DirSrv.backend.methodName()
"""

from . import config
from .config import *

import lib389
from lib389 import DirSrv


conn = None
added_entries = None
added_backends = None

MOCK_REPLICA_ID = '12'
MOCK_TESTREPLICA_DN = "cn=testReplica,cn=ldbm database,cn=plugins,cn=config"


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
    """
    # add a backend for testing ruv and agreements
    addbackend_harn(conn, 'testReplica')

    # add another backend for testing replica.add()
    addbackend_harn(conn, 'testReplicaCreation')
    """


def teardown():
    global conn
    conn.config.loglevel([lib389.LOG_CACHE])
    conn.config.loglevel([256], service='access')

    """
    drop_added_entries(conn)
    conn.delete_s(','.join(['cn="o=testreplica"', DN_MAPPING_TREE]))
    drop_backend(conn, 'o=testreplica')
    #conn.delete_s('o=testreplica')
    """


def loglevel_test():
    vals = [lib389.LOG_CACHE, lib389.LOG_REPLICA, lib389.LOG_CONNECT]
    expected = sum(vals)
    assert conn.config.loglevel(vals) == expected
    ret = conn.config.get('nsslapd-errorlog-level')
    assert ret == str(expected), "expected: %r got: %r" % (expected, ret)


def loglevel_update_test():
    vals = [lib389.LOG_CACHE, lib389.LOG_CONNECT]
    e = sum(vals)
    assert conn.config.loglevel(vals) == e
    vals = [lib389.LOG_REPLICA]
    ret = conn.config.loglevel(vals, update=True)
    assert ret == (e + sum(vals)), "expected %s got %s" % (e + sum(vals), ret)


def access_loglevel_test():
    vals = [lib389.LOG_CACHE, lib389.LOG_REPLICA, lib389.LOG_CONNECT]
    assert conn.config.loglevel(vals, service='access') == sum(vals)
