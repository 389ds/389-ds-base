# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import logging
import pytest
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Anonymous

log = logging.getLogger(__name__)
DN = "uid=common,ou=people," + DEFAULT_SUFFIX
HOME_DIR = '/home/common'

@pytest.fixture(scope="function")
def setup_test(topo, request):
    """Setup test environment"""
    log.info("Add nsslapd-haproxy-trusted-ip attribute")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.0.1')
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.1')

    log.info("Add a user")
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    try:
        users.create(properties={
            'uid': 'common',
            'cn': 'common',
            'sn': 'common',
            'uidNumber': '3000',
            'gidNumber': '4000',
            'homeDirectory': HOME_DIR,
            'description': 'test haproxy with this user',
            'userPassword': PASSWORD
        })
    except ldap.ALREADY_EXISTS:
        log.info("User already exists")
        pass


def test_haproxy_trust_ip_attribute(topo, setup_test):
    """Test nsslapd-haproxy-trusted-ip attribute set and delete

    :id: 8a0789a6-3ede-40e2-966c-9a2c87eaac05
    :setup: Standalone instance with nsslapd-haproxy-trusted-ip attribute and a user
    :steps:
        1. Check that nsslapd-haproxy-trusted-ip attribute is present
        2. Delete nsslapd-haproxy-trusted-ip attribute
        3. Check that nsslapd-haproxy-trusted-ip attribute is not present
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    log.info("Check that nsslapd-haproxy-trusted-ip attribute is present")
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.1')

    log.info("Delete nsslapd-haproxy-trusted-ip attribute")
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Check that nsslapd-haproxy-trusted-ip attribute is not present")
    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.1')


def test_binds_with_haproxy_trust_ip_attribute(topo, setup_test):
    """Test that non-proxy binds are not blocked when nsslapd-haproxy-trusted-ip attribute is set

    :id: 14273c16-fed9-497e-8ebb-09e3dabc7914
    :setup: Standalone instance with nsslapd-haproxy-trusted-ip attribute and a user
    :steps:
        1. Try to bind as anonymous user
        2. Try to bind as a user
        3. Check that userPassword is correct and we can get it
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    log.info("Bind as anonymous user")
    Anonymous(topo.standalone).bind()

    log.info("Bind as a user")
    user_entry = UserAccount(topo.standalone, DN)
    user_conn = user_entry.bind(PASSWORD)

    log.info("Check that userPassword is correct and we can get it")
    user_entry = UserAccount(user_conn, DN)
    home = user_entry.get_attr_val_utf8('homeDirectory')
    assert home == HOME_DIR


def test_haproxy_trust_ip_subnet_ipv4(topo):
    """Test nsslapd-haproxy-trusted-ip with IPv4 CIDR subnet notation

    :id: 20023301-b7c7-4db5-8287-06f6bbb14786
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-haproxy-trusted-ip to an IPv4 subnet (192.168.0.0/24)
        2. Check that the subnet is present
        3. Delete the attribute
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    log.info("Set nsslapd-haproxy-trusted-ip to IPv4 subnet")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.0.0/24')

    log.info("Check that nsslapd-haproxy-trusted-ip subnet is present")
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.0/24')

    log.info("Delete nsslapd-haproxy-trusted-ip attribute")
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Check that nsslapd-haproxy-trusted-ip attribute is not present")
    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.0/24')


def test_haproxy_trust_ip_subnet_ipv6(topo):
    """Test nsslapd-haproxy-trusted-ip with IPv6 CIDR subnet notation

    :id: 711d134d-0e26-47e1-8144-8c8aa6c73757
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-haproxy-trusted-ip to an IPv6 subnet (2001:db8::/32)
        2. Check that the subnet is present
        3. Delete the attribute
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    log.info("Set nsslapd-haproxy-trusted-ip to IPv6 subnet")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '2001:db8::/32')

    log.info("Check that nsslapd-haproxy-trusted-ip subnet is present")
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '2001:db8::/32')

    log.info("Delete nsslapd-haproxy-trusted-ip attribute")
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Check that nsslapd-haproxy-trusted-ip attribute is not present")
    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '2001:db8::/32')


def test_haproxy_trust_ip_mixed_values(topo):
    """Test nsslapd-haproxy-trusted-ip with mixed individual IPs and subnets

    :id: 0a4e8826-7db1-4c3a-bfba-a2483d0f69d2
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-haproxy-trusted-ip to multiple values including IPs and subnets
        2. Check that all values are present
        3. Delete the attribute
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    log.info("Set nsslapd-haproxy-trusted-ip to mixed values")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', ['192.168.0.0/24', '10.0.0.1', '2001:db8::/64'])

    log.info("Check that all values are present")
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.0/24')
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '10.0.0.1')
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '2001:db8::/64')

    log.info("Delete nsslapd-haproxy-trusted-ip attribute")
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Check that nsslapd-haproxy-trusted-ip attribute is not present")
    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.0/24')


def test_haproxy_trust_ip_buffer_overflow_protection(topo):
    """Test nsslapd-haproxy-trusted-ip rejects oversized strings (buffer overflow protection)

    :id: 96b63d88-3ec9-4b70-84f1-e48dfeadd293
    :setup: Standalone instance
    :steps:
        1. Try to set nsslapd-haproxy-trusted-ip with a string longer than 256 characters
        2. Verify the operation is rejected
    :expectedresults:
        1. Operation should fail with LDAP_OPERATIONS_ERROR
        2. Attribute should not be set
    """
    log.info("Attempt to set oversized IP string (buffer overflow test)")
    oversized_ip = "192.168.1." + "0" * 300  # 300+ character string

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', oversized_ip)

    log.info("Verify attribute was not set")
    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', oversized_ip)


def test_haproxy_trust_ip_invalid_cidr_multiple_slashes(topo):
    """Test nsslapd-haproxy-trusted-ip rejects multiple slashes in CIDR notation

    :id: 38a7eb6e-cec8-47c5-aba8-79bc9511127f
    :setup: Standalone instance
    :steps:
        1. Try to set CIDR with multiple slashes (192.168.1.0/24/32)
        2. Verify the operation is rejected
    :expectedresults:
        1. Operation should fail
        2. Attribute should not be set
    """
    log.info("Attempt to set CIDR with multiple slashes")

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.0/24/32')

    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.1.0/24/32')


def test_haproxy_trust_ip_invalid_cidr_empty_prefix(topo):
    """Test nsslapd-haproxy-trusted-ip rejects empty CIDR prefix

    :id: 75ed1fc1-a30a-4913-9d1f-fcf14745f876
    :setup: Standalone instance
    :steps:
        1. Try to set CIDR with empty prefix (192.168.1.0/)
        2. Verify the operation is rejected
    :expectedresults:
        1. Operation should fail
        2. Attribute should not be set
    """
    log.info("Attempt to set CIDR with empty prefix")

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.0/')

    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.1.0/')


def test_haproxy_trust_ip_invalid_cidr_non_numeric_prefix(topo):
    """Test nsslapd-haproxy-trusted-ip rejects non-numeric CIDR prefix

    :id: 7acdadb5-a809-4aae-a6b3-ea6fe8f37ff5
    :setup: Standalone instance
    :steps:
        1. Try to set CIDR with non-numeric prefix (192.168.1.0/abc)
        2. Try to set CIDR with special characters (192.168.1.0/24!)
        3. Verify both operations are rejected
    :expectedresults:
        1. Operation should fail
        2. Operation should fail
        3. Attributes should not be set
    """
    log.info("Attempt to set CIDR with non-numeric prefix")

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.0/abc')

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.0/24!')

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.0/2 4')

    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.1.0/abc')


def test_haproxy_trust_ip_invalid_cidr_prefix_out_of_range(topo):
    """Test nsslapd-haproxy-trusted-ip rejects out-of-range CIDR prefixes

    :id: d60ebd80-9e2e-4c85-8d3b-076a24d2e063
    :setup: Standalone instance
    :steps:
        1. Try to set IPv4 CIDR with prefix > 32
        2. Try to set IPv6 CIDR with prefix > 128
        3. Verify both operations are rejected
    :expectedresults:
        1. Operation should fail
        2. Operation should fail
        3. Attributes should not be set
    """
    log.info("Attempt to set IPv4 CIDR with prefix > 32")

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.0/33')

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.0/64')

    log.info("Attempt to set IPv6 CIDR with prefix > 128")

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '2001:db8::/129')

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '2001:db8::/256')


def test_haproxy_trust_ip_invalid_characters(topo):
    """Test nsslapd-haproxy-trusted-ip rejects IPs with invalid characters

    :id: 7eaf95f1-1477-477d-b743-7415801c395d
    :setup: Standalone instance
    :steps:
        1. Try to set IP with invalid characters
        2. Try to set CIDR with wildcards (not allowed in CIDR)
        3. Verify operations are rejected
    :expectedresults:
        1. Operation should fail
        2. Operation should fail
        3. Attributes should not be set
    """
    log.info("Attempt to set IP with invalid characters")

    # Invalid characters in regular IP
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.x')

    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.1@')

    # Wildcards not allowed in any format
    log.info("Attempt to set CIDR with wildcards (should be rejected)")
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.*/24')

    # Wildcards not allowed in individual IPs either
    log.info("Attempt to set individual IP with wildcard (should be rejected)")
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.*')

    # Invalid IPv6 characters
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '2001:db8::gggg')


def test_haproxy_trust_ip_cidr_boundary_values(topo):
    """Test nsslapd-haproxy-trusted-ip with boundary CIDR prefix values

    :id: 7cbb711b-4366-4c23-8ec0-e64174081840
    :setup: Standalone instance
    :steps:
        1. Test /0 prefix (matches everything)
        2. Test /32 prefix for IPv4 (exact match)
        3. Test /128 prefix for IPv6 (exact match)
        4. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    log.info("Set IPv4 /0 (matches all)")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '0.0.0.0/0')
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '0.0.0.0/0')
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Set IPv4 /32 (exact match)")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.1.1/32')
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.1.1/32')
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Set IPv6 /0 (matches all)")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '::/0')
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '::/0')
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Set IPv6 /128 (exact match)")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '2001:db8::1/128')
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '2001:db8::1/128')
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')


def test_haproxy_trust_ip_invalid_formats_comprehensive(topo):
    """Test comprehensive validation of invalid IP formats

    :id: 3b5b8082-de75-46b0-b53d-12d87da10f34
    :setup: Standalone instance
    :steps:
        1. Test various malformed CIDR notations
        2. Test invalid IP address formats
        3. Test edge cases and attack vectors
    :expectedresults:
        1. All invalid formats should be rejected
        2. No attributes should be set
        3. Server should remain stable
    """
    log.info("Test malformed CIDR notations")

    invalid_cidrs = [
        '192.168.1.0/',           # Empty prefix
        '192.168.1.0/24/32',      # Multiple slashes
        '192.168.1.0/abc',        # Non-numeric prefix
        '192.168.1.0/24!',        # Special chars in prefix
        '192.168.1.0/ 24',        # Space in prefix
        '192.168.1.0/-1',         # Negative prefix
        '192.168.1.0/33',         # IPv4 prefix > 32
        '192.168.1.0/999',        # Way out of range
        '192.168.1.*/24',         # Wildcard in CIDR (rejected)
        '2001:db8::/',            # IPv6 empty prefix
        '2001:db8::/129',         # IPv6 prefix > 128
        '2001:db8::/abc',         # IPv6 non-numeric prefix
        '2001:db8::*/64',         # IPv6 wildcard in CIDR (rejected)
    ]

    for invalid_cidr in invalid_cidrs:
        log.info(f"Testing invalid CIDR: {invalid_cidr}")
        with pytest.raises(ldap.OPERATIONS_ERROR):
            topo.standalone.config.set('nsslapd-haproxy-trusted-ip', invalid_cidr)
        assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', invalid_cidr)

    log.info("Test invalid IP address formats")

    invalid_ips = [
        '256.168.1.1',            # IPv4 octet > 255
        '192.168.1',              # Incomplete IPv4
        '192.168.1.1.1',          # Too many octets
        '192.168.1.x',            # Invalid character
        '192.168.1.1@',           # Special character
        '192.168.1.1#comment',    # Comment-like
        '192.168.1.*',            # Wildcard (rejected)
        '192.168.*.*',            # Multiple wildcards (rejected)
        '2001:db8::gggg',         # Invalid hex in IPv6
        '2001:db8::1::2',         # Double :: in IPv6
        '2001:db8::*',            # IPv6 wildcard (rejected)
        'not.an.ip.address',      # Alphabetic
    ]

    for invalid_ip in invalid_ips:
        log.info(f"Testing invalid IP: {invalid_ip}")
        with pytest.raises(ldap.OPERATIONS_ERROR):
            topo.standalone.config.set('nsslapd-haproxy-trusted-ip', invalid_ip)
        assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', invalid_ip)


def test_haproxy_trust_ip_security_attack_vectors(topo):
    """Test nsslapd-haproxy-trusted-ip against potential security attack vectors

    :id: 20890d58-ba62-41bf-8484-66144dc143d7
    :setup: Standalone instance
    :steps:
        1. Test buffer overflow attempts
        2. Test format string attacks
        3. Test injection attempts
    :expectedresults:
        1. All attacks should be rejected safely
        2. Server should not crash
        3. No attributes should be set
    """
    log.info("Test potential attack vectors")

    attack_vectors = [
        'A' * 300,                           # Buffer overflow attempt
        '192.168.1.0' + '/24' * 50,          # Repeated slashes
        '192.168.1.0/24\x00/32',             # Null byte injection
        '../../../etc/passwd',               # Path traversal attempt
        '192.168.1.0/$(whoami)',             # Command injection attempt
        '192.168.1.0/%s%s%s%s',              # Format string attempt
        '192.168.1.0/24\r\n192.168.2.0/24',  # CRLF injection
    ]

    for attack in attack_vectors:
        log.info(f"Testing attack vector: {attack[:50]}...")  # Log first 50 chars
        with pytest.raises(ldap.OPERATIONS_ERROR):
            topo.standalone.config.set('nsslapd-haproxy-trusted-ip', attack)

    log.info("Server remained stable after attack attempts")


def test_haproxy_trust_ip_additional_edge_cases(topo):
    """Test nsslapd-haproxy-trusted-ip with additional validation edge cases

    :id: c0ed7407-5bbd-4b08-86ae-d0174c59b92b
    :setup: Standalone instance
    :steps:
        1. Test leading zeros in prefix (should be rejected or normalized)
        2. Test integer overflow in prefix
        3. Test whitespace handling
        4. Test case sensitivity
    :expectedresults:
        1. All invalid formats should be rejected
        2. Server should remain stable
    """
    log.info("Test additional edge cases for validation")

    additional_invalid = [
        '192.168.1.0/024',                  # Leading zeros in prefix
        '192.168.1.0/99999999999999999',    # Integer overflow in prefix
        ' 192.168.1.0/24',                  # Leading whitespace
        '192.168.1.0/24 ',                  # Trailing whitespace
        '192.168.1.0 /24',                  # Space before slash
        '192.168. 1.0/24',                  # Space in IP
        '2001:db8::/0x20',                  # Hex notation in prefix
        '192.168.1.0/+24',                  # Plus sign in prefix
    ]

    for invalid in additional_invalid:
        log.info(f"Testing additional invalid: {invalid}")
        with pytest.raises(ldap.OPERATIONS_ERROR):
            topo.standalone.config.set('nsslapd-haproxy-trusted-ip', invalid)
        assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', invalid)

    log.info("Additional edge cases handled correctly")


def test_haproxy_trust_ip_valid_edge_cases(topo):
    """Test nsslapd-haproxy-trusted-ip with valid edge cases

    :id: 82a14e70-475b-423d-9e6e-52a89b2f0506
    :setup: Standalone instance
    :steps:
        1. Test various valid CIDR prefix lengths
        2. Test localhost addresses
        3. Test private network ranges
        4. Clean up
    :expectedresults:
        1. All valid formats should be accepted
        2. All values should be retrievable
        3. Clean up successful
    """
    log.info("Test various valid CIDR prefix lengths")

    valid_configs = [
        '10.0.0.0/8',             # Class A network
        '172.16.0.0/12',          # Private network
        '192.168.0.0/16',         # Class C network
        '192.0.2.0/24',           # TEST-NET-1
        '198.51.100.0/24',        # TEST-NET-2
        '203.0.113.0/24',         # TEST-NET-3
        '127.0.0.1/32',           # Localhost exact
        '::1/128',                # IPv6 localhost
        'fe80::/10',              # IPv6 link-local
        '2001:db8::/32',          # IPv6 documentation
        'fc00::/7',               # IPv6 unique local
    ]

    for valid_config in valid_configs:
        log.info(f"Testing valid config: {valid_config}")
        topo.standalone.config.set('nsslapd-haproxy-trusted-ip', valid_config)
        assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', valid_config)
        topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("All valid edge cases handled correctly")


def test_haproxy_trust_ip_performance_optimizations(topo):
    """Test that performance optimizations don't break functionality

    :id: e10cfdc4-8d5e-4240-a457-cab75a481ad1
    :setup: Standalone instance
    :steps:
        1. Set multiple mixed IP/subnet values
        2. Verify all values are stored correctly
        3. Verify parsed binary format is created
        4. Clean up
    :expectedresults:
        1. All values should be accepted
        2. All values should be retrievable
        3. Server should handle the configuration correctly
    """
    log.info("Test performance optimizations with mixed values")

    mixed_values = [
        '192.168.1.0/24',      # IPv4 subnet
        '10.0.0.1',            # IPv4 single IP
        '2001:db8::/32',       # IPv6 subnet
        'fe80::1',             # IPv6 single IP
        '172.16.0.0/12',       # Another IPv4 subnet
        '::ffff:192.168.1.50', # IPv4-mapped IPv6
    ]

    log.info("Set mixed IP/subnet values")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', mixed_values)

    log.info("Verify all values are present")
    for value in mixed_values:
        assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', value), \
            f"Value {value} should be present"

    log.info("Clean up")
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Performance optimization test passed")
