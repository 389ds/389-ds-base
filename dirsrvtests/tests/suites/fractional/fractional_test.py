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

import os
import pytest
from lib389.topologies import topology_m2c2
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts, UserAccount
from lib389.replica import ReplicationManager
from lib389.agreement import Agreements
from lib389.plugins import MemberOfPlugin
from lib389.idm.group import Groups
from lib389.config import Config
import ldap

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
    :expected results:
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
    :expected results:
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
    :expected results:
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
    :expected results:
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


@pytest.mark.bz154948
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
    :expected results:
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


@pytest.mark.bz739172
def test_newly_added_attribute_nsds5replicatedattributelisttotal(_create_entries, _add_user_clean):
    """This test case is to test the newly added attribute nsds5replicatedattributelistTotal.

    :id: 2df5971c-38eb-11ea-9e8e-8c16451d917b
    :setup: Supplier and Consumer
    :steps:
        1. Enabling memberOf plugin and then adding few groups with member attributes.
        2. No memberOf plugin enabled on read only replicas
        3. The attributes mentioned in the nsds5replicatedattributelist
           excluded from incremental updates.
    :expected results:
        1. Success
        2. Success
        3. Success
    """
    check_all_replicated()
    user = f'uid=test_user_1000,ou=People,{DEFAULT_SUFFIX}'
    for instance in (SUPPLIER1, SUPPLIER2, CONSUMER1, CONSUMER2):
        assert Groups(instance, DEFAULT_SUFFIX).list()[1].get_attr_val_utf8("member") == user
        assert UserAccount(instance, user).get_attr_val_utf8("sn") == "test_user_1000"
    # The attributes mentioned in the nsds5replicatedattributelist
    # excluded from incremental updates.
    for instance in (CONSUMER1, CONSUMER2):
        for value in ("roomnumber", "manager", "telephoneNumber"):
            assert not UserAccount(instance, user).get_attr_val_utf8(value)


@pytest.mark.bz739172
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
    :expected results:
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


@pytest.mark.bz800173
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
    :expected results:
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


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
