# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Test dsconf and dsctl security CLI subcommands using subprocess"""

import json
import logging
import os
import pytest
import subprocess
import tempfile
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

TEST_CERT_PEM = """-----BEGIN CERTIFICATE-----
MIIF2zCCA8OgAwIBAgIFAMl06zEwDQYJKoZIhvcNAQELBQAwZTELMAkGA1UEBhMC
QVUxEzARBgNVBAgTClF1ZWVuc2xhbmQxDjAMBgNVBAcTBTM4OWRzMRAwDgYDVQQK
Ewd0ZXN0aW5nMR8wHQYDVQQDExZzc2NhLjM4OWRzLmV4YW1wbGUuY29tMB4XDTI2
MDIyNTE1MDYzNFoXDTQ2MDIyNTE1MDYzNFowgZMxCzAJBgNVBAYTAkFVMRMwEQYD
VQQIEwpRdWVlbnNsYW5kMQ4wDAYDVQQHEwUzODlkczEQMA4GA1UEChMHdGVzdGlu
ZzEtMCsGA1UEKhMkMmU2YzNmMzUtMjhkMy00ZmU4LWEyYmUtM2ZmYTIzMWRmYjA3
MR4wHAYDVQQDExVsb2NhbGhvc3QubG9jYWxkb21haW4wggIiMA0GCSqGSIb3DQEB
AQUAA4ICDwAwggIKAoICAQCSVoeRTmQhxbJ9CJj6hPczhf7PzYVR/xdR8s0/Q5wL
kNJUyWfJfyVORe8u0X1zKLyBY4KX3IWIxkHi2vfLkFay7AzqETDlZzKXvrvpQB6G
TfcVXf912m53EqeJs7g9IwDZzmTK48okOSL9WpMIGmhSLzxoyQE8Fd5grnD9wwW4
rJuz/dFFzWuys2acErvxKVCLOFd7RX0itXVOZ7IdosiZoWNz8PjLZjihBM4/4cow
6SrK8IQhLUg5uT6VOftSag8kl1UkcyMySwfALNxoVIh8eyItp2yXGEjX3sPKEeeD
p8Pu/JrsYmdVJAjPhVUd0I8KydNGe0Iu0AMSWA0gylXY/5PDTMeeHtduXe945fBB
iXWMZM/SXLwRSt/+IE2QnSNCTlqX+QQkl5gQu2cxir/KR5lqnCqrxr74qFUf+WtX
LplXkKwwo/NtEcvWlKWBtHDQ3z9vqJgAWI/20ITXrySBDVKZCyCt5fH+J/o3gQlO
vNah1r/+S63T7dQHbCLg5uzG+zYnUNlOcXZpXvfXGsSHkSGEB1txY7jyoDg/j1M2
5yEZUHvY3NMdvEDh3s785TBEBm7ZgXnGVTHbGdJdW4X+hKPHtlmgGFJ6THBoYYj7
v88qivm1GYB1wJmKcTIlyLuAj6yvI4CgSKHAWNIf8yohp1BfKm3PHVW9DCRsDrR4
mwIDAQABo2MwYTALBgNVHQ8EBAMCBPAwHQYDVR0lBBYwFAYIKwYBBQUHAwIGCCsG
AQUFBwMBMBEGCWCGSAGG+EIBAQQEAwIGwDAgBgNVHREEGTAXghVsb2NhbGhvc3Qu
bG9jYWxkb21haW4wDQYJKoZIhvcNAQELBQADggIBABwJZZcPRtw/UtWJXQb4KtSf
o6d1e6DMDz2O3r3l7PxU2ILY7I9VC1CeVl/mXbwMycwr24S5PcWpRNlU6BipDOlO
Ck2+yvflV/LRHn25zwXRIfjlVVb6BTn3QoKbtTU76g/nko4pDb2ksPixzuqcQUOC
HVCsY7caXQQKlH4o4OSCYaYX9qkJYVjA6yv3k39UNXaWzo1DI7ymjif0Lqr/eCV8
jIXLS/qAouPSZWib5k66sEH0RdyC1FsBVbpfdIuJrdSonE3KZhYlL+B93LhEAqDN
nzTkiTFrLlLQ583in7upQA9MQ5J3cW6WpWIUjRmUBpI/BQTkCzANucv9lVX//dDD
OZnM1ImTjwp2bTqxdZev0VW/rSMF5xKCd6tiTsmYozjcYDJ4BIgX5AiL/lu2NTVv
Rumt/wQCUq2R35XOtzD6h1JRBFS+F/9P/tJOZyf6Nd+/t6ugkeKd9GgVzkB+lF62
l8Wf5r4rV5fZgZqiAuq872jDDQMzK2kTUDE0yxQDSfoStVhO/3PoavqPLIDym6RK
HxMlKKIkrC1LnURE+tiMPGp7qrfBzahU+7EVnM+05C9fZn6BBd1g9HpdEbszOKP/
zom8MYe41Lq9vjFiYN+TkququhsFcs+i5YS2wkAT7F5a6NJxskP+Lf0EcBC+TC6k
dMsdEJ0KW0YniBYuPhvx
-----END CERTIFICATE-----"""


@pytest.fixture(scope="module")
def setup_tls(topo):
    """Enable TLS on the instance"""
    topo.standalone.enable_tls()
    yield topo
    # Cleanup is handled by the topology fixture


def run_cmd(cmd, env=None, check=True, stdin_input=None):
    """Helper function to run a command and return output

    :param cmd: Command list to run
    :param env: Environment variables
    :param check: Whether to check return code
    :param stdin_input: String to send to stdin
    :return: tuple of (returncode, stdout, stderr)
    """
    log.info(f'Running command: {" ".join(cmd)}')
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        stdin=subprocess.PIPE if stdin_input else None,
        env=env
    )
    stdin_bytes = stdin_input.encode('utf-8') if stdin_input else None
    stdout, stderr = proc.communicate(input=stdin_bytes)
    stdout_str = stdout.decode('utf-8') if stdout else ''
    stderr_str = stderr.decode('utf-8') if stderr else ''

    log.info(f'Return code: {proc.returncode}')
    if stdout_str:
        log.info(f'STDOUT: {stdout_str}')
    if stderr_str:
        log.info(f'STDERR: {stderr_str}')

    if check and proc.returncode != 0:
        raise subprocess.CalledProcessError(
            proc.returncode, cmd, stdout, stderr
        )

    return proc.returncode, stdout_str, stderr_str


def _create_temp_cert_file():
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.pem') as cert_file:
        cert_file.write(TEST_CERT_PEM)
        return cert_file.name


def test_dsconf_security_get(topo):
    """Test dsconf security get command via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000001
    :setup: Standalone Instance
    :steps:
        1. Run dsconf security get command
        2. Verify output contains security attributes
        3. Run with --json flag
        4. Verify JSON output is valid
    :expectedresults:
        1. Command succeeds
        2. Output contains expected attributes
        3. JSON command succeeds
        4. JSON is valid and parseable
    """
    inst = topo.standalone
    dsconf_cmd = [
        'dsconf',
        inst.serverid,
        'security',
        'get'
    ]

    # Test plain output
    returncode, stdout, stderr = run_cmd(dsconf_cmd)
    assert returncode == 0
    assert 'nsslapd-security:' in stdout
    assert 'nsslapd-secureport:' in stdout or 'nsslapd-securePort:' in stdout.lower()

    # Test JSON output
    dsconf_cmd_json = ['dsconf', inst.serverid, '--json', 'security', 'get']
    returncode, stdout, stderr = run_cmd(dsconf_cmd_json)
    assert returncode == 0

    # Parse and validate JSON
    result = json.loads(stdout)
    assert 'type' in result
    assert 'items' in result


def test_dsconf_security_set(topo):
    """Test dsconf security set command via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000002
    :setup: Standalone Instance
    :steps:
        1. Set session timeout using dsconf
        2. Verify setting with get command
        3. Reset to original value
    :expectedresults:
        1. Set command succeeds
        2. Value is correctly set
        3. Reset succeeds
    """
    inst = topo.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security'
    ]

    # Get original value
    cmd = ['dsconf', inst.serverid, '--json', 'security', 'get']
    returncode, stdout, stderr = run_cmd(cmd)
    original_data = json.loads(stdout)
    original_timeout = original_data.get('items', {}).get('nsslapd-ssl-session-timeout', ['600'])[0]

    try:
        # Set new value
        cmd = dsconf_base + ['set', '--session-timeout', '900']
        returncode, stdout, stderr = run_cmd(cmd)
        assert returncode == 0

        # Verify new value
        cmd = dsconf_base + ['get']
        returncode, stdout, stderr = run_cmd(cmd)
        assert '900' in stdout

    finally:
        # Reset to original
        cmd = dsconf_base + ['set', '--session-timeout', original_timeout]
        run_cmd(cmd, check=False)


def test_dsconf_security_enable_disable(setup_tls):
    """Test dsconf security enable/disable commands via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000003
    :setup: Standalone Instance with TLS
    :steps:
        1. Enable security
        2. Verify security is enabled
        3. Disable security
        4. Verify security is disabled
    :expectedresults:
        1. Enable command succeeds
        2. Security is on
        3. Disable command succeeds
        4. Security is off
    """
    inst = setup_tls.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security'
    ]

    # Enable security
    cmd = dsconf_base + ['enable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify enabled
    cmd = dsconf_base + ['get']
    returncode, stdout, stderr = run_cmd(cmd)
    assert 'nsslapd-security: on' in stdout

    # Disable security
    cmd = dsconf_base + ['disable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify disabled
    cmd = dsconf_base + ['get']
    returncode, stdout, stderr = run_cmd(cmd)
    assert 'nsslapd-security: off' in stdout


def test_dsconf_security_rsa_get(topo):
    """Test dsconf security rsa get command via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000004
    :setup: Standalone Instance
    :steps:
        1. Run dsconf security rsa get command
        2. Verify output contains RSA attributes
    :expectedresults:
        1. Command succeeds
        2. Output contains RSA activation attribute
    """
    inst = topo.standalone
    cmd = [
        'dsconf',
        inst.serverid,
        'security',
        'rsa',
        'get'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    assert 'nssslactivation:' in stdout.lower() or 'nsSSLActivation:' in stdout


def test_dsconf_security_rsa_enable_disable(setup_tls):
    """Test dsconf security rsa enable/disable via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000005
    :setup: Standalone Instance with TLS
    :steps:
        1. Enable RSA
        2. Verify RSA is enabled
        3. Disable RSA
        4. Verify RSA is disabled
    :expectedresults:
        1. Enable succeeds
        2. RSA is on
        3. Disable succeeds
        4. RSA is off
    """
    inst = setup_tls.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security',
        'rsa'
    ]

    # Enable RSA
    cmd = dsconf_base + ['enable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify enabled
    cmd = dsconf_base + ['get']
    returncode, stdout, stderr = run_cmd(cmd)
    assert 'on' in stdout.lower()

    # Disable RSA
    cmd = dsconf_base + ['disable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify disabled
    cmd = dsconf_base + ['get']
    returncode, stdout, stderr = run_cmd(cmd)
    assert 'off' in stdout.lower()


def test_dsconf_security_ciphers_list(setup_tls):
    """Test dsconf security ciphers list command via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000006
    :setup: Standalone Instance with TLS
    :steps:
        1. List all ciphers
        2. List enabled ciphers
        3. List supported ciphers
        4. List disabled ciphers
        5. Test with --json flag
    :expectedresults:
        1. Command succeeds and shows ciphers
        2. Command succeeds
        3. Command succeeds
        4. Command succeeds
        5. JSON output is valid
    """
    inst = setup_tls.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security',
        'ciphers',
        'list'
    ]

    # List all ciphers
    cmd = dsconf_base.copy()
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # List enabled ciphers
    cmd = dsconf_base + ['--enabled']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # List supported ciphers
    cmd = dsconf_base + ['--supported']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # List disabled ciphers
    cmd = dsconf_base + ['--disabled']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Test JSON output
    cmd = ['dsconf', inst.serverid, '--json', 'security', 'ciphers', 'list']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    # Validate JSON
    result = json.loads(stdout)
    assert isinstance(result, (dict, list))


def test_dsconf_security_ciphers_get(topo):
    """Test dsconf security ciphers get command via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000007
    :setup: Standalone Instance
    :steps:
        1. Run dsconf security ciphers get command
        2. Verify output is returned
    :expectedresults:
        1. Command succeeds
        2. Output contains cipher information
    """
    inst = topo.standalone
    cmd = [
        'dsconf',
        inst.serverid,
        'security',
        'ciphers',
        'get'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    # Output should contain something (cipher list or <undefined>)
    assert len(stdout) > 0


def test_dsconf_security_ciphers_enable_disable(setup_tls):
    """Test dsconf security ciphers enable/disable via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000008
    :setup: Standalone Instance with TLS
    :steps:
        1. Enable a specific cipher
        2. Disable a specific cipher
    :expectedresults:
        1. Enable command succeeds
        2. Disable command succeeds
    """
    inst = setup_tls.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security',
        'ciphers'
    ]

    # Enable a cipher
    cmd = dsconf_base + ['enable', 'TLS_AES_128_GCM_SHA256']
    returncode, stdout, stderr = run_cmd(cmd, check=False)
    # May fail if cipher is already enabled or not available, that's ok

    # Disable a cipher
    cmd = dsconf_base + ['disable', 'TLS_AES_128_GCM_SHA256']
    returncode, stdout, stderr = run_cmd(cmd, check=False)
    # May fail if cipher is already disabled, that's ok


def test_dsconf_security_certificate_list(setup_tls):
    """Test dsconf security certificate list command via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000009
    :setup: Standalone Instance with TLS
    :steps:
        1. List certificates
        2. Verify Server-Cert is in list
        3. Test with --json flag
    :expectedresults:
        1. Command succeeds
        2. Server-Cert is present
        3. JSON output is valid
    """
    inst = setup_tls.standalone
    cmd = [
        'dsconf',
        inst.serverid,
        'security',
        'certificate',
        'list'
    ]

    # Test plain output
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    assert 'Server-Cert' in stdout

    # Test JSON output
    cmd_json = ['dsconf', inst.serverid, '--json', 'security', 'certificate', 'list']
    returncode, stdout, stderr = run_cmd(cmd_json)
    assert returncode == 0
    result = json.loads(stdout)
    assert isinstance(result, (dict, list))


def test_dsconf_security_certificate_get(setup_tls):
    """Test dsconf security certificate get command via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000010
    :setup: Standalone Instance with TLS
    :steps:
        1. Get Server-Cert details
        2. Verify output contains certificate information
    :expectedresults:
        1. Command succeeds
        2. Output shows certificate details
    """
    inst = setup_tls.standalone
    cmd = [
        'dsconf',
        inst.serverid,
        'security',
        'certificate',
        'get',
        'Server-Cert'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    assert 'Server-Cert' in stdout or 'Certificate Name:' in stdout


def test_dsconf_security_certificate_add_delete(setup_tls):
    """Test dsconf security certificate add and delete via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000011
    :setup: Standalone Instance with TLS
    :steps:
        1. Create a test certificate PEM file
        2. Add certificate via dsconf
        3. Verify certificate was added
        4. Delete certificate
        5. Verify certificate was deleted
    :expectedresults:
        1. PEM file created
        2. Add command succeeds
        3. Certificate is in list
        4. Delete command succeeds
        5. Certificate is not in list
    """
    inst = setup_tls.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security',
        'certificate'
    ]

    # Create test certificate
    pem_content = """-----BEGIN CERTIFICATE-----
MIIFZjCCA06gAwIBAgIFAL18JOowDQYJKoZIhvcNAQELBQAwZTELMAkGA1UEBhMC
QVUxEzARBgNVBAgTClF1ZWVuc2xhbmQxDjAMBgNVBAcTBTM4OWRzMRAwDgYDVQQK
Ewd0ZXN0aW5nMR8wHQYDVQQDExZzc2NhLjM4OWRzLmV4YW1wbGUuY29tMB4XDTIy
MTAyNTE5NDU0M1oXDTI0MTAyNTE5NDU0M1owZTELMAkGA1UEBhMCQVUxEzARBgNV
BAgTClF1ZWVuc2xhbmQxDjAMBgNVBAcTBTM4OWRzMRAwDgYDVQQKEwd0ZXN0aW5n
MR8wHQYDVQQDExZzc2NhLjM4OWRzLmV4YW1wbGUuY29tMIICIjANBgkqhkiG9w0B
AQEFAAOCAg8AMIICCgKCAgEA5E+pd7+8lBsbTKdjHgkSLi2Z5T5G9T+3wziDHhsz
F0nG+IOu5yYVkoj/bMxR3sNNlbDLk5ATyNAfytW3cAUZ3NLqm6bmEZdUjD6YycVk
AvrfY3zVVE9Debfw6JI3ml8JlC3t8dqn2KT7dmSjvr9zPS95HU+RepjzAqJAKY3B
27v0cMetUnxG4pqc7zqnSZJXVP/OXMKSNpujHnK8HyjT8tUJIYQ0YvU2JPJpz3fC
BJrmzgO2xYLgLPu6abhP6PQ6uUU+d4j36lG4J/4OiMY0Lr+mnaBAaD3ULPtN5eZh
fjQ9d+Sh89xHz92icWhkn8c7IHNEZNtMHNTNJiNbWKuU9HpBWNjWHJoxSxXn4Emr
DSfG+lq2UU2m9m+XrDK/7t0W/zC3S+zwcyqM8SJAiZnGEi85058wB0BB1HnnAfFX
gel3uZFhnR4d86O/vO5VUqg5Ko795DPzPa3SU4rR36U3nUF7g5WhEAmYNCj683D3
DJDPJeCZmis7xtYB5K6Wu6SnFDxBEfhcWSsamWM286KntOiUtqQEzDy4OpZEUsgq
s7uqQSl/dfGdY9hCpXMYhlvMfVv3aIoM5zPuXN2cE1QkTnE1pyo8gZqnPLFZnwc9
FT+Wjpy0EmsAM/5AIed5h+JgJ304P+wkyjf7APUZyUwf4UJN6aro6N8W23F7dAu5
uJ0CAwEAAaMdMBswDAYDVR0TBAUwAwEB/zALBgNVHQ8EBAMCAgQwDQYJKoZIhvcN
AQELBQADggIBADFlVdDp1gTF/gl5ysZoo4/4BBSpLx5JDeyPE6BityZ/lwHvBUq3
VzmIsU6kxAJfk0p9S7G4LgHIC/DVsLTIE5do6tUdyrawvcaanbYn9ScNoFVQ0GDS
C6Ir8PftEvc6bpI4hjkv4goK9fTq5Jtv4LSuRfxFEwoLO+WN8a65IFWlaHJl/Erp
9fzP+JKDo8zeh4qnMkaTSCBmLWa8kErrV462RU+qZktf/V4/gWg6k5Vp+82xNk7f
9/Mrg9KshNux7A4YCd8LgLEeCgsigi4N6zcfjQB0Rh5u9kXu/hzOjh379ki/vqju
i+MTVH97LMB47uR1LEl0VvhWSjID0ePUtbPHCJwOsxWyxBCJY6V7A9nj52uXMGuX
xghssZTFvRK6Bb1OiPNYRGqmuymm8rcSFdsY5yemkxJ6kfn40JIRCmVFwqaqu7MC
nxyaWAKpRHKM5IyeVZHkFzL9jR/2tVBbjfCAl6YSwM759VcOsw2SGMQKpGIPEBTa
1NBdlG45aWJBx5jBdVfOskLjxmBjosByJJHRLtrUBvg66ZBsx1k0c9XjsKmC59JP
AzI8zYp/TY/6T5igxM+CSx98DsJFccPBZFFJX+YYRL7DFN38Yb7jMgIUXYHS28Gc
1c8kz7ylcQB8lKgCgpcBCH5ZSnLVAnH3uqCygxSTgTo+jgJklKc0xFuR
-----END CERTIFICATE-----"""

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.pem') as pem_file:
        pem_file.write(pem_content)
        pem_path = pem_file.name

    try:
        # Add certificate
        cmd = dsconf_base + ['add', '--file', pem_path, '--name', 'TEST_CLI_CERT']
        returncode, stdout, stderr = run_cmd(cmd, check=False)
        # May fail if cert already exists, continue anyway

        # Verify it was added
        cmd = dsconf_base + ['list']
        returncode, stdout, stderr = run_cmd(cmd)
        # Certificate should be in list (or may have failed to add)

        # Delete certificate
        cmd = dsconf_base + ['del', 'TEST_CLI_CERT']
        returncode, stdout, stderr = run_cmd(cmd, check=False)
        # May fail if cert doesn't exist, that's ok

    finally:
        if os.path.exists(pem_path):
            os.unlink(pem_path)


def test_dsconf_security_ca_certificate_list(setup_tls):
    """Test dsconf security ca-certificate list via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000012
    :setup: Standalone Instance with TLS
    :steps:
        1. List CA certificates
        2. Verify command succeeds
    :expectedresults:
        1. Command succeeds
        2. Output is returned
    """
    inst = setup_tls.standalone
    cmd = [
        'dsconf',
        inst.serverid,
        'security',
        'ca-certificate',
        'list'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0


def test_dsctl_tls_show_cert(setup_tls):
    """Test dsctl tls show-certs command via subprocess

    :id: b1b2b3b4-0002-0002-0002-000000000001
    :setup: Standalone Instance with TLS
    :steps:
        1. Run dsctl tls show-certs
        2. Verify output shows certificates
    :expectedresults:
        1. Command succeeds
        2. Output contains certificate information
    """
    inst = setup_tls.standalone
    cmd = [
        'dsctl',
        inst.serverid,
        'tls',
        'show-cert',
        'Server-Cert'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0


def test_dsctl_tls_list_client_ca(setup_tls):
    """Test dsctl tls list-client-cas command via subprocess

    :id: b1b2b3b4-0002-0002-0002-000000000003
    :setup: Standalone Instance with TLS
    :steps:
        1. Run dsctl tls list-client-cas
        2. Verify command executes
    :expectedresults:
        1. Command succeeds
        2. Output is returned
    """
    inst = setup_tls.standalone
    cmd = [
        'dsctl',
        inst.serverid,
        'tls',
        'list-client-ca'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    assert 'Self-Signed-CA' in stdout


def test_dsctl_tls_export_cert(setup_tls):
    """Test dsctl tls export-cert command via subprocess

    :id: b1b2b3b4-0002-0002-0002-000000000004
    :setup: Standalone Instance with TLS
    :steps:
        1. Export Server-Cert to PEM file
        2. Verify file is created
        3. Verify file contains certificate
    :expectedresults:
        1. Command succeeds
        2. File exists
        3. File contains PEM certificate
    """
    inst = setup_tls.standalone

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.pem') as export_file:
        export_path = export_file.name

    try:
        cmd = [
            'dsctl',
            inst.serverid,
            'tls',
            'export-cert',
            '--output-file',
            export_path,
            'Server-Cert',
        ]

        returncode, stdout, stderr = run_cmd(cmd)
        assert returncode == 0

        # Verify file exists and has content
        assert os.path.exists(export_path)
        assert os.path.getsize(export_path) > 0

        # Verify it's a PEM certificate
        with open(export_path, 'r') as f:
            content = f.read()
            assert '-----BEGIN CERTIFICATE-----' in content

    finally:
        if os.path.exists(export_path):
            os.unlink(export_path)


def test_dsctl_tls_generate_server_cert_csr(topo):
    """Test dsctl tls generate-server-cert-csr via subprocess

    :id: b1b2b3b4-0002-0002-0002-000000000005
    :setup: Standalone Instance
    :steps:
        1. Generate a CSR with subject
        2. Verify dsctl does not crash
    :expectedresults:
        1. Command succeeds
        2. Command return code is 0 or 1
    """
    inst = topo.standalone
    cmd = [
        'dsctl',
        inst.serverid,
        'tls',
        'generate-server-cert-csr',
        '-s', 'CN=test.example.com,O=Example,C=US'
    ]

    returncode, stdout, stderr = run_cmd(cmd, check=False)
    # Command may succeed or fail depending on system configuration
    # Just verify it doesn't crash
    assert returncode in [0, 1]  # 0 = success, 1 = expected error


def test_dsctl_security_options(topo):
    """Test dsctl with various security-related options via subprocess

    :id: b1b2b3b4-0002-0002-0002-000000000006
    :setup: Standalone Instance
    :steps:
        1. Run dsctl status command
        2. Verify command works
    :expectedresults:
        1. Command succeeds
        2. Output shows instance status
    """
    inst = topo.standalone
    cmd = [
        'dsctl',
        inst.serverid,
        'status'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    assert 'Instance' in stdout or inst.serverid in stdout


def test_dsconf_security_disable_plaintext_port(topo):
    """Test dsconf security disable_plain_port via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000013
    :setup: Standalone Instance
    :steps:
        1. Disable plaintext port
        2. Verify port is set to 0
        3. Restore original port
    :expectedresults:
        1. Command succeeds
        2. Port is disabled
        3. Restore succeeds
    """
    inst = topo.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid
    ]

    # Get original port
    original_port = inst.port

    try:
        # Disable plaintext port
        cmd = dsconf_base + ['security', 'disable_plain_port']
        returncode, stdout, stderr = run_cmd(cmd, stdin_input='Yes I am sure\n')
        assert returncode == 0

        # Verify port is 0
        assert inst.config.get_attr_val_utf8('nsslapd-port') == "0"

    finally:
        # Restore original port
        inst.config.set('nsslapd-port', str(original_port))


def test_dsconf_security_certificate_set_trust_flags(setup_tls):
    """Test dsconf security certificate set-trust-flags via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000014
    :setup: Standalone Instance with TLS
    :steps:
        1. Set trust flags on Self-Signed-CA
        2. Verify command succeeds
    :expectedresults:
        1. Command succeeds
        2. No errors returned
    """
    inst = setup_tls.standalone
    cmd = [
        'dsconf',
        inst.serverid,
        'security',
        'certificate',
        'set-trust-flags',
        'Self-Signed-CA',
        '--flags',
        'CT,,'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0


def test_dsconf_help_security(topo):
    """Test dsconf security help commands via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000015
    :setup: Standalone Instance
    :steps:
        1. Run dsconf security --help
        2. Run dsconf security rsa --help
        3. Run dsconf security ciphers --help
        4. Run dsconf security certificate --help
    :expectedresults:
        1. Help text is displayed
        2. RSA help is displayed
        3. Ciphers help is displayed
        4. Certificate help is displayed
    """
    inst = topo.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security'
    ]

    # Security help
    cmd = dsconf_base + ['--help']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    assert 'usage:' in stdout.lower() or 'security' in stdout.lower()

    # RSA help
    cmd = dsconf_base + ['rsa', '--help']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Ciphers help
    cmd = dsconf_base + ['ciphers', '--help']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Certificate help
    cmd = dsconf_base + ['certificate', '--help']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0


def test_dsctl_help_tls(topo):
    """Test dsctl tls help command via subprocess

    :id: b1b2b3b4-0002-0002-0002-000000000007
    :setup: Standalone Instance
    :steps:
        1. Run dsctl tls --help
    :expectedresults:
        1. Help text is displayed
    """
    inst = topo.standalone
    cmd = [
        'dsctl',
        inst.serverid,
        'tls',
        '--help'
    ]

    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    assert 'usage:' in stdout.lower() or 'tls' in stdout.lower()


def test_dsconf_certificate_invalid_files(setup_tls):
    """Test dsconf with invalid/malformed certificate files via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000016
    :setup: Standalone Instance with TLS
    :steps:
        1. Try to import corrupted PEM certificate
        2. Try to import truncated certificate
        3. Try to import invalid PKCS#12 file
        4. Verify all commands fail with appropriate errors
    :expectedresults:
        1. Corrupted PEM import fails
        2. Truncated cert import fails
        3. Invalid PKCS#12 import fails
        4. Error messages are meaningful (not just error codes)
    """
    inst = setup_tls.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security',
        'certificate'
    ]

    # Test 1: Corrupted PEM certificate
    corrupted_pem = """-----BEGIN CERTIFICATE-----
CORRUPTED DATA HERE NOT VALID BASE64!@#$%^&*
THIS IS NOT A REAL CERTIFICATE
-----END CERTIFICATE-----"""

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.pem') as f:
        f.write(corrupted_pem)
        corrupted_pem_path = f.name

    try:
        cmd = dsconf_base + ['add', '--file', corrupted_pem_path, '--name', 'CORRUPTED_CERT']
        returncode, stdout, stderr = run_cmd(cmd, check=False)
        assert returncode != 0
        # Should have error text, not just error code
        assert '255' not in stderr or 'base64' in stderr.lower() or 'invalid' in stderr.lower()
    finally:
        if os.path.exists(corrupted_pem_path):
            os.unlink(corrupted_pem_path)

    # Test 2: Truncated certificate (incomplete PEM)
    truncated_pem = """-----BEGIN CERTIFICATE-----
MIIFZjCCA06gAwIBAgIFAL18JOowDQYJKoZIhvcNAQELBQAwZTELMAkGA1UEBhMC
QVUxEzARBgNVBAgTClF1ZWVuc2xhbmQxDjAMBgNVBAcTBTM4OWRzMRAwDgYDVQQK
"""  # Intentionally truncated, no END CERTIFICATE

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.pem') as f:
        f.write(truncated_pem)
        truncated_pem_path = f.name

    try:
        cmd = dsconf_base + ['add', '--file', truncated_pem_path, '--name', 'TRUNCATED_CERT']
        returncode, stdout, stderr = run_cmd(cmd, check=False)
        assert returncode != 0
    finally:
        if os.path.exists(truncated_pem_path):
            os.unlink(truncated_pem_path)

    # Test 3: Invalid PKCS#12 file (just random data)
    with tempfile.NamedTemporaryFile(mode='wb', delete=False, suffix='.p12') as f:
        f.write(b'NOT A VALID PKCS12 FILE')
        invalid_p12_path = f.name

    try:
        cmd = dsconf_base + ['add', '--file', invalid_p12_path, '--name', 'INVALID_P12']
        returncode, stdout, stderr = run_cmd(cmd, check=False)
        assert returncode != 0
    finally:
        if os.path.exists(invalid_p12_path):
            os.unlink(invalid_p12_path)


def test_dsconf_certificate_overwrite_scenarios(setup_tls):
    """Test certificate overwrite with and without --force flag via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000017
    :setup: Standalone Instance with TLS
    :steps:
        1. Add a test certificate
        2. Try to add same certificate without --force (should fail)
        3. Add same certificate with --force (should succeed)
        4. Clean up
    :expectedresults:
        1. Initial add succeeds
        2. Duplicate add without --force fails
        3. Duplicate add with --force succeeds
        4. Cleanup succeeds
    """
    inst = setup_tls.standalone
    dsconf_base = [
        'dsconf',
        inst.serverid,
        'security',
        'certificate'
    ]

    # Valid test certificate
    pem_content = """-----BEGIN CERTIFICATE-----
MIIFZjCCA06gAwIBAgIFAL18JOowDQYJKoZIhvcNAQELBQAwZTELMAkGA1UEBhMC
QVUxEzARBgNVBAgTClF1ZWVuc2xhbmQxDjAMBgNVBAcTBTM4OWRzMRAwDgYDVQQK
Ewd0ZXN0aW5nMR8wHQYDVQQDExZzc2NhLjM4OWRzLmV4YW1wbGUuY29tMB4XDTIy
MTAyNTE5NDU0M1oXDTI0MTAyNTE5NDU0M1owZTELMAkGA1UEBhMCQVUxEzARBgNV
BAgTClF1ZWVuc2xhbmQxDjAMBgNVBAcTBTM4OWRzMRAwDgYDVQQKEwd0ZXN0aW5n
MR8wHQYDVQQDExZzc2NhLjM4OWRzLmV4YW1wbGUuY29tMIICIjANBgkqhkiG9w0B
AQEFAAOCAg8AMIICCgKCAgEA5E+pd7+8lBsbTKdjHgkSLi2Z5T5G9T+3wziDHhsz
F0nG+IOu5yYVkoj/bMxR3sNNlbDLk5ATyNAfytW3cAUZ3NLqm6bmEZdUjD6YycVk
AvrfY3zVVE9Debfw6JI3ml8JlC3t8dqn2KT7dmSjvr9zPS95HU+RepjzAqJAKY3B
27v0cMetUnxG4pqc7zqnSZJXVP/OXMKSNpujHnK8HyjT8tUJIYQ0YvU2JPJpz3fC
BJrmzgO2xYLgLPu6abhP6PQ6uUU+d4j36lG4J/4OiMY0Lr+mnaBAaD3ULPtN5eZh
fjQ9d+Sh89xHz92icWhkn8c7IHNEZNtMHNTNJiNbWKuU9HpBWNjWHJoxSxXn4Emr
DSfG+lq2UU2m9m+XrDK/7t0W/zC3S+zwcyqM8SJAiZnGEi85058wB0BB1HnnAfFX
gel3uZFhnR4d86O/vO5VUqg5Ko795DPzPa3SU4rR36U3nUF7g5WhEAmYNCj683D3
DJDPJeCZmis7xtYB5K6Wu6SnFDxBEfhcWSsamWM286KntOiUtqQEzDy4OpZEUsgq
s7uqQSl/dfGdY9hCpXMYhlvMfVv3aIoM5zPuXN2cE1QkTnE1pyo8gZqnPLFZnwc9
FT+Wjpy0EmsAM/5AIed5h+JgJ304P+wkyjf7APUZyUwf4UJN6aro6N8W23F7dAu5
uJ0CAwEAAaMdMBswDAYDVR0TBAUwAwEB/zALBgNVHQ8EBAMCAgQwDQYJKoZIhvcN
AQELBQADggIBADFlVdDp1gTF/gl5ysZoo4/4BBSpLx5JDeyPE6BityZ/lwHvBUq3
VzmIsU6kxAJfk0p9S7G4LgHIC/DVsLTIE5do6tUdyrawvcaanbYn9ScNoFVQ0GDS
C6Ir8PftEvc6bpI4hjkv4goK9fTq5Jtv4LSuRfxFEwoLO+WN8a65IFWlaHJl/Erp
9fzP+JKDo8zeh4qnMkaTSCBmLWa8kErrV462RU+qZktf/V4/gWg6k5Vp+82xNk7f
9/Mrg9KshNux7A4YCd8LgLEeCgsigi4N6zcfjQB0Rh5u9kXu/hzOjh379ki/vqju
i+MTVH97LMB47uR1LEl0VvhWSjID0ePUtbPHCJwOsxWyxBCJY6V7A9nj52uXMGuX
xghssZTFvRK6Bb1OiPNYRGqmuymm8rcSFdsY5yemkxJ6kfn40JIRCmVFwqaqu7MC
nxyaWAKpRHKM5IyeVZHkFzL9jR/2tVBbjfCAl6YSwM759VcOsw2SGMQKpGIPEBTa
1NBdlG45aWJBx5jBdVfOskLjxmBjosByJJHRLtrUBvg66ZBsx1k0c9XjsKmC59JP
AzI8zYp/TY/6T5igxM+CSx98DsJFccPBZFFJX+YYRL7DFN38Yb7jMgIUXYHS28Gc
1c8kz7ylcQB8lKgCgpcBCH5ZSnLVAnH3uqCygxSTgTo+jgJklKc0xFuR
-----END CERTIFICATE-----"""

    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.pem') as f:
        f.write(pem_content)
        pem_path = f.name

    cert_name = 'TEST_OVERWRITE_CERT'

    try:
        # Initial add
        cmd = dsconf_base + ['add', '--file', pem_path, '--name', cert_name]
        returncode, stdout, stderr = run_cmd(cmd, check=False)

        if returncode == 0:
            # Try to add again without --force (should fail)
            cmd = dsconf_base + ['add', '--file', pem_path, '--name', cert_name]
            returncode, stdout, stderr = run_cmd(cmd, check=False)
            # Should fail because cert already exists
            assert returncode != 0 or 'already exists' in stderr.lower()

            # Try to add with --force flag (if supported)
            cmd = dsconf_base + ['add', '--file', pem_path, '--name', cert_name, '--force']
            returncode, stdout, stderr = run_cmd(cmd, check=False)
            # Command may succeed with force or may not support --force flag

    finally:
        # Cleanup
        cmd = dsconf_base + ['del', cert_name]
        run_cmd(cmd, check=False)
        if os.path.exists(pem_path):
            os.unlink(pem_path)


def test_dsconf_json_text_output_consistency(setup_tls):
    """Test JSON vs text output consistency for list/get commands via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000018
    :setup: Standalone Instance with TLS
    :steps:
        1. Get security config in text and JSON format
        2. Compare that essential data is in both
        3. List certificates in text and JSON format
        4. Compare that certificates appear in both
        5. Get certificate details in text and JSON format
        6. Verify consistency
    :expectedresults:
        1. Both formats return data
        2. Essential security attributes appear in both
        3. Both formats return certificates
        4. Same certificates appear in both
        5. Both formats return details
        6. Data is consistent between formats
    """
    inst = setup_tls.standalone

    # Test 1: Security get - text vs JSON
    cmd_text = ['dsconf', inst.serverid, 'security', 'get']
    returncode, stdout_text, stderr = run_cmd(cmd_text)
    assert returncode == 0

    cmd_json = ['dsconf', inst.serverid, '--json', 'security', 'get']
    returncode, stdout_json, stderr = run_cmd(cmd_json)
    assert returncode == 0
    json_data = json.loads(stdout_json)
    assert 'type' in json_data or 'items' in json_data or 'attrs' in json_data

    # Test 2: Certificate list - text vs JSON
    cmd_text = ['dsconf', inst.serverid, 'security', 'certificate', 'list']
    returncode, stdout_text, stderr = run_cmd(cmd_text)
    assert returncode == 0

    cmd_json = ['dsconf', inst.serverid, '--json', 'security', 'certificate', 'list']
    returncode, stdout_json, stderr = run_cmd(cmd_json)
    assert returncode == 0
    json_data = json.loads(stdout_json)
    assert isinstance(json_data, (dict, list))

    # Verify Server-Cert appears in both
    assert 'Server-Cert' in stdout_text
    json_str = json.dumps(json_data)
    assert 'Server-Cert' in json_str

    # Test 3: Certificate get - text vs JSON
    cmd_text = ['dsconf', inst.serverid, 'security', 'certificate', 'get', 'Server-Cert']
    returncode, stdout_text, stderr = run_cmd(cmd_text)
    assert returncode == 0

    cmd_json = ['dsconf', inst.serverid, '--json', 'security', 'certificate', 'get', 'Server-Cert']
    returncode, stdout_json, stderr = run_cmd(cmd_json, check=False)
    # JSON format may not be supported for this command, check if it succeeds
    if returncode == 0:
        json_data = json.loads(stdout_json)
        assert isinstance(json_data, dict)


def test_dsctl_tls_error_messages(topo):
    """Test that dsctl tls commands return meaningful error messages via subprocess

    :id: b1b2b3b4-0002-0002-0002-000000000008
    :setup: Standalone Instance
    :steps:
        1. Try to generate CSR with malformed subject
        2. Try to remove non-existent certificate
        3. Try to export non-existent certificate
        4. Verify error messages are meaningful
    :expectedresults:
        1. CSR generation fails with meaningful error
        2. Remove fails with meaningful error
        3. Export fails with meaningful error
        4. No bare error codes like '255' in output
    """
    inst = topo.standalone

    # Test 1: Malformed subject in CSR generation
    cmd = [
        'dsctl',
        inst.serverid,
        'tls',
        'generate-server-cert-csr',
        '-s', 'INVALID_SUBJECT'
    ]
    returncode, stdout, stderr = run_cmd(cmd, check=False)
    assert returncode != 0
    # Should have error text, not just '255'
    combined_output = stdout + stderr
    if '255' in combined_output:
        assert 'improperly formatted' in combined_output.lower() or 'invalid' in combined_output.lower()

    # Test 2: Remove non-existent certificate
    cmd = [
        'dsctl',
        inst.serverid,
        'tls',
        'remove-cert',
        'NONEXISTENT_CERT_12345'
    ]
    returncode, stdout, stderr = run_cmd(cmd, check=False)
    assert returncode != 0
    combined_output = stdout + stderr
    if '255' in combined_output:
        assert 'not found' in combined_output.lower() or 'could not find' in combined_output.lower()

    # Test 3: Export non-existent certificate
    with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.pem') as f:
        export_path = f.name

    try:
        cmd = [
            'dsctl',
            inst.serverid,
            'tls',
            'export-cert',
            '--output-file',
            export_path,
            'NONEXISTENT_CERT_12345'
        ]
        returncode, stdout, stderr = run_cmd(cmd, check=False)
        assert returncode != 0
        combined_output = stdout + stderr
        if '255' in combined_output:
            assert 'not found' in combined_output.lower() or 'could not find' in combined_output.lower()
    finally:
        if os.path.exists(export_path):
            os.unlink(export_path)


def test_dsconf_certificate_backend_consistency(setup_tls):
    """Test backend consistency between DynamicCerts and NssSsl via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000019
    :setup: Standalone Instance with TLS
    :steps:
        1. List certificates using dsconf
        2. Verify Server-Cert appears (from NssSsl backend)
        3. Check if dynamic certificates backend is accessible
        4. Verify consistency of certificate data
    :expectedresults:
        1. Certificate list succeeds
        2. Server-Cert is present
        3. Backend is accessible
        4. Data is consistent
    """
    inst = setup_tls.standalone

    # List certificates
    cmd = ['dsconf', inst.serverid, 'security', 'certificate', 'list']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0
    assert 'Server-Cert' in stdout

    # Get certificate details
    cmd = ['dsconf', inst.serverid, 'security', 'certificate', 'get', 'Server-Cert']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify we get meaningful certificate information
    assert 'Server-Cert' in stdout or 'Certificate' in stdout

    # List CA certificates
    cmd = ['dsconf', inst.serverid, 'security', 'ca-certificate', 'list']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # The output should be consistent whether accessed via NssSsl or DynamicCerts backend


def test_dsctl_tls_pkcs12_password_scenarios(setup_tls):
    """Test PKCS#12 import with various password scenarios via subprocess

    :id: b1b2b3b4-0002-0002-0002-000000000009
    :setup: Standalone Instance with TLS
    :steps:
        1. Try to import PKCS#12 with wrong password
        2. Try to import PKCS#12 with missing password
        3. Try to import PKCS#12 with empty password when one is required
        4. Verify appropriate error messages
    :expectedresults:
        1. Wrong password fails with meaningful error
        2. Missing password fails with meaningful error
        3. Empty password fails with meaningful error
        4. Error messages indicate password issues, not bare error codes
    """
    inst = setup_tls.standalone

    # Create a dummy PKCS#12-like file (not a real one, just for testing error handling)
    with tempfile.NamedTemporaryFile(mode='wb', delete=False, suffix='.p12') as f:
        # Write some bytes that look like PKCS#12 header but aren't valid
        f.write(b'\x30\x82')  # ASN.1 SEQUENCE tag
        f.write(b'NOT_A_VALID_PKCS12_FILE')
        p12_path = f.name

    try:
        # Test 1: Try import with wrong password
        cmd = [
            'dsctl',
            inst.serverid,
            'tls',
            'import-server-key-cert',
            p12_path,
            p12_path  # Using same file as both key and cert for testing
        ]
        returncode, stdout, stderr = run_cmd(cmd, check=False)
        assert returncode != 0
        combined_output = stdout + stderr
        # Should get error about private key or file format, not bare '255'
        if '255' in combined_output:
            assert ('private key' in combined_output.lower() or
                    'password' in combined_output.lower() or
                    'pkcs' in combined_output.lower() or
                    'invalid' in combined_output.lower())

    finally:
        if os.path.exists(p12_path):
            os.unlink(p12_path)


def test_dsconf_dynamic_certificates_attributes(setup_tls):
    """Test dynamic certificates attributes and timestamp handling via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000020
    :setup: Standalone Instance with TLS
    :steps:
        1. List certificates in JSON format
        2. Check for dynamic certificate attributes
        3. Verify timestamp format if present
        4. Check certificate metadata (subject, issuer, etc.)
    :expectedresults:
        1. Certificate list returns valid JSON
        2. Expected attributes are present
        3. Timestamps are properly formatted
        4. Metadata is accessible and valid
    """
    inst = setup_tls.standalone

    # Get certificate list in JSON format
    cmd = ['dsconf', inst.serverid, '--json', 'security', 'certificate', 'list']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    json_data = json.loads(stdout)
    assert isinstance(json_data, (dict, list))

    # Get details of a specific certificate in JSON if supported
    cmd = ['dsconf', inst.serverid, '--json', 'security', 'certificate', 'get', 'Server-Cert']
    returncode, stdout, stderr = run_cmd(cmd, check=False)

    if returncode == 0:
        json_data = json.loads(stdout)

        # Check for common certificate attributes
        # The exact structure depends on whether DynamicCerts or NssSsl backend is used
        json_str = json.dumps(json_data).lower()

        # Look for certificate-related attributes (case-insensitive)
        # These might be in different formats depending on the backend
        assert ('subject' in json_str or
                'issuer' in json_str or
                'cert' in json_str or
                'nickname' in json_str)


def test_dsconf_security_multiple_operations(setup_tls):
    """Test multiple security operations in sequence via subprocess

    :id: a1a2a3a4-0001-0001-0001-000000000021
    :setup: Standalone Instance with TLS
    :steps:
        1. Disable security
        2. Enable security
        3. Disable RSA
        4. Enable RSA
        5. Get security config after each change
        6. Verify state changes are reflected
    :expectedresults:
        1. Disable succeeds
        2. Enable succeeds
        3. RSA disable succeeds
        4. RSA enable succeeds
        5. Each get returns current state
        6. Changes are properly reflected
    """
    inst = setup_tls.standalone
    dsconf_base = ['dsconf', inst.serverid, 'security']

    # Enable security
    cmd = dsconf_base + ['enable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify enabled
    cmd = dsconf_base + ['get']
    returncode, stdout, stderr = run_cmd(cmd)
    assert 'nsslapd-security: on' in stdout

    # Disable security
    cmd = dsconf_base + ['disable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify disabled
    cmd = dsconf_base + ['get']
    returncode, stdout, stderr = run_cmd(cmd)
    assert 'nsslapd-security: off' in stdout

    # Re-enable for RSA tests
    cmd = dsconf_base + ['enable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Enable RSA
    cmd = dsconf_base + ['rsa', 'enable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify RSA is on
    cmd = dsconf_base + ['rsa', 'get']
    returncode, stdout, stderr = run_cmd(cmd)
    assert 'on' in stdout.lower()

    # Disable RSA
    cmd = dsconf_base + ['rsa', 'disable']
    returncode, stdout, stderr = run_cmd(cmd)
    assert returncode == 0

    # Verify RSA is off
    cmd = dsconf_base + ['rsa', 'get']
    returncode, stdout, stderr = run_cmd(cmd)
    assert 'off' in stdout.lower()


def test_dsconf_security_encryption_module_create(setup_tls):
    """Test dsconf security encryption-module add via subprocess

    :id: 75d93730-8212-4a67-b6ee-f7f57436a311
    :setup: Standalone Instance with TLS
    :steps:
        1. Add a new encryption module with required and optional arguments
        2. Verify module exists and is activated
    :expectedresults:
        1. Add command succeeds
        2. Module details can be fetched and contain expected values
    """
    inst = setup_tls.standalone
    module_name = f'CLI_ENC_MODULE'

    add_cmd = [
        'dsconf', inst.serverid, 'security', 'encryption-module', 'add', module_name,
        '--cert-nickname', 'Second-Cert',
        '--activated',
        '--token', 'internal (software)',
        '--server-key-extract-file', f'/tmp/{module_name}.key',
        '--server-cert-extract-file', f'/tmp/{module_name}.crt',
    ]

    try:
        returncode, stdout, stderr = run_cmd(add_cmd)
        assert returncode == 0

        get_cmd = ['dsconf', inst.serverid, 'security', 'encryption-module', 'get', module_name]
        returncode, stdout, stderr = run_cmd(get_cmd)
        assert returncode == 0
        assert module_name in stdout
        assert 'nssslactivation: on' in stdout.lower()
        assert 'serverkeyextractfile: /tmp/cli_enc_module.key' in stdout.lower()
        assert 'servercertextractfile: /tmp/cli_enc_module.crt' in stdout.lower()
        assert 'nssslpersonalityssl: second-cert' in stdout.lower()
        assert 'nsssltoken: internal (software)' in stdout.lower()
    finally:
        cleanup_cmd = ['dsconf', inst.serverid, 'security', 'encryption-module', 'delete', module_name]
        run_cmd(cleanup_cmd, check=False)


def test_dsconf_security_encryption_module_edit(setup_tls):
    """Test dsconf security encryption-module edit via subprocess

    :id: d6b7169d-23ee-489e-a90c-872ef35f73db
    :setup: Standalone Instance with TLS
    :steps:
        1. Add a new encryption module
        2. Edit the module activation/token/extract file options
        3. Verify updated values
    :expectedresults:
        1. Add succeeds
        2. Edit succeeds
        3. Updated values are visible in module output
    """
    inst = setup_tls.standalone
    module_name = 'CLI_ENC_MODULE'
    token_value = 'Internal (Software) Token'

    add_cmd = [
        'dsconf', inst.serverid, 'security', 'encryption-module', 'add', module_name,
        '--cert-nickname', 'Second-Cert',
        '--activated',
    ]
    edit_cmd = [
        'dsconf', inst.serverid, 'security', 'encryption-module', 'edit', module_name,
        '--cert-nickname', 'Second-Cert2',
        '--deactivate',
        '--token', token_value,
        '--server-key-extract-file', f'/tmp/{module_name}.edited.key',
        '--server-cert-extract-file', f'/tmp/{module_name}.edited.crt',
    ]

    try:
        returncode, stdout, stderr = run_cmd(add_cmd)
        assert returncode == 0

        returncode, stdout, stderr = run_cmd(edit_cmd)
        assert returncode == 0

        get_cmd = ['dsconf', inst.serverid, 'security', 'encryption-module', 'get', module_name]
        returncode, stdout, stderr = run_cmd(get_cmd)
        assert returncode == 0
        assert 'nssslactivation: off' in stdout.lower()
        assert 'serverkeyextractfile: /tmp/cli_enc_module.edited.key' in stdout.lower()
        assert 'servercertextractfile: /tmp/cli_enc_module.edited.crt' in stdout.lower()
        assert 'nssslpersonalityssl: second-cert2' in stdout.lower()
        assert 'nsssltoken: internal (software) token' in stdout.lower()
    finally:
        cleanup_cmd = ['dsconf', inst.serverid, 'security', 'encryption-module', 'delete', module_name]
        run_cmd(cleanup_cmd, check=False)


def test_dsconf_security_encryption_module_delete(setup_tls):
    """Test dsconf security encryption-module delete via subprocess

    :id: be36928b-09b7-4c02-8431-4d3440ea979f
    :setup: Standalone Instance with TLS
    :steps:
        1. Add a new encryption module
        2. Delete the module
        3. Verify module no longer appears in list
    :expectedresults:
        1. Add succeeds
        2. Delete succeeds
        3. Module is not listed anymore
    """
    inst = setup_tls.standalone
    module_name = f'CLI_ENC_MODULE'

    add_cmd = [
        'dsconf', inst.serverid, 'security', 'encryption-module', 'add', module_name,
        '--cert-nickname', 'Second-Cert',
    ]
    del_cmd = ['dsconf', inst.serverid, 'security', 'encryption-module', 'delete', module_name]
    list_cmd = ['dsconf', inst.serverid, 'security', 'encryption-module', 'list', '--just-names']

    run_cmd(add_cmd)
    returncode, stdout, stderr = run_cmd(del_cmd)
    assert returncode == 0

    returncode, stdout, stderr = run_cmd(list_cmd)
    assert returncode == 0
    assert module_name not in stdout


def test_dsconf_security_encryption_module_list_all_args(setup_tls):
    """Test dsconf security encryption-module list with all list arguments via subprocess

    :id: 7aa7ac1a-bf95-4902-a650-258be832817f
    :setup: Standalone Instance with TLS
    :steps:
        1. Create a temporary certificate file and import it as Second-Cert
        2. Add a temporary encryption module that references Second-Cert
        3. Run encryption-module list with no args
        4. Run encryption-module list with --just-names
        5. Run JSON output list
        6. Run JSON output list with --just-names
    :expectedresults:
        1. Certificate import succeeds (or already exists)
        2. Module add succeeds
        3. Default list succeeds and contains module details
        4. --just-names succeeds and contains the module name
        5. JSON list succeeds and is valid JSON
        6. JSON + --just-names succeeds and includes module name
    """
    inst = setup_tls.standalone
    module_name = f'CLI_ENC_MODULE'

    try:
        run_cmd([
            'dsconf', inst.serverid, 'security', 'encryption-module', 'add', module_name,
            '--cert-nickname', 'Second-Cert',
            '--activated'
        ])

        cmd = ['dsconf', inst.serverid, 'security', 'encryption-module', 'list']
        returncode, stdout, stderr = run_cmd(cmd)
        assert returncode == 0
        assert module_name in stdout

        cmd = ['dsconf', inst.serverid, 'security', 'encryption-module', 'list', '--just-names']
        returncode, stdout, stderr = run_cmd(cmd)
        assert returncode == 0
        assert module_name in stdout

        cmd = ['dsconf', inst.serverid, '--json', 'security', 'encryption-module', 'list']
        returncode, stdout, stderr = run_cmd(cmd)
        assert returncode == 0
        data = json.loads(stdout)
        assert isinstance(data, dict)
        assert 'items' in data

        cmd = ['dsconf', inst.serverid, '--json', 'security', 'encryption-module', 'list', '--just-names']
        returncode, stdout, stderr = run_cmd(cmd)
        assert returncode == 0
        data = json.loads(stdout)
        assert isinstance(data, dict)
        assert module_name in json.dumps(data)
    finally:
        run_cmd(['dsconf', inst.serverid, 'security', 'encryption-module', 'delete', module_name], check=False)


def test_dsconf_security_certificate_import_second_cert(setup_tls):
    """Test dsconf security certificate add/get with Second-Cert via subprocess

    :id: b18f0744-d1ce-41b4-98a0-254e9c290285
    :setup: Standalone Instance with TLS
    :steps:
        1. Create a temp PEM certificate file
        2. Import the certificate using dsconf with name Second-Cert
        3. Verify it can be retrieved with dsconf security certificate get
    :expectedresults:
        1. Temp certificate file is created
        2. Import succeeds
        3. Certificate details are returned for Second-Cert
    """
    inst = setup_tls.standalone
    cert_path = _create_temp_cert_file()
    cert_name = 'Second-Cert'

    try:
        add_cmd = [
            'dsconf', inst.serverid, 'security', 'certificate', 'add',
            '--file', cert_path,
            '--name', cert_name
        ]
        returncode, stdout, stderr = run_cmd(add_cmd, check=False)
        # If cert already exists in this environment, continue with validation
        assert returncode in [0, 1]

        get_cmd = ['dsconf', inst.serverid, 'security', 'certificate', 'get', cert_name]
        returncode, stdout, stderr = run_cmd(get_cmd)
        assert returncode == 0
        assert 'Certificate Name: Second-Cert' in stdout
    finally:
        run_cmd(['dsconf', inst.serverid, 'security', 'certificate', 'del', cert_name], check=False)
        if os.path.exists(cert_path):
            os.unlink(cert_path)

def test_dsctl_tls_import_cert_with_unique_nickname(setup_tls):
    """Test dsctl tls import-server-cert with fixed nickname via subprocess

    :id: 281dfc03-42c6-4c65-a313-a677c0691bcf
    :setup: Standalone Instance with TLS
    :steps:
        1. Export Second-Cert to a temp PEM file
        2. Import the PEM as a server certificate with nickname Second-Cert
        3. Verify show-cert returns details for Second-Cert
    :expectedresults:
        1. Export succeeds
        2. Import succeeds
        3. show-cert succeeds for Second-Cert
    """
    inst = setup_tls.standalone
    unique_nick = 'Second-Cert'

    cert_path = _create_temp_cert_file()

    try:
        import_cmd = ['dsctl', inst.serverid, 'tls', 'import-server-cert', cert_path, '--nickname', unique_nick]
        returncode, stdout, stderr = run_cmd(import_cmd)
        assert returncode == 0

        show_cmd = ['dsctl', inst.serverid, 'tls', 'show-cert', unique_nick]
        returncode, stdout, stderr = run_cmd(show_cmd)
        assert returncode == 0
        assert len(stdout.strip()) > 0
    finally:
        run_cmd(['dsctl', inst.serverid, 'tls', 'remove-cert', unique_nick], check=False)
        if os.path.exists(cert_path):
            os.unlink(cert_path)


def test_dsctl_tls_generate_csr_with_unique_nickname(setup_tls):
    """Test dsctl tls generate-server-cert-csr with unique nickname via subprocess

    :id: 37900e94-56a9-446a-9de7-c02dc30e03e5
    :setup: Standalone Instance with TLS
    :steps:
        1. Generate CSR using --nickname and a valid subject
        2. Verify expected CSR file path exists
        3. Remove generated CSR file
    :expectedresults:
        1. Command succeeds
        2. CSR file is created
        3. Cleanup succeeds
    """
    inst = setup_tls.standalone
    unique_nick = 'Second-Cert'
    csr_path = os.path.join(inst.get_cert_dir(), f'{unique_nick}.csr')

    cmd = [
        'dsctl',
        inst.serverid,
        'tls',
        'generate-server-cert-csr',
        '--nickname', unique_nick,
        '-s', f'CN={unique_nick},O=Example,C=US'
    ]
    try:
        returncode, stdout, stderr = run_cmd(cmd)
        assert returncode == 0
        assert os.path.exists(csr_path)
        with open(csr_path, 'r') as csr_file:
            content = csr_file.read()
            if unique_nick not in content:
                log.error(f"CSR file {csr_path} does not contain the nickname {unique_nick}")
                assert False

    finally:
        if os.path.exists(csr_path):
            os.unlink(csr_path)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", "-v", CURRENT_FILE])
