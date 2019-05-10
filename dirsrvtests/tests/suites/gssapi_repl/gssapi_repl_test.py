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

pytestmark = pytest.mark.tier2

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
    """Test gssapi authenticated replication agreement of two masters using KDC

    :id: 552850aa-afc3-473e-9c39-aae802b46f11

    :setup: MMR with two masters

    :steps:
         1. Create the locations on each master for the other master to bind to
         2. Set on the cn=replica config to accept the other masters mapping under mapping tree
         3. Create the replication agreements from M1->M2 and vice versa (M2->M1)
         4. Set the replica bind method to sasl gssapi for both agreements
         5. Initialize all the agreements
         6. Create a user on M1 and check if user is created on M2
         7. Create a user on M2 and check if user is created on M1

    :expectedresults:
         1. Locations should be added successfully
         2. Configuration should be added successfully
         3. Replication agreements should be added successfully
         4. Bind method should be set to sasl gssapi for both agreements
         5. Agreements should be initialized successfully
         6. Test User should be created on M1 and M2 both
         7. Test User should be created on M1 and M2 both
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
