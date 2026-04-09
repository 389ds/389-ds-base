# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""
This test script will test fractional replication.
"""

import logging
import os
import re
import time
from types import SimpleNamespace

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.agreement import Agreements
from lib389.config import Config
from lib389.idm.group import Groups
from lib389.idm.user import UserAccount, UserAccounts
from lib389.plugins import MemberOfPlugin
from lib389.replica import ReplicationManager, Replicas
from lib389.topologies import topology_m2, topology_m2c2
from lib389.utils import ensure_bytes, ensure_str

log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1

SUPPLIER1 = SUPPLIER2 = CONSUMER1 = CONSUMER2 = None


def _create_users(instance, cn_cn, sn_sn, givenname, ou_ou, l_l, uid, mail,
                  telephonenumber, facsimiletelephonenumber, roomnumber):
    """
    Will create sample user.
    """
    user = instance.create(properties={
        'cn': cn_cn,
        'sn': sn_sn,
        'givenname': givenname,
        'ou': ou_ou,
        'l': l_l,
        'uid': uid,
        'mail': mail,
        'telephonenumber': telephonenumber,
        'facsimiletelephonenumber': facsimiletelephonenumber,
        'roomnumber': roomnumber,
        'uidNumber': '111',
        'gidNumber': '111',
        'homeDirectory': f'/home/{uid}',

    })
    return user


def check_all_replicated():
    """
    Will check replication status
    """
    for supplier in [SUPPLIER2, CONSUMER1, CONSUMER2]:
        ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(SUPPLIER1, supplier, timeout=100)


@pytest.fixture(scope="module")
def _create_entries(topology_m2c2):
    """
    A fixture that will create first test user and create fractional Agreement
    """
    # Defining as global , as same value will be used everywhere with same name.
    global SUPPLIER1, SUPPLIER2, CONSUMER1, CONSUMER2
    SUPPLIER1 = topology_m2c2.ms['supplier1']
    SUPPLIER2 = topology_m2c2.ms['supplier2']
    CONSUMER1 = topology_m2c2.cs['consumer1']
    CONSUMER2 = topology_m2c2.cs['consumer2']
    users = UserAccounts(SUPPLIER1, DEFAULT_SUFFIX)
    _create_users(users, 'Sam Carter', 'Carter', 'Sam', ['Accounting', 'People'],
                  'Sunnyvale', 'scarter', 'scarter@red.com', '+1 408 555 4798',
                  '+1 408 555 9751', '4612')
    for ins, num in [(SUPPLIER1, 1), (SUPPLIER2, 2), (SUPPLIER1, 2), (SUPPLIER2, 1)]:
        Agreements(ins).list()[num].replace(
            'nsDS5ReplicatedAttributeList',
            '(objectclass=*) $ EXCLUDE audio businessCategory carLicense departmentNumber '
            'destinationIndicator displayName employeeNumber employeeType facsimileTelephoneNumber '
            'roomNumber telephoneNumber memberOf manager accountUnlockTime '
            'passwordRetryCount retryCountResetTime')
        Agreements(ins).list()[num].replace(
            'nsDS5ReplicatedAttributeListTotal',
            '(objectclass=*) $ EXCLUDE audio businessCategory carLicense departmentNumber '
            'destinationIndicator displayName employeeNumber employeeType facsimileTelephoneNumber '
            'roomNumber telephoneNumber accountUnlockTime passwordRetryCount retryCountResetTime')
        Agreements(ins).list()[num].begin_reinit()
        Agreements(ins).list()[num].wait_reinit()


def test_fractional_agreements(_create_entries):
    """The attributes should be present on the two suppliers with traditional replication
    agreements, but not with fractional agreements.

    :id: f22395e0-38ea-11ea-abe0-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Add test entry
        2. Search for an entry with disallowed attributes on every server.
        3. The attributes should be present on the two suppliers with traditional replication
           agreements
        4. Should be missing on both consumers with fractional agreements.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    check_all_replicated()
    # Search for an entry with disallowed attributes on every server.
    for attr in ['telephonenumber', 'facsimiletelephonenumber', 'roomnumber']:
        assert UserAccount(SUPPLIER1, f'uid=scarter,ou=People,{DEFAULT_SUFFIX}').get_attr_val(attr)
        assert UserAccount(SUPPLIER2, f'uid=scarter,ou=People,{DEFAULT_SUFFIX}').get_attr_val(attr)
    # The attributes should be present on the two suppliers with
    # traditional replication agreements
    for attr in ['telephonenumber', 'facsimiletelephonenumber', 'roomnumber']:
        assert not UserAccount(CONSUMER1,
                               f'uid=scarter,ou=People,{DEFAULT_SUFFIX}').get_attr_val(attr)
        assert not UserAccount(CONSUMER2,
                               f'uid=scarter,ou=People,{DEFAULT_SUFFIX}').get_attr_val(attr)


def test_read_only_consumer(_create_entries):
    """Attempt to modify an entry on read-only consumer.

    :id: f97f0fea-38ea-11ea-a617-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Add test entry
        2. First attempt to modify an attribute that should be visible (mail)
        3. Then attempt to modify one that should not be visible (roomnumber)
    :expectedresults:
        1. Success
        2. Fail(ldap.INSUFFICIENT_ACCESS)
        3. Fail(ldap.INSUFFICIENT_ACCESS)
    """
    # Add test entry
    user_consumer1 = UserAccount(CONSUMER1, f'uid=scarter,ou=People,{DEFAULT_SUFFIX}')
    user_consumer2 = UserAccount(CONSUMER2, f'uid=scarter,ou=People,{DEFAULT_SUFFIX}')
    # First attempt to modify an attribute that should be visible (mail)
    for attr, value in [('mail', 'anuj@borah.com'), ('roomnumber', '123')]:
        with pytest.raises(ldap.INSUFFICIENT_ACCESS):
            user_consumer1.replace(attr, value)
    # Then attempt to modify one that should not be visible (room number)
    for attr, value in [('mail', 'anuj@borah.com'), ('roomnumber', '123')]:
        with pytest.raises(ldap.INSUFFICIENT_ACCESS):
            user_consumer2.replace(attr, value)


def test_read_write_supplier(_create_entries):
    """Attempt to modify an entry on read-write supplier

    :id: ff50a8b6-38ea-11ea-870f-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Add test entry
        2. First attempt to modify an attribute that should be visible (mail)
        3. Then attempt to modify one that should not be visible (roomnumber)
        4. The change to mail should appear on all servers; the change to
           room number should only appear on the suppliers INST[0] and INST[1].
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    # Add test entry
    user_supplier1 = UserAccount(SUPPLIER1, f'uid=scarter,ou=People,{DEFAULT_SUFFIX}')
    # First attempt to modify an attribute that should be visible (mail)
    for attr, value in [('mail', 'anuj@borah.com'), ('roomnumber', '123')]:
        user_supplier1.replace(attr, value)
    check_all_replicated()
    for ins, attr in [(SUPPLIER2, 'mail'),
                      (SUPPLIER2, 'roomnumber'),
                      (CONSUMER1, 'mail'),
                      (CONSUMER2, 'mail')]:
        if attr == 'mail':
            assert UserAccount(ins,
                               f'uid=scarter,'
                               f'ou=People,{DEFAULT_SUFFIX}').get_attr_val_utf8(attr) == \
                   'anuj@borah.com'
        elif attr == 'roomnumber':
            assert UserAccount(ins,
                               f'uid=scarter,'
                               f'ou=People,{DEFAULT_SUFFIX}').get_attr_val_utf8(attr) == '123'
    # Attempt to modify one that should not be visible (room number)
    for ins in [CONSUMER1, CONSUMER2]:
        assert not UserAccount(ins,
                               f'uid=scarter,ou=People,{DEFAULT_SUFFIX}').get_attr_val('roomnumber')


def test_filtered_attributes(_create_entries):
    """Filtered attributes are not replicated to CONSUMER1 or CONSUMER2.

    :id: 051b40ee-38eb-11ea-9126-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Add a new entry to SUPPLIER1.
        2. Confirm that it is replicated in entirety
           to SUPPLIER2, but that filtered attributes are not replicated to
           CONSUMER1 or CONSUMER2.
        3. The entry should be present in all servers.  Filtered attributes should not
           be available from the consumers with fractional replication agreements.
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    # Add a new entry to SUPPLIER1.
    users = UserAccounts(SUPPLIER1, DEFAULT_SUFFIX)
    _create_users(users, 'Anuj Borah', 'aborah', 'Anuj', 'People', 'ok',
                  'aborah', 'aborah@aborah.com', '+1121', '+121', '2121')
    check_all_replicated()
    for instance in [SUPPLIER1, SUPPLIER2, CONSUMER1, CONSUMER2]:
        assert UserAccount(instance,
                           f'uid=aborah,'
                           f'ou=People,{DEFAULT_SUFFIX}').get_attr_val_utf8('mail') == \
               'aborah@aborah.com'
    for instance in [SUPPLIER1, SUPPLIER2]:
        assert UserAccount(instance,
                           f'uid=aborah,'
                           f'ou=People,{DEFAULT_SUFFIX}').get_attr_val_utf8('roomnumber') == '2121'
    # The entry should be present in all servers.  Filtered attributes should not
    # be available from the consumers with fractional replication agreements.
    for instance in [CONSUMER1, CONSUMER2]:
        assert not UserAccount(instance,
                               f'uid=aborah,'
                               f'ou=People,{DEFAULT_SUFFIX}').get_attr_val_utf8('roomnumber')


def test_fewer_changes_in_single_operation(_create_entries):
    """For bug 154948, which cause the server to crash if there were
    fewer changes (but more than one) in a single operation to fractionally
    replicated attributes than the number of fractionally replicated attributes.
    The primary test is that all servers are still alive.

    :id: 0d1d6218-38eb-11ea-8945-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Add a new entry to SUPPLIER1.
        2. Fewer changes (but more than one) in a single operation to fractionally
           replicated attributes than the number of fractionally replicated attributes.
        3. All servers are still alive.
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    users = UserAccounts(SUPPLIER1, DEFAULT_SUFFIX)
    user = _create_users(users, 'Anuj Borah1', 'aborah1', 'Anuj1', 'People',
                         'ok1', 'aborah1', 'aborah1@aborah1.com', '+11212', '+1212', '21231')
    check_all_replicated()
    # Fewer changes (but more than one) in a single operation to fractionally
    # replicated attributes than the number of fractionally replicated attributes.
    user.replace_many(('carLicense', '111-1111'), ('description', 'Hi'))
    check_all_replicated()
    user.replace_many(('mail', 'memail@ok.com'), ('sn', 'Oak'), ('l', 'NewPlace'))
    check_all_replicated()
    # All servers are still alive.
    for ints in [SUPPLIER1, SUPPLIER2, CONSUMER1, CONSUMER2]:
        assert UserAccount(ints, user.dn).get_attr_val_utf8('mail') == 'memail@ok.com'
        assert UserAccount(ints, user.dn).get_attr_val_utf8('sn') == 'Oak'


@pytest.fixture(scope="function")
def _add_user_clean(request):
    # Enabling memberOf plugin and then adding few groups with member attributes.
    MemberOfPlugin(SUPPLIER1).enable()
    for instance in (SUPPLIER1, SUPPLIER2):
        instance.restart()
    user1 = UserAccounts(SUPPLIER1, DEFAULT_SUFFIX).create_test_user()
    for attribute, value in [("displayName", "Anuj Borah"),
                             ("givenName", "aborah"),
                             ("telephoneNumber", "+1 555 999 333"),
                             ("roomnumber", "123"),
                             ("manager", f'uid=dsmith,ou=People,{DEFAULT_SUFFIX}')]:
        user1.set(attribute, value)
    grp = Groups(SUPPLIER1, DEFAULT_SUFFIX).create(properties={
        "cn": "bug739172_01group",
        "member": f'uid=test_user_1000,ou=People,{DEFAULT_SUFFIX}'
    })

    def final_call():
        """
        Removes User and Group after the test.
        """
        user1.delete()
        grp.delete()
    request.addfinalizer(final_call)


def test_newly_added_attribute_nsds5replicatedattributelisttotal(_create_entries, _add_user_clean):
    """This test case is to test the newly added attribute nsds5replicatedattributelistTotal.

    :id: 2df5971c-38eb-11ea-9e8e-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Enabling memberOf plugin and then adding few groups with member attributes.
        2. No memberOf plugin enabled on read only replicas
        3. The attributes mentioned in the nsds5replicatedattributelist
           excluded from incremental updates.
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    check_all_replicated()
    user = f'uid=test_user_1000,ou=People,{DEFAULT_SUFFIX}'
    for instance in (SUPPLIER1, SUPPLIER2, CONSUMER1, CONSUMER2):
        g = Groups(instance, DEFAULT_SUFFIX).get('bug739172_01group')
        assert g.get_attr_val_utf8("member") == user
        assert UserAccount(instance, user).get_attr_val_utf8("sn") == "test_user_1000"
    # The attributes mentioned in the nsds5replicatedattributelist
    # excluded from incremental updates.
    for instance in (CONSUMER1, CONSUMER2):
        for value in ("roomnumber", "manager", "telephoneNumber"):
            assert not UserAccount(instance, user).get_attr_val_utf8(value)


def test_attribute_nsds5replicatedattributelisttotal(_create_entries, _add_user_clean):
    """This test case is to test the newly added attribute nsds5replicatedattributelistTotal.

    :id: 35de9ff0-38eb-11ea-b420-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Add a new entry to SUPPLIER1.
        2. Enabling memberOf plugin and then adding few groups with member attributes.
        3. No memberOf plugin enabled in other consumers,ie., the read only replicas
           won't get incremental updates for the attributes mentioned in the list.
        4. Run total update and verify the same attributes added/modified in the read-only replicas.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    # Run total update and verify the same attributes added/modified in the read-only replicas.
    user = f'uid=test_user_1000,ou=People,{DEFAULT_SUFFIX}'
    for agreement in Agreements(SUPPLIER1).list():
        agreement.begin_reinit()
        agreement.wait_reinit()
    check_all_replicated()
    for instance in (SUPPLIER1, SUPPLIER2):
        assert Groups(SUPPLIER1, DEFAULT_SUFFIX).list()[1].get_attr_val_utf8("member") == user
        assert UserAccount(instance, user).get_attr_val_utf8("sn") == "test_user_1000"
    for instance in (CONSUMER1, CONSUMER2):
        for value in ("memberOf", "manager", "sn"):
            assert UserAccount(instance, user).get_attr_val_utf8(value)


def test_implicit_replication_of_password_policy(_create_entries):
    """For bug 800173, we want to cause the implicit replication of password policy
    attributes due to failed bind operations
    we want to make sure that replication still works despite
    the policy attributes being removed from the update leaving an empty
    modify operation

    :id: 3f4affe8-38eb-11ea-8936-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Add a new entry to SUPPLIER1.
        2. Try binding user with correct password
        3. Try binding user with incorrect password (twice)
        4. Make sure user got locked
        5. Run total update and verify the same attributes added/modified in the read-only replicas.
    :expectedresults:
        1. Success
        2. Success
        3. FAIL(ldap.INVALID_CREDENTIALS)
        4. Success
        5. Success
    """
    for attribute, value in [("passwordlockout", "on"),
                             ("passwordmaxfailure", "1")]:
        Config(SUPPLIER1).set(attribute, value)
    user = UserAccounts(SUPPLIER1, DEFAULT_SUFFIX).create_test_user()
    user.set("userpassword", "ItsmeAnuj")
    check_all_replicated()
    assert UserAccount(SUPPLIER2, user.dn).get_attr_val_utf8("uid") == "test_user_1000"
    # Try binding user with correct password
    conn = UserAccount(SUPPLIER2, user.dn).bind("ItsmeAnuj")
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        UserAccount(SUPPLIER1, user.dn).bind("badpass")
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        UserAccount(SUPPLIER1, user.dn).bind("badpass")
    # asserting user got locked
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        conn = UserAccount(SUPPLIER1, user.dn).bind("ItsmeAnuj")
    check_all_replicated()
    # modify user and verify that replication is still working
    user.replace("seealso", "cn=seealso")
    check_all_replicated()
    for instance in (SUPPLIER1, SUPPLIER2):
        assert UserAccount(instance, user.dn).get_attr_val_utf8("seealso") == "cn=seealso"


# --- Two-supplier fractional replication (CSN evaluation, description sync) ---

FRACTIONAL_M2_STAGING_ACCOUNT = "new_account"
FRACTIONAL_M2_STAGING_COUNT = 20


def _fractional_m2_agreement_list(inst):
    return Replicas(inst).get(DEFAULT_SUFFIX).get_agreements().list()


@pytest.fixture(scope="function")
def fractional_m2_setup(request, topology_m2):
    """Create entries, tune logs, and configure fractional replication on both suppliers."""
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]

    users = UserAccounts(supplier1, DEFAULT_SUFFIX)
    for cpt in range(FRACTIONAL_M2_STAGING_COUNT):
        name = f"{FRACTIONAL_M2_STAGING_ACCOUNT}{cpt}"
        props = {"uid": name, "sn": name, "cn": name,
                 "uidNumber": str(cpt), "gidNumber": str(cpt),
                 "homeDirectory": f"/home/{name}"}
        user = users.create(properties=props)

    Config(supplier1).set("nsslapd-accesslog-logbuffering", "off")
    Config(supplier1).set("nsslapd-errorlog-level", "8192")
    Config(supplier1).set("nsslapd-accesslog-level", "260")

    Config(supplier2).set("nsslapd-accesslog-logbuffering", "off")
    Config(supplier2).set("nsslapd-errorlog-level", "8192")
    Config(supplier2).set("nsslapd-accesslog-level", "260")

    assert len(_fractional_m2_agreement_list(supplier1)) == 1
    assert len(_fractional_m2_agreement_list(supplier2)) == 1

    fractional = (
        ("nsDS5ReplicatedAttributeList", "(objectclass=*) $ EXCLUDE telephonenumber"),
        ("nsds5ReplicaStripAttrs", "modifiersname modifytimestamp"),
    )
    for inst in (supplier1, supplier2):
        _fractional_m2_agreement_list(inst)[0].replace_many(*fractional)

    supplier1.restart()
    supplier2.restart()

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.ensure_agreement(supplier1, supplier2)
    repl.test_replication(supplier1, supplier2)

    def fin():
        for cpt in range(FRACTIONAL_M2_STAGING_COUNT):
            name = f"{FRACTIONAL_M2_STAGING_ACCOUNT}{cpt}"
            user = UserAccounts(supplier1, DEFAULT_SUFFIX).get(name)
            user.delete()
        supplier1.restart()
        supplier2.restart()
        repl = ReplicationManager(DEFAULT_SUFFIX)
        repl.ensure_agreement(supplier1, supplier2)
        repl.test_replication(supplier1, supplier2)

    request.addfinalizer(fin)

    yield SimpleNamespace(
        topology_m2=topology_m2,
        supplier1=supplier1,
        supplier2=supplier2,
    )


def _parse_csn_line(line):
    """ Helper function to extract the CSN from a line """
    parsed = {item.split('=')[0]: item.split('=')[1] for item in line['rem'].split()}
    return parsed['csn']


def _fractional_m2_get_unreplicated_csn(supplier1, dn):
    """CSN for a telephoneNumber change (not replicated fractionally)."""
    UserAccount(supplier1, dn).replace("telephonenumber", "123456")

    raw = supplier1.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)", ["nscpentrywsi"])
    nscp_vals = raw.getValues("nscpentrywsi")
    assert nscp_vals
    tel_line = None
    for val in nscp_vals:
        if ensure_str(val.lower()).startswith("telephonenumber"):
            tel_line = val
            break
    assert tel_line

    found_ops = supplier1.ds_access_log.match(f".*MOD dn=\"{dn}\".*")
    assert len(found_ops) > 0
    found_op = supplier1.ds_access_log.parse_line(found_ops[-1])

    found_csns = supplier1.ds_access_log.match(
        f".*conn={found_op['conn']} op={found_op['op']} RESULT.*")
    assert len(found_csns) > 0

    found_csn = supplier1.ds_access_log.parse_line(found_csns[-1])
    return _parse_csn_line(found_csn)


def _fractional_m2_count_full_session(supplier1):
    pattern = ".*No more updates to send.*"
    regex = re.compile(pattern)
    no_more_updates = 0
    with open(supplier1.errlog, "r") as file_obj:
        while True:
            line = file_obj.readline()
            if regex.search(line):
                no_more_updates = no_more_updates + 1
            if line == "":
                break
    return no_more_updates


def test_fractional_m2_replication(topology_m2, fractional_m2_setup):
    """Replication works between two suppliers after fractional agreement configuration.

    :id: a7f39c2e-4b1d-42f8-9e63-8d501a6c2f94
    :setup: Two suppliers (topology_m2), fractional_m2_setup
    :steps:
        1. Confirm a single replication agreement exists on supplier1.
        2. Ensure the agreement from supplier1 to supplier2 exists and run a replication check.
    :expectedresults:
        1. Success
        2. Success
    """
    ents = _fractional_m2_agreement_list(topology_m2.ms["supplier1"])
    assert len(ents) == 1

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.ensure_agreement(topology_m2.ms["supplier1"], topology_m2.ms["supplier2"])
    repl.test_replication(topology_m2.ms["supplier1"], topology_m2.ms["supplier2"])


def test_fractional_m2_replicated_description(topology_m2, fractional_m2_setup):
    """A non-fractional attribute modified on supplier1 appears on supplier2.

    :id: b3e81d5f-6c2e-53a9-af74-9e612b7d3ea5
    :setup: Two suppliers (topology_m2), fractional_m2_setup
    :steps:
        1. Set description on a staging user on supplier1.
        2. Poll supplier2 until the same description value is present on the entry.
    :expectedresults:
        1. Success
        2. Success within the wait loop
    """
    name = f"{FRACTIONAL_M2_STAGING_ACCOUNT}1"
    users = UserAccounts(topology_m2.ms["supplier1"], DEFAULT_SUFFIX)
    user = users.get(name)
    description = "check repl. description"
    user.set("description", description)

    ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topology_m2.ms["supplier1"], topology_m2.ms["supplier2"])
    ent = topology_m2.ms["supplier2"].getEntry(user.dn, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr("description") and ent.getValue("description") == ensure_bytes(description)


def _wait_for_replication_sessions_to_complete(supplier1, no_more_update_cnt):
    max_loop = 10
    cnt = 0
    current_no_more_update = _fractional_m2_count_full_session(supplier1)
    while current_no_more_update == no_more_update_cnt:
        cnt = cnt + 1
        if cnt > max_loop:
            assert False, f"Timeout waiting for replication sessions to complete after {cnt} loops"
        time.sleep(5)
        current_no_more_update = _fractional_m2_count_full_session(supplier1)

    return current_no_more_update

def test_fractional_m2_csn_evaluation_count(topology_m2, fractional_m2_setup):
    """Verify that the CSN evaluation count is correct

    :id: c9f54a8b-7d3f-64ba-b085-0f723c8e4fb6
    :setup: Two suppliers (topology_m2), fractional_m2_setup
    :steps:
        1. Record a CSN for a telephoneNumber change that is not replicated (fractional exclude).
        2. Pause the agreement, apply many telephoneNumber updates on supplier1, resume and wait
           for replication sessions to complete.
        3. Drive further non-replicated updates and waits; scan the error log after the last CSN
           ruv line and assert first CSN is reported with "Skipping update operation" message.
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    supplier1 = topology_m2.ms["supplier1"]
    assert len(_fractional_m2_agreement_list(supplier1)) == 1
    dn2 = f"uid={FRACTIONAL_M2_STAGING_ACCOUNT}2,ou=people,{DEFAULT_SUFFIX}"
    first_csn = _fractional_m2_get_unreplicated_csn(supplier1, dn2)
    
    dn = f"uid={FRACTIONAL_M2_STAGING_ACCOUNT}3,ou=people,{DEFAULT_SUFFIX}"
    user = UserAccount(supplier1, dn)
    nb_session = 102

    no_more_update_cnt = _fractional_m2_count_full_session(supplier1)
    agmt = _fractional_m2_agreement_list(supplier1)[0]
    agmt.pause()
    for tel_number in range(nb_session):
        user.replace("telephonenumber", str(tel_number))

    agmt.resume()

    current_no_more_update = _wait_for_replication_sessions_to_complete(supplier1, no_more_update_cnt)
    log.info(
        f"after {nb_session} MODs we have completed {(current_no_more_update - no_more_update_cnt)} replication sessions")
    no_more_update_cnt = current_no_more_update

    dn5 = f"uid={FRACTIONAL_M2_STAGING_ACCOUNT}5,ou=people,{DEFAULT_SUFFIX}"
    last_csn = _fractional_m2_get_unreplicated_csn(supplier1, dn5)

    current_no_more_update = _wait_for_replication_sessions_to_complete(supplier1, no_more_update_cnt)
    log.info(
        f"This MODs {last_csn} triggered the send of the dummy update completed {(current_no_more_update - no_more_update_cnt)} replication sessions")
    no_more_update_cnt = current_no_more_update

    agmt = _fractional_m2_agreement_list(supplier1)[0]
    agmt.pause()
    last_csn = _fractional_m2_get_unreplicated_csn(supplier1, dn5)
    agmt.resume()

    current_no_more_update = _wait_for_replication_sessions_to_complete(supplier1, no_more_update_cnt)
    log.info(
        f"This MODs {last_csn} completed in {(current_no_more_update - no_more_update_cnt)} replication sessions, should be sent without evaluating {first_csn}")
    no_more_update_cnt = current_no_more_update

    pattern = f".*ruv_add_csn_inprogress - Successfully inserted csn {last_csn}.*"
    regex = re.compile(pattern)
    found_ruv = None

    with open(supplier1.errlog, "r") as file_obj:
        while True:
            line = file_obj.readline()
            if regex.search(line):
                found_ruv = line
                break
            if line == "":
                break
        assert found_ruv
        log.info(f"Last operation was found at {file_obj.tell()}")
        log.info(found_ruv)
 
        log.info(f"Now check the first csn {first_csn} is present in the log")

        pattern_skip = f".*Skipping update operation.*CSN {first_csn}.*"
        regex_skip = re.compile(pattern_skip)
        found_skip = False
        while True:
            line = file_obj.readline()
            if regex_skip.search(line):
                found_skip = True
                break
            if line == "":
                break
        assert found_skip
        log.info(f"First csn {first_csn} found in the log")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
