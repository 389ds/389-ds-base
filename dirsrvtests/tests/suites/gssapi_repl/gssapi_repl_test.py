# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.mit_krb5 import MitKrb5
from lib389.topologies import topology_m2

#########################################
#
# WARNING!!!!! If this test is failing, and your here to find out why, the
# reason is very likely your hosts file!!!!
#
# IT MUST LOOK LIKE THIS BELOW: Note the unique IPS for each kdc name!
#
# 127.0.0.1       ldapkdc.example.com localhost
# 127.0.1.1       ldapkdc1.example.com
# 127.0.2.1       ldapkdc2.example.com
#
#########################################

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

REALM = "EXAMPLE.COM"

HOST_MASTER_1 = 'ldapkdc1.example.com'
HOST_MASTER_2 = 'ldapkdc2.example.com'


def _create_machine_ou(inst):
    inst.add_s(Entry(("ou=Machines,%s" % DEFAULT_SUFFIX, {
        'objectClass': 'top organizationalUnit'.split(),
        'ou': 'Machines'
    }
                      ))
               )


def _create_machine_account(inst, name):
    # Create the simple security objects for the servers to replicate to
    inst.add_s(Entry(("uid=%s,ou=Machines,%s" % (name, DEFAULT_SUFFIX),
                      {
                          'objectClass': 'top account'.split(),
                          'uid': name
                      }
                      )))


def _check_machine_account(inst, name):
    r = inst.search_s('ou=Machines,%s' % DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=%s)' % name)
    if len(r) > 0:
        return True
    return False


def _allow_machine_account(inst, name):
    # First we need to get the mapping tree dn
    mt = inst.mappingtree.list(suffix=DEFAULT_SUFFIX)[0]
    inst.modify_s('cn=replica,%s' % mt.dn, [
        (ldap.MOD_REPLACE, 'nsDS5ReplicaBindDN', "uid=%s,ou=Machines,%s" % (name, DEFAULT_SUFFIX))
    ])


def test_gssapi_repl(topology_m2):
    """Create a kdc, then using that, provision two masters which have a gssapi
    authenticated replication agreement.
    """

    return
    master1 = topology_m2.ms["master1"]
    master2 = topology_m2.ms["master2"]

    # Create the locations on each master for the other to bind to.
    _create_machine_ou(master1)
    _create_machine_ou(master2)

    _create_machine_account(master1, 'ldap/%s' % HOST_MASTER_1)
    _create_machine_account(master1, 'ldap/%s' % HOST_MASTER_2)
    _create_machine_account(master2, 'ldap/%s' % HOST_MASTER_1)
    _create_machine_account(master2, 'ldap/%s' % HOST_MASTER_2)

    # Set on the cn=replica config to accept the other masters princ mapping under mapping tree
    _allow_machine_account(master1, 'ldap/%s' % HOST_MASTER_2)
    _allow_machine_account(master2, 'ldap/%s' % HOST_MASTER_1)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to master 2

    # Set the replica bind method to sasl gssapi
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_METHOD: 'SASL/GSSAPI',
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1

    # Set the replica bind method to sasl gssapi
    properties = {RA_NAME: r'meTo_$host:$port',
                  RA_METHOD: 'SASL/GSSAPI',
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Add a user to master 1
    _create_machine_account(master1, 'http/one.example.com')
    # Check it's on 2
    time.sleep(5)
    assert (_check_machine_account(master2, 'http/one.example.com'))
    # Add a user to master 2
    _create_machine_account(master2, 'http/two.example.com')
    # Check it's on 1
    time.sleep(5)
    assert (_check_machine_account(master2, 'http/two.example.com'))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
