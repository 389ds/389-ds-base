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
# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.5'), reason="Not implemented")]

from lib389.topologies import topology_m2

from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
ENCRYPTION_DN = 'cn=encryption,%s' % CONFIG_DN
RSA = 'RSA'
RSA_DN = 'cn=%s,%s' % (RSA, ENCRYPTION_DN)
ISSUER = 'cn=CAcert'
CACERT = 'CAcertificate'
SERVERCERT = 'Server-Cert'


@pytest.fixture(scope="module")
def add_entry(server, name, rdntmpl, start, num):
    log.info("\n######################### Adding %d entries to %s ######################" % (num, name))

    for i in range(num):
        ii = start + i
        dn = '%s%d,%s' % (rdntmpl, ii, DEFAULT_SUFFIX)
        try:
            server.add_s(Entry((dn, {'objectclass': 'top person extensibleObject'.split(),
                                     'uid': '%s%d' % (rdntmpl, ii),
                                     'cn': '%s user%d' % (name, ii),
                                     'sn': 'user%d' % (ii)})))
        except ldap.LDAPError as e:
            log.error('Failed to add %s ' % dn + e.message['desc'])
            assert False

def config_tls_agreements(topology_m2):
    log.info("######################### Configure SSL/TLS agreements ######################")
    log.info("######################## supplier1 <-- startTLS -> supplier2 #####################")

    log.info("##### Update the agreement of supplier1")
    m1 = topology_m2.ms["supplier1"]
    m1_m2_agmt = m1.agreement.list(suffix=DEFAULT_SUFFIX)[0].dn
    topology_m2.ms["supplier1"].modify_s(m1_m2_agmt, [(ldap.MOD_REPLACE, 'nsDS5ReplicaTransportInfo', b'TLS')])

    log.info("##### Update the agreement of supplier2")
    m2 = topology_m2.ms["supplier2"]
    m2_m1_agmt = m2.agreement.list(suffix=DEFAULT_SUFFIX)[0].dn
    topology_m2.ms["supplier2"].modify_s(m2_m1_agmt, [(ldap.MOD_REPLACE, 'nsDS5ReplicaTransportInfo', b'TLS')])

    time.sleep(1)

    topology_m2.ms["supplier1"].restart(10)
    topology_m2.ms["supplier2"].restart(10)

    log.info("\n######################### Configure SSL/TLS agreements Done ######################\n")


def set_ssl_Version(server, name, version):
    log.info("\n######################### Set %s on %s ######################\n" %
             (version, name))
    server.simple_bind_s(DN_DM, PASSWORD)
    server.modify_s(ENCRYPTION_DN, [(ldap.MOD_REPLACE, 'nsSSL3', b'off'),
                                    (ldap.MOD_REPLACE, 'nsTLS1', b'on'),
                                    (ldap.MOD_REPLACE, 'sslVersionMin', ensure_bytes(version)),
                                    (ldap.MOD_REPLACE, 'sslVersionMax', ensure_bytes(version))])


def test_ticket48784(topology_m2):
    """
    Set up 2way MMR:
        supplier_1 <----- startTLS -----> supplier_2

    Make sure the replication is working.
    Then, stop the servers and set only TLS1.0 on supplier_1 while TLS1.2 on supplier_2
    Replication is supposed to fail.
    """
    log.info("Ticket 48784 - Allow usage of OpenLDAP libraries that don't use NSS for crypto")

    #create_keys_certs(topology_m2)
    [i.enable_tls() for i in topology_m2]

    config_tls_agreements(topology_m2)

    add_entry(topology_m2.ms["supplier1"], 'supplier1', 'uid=m1user', 0, 5)
    add_entry(topology_m2.ms["supplier2"], 'supplier2', 'uid=m2user', 0, 5)

    time.sleep(10)

    log.info('##### Searching for entries on supplier1...')
    entries = topology_m2.ms["supplier1"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 10 == len(entries)

    log.info('##### Searching for entries on supplier2...')
    entries = topology_m2.ms["supplier2"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 10 == len(entries)

    log.info("##### openldap client just accepts sslVersionMin not Max.")
    set_ssl_Version(topology_m2.ms["supplier1"], 'supplier1', 'TLS1.0')
    set_ssl_Version(topology_m2.ms["supplier2"], 'supplier2', 'TLS1.2')

    log.info("##### restart supplier[12]")
    topology_m2.ms["supplier1"].restart(timeout=10)
    topology_m2.ms["supplier2"].restart(timeout=10)

    log.info("##### replication from supplier_1 to supplier_2 should be ok.")
    add_entry(topology_m2.ms["supplier1"], 'supplier1', 'uid=m1user', 10, 1)
    log.info("##### replication from supplier_2 to supplier_1 should fail.")
    add_entry(topology_m2.ms["supplier2"], 'supplier2', 'uid=m2user', 10, 1)

    time.sleep(10)

    log.info('##### Searching for entries on supplier1...')
    entries = topology_m2.ms["supplier1"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 11 == len(entries)  # This is supposed to be "1" less than supplier 2's entry count

    log.info('##### Searching for entries on supplier2...')
    entries = topology_m2.ms["supplier2"].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*)')
    assert 12 == len(entries)

    log.info("Ticket 48784 - PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode

    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
