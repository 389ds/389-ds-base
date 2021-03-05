import logging
import pytest
import os
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m2 as topo
from lib389.replica import BootstrapReplicationManager,  Replicas
from lib389.idm.user import TEST_USER_PROPERTIES, UserAccounts,  UserAccount
from lib389.idm.group import Group

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

BOOTSTRAP_MGR_DN = 'uid=replication manager,cn=config'
BOOTSTRAP_MGR_PWD = 'boostrap_manager_password'
BIND_GROUP_DN = 'cn=replication_managers,' + DEFAULT_SUFFIX


def test_repl_agmt_bootstrap_credentials(topo):
    """Test that the agreement bootstrap credentials works if the default
    credentials fail for some reason.

    :id: 38c8095c-d958-415a-b602-74854b7882b3
    :customerscenario: True
    :setup: 2 Supplier Instances
    :steps:
        1.  Change the bind dn group member passwords
        2.  Verify replication is not working
        3.  Create a new repl manager on supplier 2 for bootstrapping
        4.  Add bootstrap credentials to agmt on supplier 1
        5.  Verify replication is now working with bootstrap creds
        6.  Trigger new repl session and default credentials are used first
    :expectedresults:
        1.  Success
        2.  Success
        3.  Success
        4.  Success
        5.  Success
        6.  Success
    """

    # Gather all of our objects for the test
    m1 = topo.ms["supplier1"]
    m2 = topo.ms["supplier2"]
    supplier1_replica = Replicas(m1).get(DEFAULT_SUFFIX)
    supplier2_replica = Replicas(m2).get(DEFAULT_SUFFIX)
    supplier2_users = UserAccounts(m2, DEFAULT_SUFFIX)
    m1_agmt = supplier1_replica.get_agreements().list()[0]
    num_of_original_users = len(supplier2_users.list())

    # Change the member's passwords which should break replication
    bind_group = Group(m2, dn=BIND_GROUP_DN)
    members = bind_group.list_members()
    for member_dn in members:
        member = UserAccount(m2, dn=member_dn)
        member.replace('userPassword', 'not_right')
    time.sleep(3)
    m1_agmt.pause()
    m1_agmt.resume()

    # Verify replication is not working, a new user should not be replicated
    users = UserAccounts(m1, DEFAULT_SUFFIX)
    test_user = users.ensure_state(properties=TEST_USER_PROPERTIES)
    time.sleep(3)
    assert len(supplier2_users.list()) == num_of_original_users

    # Create a repl manager on replica
    repl_mgr = BootstrapReplicationManager(m2, dn=BOOTSTRAP_MGR_DN)
    mgr_properties = {
        'uid': 'replication manager',
        'cn': 'replication manager',
        'userPassword': BOOTSTRAP_MGR_PWD,
    }
    repl_mgr.create(properties=mgr_properties)

    # Update supplier 2 config
    supplier2_replica.remove_all('nsDS5ReplicaBindDNGroup')
    supplier2_replica.remove_all('nsDS5ReplicaBindDnGroupCheckInterval')
    supplier2_replica.replace('nsDS5ReplicaBindDN', BOOTSTRAP_MGR_DN)

    # Add bootstrap credentials to supplier1 agmt, and restart agmt
    m1_agmt.replace('nsds5ReplicaBootstrapTransportInfo', 'LDAP')
    m1_agmt.replace('nsds5ReplicaBootstrapBindMethod', 'SIMPLE')
    m1_agmt.replace('nsds5ReplicaBootstrapCredentials', BOOTSTRAP_MGR_PWD)
    m1_agmt.replace('nsds5ReplicaBootstrapBindDN', BOOTSTRAP_MGR_DN)
    m1_agmt.pause()
    m1_agmt.resume()

    # Verify replication is working.  The user should have been replicated
    time.sleep(3)
    assert len(supplier2_users.list()) > num_of_original_users

    # Finally check if the default credentials are used on the next repl
    # session.  Clear out the logs, and disable log buffering.  Then
    # trigger a replication update/session.
    m1_agmt.pause()
    m2.stop()
    m2.deleteLog(m2.accesslog)  # Clear out the logs
    m2.start()
    m2.config.set('nsslapd-accesslog-logbuffering', 'off')
    m1_agmt.resume()
    test_user.delete()
    time.sleep(3)

    # We know if the default credentials are used it will fail (err=49)
    results = m2.ds_access_log.match('.* err=49 .*')
    assert len(results) > 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

