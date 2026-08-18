# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#


import logging
import os
import time

import ldap
import pytest
from test389.topologies import create_topology

from lib389._constants import DEFAULT_SUFFIX, ReplicaRole
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import DEFAULT_BASEDN_RDN
from lib389.replica import ReplicationManager
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

MAX_EMPLOYEENUMBER_USER = 20
MAX_STANDARD_USER = 100
MAX_USER = MAX_STANDARD_USER + MAX_EMPLOYEENUMBER_USER
EMPLOYEENUMBER_RDN_START = 0

USER_UID_PREFIX = "user_"
BASE_PEOPLE = f"{DEFAULT_BASEDN_RDN},{DEFAULT_SUFFIX}"
BASE_DISTINGUISHED = f"ou=distinguished,{BASE_PEOPLE}"
BASE_REGULAR = f"ou=regular,{BASE_PEOPLE}"
INITIAL_EMPLOYEE_NUMBER = b"11"
COMMON_EMPLOYEE_NUMBER = b"1000"


def _regular_user_dn(user_no):
    """Return uid and DN for a test user under ou=regular."""
    uid = f"{USER_UID_PREFIX}{user_no}"
    return uid, f"uid={uid},{BASE_REGULAR}"


def _employeenumber_user_get_dn(no):
    """Return employeeNumber RDN value and DN under ou=distinguished."""
    employee_number = str(no)
    dn = f"employeeNumber={employee_number},{BASE_DISTINGUISHED}"
    return employee_number, dn


def pause_supplier_pair_isolated(m1, m2, m3):
    """Pause all agreements touching M1 or M2 so only M3 receives later updates."""
    agreement_m1_m2 = m1.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m2.host, consumer_port=m2.port
    )
    agreement_m1_m3 = m1.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m3.host, consumer_port=m3.port
    )
    agreement_m2_m1 = m2.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m1.host, consumer_port=m1.port
    )
    agreement_m2_m3 = m2.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m3.host, consumer_port=m3.port
    )
    m1.agreement.pause(agreement_m1_m2[0].dn)
    m1.agreement.pause(agreement_m1_m3[0].dn)
    m2.agreement.pause(agreement_m2_m1[0].dn)
    m2.agreement.pause(agreement_m2_m3[0].dn)
    return agreement_m1_m2, agreement_m1_m3, agreement_m2_m1, agreement_m2_m3


def resume_m2_before_m1(m1, m2, agreement_m2_m1, agreement_m2_m3, agreement_m1_m2, agreement_m1_m3):
    """Resume M2 replication agreements to M1/M3, then M1 agreements to M2/M3."""
    repl = ReplicationManager(DEFAULT_SUFFIX)

    for ra in (agreement_m2_m1, agreement_m2_m3):
        m2.agreement.resume(ra[0].dn)
    repl.wait_for_replication(m2, m1)
    for ra in (agreement_m1_m2, agreement_m1_m3):
        m1.agreement.resume(ra[0].dn)


def resume_m2_then_m1_mesh(m1, m2, m3):
    """Look up agreements and resume M2 outbound links before M1 outbound links."""
    repl = ReplicationManager(DEFAULT_SUFFIX)

    agreement_m1_m2 = m1.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m2.host, consumer_port=m2.port
    )
    agreement_m1_m3 = m1.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m3.host, consumer_port=m3.port
    )
    agreement_m2_m1 = m2.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m1.host, consumer_port=m1.port
    )
    agreement_m2_m3 = m2.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m3.host, consumer_port=m3.port
    )
    for ra in (agreement_m2_m1, agreement_m2_m3):
        m2.agreement.resume(ra[0].dn)
    repl.wait_for_replication(m2, m1)
    for ra in (agreement_m1_m2, agreement_m1_m3):
        m1.agreement.resume(ra[0].dn)
    repl.wait_for_replication(m1, m2)


def resume_m1_then_m2_mesh(m1, m2, m3):
    """Look up agreements and resume M1 outbound links before M2 outbound links."""
    repl = ReplicationManager(DEFAULT_SUFFIX)

    agreement_m1_m2 = m1.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m2.host, consumer_port=m2.port
    )
    agreement_m1_m3 = m1.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m3.host, consumer_port=m3.port
    )
    agreement_m2_m1 = m2.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m1.host, consumer_port=m1.port
    )
    agreement_m2_m3 = m2.agreement.list(
        suffix=DEFAULT_SUFFIX, consumer_host=m3.host, consumer_port=m3.port
    )
    for ra in (agreement_m1_m2, agreement_m1_m3):
        m1.agreement.resume(ra[0].dn)
    repl.wait_for_replication(m1, m2)
    for ra in (agreement_m2_m1, agreement_m2_m3):
        m2.agreement.resume(ra[0].dn)
    repl.wait_for_replication(m2, m1)


def assert_regular_employee_number_all_suppliers(m1, m2, m3, expected):
    """Assert one regular user has the expected employeeNumber on all three suppliers."""
    filt = f"(employeeNumber={expected})"
    for inst, label in ((m1, "M1"), (m2, "M2"), (m3, "M3")):
        users = UserAccounts(inst, DEFAULT_SUFFIX, rdn=f'ou=regular,ou=people')
        ents = users.filter(filt)
        assert len(ents) == 1, label
        assert ents[0].present("employeeNumber")
        assert ents[0].get_attr_val_utf8("employeeNumber") == expected, label
    
    users = UserAccounts(m3, DEFAULT_SUFFIX, rdn=f'ou=regular,ou=people')
    ents = users.filter("(employeeNumber=*)")
    assert len(ents) == MAX_STANDARD_USER


def assert_distinguished_employee_number_all_suppliers(m1, m2, m3, expected):
    """Assert one distinguished entry has the expected employeeNumber on all suppliers."""
    filt = f"(employeeNumber={expected})"
    for inst, label in ((m1, "M1"), (m2, "M2"), (m3, "M3")):
        users = UserAccounts(inst, DEFAULT_SUFFIX, rdn=f'ou=distinguished,ou=people')
        ents = users.filter(filt)
        assert len(ents) == 1, label
        assert ents[0].present("employeeNumber")
        assert ents[0].get_attr_val_utf8("employeeNumber") == expected, label
    users = UserAccounts(m3, DEFAULT_SUFFIX, rdn=f'ou=distinguished,ou=people')
    ents = users.filter("(employeeNumber=*)")
    assert len(ents) == MAX_EMPLOYEENUMBER_USER


def _distinguished_user_accounts(m1, m2):
    """Return UserAccounts instances for ou=distinguished on M1 and M2."""
    rdn = "ou=distinguished,ou=people"
    return (
        UserAccounts(m1, DEFAULT_SUFFIX, rdn=rdn),
        UserAccounts(m2, DEFAULT_SUFFIX, rdn=rdn),
    )


def _assert_one_distinguished(users, employee_number):
    """Assert exactly one distinguished entry matches the employeeNumber."""
    ents = users.filter(f"(employeeNumber={employee_number})")
    assert len(ents) == 1


def _bootstrap_employee_number_dataset(topology):
    """Create distinguished and regular test users on M3 and verify full-mesh replication."""
    m1 = topology.ms["supplier1"]
    m2 = topology.ms["supplier2"]
    m3 = topology.ms["supplier3"]

    ous = OrganizationalUnits(m3, DEFAULT_SUFFIX)
    if not ous.exists("People"):
        ous.create(properties={"ou": "People"})
    ous_people = OrganizationalUnits(m3, BASE_PEOPLE)
    if not ous_people.exists("distinguished"):
        ous_people.create(properties={"ou": "distinguished"})
    if not ous_people.exists("regular"):
        ous_people.create(properties={"ou": "regular"})

    users = UserAccounts(m3, DEFAULT_SUFFIX, rdn=f'ou=distinguished,ou=people')
    for i in range(MAX_EMPLOYEENUMBER_USER):
        en = str(EMPLOYEENUMBER_RDN_START + i)
        rdn = f"employeeNumber={en}"
        uid = str(i)

        users.create(rdn=rdn, properties={
            "uid": uid,
            "sn": uid,
            "cn": uid,
            "employeeNumber": en,
            "uidNumber": f"10{i}",
            "gidNumber": f"100{i}",
            "homeDirectory": f"/home/{uid}",
        })

    users = UserAccounts(m3, DEFAULT_SUFFIX, rdn=f'ou=regular,ou=people')
    for i in range(MAX_STANDARD_USER):
        uid = f"{USER_UID_PREFIX}{i}"
        user = users.create(properties={
            "uid": uid,
            "sn": uid,
            "cn": uid,
            "employeeNumber": INITIAL_EMPLOYEE_NUMBER.decode(),
            "uidNumber": f"10{i}",
            "gidNumber": f"100{i}",
            "homeDirectory": f"/home/{uid}",
        })

        user.replace("employeeNumber", COMMON_EMPLOYEE_NUMBER.decode())

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication_topology([m1, m2, m3], timeout=120)
    
    users = UserAccounts(m2, DEFAULT_SUFFIX)
    ents = users.filter("(employeeNumber=*)")
    assert len(ents) == MAX_USER


@pytest.fixture(scope="module")
def topology_m3_employee_number_conflict(request):
    """Provide three suppliers with a replicated employeeNumber conflict test dataset."""
    topology = create_topology({ReplicaRole.SUPPLIER: 3}, request=request)
    _bootstrap_employee_number_dataset(topology)

    def fin():
        users = UserAccounts(topology.ms["supplier1"], DEFAULT_SUFFIX)
        for user in users.filter("(employeeNumber=*)"):
            user.delete()
        ous = OrganizationalUnits(topology.ms["supplier1"], DEFAULT_SUFFIX)
        ous.get("distinguished").delete()
        ous.get("regular").delete()
        repl = ReplicationManager(DEFAULT_SUFFIX)
        repl.wait_for_replication(topology.ms["supplier1"], topology.ms["supplier2"])
        repl.wait_for_replication(topology.ms["supplier1"], topology.ms["supplier3"])

        users = UserAccounts(topology.ms["supplier2"], DEFAULT_SUFFIX)
        assert len(users.filter("(employeeNumber=*)")) == 0
        users = UserAccounts(topology.ms["supplier3"], DEFAULT_SUFFIX)
        assert len(users.filter("(employeeNumber=*)")) == 0
        
    request.addfinalizer(fin)

    return topology


@pytest.mark.parametrize(
    "user_no,m1_modifies_first,mod_kind",
    [
        (0, True, "DEL_ADD"),
        (1, False, "DEL_ADD"),
        (2, False, "ADD_DEL"),
        (3, True, "ADD_DEL"),
        (4, True, "M1_ADD_DEL_M2_REPLACE"),
        (5, True, "M1_REPLACE_M2_ADD_DEL"),
    ],
)
def test_regular_user_identical_final_value_ordering(
    topology_m3_employee_number_conflict, user_no, m1_modifies_first, mod_kind
):
    """Check conflict resolution when M1 and M2 apply the same employeeNumber change.

    :id: 79662e57-068d-4664-87cd-32d2e7ec369e
    :setup: Three suppliers with regular users having employeeNumber=1000
    :steps:
        1. Pause replication agreements on M1 and M2
        2. Apply the same modify shape on M1 and M2 for the chosen user
        3. Resume M2 agreements to M3, then M1 agreements to M3
        4. Verify employeeNumber on M1, M2, and M3
    :expectedresults:
        1. M1 and M2 are isolated from each other and from M3
        2. Both suppliers hold the conflicting update locally
        3. M3 receives M2's update before M1's update
        4. All suppliers show the same final employeeNumber for that user
    """
    m1 = topology_m3_employee_number_conflict.ms["supplier1"]
    m2 = topology_m3_employee_number_conflict.ms["supplier2"]
    m3 = topology_m3_employee_number_conflict.ms["supplier3"]
    _, test_user_dn = _regular_user_dn(user_no)
    value_end = str(user_no).encode()
    filt = f"(employeeNumber={user_no})"

    agreement_m1_m2, agreement_m1_m3, agreement_m2_m1, agreement_m2_m3 = pause_supplier_pair_isolated(
        m1, m2, m3
    )

    users1 = UserAccounts(m1, DEFAULT_SUFFIX, rdn=f'ou=regular,ou=people')
    users2 = UserAccounts(m2, DEFAULT_SUFFIX, rdn=f'ou=regular,ou=people')

    def mod_del_add(user):
        user.apply_mods([
            (ldap.MOD_DELETE, "employeeNumber", COMMON_EMPLOYEE_NUMBER),
            (ldap.MOD_ADD, "employeeNumber", value_end)])

    def mod_add_del(user):
        user.apply_mods([
            (ldap.MOD_ADD, "employeeNumber", value_end),
            (ldap.MOD_DELETE, "employeeNumber", COMMON_EMPLOYEE_NUMBER)])

    def mod_replace(user):
        user.replace("employeeNumber", value_end)

    if mod_kind == "DEL_ADD":
        first, second = (mod_del_add, mod_del_add)
    elif mod_kind == "ADD_DEL":
        first, second = (mod_add_del, mod_add_del)
    elif mod_kind == "M1_ADD_DEL_M2_REPLACE":
        first, second = (mod_add_del, mod_replace)
    elif mod_kind == "M1_REPLACE_M2_ADD_DEL":
        first, second = (mod_replace, mod_add_del)
    else:
        raise AssertionError(mod_kind)

    if m1_modifies_first:
        user1 = users1.get(dn=test_user_dn)
        first(user1)
        assert len(users1.filter(filt)) == 1
        time.sleep(1)

        user2 = users2.get(dn=test_user_dn)
        second(user2)
        assert len(users2.filter(filt)) == 1
        time.sleep(1)
    else:
        user2 = users2.get(dn=test_user_dn)
        first(user2)
        assert len(users2.filter(filt)) == 1
        time.sleep(1)

        user1 = users1.get(dn=test_user_dn)
        second(user1)
        assert len(users1.filter(filt)) == 1
        time.sleep(1)

    resume_m2_before_m1(
        m1, m2, agreement_m2_m1, agreement_m2_m3, agreement_m1_m2, agreement_m1_m3
    )
    assert_regular_employee_number_all_suppliers(m1, m2, m3, str(user_no))


def _build_mixed_mods(user_no, s1_kind, s2_kind):
    """Build LDAP mod lists for two different single-valued employeeNumber update shapes."""
    v1000 = b"1000"
    s1 = f"{user_no}.1".encode()
    s2 = f"{user_no}.2".encode()
    m1 = None
    m2 = None
    if s1_kind == "REPLACE":
        m1 = [(ldap.MOD_REPLACE, "employeeNumber", s1)]
    elif s1_kind == "ADD_DEL":
        m1 = [(ldap.MOD_ADD, "employeeNumber", s1), (ldap.MOD_DELETE, "employeeNumber", v1000)]
    elif s1_kind == "DEL_ADD":
        m1 = [(ldap.MOD_DELETE, "employeeNumber", v1000), (ldap.MOD_ADD, "employeeNumber", s1)]
    if s2_kind == "REPLACE":
        m2 = [(ldap.MOD_REPLACE, "employeeNumber", s2)]
    elif s2_kind == "ADD_DEL":
        m2 = [(ldap.MOD_ADD, "employeeNumber", s2), (ldap.MOD_DELETE, "employeeNumber", v1000)]
    elif s2_kind == "DEL_ADD":
        m2 = [(ldap.MOD_DELETE, "employeeNumber", v1000), (ldap.MOD_ADD, "employeeNumber", s2)]
    return m1, m2


@pytest.mark.parametrize(
    "user_no,first_server,s1_kind,s2_kind,expected",
    [
        (6, "M1", "REPLACE", "ADD_DEL", "6.2"),
        (7, "M1", "ADD_DEL", "REPLACE", "7.2"),
        (8, "M1", "DEL_ADD", "REPLACE", "8.2"),
        (9, "M1", "REPLACE", "DEL_ADD", "9.2"),
        (10, "M1", "REPLACE", "REPLACE", "10.2"),
        (11, "M2", "REPLACE", "REPLACE", "11.1"),
        (12, "M2", "REPLACE", "ADD_DEL", "12.1"),
        (13, "M2", "REPLACE", "DEL_ADD", "13.1"),
        (14, "M2", "DEL_ADD", "DEL_ADD", "14.1"),
        (15, "M2", "ADD_DEL", "DEL_ADD", "15.1"),
    ],
)
def test_regular_user_mixed_modify_shapes(
    topology_m3_employee_number_conflict, user_no, first_server, s1_kind, s2_kind, expected
):
    """Check conflict resolution when M1 and M2 use different modify shapes.

    :id: cdcdac34-bbf6-42ce-800e-954e21f0ba1d
    :setup: Three suppliers with regular users having employeeNumber=1000
    :steps:
        1. Pause replication agreements on M1 and M2
        2. Apply distinct modify shapes on M1 and M2 for the chosen user
        3. Resume M2 agreements to M3, then M1 agreements to M3
        4. Verify the expected employeeNumber on M1, M2, and M3
    :expectedresults:
        1. M1 and M2 are isolated from each other and from M3
        2. Each supplier holds its local conflicting value
        3. M3 receives M2's update before M1's update
        4. All suppliers converge on the parametrized winning value
    """
    m1 = topology_m3_employee_number_conflict.ms["supplier1"]
    m2 = topology_m3_employee_number_conflict.ms["supplier2"]
    m3 = topology_m3_employee_number_conflict.ms["supplier3"]
    _, test_user_dn = _regular_user_dn(user_no)
    s1_mod, s2_mod = _build_mixed_mods(user_no, s1_kind, s2_kind)
    value_s1 = f"{user_no}.1"
    value_s2 = f"{user_no}.2"

    agreement_m1_m2, agreement_m1_m3, agreement_m2_m1, agreement_m2_m3 = pause_supplier_pair_isolated(
        m1, m2, m3
    )

    users1 = UserAccounts(m1, DEFAULT_SUFFIX, rdn="ou=regular,ou=people")
    users2 = UserAccounts(m2, DEFAULT_SUFFIX, rdn="ou=regular,ou=people")
    user1 = users1.get(dn=test_user_dn)
    user2 = users2.get(dn=test_user_dn)
    if first_server == "M1":
        user1.apply_mods(s1_mod)
        assert len(users1.filter(f"(employeeNumber={value_s1})")) == 1
        time.sleep(1)
        user2.apply_mods(s2_mod)
        assert len(users2.filter(f"(employeeNumber={value_s2})")) == 1
    else:
        user2.apply_mods(s2_mod)
        assert len(users2.filter(f"(employeeNumber={value_s2})")) == 1
        time.sleep(1)
        user1.apply_mods(s1_mod)
        assert len(users1.filter(f"(employeeNumber={value_s1})")) == 1

    resume_m2_before_m1(
        m1, m2, agreement_m2_m1, agreement_m2_m3, agreement_m1_m2, agreement_m1_m3
    )
    assert_regular_employee_number_all_suppliers(m1, m2, m3, expected)


def test_distinguished_modrdn_same_new_rdn_resume_m2_first(topology_m3_employee_number_conflict):
    """Check MODRDN conflict when M1 and M2 choose the same new employeeNumber RDN.

    :id: a54235dd-7951-478e-bfe0-3683986b18aa
    :setup: Three suppliers with distinguished employeeNumber RDN entries
    :steps:
        1. Pause replication agreements on M1 and M2
        2. MODRDN the same entry on M1 and M2 to the same new RDN value
        3. Resume M2 agreements, then M1 agreements
        4. Verify employeeNumber on all suppliers
    :expectedresults:
        1. M1 and M2 are isolated during the updates
        2. Both suppliers rename the entry to the same RDN locally
        3. M3 receives M2's change before M1's change
        4. All suppliers show the shared final employeeNumber value
    """
    m1 = topology_m3_employee_number_conflict.ms["supplier1"]
    m2 = topology_m3_employee_number_conflict.ms["supplier2"]
    m3 = topology_m3_employee_number_conflict.ms["supplier3"]
    value_s1 = value_s2 = "1.1"
    _, test_user_dn = _employeenumber_user_get_dn(1)
    pause_supplier_pair_isolated(m1, m2, m3)

    users1, users2 = _distinguished_user_accounts(m1, m2)
    users1.get(dn=test_user_dn).rename(
        f"employeeNumber={value_s1}",
        deloldrdn=1,
    )
    _assert_one_distinguished(users1, value_s1)
    time.sleep(1)

    users2.get(dn=test_user_dn).rename(
        f"employeeNumber={value_s2}",
        deloldrdn=1,
    )
    _assert_one_distinguished(users2, value_s2)
    resume_m2_then_m1_mesh(m1, m2, m3)
    assert_distinguished_employee_number_all_suppliers(m1, m2, m3, value_s1)


def test_distinguished_modrdn_divergent_rdn_resume_m2_first(topology_m3_employee_number_conflict):
    """Check MODRDN conflict when M1 and M2 choose different employeeNumber RDN values.

    :id: 1f3ccb99-10a5-47a1-a3a1-45386ec7a368
    :setup: Three suppliers with distinguished employeeNumber RDN entries
    :steps:
        1. Pause replication agreements on M1 and M2
        2. MODRDN the entry on M1 and M2 to different new RDN values
        3. Resume M2 agreements, then M1 agreements
        4. Verify employeeNumber on all suppliers
    :expectedresults:
        1. M1 and M2 are isolated during the updates
        2. Each supplier holds a different renamed entry locally
        3. M3 receives M2's RDN before M1's RDN
        4. M2's employeeNumber value wins on all suppliers
    """
    m1 = topology_m3_employee_number_conflict.ms["supplier1"]
    m2 = topology_m3_employee_number_conflict.ms["supplier2"]
    m3 = topology_m3_employee_number_conflict.ms["supplier3"]
    value_s1, value_s2 = "2.1", "2.2"
    _, test_user_dn = _employeenumber_user_get_dn(2)
    pause_supplier_pair_isolated(m1, m2, m3)

    users1, users2 = _distinguished_user_accounts(m1, m2)
    users1.get(dn=test_user_dn).rename(
        f"employeeNumber={value_s1}",
        deloldrdn=1,
    )
    time.sleep(1)
    users2.get(dn=test_user_dn).rename(
        f"employeeNumber={value_s2}",
        deloldrdn=1,
    )
    resume_m2_then_m1_mesh(m1, m2, m3)
    assert_distinguished_employee_number_all_suppliers(m1, m2, m3, value_s2)


def test_distinguished_modrdn_divergent_rdn_resume_m1_first(topology_m3_employee_number_conflict):
    """Check MODRDN conflict with divergent RDNs when M1 replicates to M3 first.

    :id: de14af98-f943-4e67-96f8-1e67592f351a
    :setup: Three suppliers with distinguished employeeNumber RDN entries
    :steps:
        1. Pause replication agreements on M1 and M2
        2. MODRDN the entry on M1 and M2 to different new RDN values
        3. Resume M1 agreements, then M2 agreements
        4. Verify employeeNumber on all suppliers
    :expectedresults:
        1. M1 and M2 are isolated during the updates
        2. Each supplier holds a different renamed entry locally
        3. M3 receives M1's RDN before M2's RDN
        4. M2's employeeNumber value still wins on all suppliers
    """
    m1 = topology_m3_employee_number_conflict.ms["supplier1"]
    m2 = topology_m3_employee_number_conflict.ms["supplier2"]
    m3 = topology_m3_employee_number_conflict.ms["supplier3"]
    value_s1, value_s2 = "3.1", "3.2"
    _, test_user_dn = _employeenumber_user_get_dn(3)
    pause_supplier_pair_isolated(m1, m2, m3)

    users1, users2 = _distinguished_user_accounts(m1, m2)
    users1.get(dn=test_user_dn).rename(
        f"employeeNumber={value_s1}",
        deloldrdn=1,
    )
    time.sleep(1)
    users2.get(dn=test_user_dn).rename(
        f"employeeNumber={value_s2}",
        deloldrdn=1,
    )
    resume_m1_then_m2_mesh(m1, m2, m3)
    assert_distinguished_employee_number_all_suppliers(m1, m2, m3, value_s2)


def test_distinguished_modrdn_then_replace_resume_m2_first(topology_m3_employee_number_conflict):
    """Check MODRDN conflict followed by M1 MOD(REPLACE) with M2 replicating first.

    :id: 4c6d761e-fead-4f05-98c1-b2aafea0e926
    :setup: Three suppliers with distinguished employeeNumber RDN entries
    :steps:
        1. Pause replication agreements on M1 and M2
        2. MODRDN the entry on M1 and M2 to different new RDN values
        3. On M1, MOD(REPLACE) employeeNumber back to the M1 RDN value
        4. Resume M2 agreements, then M1 agreements
        5. Verify employeeNumber on all suppliers
    :expectedresults:
        1. M1 and M2 are isolated during the updates
        2. M1 holds a replace on its renamed entry
        3. M3 receives M2's updates before M1's replace
        4. M1's employeeNumber value wins on all suppliers
    """
    m1 = topology_m3_employee_number_conflict.ms["supplier1"]
    m2 = topology_m3_employee_number_conflict.ms["supplier2"]
    m3 = topology_m3_employee_number_conflict.ms["supplier3"]
    value_s1, value_s2 = "4.1", "4.2"
    _, test_user_dn = _employeenumber_user_get_dn(4)
    pause_supplier_pair_isolated(m1, m2, m3)

    users1, users2 = _distinguished_user_accounts(m1, m2)
    user1 = users1.get(dn=test_user_dn)
    user1.rename(f"employeeNumber={value_s1}", deloldrdn=1)
    time.sleep(1)
    users2.get(dn=test_user_dn).rename(
        f"employeeNumber={value_s2}",
        deloldrdn=1,
    )
    time.sleep(1)

    _, dn_s1 = _employeenumber_user_get_dn(value_s1)
    users1.get(dn=dn_s1).replace("employeeNumber", value_s1)
    _assert_one_distinguished(users1, value_s1)
    resume_m2_then_m1_mesh(m1, m2, m3)
    assert_distinguished_employee_number_all_suppliers(m1, m2, m3, value_s1)



# --- extended distinguished employeeNumber RDN scenarios ---

def _mod_replace_employee_number(value):
    """Return a MOD_REPLACE mod list for employeeNumber."""
    return [(ldap.MOD_REPLACE, "employeeNumber", value.encode())]


def _mod_del_add_employee_number(value):
    """Return a MOD_DELETE/MOD_ADD mod list for employeeNumber."""
    return [
        (ldap.MOD_DELETE, "employeeNumber", value.encode()),
        (ldap.MOD_ADD, "employeeNumber", value.encode()),
    ]


def _employee_number_mod_for_kind(kind, value):
    """Map a mod kind name to the corresponding employeeNumber mod list."""
    if kind == "REPLACE":
        return _mod_replace_employee_number(value)
    if kind == "DEL_ADD":
        return _mod_del_add_employee_number(value)
    raise AssertionError(f"unknown mod kind: {kind}")


def _resume_mesh_for_order(resume_order):
    """Return the mesh resume helper for the requested supplier order."""
    if resume_order == "m1_first":
        return resume_m1_then_m2_mesh
    if resume_order == "m2_first":
        return resume_m2_then_m1_mesh
    raise AssertionError(f"unknown resume order: {resume_order}")


def _run_distinguished_employee_rdn_scenario(
    m1,
    m2,
    m3,
    user_index,
    value_s1,
    value_s2,
    s1_mod,
    resume_fn,
    expected,
    s2_mod=None,
    m2_final_modrdn=None,
):
    """Run isolated MODRDN/MOD steps on distinguished entries and verify convergence."""
    _, test_user_dn = _employeenumber_user_get_dn(user_index)
    pause_supplier_pair_isolated(m1, m2, m3)
    users1, users2 = _distinguished_user_accounts(m1, m2)

    user1 = users1.get(dn=test_user_dn)
    user1.rename(f"employeeNumber={value_s1}", deloldrdn=1)
    _assert_one_distinguished(users1, value_s1)
    time.sleep(1)

    user2 = users2.get(dn=test_user_dn)
    user2.rename(f"employeeNumber={value_s2}", deloldrdn=1)
    _assert_one_distinguished(users2, value_s2)
    time.sleep(1)

    if s1_mod is not None:
        _, dn_s1 = _employeenumber_user_get_dn(value_s1)
        user1 = users1.get(dn=dn_s1)
        user1.apply_mods(s1_mod)
        _assert_one_distinguished(users1, value_s1)

    if s2_mod is not None:
        time.sleep(1)
        _, dn_s2 = _employeenumber_user_get_dn(value_s2)
        user2 = users2.get(dn=dn_s2)
        user2.apply_mods(s2_mod)
        _assert_one_distinguished(users2, value_s2)

    if m2_final_modrdn is not None:
        time.sleep(1)
        _, dn_s2 = _employeenumber_user_get_dn(value_s2)
        user2 = users2.get(dn=dn_s2)
        user2.rename(f"employeeNumber={m2_final_modrdn}", deloldrdn=1)
        _assert_one_distinguished(users2, m2_final_modrdn)

    resume_fn(m1, m2, m3)
    assert_distinguished_employee_number_all_suppliers(m1, m2, m3, expected)


@pytest.mark.parametrize(
    "user_index,s1_mod_kind,s2_mod_kind,resume_order,expected_key,m2_final_to_s1,scenario_description",
    [
        pytest.param(
            5, "REPLACE", None, "m1_first", "s1", False,
            "M1/M2 MODRDN to V1/V2, M1 MOD(REPL)->V1; resume M1 then M2; expect V1",
            id="01_m1_repl_resume_m1_expect_v1",
        ),
        pytest.param(
            6, "DEL_ADD", None, "m2_first", "s1", False,
            "M1/M2 MODRDN to V1/V2, M1 MOD(DEL/ADD)->V1; resume M2 then M1; expect V1",
            id="02_m1_del_add_resume_m2_expect_v1",
        ),
        pytest.param(
            7, "DEL_ADD", None, "m1_first", "s1", False,
            "M1/M2 MODRDN to V1/V2, M1 MOD(DEL/ADD)->V1; resume M1 then M2; expect V1",
            id="03_m1_del_add_resume_m1_expect_v1",
        ),
        pytest.param(
            8, "REPLACE", "REPLACE", "m2_first", "s2", False,
            "M1/M2 MODRDN, M1 MOD(REPL)->V1, M2 MOD(REPL)->V2; resume M2 then M1; expect V2",
            id="04_m1_m2_repl_resume_m2_expect_v2",
        ),
        pytest.param(
            9, "REPLACE", "REPLACE", "m1_first", "s2", False,
            "M1/M2 MODRDN, M1 MOD(REPL)->V1, M2 MOD(REPL)->V2; resume M1 then M2; expect V2",
            id="05_m1_m2_repl_resume_m1_expect_v2",
        ),
        pytest.param(
            10, "REPLACE", "DEL_ADD", "m1_first", "s2", False,
            "M1/M2 MODRDN, M1 MOD(REPL)->V1, M2 MOD(DEL/ADD)->V2; resume M1 then M2; expect V2",
            id="06_m1_repl_m2_del_add_resume_m1_expect_v2",
        ),
        pytest.param(
            11, "REPLACE", "DEL_ADD", "m2_first", "s2", False,
            "M1/M2 MODRDN, M1 MOD(REPL)->V1, M2 MOD(DEL/ADD)->V2; resume M2 then M1; expect V2",
            id="07_m1_repl_m2_del_add_resume_m2_expect_v2",
        ),
        pytest.param(
            12, "DEL_ADD", "REPLACE", "m1_first", "s2", False,
            "M1/M2 MODRDN, M1 MOD(DEL/ADD)->V1, M2 MOD(REPL)->V2; resume M1 then M2; expect V2",
            id="08_m1_del_add_m2_repl_resume_m1_expect_v2",
        ),
        pytest.param(
            13, "DEL_ADD", "REPLACE", "m2_first", "s2", False,
            "M1/M2 MODRDN, M1 MOD(DEL/ADD)->V1, M2 MOD(REPL)->V2; resume M2 then M1; expect V2",
            id="09_m1_del_add_m2_repl_resume_m2_expect_v2",
        ),
        pytest.param(
            14, "DEL_ADD", "DEL_ADD", "m1_first", "s2", False,
            "M1/M2 MODRDN, both MOD(DEL/ADD); resume M1 then M2; expect V2",
            id="10_both_del_add_resume_m1_expect_v2",
        ),
        pytest.param(
            15, "DEL_ADD", "DEL_ADD", "m2_first", "s2", False,
            "M1/M2 MODRDN, both MOD(DEL/ADD); resume M2 then M1; expect V2",
            id="11_both_del_add_resume_m2_expect_v2",
        ),
        pytest.param(
            16, "REPLACE", "REPLACE", "m2_first", "s1", True,
            "M1/M2 MODRDN, both MOD(REPL), M2 MODRDN->V1; resume M2 then M1; expect V1",
            id="12_m2_repl_then_modrdn_v1_resume_m2_expect_v1",
        ),
        pytest.param(
            17, "REPLACE", "REPLACE", "m1_first", "s1", True,
            "M1/M2 MODRDN, both MOD(REPL), M2 MODRDN->V1; resume M1 then M2; expect V1",
            id="13_m2_repl_then_modrdn_v1_resume_m1_expect_v1",
        ),
        pytest.param(
            18, "REPLACE", None, "m2_first", "s1", True,
            "M1/M2 MODRDN, M1 MOD(REPL)->V1, M2 MODRDN->V1; resume M2 then M1; expect V1",
            id="14_m1_repl_m2_modrdn_v1_resume_m2_expect_v1",
        ),
        pytest.param(
            19, "REPLACE", None, "m1_first", "s1", True,
            "M1/M2 MODRDN, M1 MOD(REPL)->V1, M2 MODRDN->V1; resume M1 then M2; expect V1",
            id="15_m1_repl_m2_modrdn_v1_resume_m1_expect_v1",
        ),
    ],
)
def test_distinguished_employee_rdn_scenario(
    topology_m3_employee_number_conflict,
    user_index,
    s1_mod_kind,
    s2_mod_kind,
    resume_order,
    expected_key,
    m2_final_to_s1,
    scenario_description,
):
    """Check parametrized MODRDN and MOD conflicts on distinguished entries.

    :id: f3835f41-9d21-4c24-94b4-3d6b1c2cdd6f
    :setup: Three suppliers with distinguished employeeNumber RDN entries
    :steps:
        1. Pause replication agreements on M1 and M2
        2. Apply the parametrized MODRDN and MOD sequence on M1 and M2
        3. Resume replication in the parametrized supplier order
        4. Verify the expected employeeNumber on M1, M2, and M3
    :expectedresults:
        1. M1 and M2 are isolated during local updates
        2. Conflicting changes remain local until agreements resume
        3. M3 receives updates in the chosen resume order
        4. All suppliers converge on the parametrized winning employeeNumber
    """
    log.info(f"Distinguished RDN scenario: {scenario_description}")
    m1 = topology_m3_employee_number_conflict.ms["supplier1"]
    m2 = topology_m3_employee_number_conflict.ms["supplier2"]
    m3 = topology_m3_employee_number_conflict.ms["supplier3"]
    value_s1 = f"{user_index}.1"
    value_s2 = f"{user_index}.2"
    expected = value_s1 if expected_key == "s1" else value_s2
    s2_mod = (
        _employee_number_mod_for_kind(s2_mod_kind, value_s2) if s2_mod_kind else None
    )
    _run_distinguished_employee_rdn_scenario(
        m1,
        m2,
        m3,
        user_index,
        value_s1,
        value_s2,
        _employee_number_mod_for_kind(s1_mod_kind, value_s1),
        _resume_mesh_for_order(resume_order),
        expected,
        s2_mod=s2_mod,
        m2_final_modrdn=value_s1 if m2_final_to_s1 else None,
    )


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

