# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import subprocess
from lib389.utils import *
from lib389.replica import Replicas, Replica, ReplicationManager
from lib389._constants import *
from lib389.config import CertmapLegacy
from lib389.idm.nscontainer import nsContainers
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.services import ServiceAccounts
from lib389.topologies import topology_m2 as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _create_container(inst, dn, name):
    """Creates container entry"""

    conts = nsContainers(inst, dn)
    cont = conts.create(properties={'cn': name})
    time.sleep(1)
    return cont


def _delete_container(cont):
    """Deletes container entry"""

    cont.delete()
    time.sleep(1)


@pytest.fixture(scope="module")
def topo_tls_ldapi(topo):
    """Enable TLS on both suppliers and reconfigure both agreements
    to use TLS Client auth. Also, setup ldapi and export DB
    """

    m1 = topo.ms["supplier1"]
    m2 = topo.ms["supplier2"]
    # Create the certmap before we restart for enable_tls
    cm_m1 = CertmapLegacy(m1)
    cm_m2 = CertmapLegacy(m2)

    # We need to configure the same maps for both ....
    certmaps = cm_m1.list()
    certmaps['default']['DNComps'] = None
    certmaps['default']['CmapLdapAttr'] = 'nsCertSubjectDN'

    cm_m1.set(certmaps)
    cm_m2.set(certmaps)

    [i.enable_tls() for i in topo]

    # Create the replication dns
    services = ServiceAccounts(m1, DEFAULT_SUFFIX)
    repl_m1 = services.get('%s:%s' % (m1.host, m1.sslport))
    repl_m1.set('nsCertSubjectDN', m1.get_server_tls_subject())

    repl_m2 = services.get('%s:%s' % (m2.host, m2.sslport))
    repl_m2.set('nsCertSubjectDN', m2.get_server_tls_subject())

    # Check the replication is "done".
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(m1, m2)
    # Now change the auth type

    replica_m1 = Replicas(m1).get(DEFAULT_SUFFIX)
    agmt_m1 = replica_m1.get_agreements().list()[0]

    agmt_m1.replace_many(
        ('nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH'),
        ('nsDS5ReplicaTransportInfo', 'SSL'),
        ('nsDS5ReplicaPort', '%s' % m2.sslport),
    )
    agmt_m1.remove_all('nsDS5ReplicaBindDN')

    replica_m2 = Replicas(m2).get(DEFAULT_SUFFIX)
    agmt_m2 = replica_m2.get_agreements().list()[0]

    agmt_m2.replace_many(
        ('nsDS5ReplicaBindMethod', 'SSLCLIENTAUTH'),
        ('nsDS5ReplicaTransportInfo', 'SSL'),
        ('nsDS5ReplicaPort', '%s' % m1.sslport),
    )
    agmt_m2.remove_all('nsDS5ReplicaBindDN')

    log.info("Export LDAPTLS_CACERTDIR env variable for ds-replcheck")
    os.environ["LDAPTLS_CACERTDIR"] = m1.get_ssca_dir()

    for inst in topo:
        inst.config.set('nsslapd-ldapilisten', 'on')
        inst.config.set('nsslapd-ldapifilepath', '/var/run/slapd-{}.socket'.format(inst.serverid))
        inst.restart()

    repl.test_replication(m1, m2)
    repl.test_replication(m2, m1)

    return topo


def replcheck_cmd_list(topo_tls_ldapi):
    """Check ds-replcheck tool through ldap, ldaps, ldap with StartTLS, ldapi
    and compare exported ldif files
    """

    m1 = topo_tls_ldapi.ms["supplier1"]
    m2 = topo_tls_ldapi.ms["supplier2"]

    for inst in topo_tls_ldapi:
        inst.stop()
        inst.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[],
                     encrypt=False, repl_data=True, outputfile='/tmp/export_{}.ldif'.format(inst.serverid))
        inst.start()

    ds_replcheck_path = os.path.join(m1.ds_paths.bin_dir, 'ds-replcheck')

    if ds_is_newer("1.4.1.2"):
        replcheck_cmd = [[ds_replcheck_path, 'online', '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '-l', '1',
                          '-m', 'ldap://{}:{}'.format(m1.host, m1.port), '--conflicts',
                          '-r', 'ldap://{}:{}'.format(m2.host, m2.port)],
                         [ds_replcheck_path, 'online', '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '-l', '1',
                          '-m', 'ldaps://{}:{}'.format(m1.host, m1.sslport), '--conflicts',
                          '-r', 'ldaps://{}:{}'.format(m2.host, m2.sslport)],
                         [ds_replcheck_path, 'online', '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '-l', '1',
                          '-m', 'ldap://{}:{}'.format(m1.host, m1.port), '-Z', m1.get_ssca_dir(),
                          '-r', 'ldap://{}:{}'.format(m2.host, m2.port), '--conflicts'],
                         [ds_replcheck_path, 'online', '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '-l', '1',
                          '-m', 'ldapi://%2fvar%2frun%2fslapd-{}.socket'.format(m1.serverid), '--conflict',
                          '-r', 'ldapi://%2fvar%2frun%2fslapd-{}.socket'.format(m2.serverid)],
                         [ds_replcheck_path, 'offline', '-b', DEFAULT_SUFFIX, '--conflicts', '--rid', '1',
                          '-m', '/tmp/export_{}.ldif'.format(m1.serverid),
                          '-r', '/tmp/export_{}.ldif'.format(m2.serverid)]]
    else:
        replcheck_cmd = [[ds_replcheck_path, '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '-l', '1',
                          '-m', 'ldap://{}:{}'.format(m1.host, m1.port), '--conflicts',
                          '-r', 'ldap://{}:{}'.format(m2.host, m2.port)],
                         [ds_replcheck_path, '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '-l', '1',
                          '-m', 'ldaps://{}:{}'.format(m1.host, m1.sslport), '--conflicts',
                          '-r', 'ldaps://{}:{}'.format(m2.host, m2.sslport)],
                         [ds_replcheck_path, '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '-l', '1',
                          '-m', 'ldap://{}:{}'.format(m1.host, m1.port), '-Z', m1.get_ssca_dir(),
                          '-r', 'ldap://{}:{}'.format(m2.host, m2.port), '--conflicts'],
                         [ds_replcheck_path, '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '-l', '1',
                          '-m', 'ldapi://%2fvar%2frun%2fslapd-{}.socket'.format(m1.serverid), '--conflict',
                          '-r', 'ldapi://%2fvar%2frun%2fslapd-{}.socket'.format(m2.serverid)],
                         [ds_replcheck_path, '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM, '--conflicts',
                          '-M', '/tmp/export_{}.ldif'.format(m1.serverid),
                          '-R', '/tmp/export_{}.ldif'.format(m2.serverid)]]

    return replcheck_cmd

@pytest.mark.skipif(ds_is_older("1.4.1.2"), reason="Not implemented")
def test_state(topo_tls_ldapi):
    """Check "state" report

    :id: 1cc6b28b-8a42-45fb-ab50-9552db0ac178
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Get the replication state value
        2. The state value is as expected
    :expectedresults:
        1. It should be successful
        2. It should be successful
    """
    m1 = topo_tls_ldapi.ms["supplier1"]
    m2 = topo_tls_ldapi.ms["supplier2"]
    ds_replcheck_path = os.path.join(m1.ds_paths.bin_dir, 'ds-replcheck')

    tool_cmd = [ds_replcheck_path, 'state', '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-w', PW_DM,
                '-m', 'ldaps://{}:{}'.format(m1.host, m1.sslport),
                '-r', 'ldaps://{}:{}'.format(m2.host, m2.sslport)]
    result = subprocess.check_output(tool_cmd, encoding='utf-8')
    assert (result.rstrip() == "Replication State: Supplier and Replica are in perfect synchronization")


def test_check_ruv(topo_tls_ldapi):
    """Check that the report has RUV

    :id: 1cc6b28b-8a42-45fb-ab50-9552db0ac179
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Get RUV from supplier and replica
        2. Generate the report
        3. Check that the RUV is mentioned in the report
    :expectedresults:
        1. It should be successful
        2. It should be successful
        3. The RUV should be mentioned in the report
    """

    m1 = topo_tls_ldapi.ms["supplier1"]

    replicas_m1 = Replica(m1, DEFAULT_SUFFIX)
    ruv_entries = replicas_m1.get_attr_vals_utf8('nsds50ruv')

    for tool_cmd in replcheck_cmd_list(topo_tls_ldapi):
        result = subprocess.check_output(tool_cmd, encoding='utf-8')
        assert all([ruv_entry in result for ruv_entry in ruv_entries])


def test_missing_entries(topo_tls_ldapi):
    """Check that the report has missing entries

    :id: f91b6798-6e6e-420a-ad2f-3222bb908b7d
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Pause replication between supplier and replica
        2. Add two entries to supplier and two entries to replica
        3. Generate the report
        4. Check that the entries DN are mentioned in the report
    :expectedresults:
        1. It should be successful
        2. It should be successful
        3. It should be successful
        4. The entries DN should be mentioned in the report
    """

    m1 = topo_tls_ldapi.ms["supplier1"]
    m2 = topo_tls_ldapi.ms["supplier2"]

    try:
        topo_tls_ldapi.pause_all_replicas()
        users_m1 = UserAccounts(m1, DEFAULT_SUFFIX)
        user0 = users_m1.create_test_user(1000)
        user1 = users_m1.create_test_user(1001)
        users_m2 = UserAccounts(m2, DEFAULT_SUFFIX)
        user2 = users_m2.create_test_user(1002)
        user3 = users_m2.create_test_user(1003)

        for tool_cmd in replcheck_cmd_list(topo_tls_ldapi):
            result = subprocess.check_output(tool_cmd, encoding='utf-8').lower()
            assert user0.dn.lower() in result
            assert user1.dn.lower() in result
    finally:
        user0.delete()
        user1.delete()
        user2.delete()
        user3.delete()
        topo_tls_ldapi.resume_all_replicas()


def test_tombstones(topo_tls_ldapi):
    """Check that the report mentions right number of tombstones

    :id: bd27de78-0046-431c-8240-a93052df1cdc
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Add an entry to supplier and wait for replication
        2. Pause replication between supplier and replica
        3. Delete the entry from supplier
        4. Generate the report
        5. Check that we have different number of tombstones in the report
    :expectedresults:
        1. It should be successful
        2. It should be successful
        3. It should be successful
        4. It should be successful
        5. It should be successful
    """

    m1 = topo_tls_ldapi.ms["supplier1"]

    try:
        users_m1 = UserAccounts(m1, DEFAULT_SUFFIX)
        user_m1 = users_m1.create(properties=TEST_USER_PROPERTIES)
        time.sleep(1)
        topo_tls_ldapi.pause_all_replicas()
        user_m1.delete()
        time.sleep(2)

        for tool_cmd in replcheck_cmd_list(topo_tls_ldapi):
            result = subprocess.check_output(tool_cmd, encoding='utf-8').lower()
            log.debug(result)
    finally:
        topo_tls_ldapi.resume_all_replicas()


def test_conflict_entries(topo_tls_ldapi):
    """Check that the report has conflict entries

    :id: 4eda0c5d-0824-4cfd-896e-845faf49ddaf
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Pause replication between supplier and replica
        2. Add two entries to supplier and two entries to replica
        3. Delete first entry from supplier
        4. Add a child to the first entry
        5. Resume replication between supplier and replica
        6. Generate the report
        7. Check that the entries DN are mentioned in the report
    :expectedresults:
        1. It should be successful
        2. It should be successful
        3. It should be successful
        4. It should be successful
        5. It should be successful
        6. It should be successful
        7. The entries DN should be mentioned in the report
    """

    m1 = topo_tls_ldapi.ms["supplier1"]
    m2 = topo_tls_ldapi.ms["supplier2"]

    topo_tls_ldapi.pause_all_replicas()

    _create_container(m1, DEFAULT_SUFFIX, 'conflict_parent0')
    _create_container(m2, DEFAULT_SUFFIX, 'conflict_parent0')
    cont_p_m1 = _create_container(m1, DEFAULT_SUFFIX, 'conflict_parent1')
    cont_p_m2 = _create_container(m2, DEFAULT_SUFFIX, 'conflict_parent1')
    _delete_container(cont_p_m1)
    _create_container(m2, cont_p_m2.dn, 'conflict_child0')

    topo_tls_ldapi.resume_all_replicas()
    time.sleep(5)

    for tool_cmd in replcheck_cmd_list(topo_tls_ldapi):
        result = subprocess.check_output(tool_cmd, encoding='utf-8')
        assert 'conflict_parent1' in result


def test_inconsistencies(topo_tls_ldapi):
    """Check that the report mentions inconsistencies with attributes

    :id: c8fe3e84-b346-4969-8f5d-3462b643a1d2
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Add an entry to supplier and wait for replication
        2. Pause replication between supplier and replica
        3. Set different description attr values to supplier and replica
        4. Add telephoneNumber attribute to supplier and not to replica
        5. Generate the report
        6. Check that attribute values are mentioned in the report
        7. Generate the report with -i option to ignore some attributes
        8. Check that attribute values are mentioned in the report
    :expectedresults:
        1. It should be successful
        2. It should be successful
        3. It should be successful
        4. It should be successful
        5. It should be successful
        6. The attribute values should be mentioned in the report
        7. It should be successful
        8. The attribute values should not be mentioned in the report
    """

    m1 = topo_tls_ldapi.ms["supplier1"]
    m2 = topo_tls_ldapi.ms["supplier2"]
    attr_m1 = "m1_inconsistency"
    attr_m2 = "m2_inconsistency"
    attr_first = "first ordered valued"
    attr_second = "second ordered valued"
    attr_m1_only = "123123123"

    try:
        users_m1 = UserAccounts(m1, DEFAULT_SUFFIX)
        users_m2 = UserAccounts(m2, DEFAULT_SUFFIX)
        user_m1 = users_m1.create(properties=TEST_USER_PROPERTIES)
        time.sleep(1)
        user_m2 = users_m2.get(user_m1.rdn)
        topo_tls_ldapi.pause_all_replicas()
        user_m1.set("description", attr_m1)
        user_m2.set("description", attr_m2)
        user_m1.set("telephonenumber", attr_m1_only)
        # Add the same multi-valued attrs, but out of order
        user_m1.set("cn", [attr_first, attr_second])
        user_m2.set("cn", [attr_second, attr_first])
        time.sleep(2)

        for tool_cmd in replcheck_cmd_list(topo_tls_ldapi):
            result = subprocess.check_output(tool_cmd, encoding='utf-8').lower()
            assert attr_m1 in result
            assert attr_m2 in result
            assert attr_m1_only in result
            if ds_is_newer("1.3.9.1", "1.4.1.2"):
                assert attr_first not in result
                assert attr_second not in result
            # Ignore some attributes and check the output
            tool_cmd.extend(['-i', '{},{}'.format('description', 'telephonenumber')])
            result = subprocess.check_output(tool_cmd, encoding='utf-8').lower()
            assert attr_m1 not in result
            assert attr_m2 not in result
            assert attr_m1_only not in result
            if ds_is_newer("1.3.9.1", "1.4.1.2"):
                assert attr_first not in result
                assert attr_second not in result

    finally:
        topo_tls_ldapi.resume_all_replicas()
        user_m1.delete()


def test_suffix_exists(topo_tls_ldapi):
    """Check if wrong suffix is provided, server is giving Error: Failed
    to validate suffix.

    :id: ce75debc-c07f-4e72-8787-8f99cbfaf1e2
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Run ds-replcheck with wrong suffix (Non Existing)
    :expectedresults:
        1. It should be unsuccessful
    """
    m1 = topo_tls_ldapi.ms["supplier1"]
    m2 = topo_tls_ldapi.ms["supplier2"]
    ds_replcheck_path = os.path.join(m1.ds_paths.bin_dir, 'ds-replcheck')

    if ds_is_newer("1.4.1.2"):
        tool_cmd = [ds_replcheck_path, 'online', '-b', 'dc=test,dc=com', '-D', DN_DM, '-w', PW_DM,
                    '-m', 'ldaps://{}:{}'.format(m1.host, m1.sslport),
                    '-r', 'ldaps://{}:{}'.format(m2.host, m2.sslport)]
    else:
        tool_cmd = [ds_replcheck_path, '-b', 'dc=test,dc=com', '-D', DN_DM, '-w', PW_DM,
                    '-m', 'ldaps://{}:{}'.format(m1.host, m1.sslport),
                    '-r', 'ldaps://{}:{}'.format(m2.host, m2.sslport)]

    result1 = subprocess.Popen(tool_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
    result = result1.communicate()
    assert "Failed to validate suffix" in result[0]


def test_check_missing_tombstones(topo_tls_ldapi):
    """Check missing tombstone entries is not reported.

    :id: 93067a5a-416e-4243-9418-c4dfcf42e093
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Pause replication between supplier and replica
        2. Add and delete an entry on the supplier
        3. Run ds-replcheck
        4. Verify there are NO complaints about missing entries/tombstones
    :expectedresults:
        1. It should be successful
        2. It should be successful
        3. It should be successful
        4. It should be successful
    """
    m1 = topo_tls_ldapi.ms["supplier1"]
    m2 = topo_tls_ldapi.ms["supplier2"]

    try:
        topo_tls_ldapi.pause_all_replicas()
        users_m1 = UserAccounts(m1, DEFAULT_SUFFIX)
        user0 = users_m1.create_test_user(1000)
        user0.delete()
        for tool_cmd in replcheck_cmd_list(topo_tls_ldapi):
            result = subprocess.check_output(tool_cmd, encoding='utf-8').lower()
            assert "entries missing on replica" not in result

    finally:
        topo_tls_ldapi.resume_all_replicas()


def test_dsreplcheck_with_password_file(topo_tls_ldapi, tmpdir):
    """Check ds-replcheck works if password file is provided
    with -y option.

    :id: 0d847ec7-6eaf-4cb5-a9c6-e4a5a1778f93
    :customerscenario: True
    :setup: Two supplier replication
    :steps:
        1. Create a password file with the default password of the server.
        2. Run ds-replcheck with -y option (used to pass password file)
    :expectedresults:
        1. It should be successful
        2. It should be successful
    """
    m1 = topo_tls_ldapi.ms["supplier1"]
    m2 = topo_tls_ldapi.ms["supplier2"]

    ds_replcheck_path = os.path.join(m1.ds_paths.bin_dir, 'ds-replcheck')
    f = tmpdir.mkdir("my_dir").join("password_file.txt")
    f.write(PW_DM)

    if ds_is_newer("1.4.1.2"):
        tool_cmd = [ds_replcheck_path, 'online', '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-y', f.strpath,
                    '-m', 'ldaps://{}:{}'.format(m1.host, m1.sslport),
                    '-r', 'ldaps://{}:{}'.format(m2.host, m2.sslport)]
    else:
        tool_cmd = [ds_replcheck_path, '-b', DEFAULT_SUFFIX, '-D', DN_DM, '-y', f.strpath,
                    '-m', 'ldaps://{}:{}'.format(m1.host, m1.sslport),
                    '-r', 'ldaps://{}:{}'.format(m2.host, m2.sslport)]

    subprocess.Popen(tool_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
