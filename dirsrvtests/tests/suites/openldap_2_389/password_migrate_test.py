# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
from lib389.topologies import topology_st
from lib389.utils import ds_is_older
from lib389.idm.user import nsUserAccounts
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

@pytest.mark.skipif(ds_is_older('1.4.3'), reason="Not implemented")
def test_migrate_openldap_password_hash(topology_st):
    """Test import of an openldap password value into the directory and assert
    it can bind.

    :id: e4898e0d-5d18-4765-9249-84bcbf862fde
    :setup: Standalone Instance
    :steps:
        1. Import a hash
        2. Attempt a bind
        3. Goto 1

    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    inst = topology_st.standalone
    inst.config.set('nsslapd-allow-hashed-passwords', 'on')

    # You generate these with:
    # slappasswd -s password -o module-load=/usr/lib64/openldap/pw-argon2.so -h {ARGON2}
    pwds = [
        '{CRYPT}ZZKRwXSu3tt8s',
        '{SSHA}jdALDtX0+MVMkRsX0ilHz0O6Uos95D4s',
        '{MD5}X03MO1qnZdYdgyfeuILPmQ==',
        '{SMD5}RnexgcsjdBHMQ1yhB7+sD+a+qDI=',
        '{SHA}W6ph5Mm5Pz8GgiULbPgzG37mj9g=',
        '{SHA256}XohImNooBHFR0OVvjcYpJ3NgPQ1qq73WKhHvch0VQtg=',
        '{SSHA256}covFryM35UrKB3gMYxtYpQYTHbTn5kFphjcNHewfj581SLJwjA9jew==',
        '{SHA384}qLZLq9CsqRpZvbt3YbQh1PK7OCgNOnW6DyHyvrxFWD1EbFmGYMlM5oDEfRnDB4On',
        '{SSHA384}kNjTWdmyy2G1IgJF8WrOpq0N//Yc2Ec5TIQYceuiuHQXRXpC1bfnMqyOx0NxrSREjBWDwUpqXjo=',
        '{SHA512}sQnzu7wkTrgkQZF+0G1hi5AI3Qmzvv0bXgc5THBqi7mAsdd4Xll27ASbRt9fEyavWi6m0QP9B8lThf+rDKy8hg==',
        '{SSHA512}+7A8kA32q4mCBao4Cbatdyzl5imVwJ62ZAE7UOTP4pfrF90E9R2LabOfJFzx6guaYhTmUEVK2wRKC8bToqspdeTluX2d1BX2',
        # Need to check --
        '{PBKDF2}10000$IlfapjA351LuDSwYC0IQ8Q$saHqQTuYnjJN/tmAndT.8mJt.6w',
        '{PBKDF2-SHA1}10000$ZBEH6B07rgQpJSikyvMU2w$TAA03a5IYkz1QlPsbJKvUsTqNV',
        '{PBKDF2-SHA256}10000$henZGfPWw79Cs8ORDeVNrQ$1dTJy73v6n3bnTmTZFghxHXHLsAzKaAy8SksDfZBPIw',
        '{PBKDF2-SHA512}10000$Je1Uw19Bfv5lArzZ6V3EPw$g4T/1sqBUYWl9o93MVnyQ/8zKGSkPbKaXXsT8WmysXQJhWy8MRP2JFudSL.N9RklQYgDPxPjnfum/F2f/TrppA',
        # '{ARGON2}$argon2id$v=19$m=65536,t=2,p=1$IyTQMsvzB2JHDiWx8fq7Ew$VhYOA7AL0kbRXI5g2kOyyp8St1epkNj7WZyUY4pAIQQ',
    ]

    accounts = nsUserAccounts(inst, basedn=DEFAULT_SUFFIX)
    account = accounts.create(properties={
        'uid': 'pw_migrate_test_user',
        'cn': 'pw_migrate_test_user',
        'displayName': 'pw_migrate_test_user',
        'uidNumber': '12345',
        'gidNumber': '12345',
        'homeDirectory': '/var/empty',
    })

    for pwhash in pwds:
        inst.log.debug(f"Attempting -> {pwhash}")
        account.set('userPassword', pwhash)
        nconn = account.bind('password')
