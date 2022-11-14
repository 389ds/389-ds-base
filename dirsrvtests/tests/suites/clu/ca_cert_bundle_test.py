# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import os
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.cli_base import FakeArgs
from lib389.cli_conf.security import cacert_add, cacert_list, cert_del
from lib389.cli_ctl.tls import import_ca
from lib389.cli_base import LogCapture

log = logging.getLogger(__name__)

PEM_CONTEXT = """
-----BEGIN CERTIFICATE-----
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
AzI8zYp/TY/6T5igxM+CSx98DsJFccPBZFFJX+ZYRL7DFN38Yb7jMgIUXYHS28Gc
1c8kz7ylcQB8lKgCgpcBCH5ZSnLVAnH3uqCygxSTgTo+jgJklKc0xFuR
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFZjCCA06gAwIBAgIFAL2uT7gwDQYJKoZIhvcNAQELBQAwZTELMAkGA1UEBhMC
QVUxEzARBgNVBAgTClF1ZWVuc2xhbmQxDjAMBgNVBAcTBTM4OWRzMRAwDgYDVQQK
Ewd0ZXN0aW5nMR8wHQYDVQQDExZzc2NhLjM4OWRzLmV4YW1wbGUuY29tMB4XDTIy
MTExNDE4MzQzNloXDTI0MTExNDE4MzQzNlowZTELMAkGA1UEBhMCQVUxEzARBgNV
BAgTClF1ZWVuc2xhbmQxDjAMBgNVBAcTBTM4OWRzMRAwDgYDVQQKEwd0ZXN0aW5n
MR8wHQYDVQQDExZzc2NhLjM4OWRzLmV4YW1wbGUuY29tMIICIjANBgkqhkiG9w0B
AQEFAAOCAg8AMIICCgKCAgEA5NwbBn/W/KcZDzfw0fs/JI0+1aWWTu7PfSJxXySt
Z/CagcdKtmSRqWasI4QkdQN8ydiDuJJoWWcrO2UOuJw0m5uRbZTDn29Khr7x8SbT
L8luDi+2cZ0ewrlBdae3C1lx9fRpKxITv40D1KLGPsyy3a5+aiI/vbZqG2JjxYzn
d6DQju5mpch+ATNPp43vLRtET5Zq/QcOELBhVuBqcOf+UmwK/fCQ3GjluyIK71AD
eezafnYyZtCoaVlkyFdSBDOg8/OcwnjeWSQSoV61nwKbmLjVJf1OKgUVXViQZkKD
2vUTaG7CcmNaP/IFHSjFKWDngmCCbPH554B6vDMt7IpVUd13f8+Zl7zzUAqpAsTd
02D+GqrIYPIPI/ONComkoHwaWodWrI3/CXAMMnjelQSmt/uivGkANxWm9bG1YHAW
cqGRYm8Zb5vkUotcHQc17h5SkRoThlVynXD3oVgadHgm+LgPRiB3uCnVrCpfaa56
5UnRYZu/mB4jWVQlQ1ASA7HS0mZTPxfUvHXBdyYlpGUtvGbklW5RKjTSlM0vUYJl
kj0p2DaN9cjll+Oa4keqgIfa6Boi9rkuMGFE48rk/u6FadGgGhp2Bb19hUl6OQjD
qnWBDrX8dvfQxiz8MbETwd9TKJVHxOCdQzpmS0GgZbDO5wMMaARdntTRg2MUH7e2
Z+UCAwEAAaMdMBswDAYDVR0TBAUwAwEB/zALBgNVHQ8EBAMCAgQwDQYJKoZIhvcN
AQELBQADggIBAGauYyCzqKiJKVkqXSvUeOxQpFb21sv3dWrUwq1YsyLx8sxJSXwe
Na1YjdymUueh75rLOChZVyYBQQeW5OKpmigUH3j70tQkg6DnZXGBZ532hxTwfm4F
5uHXIfwd4wFbuu8ZDa8DFZVqQpWBAyyQsdmGG2OZaQBp4MH3kk4zLUFG9xgp1qke
KSYsOTmtuPR0Aw7vWJUClZoSC5WG+b3d19oEXVR+3vPPLkL5UJmcJgiS2BPpsbyt
fX+yD6QmRj2XLi5/T8h7ZeSRPzSsccAu/hIpzpyQxa8lSJ4+I0DjQSz3N7xW/0FS
b9yKzMgaz1ctEhtNj3paT14C4/uKUdlQLb3UzKmne1iPuJPtBOhBh6ofPPflbI/v
ceNZPSG68XiR7qvgMRYyotop106EwHDUr2YzSpoD+CmjQ4n/+6/zfyrsuSq10XjF
0U3dtQu5ETFakOwB2Mj+T1b5Q01km3FN+p7n0lLhB+F9n3WQ5uQHPXntAsh9w9HH
hTOSIFeJCMPuWd4OoGBsiT7koFQtW5I+e19sxkKuOxoO5fgpCf1IgMAuKYppGJNb
Bbc+LAFMLKxlOy8WzEFjewV2fSBtCrSlyu+aMXBYtXLeW6iqvgYQpmDKOvxLM0j2
jBqLRMRQN4FvzuCZiMl/DwJv4yhAZ8hylYjRjqjY/fEPvhvJRncPVy8z
-----END CERTIFICATE-----
"""


def test_ca_cert_bundle(topo):
    """Test we can add a CAS certificate bundle

    :id: b39c98f5-374f-4b40-abee-4dd0a0c41641
    :setup: Standalone Instance
    :steps:
        1. Create PEM file
        2. Add PEM file with two CA certs
        3. List the new CA certs
        4. Remove CA certs
        5. Add CA certs using dsctl
        6. List the new CA certs
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success

    """
    inst = topo.standalone
    lc = LogCapture()

    # Create PEM file with 2 CA certs
    pem_file = "/tmp/ca-bundle.pem"
    log.info('Write pem file')
    with open(pem_file, 'w') as f:
        for line in PEM_CONTEXT:
            f.write(line)

    # Add PEM file
    args = FakeArgs()
    args.name = ['CA_CERT_1', 'CA_CERT_2']
    args.file = pem_file
    cacert_add(inst, DEFAULT_SUFFIX, log, args)

    # List CA certs
    args = FakeArgs()
    args.json = False
    cacert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert lc.contains('CA_CERT_1')
    assert lc.contains('CA_CERT_2')

    # Test dsctl now, first remove the certs
    args = FakeArgs()
    args.name = 'CA_CERT_1'
    cert_del(inst, DEFAULT_SUFFIX, log, args)
    args.name = 'CA_CERT_2'
    cert_del(inst, DEFAULT_SUFFIX, log, args)

    # List CA certs
    lc.flush()
    args = FakeArgs()
    args.json = False
    cacert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert not lc.contains('CA_CERT_1')
    assert not lc.contains('CA_CERT_2')

    # Add certs using dsctl
    args = FakeArgs()
    args.nickname = ['CA_CERT_1', 'CA_CERT_2']
    args.cert_path = pem_file
    import_ca(inst, log, args)

    # List CA certs
    lc.flush()
    args = FakeArgs()
    args.json = False
    cacert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert lc.contains('CA_CERT_1')
    assert lc.contains('CA_CERT_2')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

