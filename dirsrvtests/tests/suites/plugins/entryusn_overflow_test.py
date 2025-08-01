# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import ldap
import logging
import pytest
import time
import random
from lib389._constants import DEFAULT_SUFFIX
from lib389.config import Config
from lib389.plugins import USNPlugin
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st
from lib389.rootdse import RootDSE

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

# Test constants
DEMO_USER_BASE_DN = "uid=demo_user,ou=people," + DEFAULT_SUFFIX
TEST_USER_PREFIX = "Demo User"
MAX_USN_64BIT = 18446744073709551615  # 2^64 - 1
ITERATIONS = 10
ADD_EXISTING_ENTRY_MAX_ATTEMPTS = 5


@pytest.fixture(scope="module")
def setup_usn_test(topology_st, request):
    """Setup USN plugin and test data for entryUSN overflow testing"""

    inst = topology_st.standalone

    log.info("Enable the USN plugin...")
    plugin = USNPlugin(inst)
    plugin.enable()
    plugin.enable_global_mode()

    inst.restart()

    # Create initial test users
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    created_users = []

    log.info("Creating initial test users...")
    for i in range(3):
        user_props = {
            'uid': f'{TEST_USER_PREFIX}-{i}',
            'cn': f'{TEST_USER_PREFIX}-{i}',
            'sn': f'User{i}',
            'uidNumber': str(1000 + i),
            'gidNumber': str(1000 + i),
            'homeDirectory': f'/home/{TEST_USER_PREFIX}-{i}',
            'userPassword': 'password123'
        }
        try:
            user = users.create(properties=user_props)
            created_users.append(user)
            log.info(f"Created user: {user.dn}")
        except ldap.ALREADY_EXISTS:
            log.info(f"User {user_props['uid']} already exists, skipping creation")
            user = users.get(user_props['uid'])
            created_users.append(user)

    def fin():
        log.info("Cleaning up test users...")
        for user in created_users:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass

    request.addfinalizer(fin)

    return created_users


@pytest.mark.xfail(reason="DS6250")
def test_entryusn_overflow_on_add_existing_entries(topology_st, setup_usn_test):
    """Test that reproduces entryUSN overflow when adding existing entries

    :id: a5a8c33d-82f3-4113-be2b-027de51791c8
    :setup: Standalone instance with USN plugin enabled and test users
    :steps:
        1. Record initial entryUSN values for existing users
        2. Attempt to add existing entries multiple times (should fail)
        3. Perform modify operations on the entries
        4. Check that entryUSN values increment correctly without overflow
        5. Verify lastusn values are consistent
    :expectedresults:
        1. Initial entryUSN values are recorded successfully
        2. Add operations fail with ALREADY_EXISTS error
        3. Modify operations succeed
        4. EntryUSN values increment properly without underflow/overflow
        5. LastUSN values are consistent and increasing
    """

    inst = topology_st.standalone
    users = setup_usn_test

    # Enable detailed logging for debugging
    config = Config(inst)
    config.replace('nsslapd-accesslog-level', '260')  # Internal op logging
    config.replace('nsslapd-errorlog-level', '65536')
    config.replace('nsslapd-plugin-logging', 'on')

    root_dse = RootDSE(inst)

    log.info("Starting entryUSN overflow reproduction test")

    # Record initial state
    initial_usn_values = {}
    for user in users:
        initial_usn = user.get_attr_val_int('entryusn')
        initial_usn_values[user.dn] = initial_usn
        log.info(f"Initial entryUSN for {user.get_attr_val_utf8('cn')}: {initial_usn}")

    initial_lastusn = root_dse.get_attr_val_int("lastusn")
    log.info(f"Initial lastUSN: {initial_lastusn}")

    # Perform test iterations
    for iteration in range(1, ITERATIONS + 1):
        log.info(f"\n--- Iteration {iteration} ---")

        # Step 1: Try to add existing entries multiple times
        selected_user = random.choice(users)
        cn_value = selected_user.get_attr_val_utf8('cn')
        attempts = random.randint(1, ADD_EXISTING_ENTRY_MAX_ATTEMPTS)

        log.info(f"Attempting to add existing entry '{cn_value}' {attempts} times")

        # Get user attributes for recreation attempt
        user_attrs = {
            'uid': selected_user.get_attr_val_utf8('uid'),
            'cn': selected_user.get_attr_val_utf8('cn'),
            'sn': selected_user.get_attr_val_utf8('sn'),
            'uidNumber': selected_user.get_attr_val_utf8('uidNumber'),
            'gidNumber': selected_user.get_attr_val_utf8('gidNumber'),
            'homeDirectory': selected_user.get_attr_val_utf8('homeDirectory'),
            'userPassword': 'password123'
        }

        users_collection = UserAccounts(inst, DEFAULT_SUFFIX)

        # Try to add the existing user multiple times
        for attempt in range(attempts):
            try:
                users_collection.create(properties=user_attrs)
                log.error(f"ERROR: Add operation should have failed but succeeded on attempt {attempt + 1}")
                assert False, "Add operation should have failed with ALREADY_EXISTS"
            except ldap.ALREADY_EXISTS:
                log.info(f"Attempt {attempt + 1}: Got expected ALREADY_EXISTS error")
            except Exception as e:
                log.error(f"Unexpected error on attempt {attempt + 1}: {e}")
                raise

        # Step 2: Perform modify operation
        target_user = random.choice(users)
        cn_value = target_user.get_attr_val_utf8('cn')
        old_usn = target_user.get_attr_val_int('entryusn')

        # Modify the user entry
        new_description = f"Modified in iteration {iteration} - {time.time()}"
        target_user.replace('description', new_description)

        # Get new USN value
        new_usn = target_user.get_attr_val_int('entryusn')

        log.info(f"Modified entry '{cn_value}': old USN = {old_usn}, new USN = {new_usn}")

        # Step 3: Validate USN values
        # Check for overflow/underflow conditions
        assert new_usn > 0, f"EntryUSN should be positive, got {new_usn}"
        assert new_usn < MAX_USN_64BIT, f"EntryUSN overflow detected: {new_usn} >= {MAX_USN_64BIT}"

        # Check that USN didn't wrap around (underflow detection)
        usn_diff = new_usn - old_usn
        assert usn_diff < 1000, f"USN increment too large, possible overflow: {usn_diff}"

        # Verify lastUSN is also reasonable
        current_lastusn = root_dse.get_attr_val_int("lastusn")
        assert current_lastusn >= new_usn, f"LastUSN ({current_lastusn}) should be >= entryUSN ({new_usn})"
        assert current_lastusn < MAX_USN_64BIT, f"LastUSN overflow detected: {current_lastusn}"

        log.info(f"USN validation passed for iteration {iteration}")

        # Add a new entry occasionally to increase USN diversity
        if iteration % 3 == 0:
            new_user_props = {
                'uid': f'{TEST_USER_PREFIX}-new-{iteration}',
                'cn': f'{TEST_USER_PREFIX}-new-{iteration}',
                'sn': f'NewUser{iteration}',
                'uidNumber': str(2000 + iteration),
                'gidNumber': str(2000 + iteration),
                'homeDirectory': f'/home/{TEST_USER_PREFIX}-new-{iteration}',
                'userPassword': 'newpassword123'
            }
            try:
                new_user = users_collection.create(properties=new_user_props)
                new_user_usn = new_user.get_attr_val_int('entryusn')
                log.info(f"Created new entry '{new_user.get_attr_val_utf8('cn')}' with USN: {new_user_usn}")
                users.append(new_user)  # Add to cleanup list
            except Exception as e:
                log.warning(f"Failed to create new user in iteration {iteration}: {e}")

    # Final validation: Check all USN values are reasonable
    log.info("\nFinal USN validation")
    final_lastusn = root_dse.get_attr_val_int("lastusn")

    for user in users:
        try:
            final_usn = user.get_attr_val_int('entryusn')
            cn_value = user.get_attr_val_utf8('cn')
            log.info(f"Final entryUSN for '{cn_value}': {final_usn}")

            # Ensure no overflow occurred
            assert final_usn > 0, f"Final entryUSN should be positive for {cn_value}: {final_usn}"
            assert final_usn < MAX_USN_64BIT, f"EntryUSN overflow for {cn_value}: {final_usn}"

        except ldap.NO_SUCH_OBJECT:
            log.info(f"User {user.dn} was deleted during test")

    log.info(f"Final lastUSN: {final_lastusn}")
    assert final_lastusn > initial_lastusn, "LastUSN should have increased during test"
    assert final_lastusn < MAX_USN_64BIT, f"LastUSN overflow detected: {final_lastusn}"

    log.info("EntryUSN overflow test completed successfully")


@pytest.mark.xfail(reason="DS6250")
def test_entryusn_consistency_after_failed_adds(topology_st, setup_usn_test):
    """Test that entryUSN remains consistent after failed add operations

    :id: e380ccad-527b-427e-a331-df5c41badbed
    :setup: Standalone instance with USN plugin enabled and test users
    :steps:
        1. Record entryUSN values before failed add attempts
        2. Attempt to add existing entries (should fail)
        3. Verify entryUSN values haven't changed due to failed operations
        4. Perform successful modify operations
        5. Verify entryUSN increments correctly
    :expectedresults:
        1. Initial entryUSN values recorded
        2. Add operations fail as expected
        3. EntryUSN values unchanged after failed adds
        4. Modify operations succeed
        5. EntryUSN values increment correctly without overflow
    """

    inst = topology_st.standalone
    users = setup_usn_test

    log.info("Testing entryUSN consistency after failed adds")

    # Record USN values before any operations
    pre_operation_usns = {}
    for user in users:
        usn = user.get_attr_val_int('entryusn')
        pre_operation_usns[user.dn] = usn
        log.info(f"Pre-operation entryUSN for {user.get_attr_val_utf8('cn')}: {usn}")

    # Attempt to add existing entries - these should fail
    users_collection = UserAccounts(inst, DEFAULT_SUFFIX)

    for user in users:
        cn_value = user.get_attr_val_utf8('cn')
        log.info(f"Attempting to add existing user: {cn_value}")

        user_attrs = {
            'uid': user.get_attr_val_utf8('uid'),
            'cn': cn_value,
            'sn': user.get_attr_val_utf8('sn'),
            'uidNumber': user.get_attr_val_utf8('uidNumber'),
            'gidNumber': user.get_attr_val_utf8('gidNumber'),
            'homeDirectory': user.get_attr_val_utf8('homeDirectory'),
            'userPassword': 'password123'
        }

        try:
            users_collection.create(properties=user_attrs)
            assert False, f"Add operation should have failed for existing user {cn_value}"
        except ldap.ALREADY_EXISTS:
            log.info(f"Got expected ALREADY_EXISTS for {cn_value}")

    # Verify USN values haven't changed after failed adds
    log.info("Verifying entryUSN values after failed add operations...")
    for user in users:
        current_usn = user.get_attr_val_int('entryusn')
        expected_usn = pre_operation_usns[user.dn]
        cn_value = user.get_attr_val_utf8('cn')

        assert current_usn == expected_usn, \
            f"EntryUSN changed after failed add for {cn_value}: was {expected_usn}, now {current_usn}"
        log.info(f"EntryUSN unchanged for {cn_value}: {current_usn}")

    # Now perform successful modify operations
    log.info("Performing successful modify operations...")
    for i, user in enumerate(users):
        cn_value = user.get_attr_val_utf8('cn')
        old_usn = user.get_attr_val_int('entryusn')

        # Modify the user
        user.replace('description', f'Consistency test modification {i + 1}')

        new_usn = user.get_attr_val_int('entryusn')
        log.info(f"Modified {cn_value}: USN {old_usn} -> {new_usn}")

        # Verify proper increment
        assert (new_usn - old_usn) == 1, f"EntryUSN should increment by 1 for {cn_value}: {old_usn} -> {new_usn}"
        assert new_usn < MAX_USN_64BIT, f"EntryUSN overflow for {cn_value}: {new_usn}"

    log.info("EntryUSN consistency test completed successfully")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)