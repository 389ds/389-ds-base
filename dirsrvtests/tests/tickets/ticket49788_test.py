# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Dj Padzensky <djpadz@padz.net>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import ldap
import base64
import pytest
import os

from lib389 import Entry
from lib389.tasks import *
from lib389.utils import *
from lib389.properties import *
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, DN_CONFIG, DN_DM, PASSWORD, DEFAULT_SUFFIX_ESCAPED

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

VALID_STRINGS = [
	'dHJpdmlhbCBzdHJpbmc='                      # trivial string
	'8J+YjQ==',                                 # 😍
	'aGVsbG8g8J+YjQ==',                         # hello 😍
	'8J+krCBTbyB0aGVyZSEg8J+YoQ==',             # 🤬 So there! 😡
	'YnJvY2NvbGkgYmVlZg==',                     # broccoli beef
	'Y2FybmUgZGUgYnLDs2NvbGk=',                 # carne de brócoli
	'2YTYrdmFINio2YLYsdmKINio2LHZiNmD2YTZig==', # لحم بقري بروكلي
        '6KW/5YWw6Iqx54mb6IKJ',                     # 西兰花牛肉
        '6KW/6Jit6Iqx54mb6IKJ',                     # 西蘭花牛肉
	'0LPQvtCy0LXQtNGB0LrQviDQvNC10YHQviDQvtC0INCx0YDQvtC60YPQu9Cw', # говедско месо од брокула
]

INVALID_STRINGS = [
	'0LPQxtCy0LXQtNGB0LrQviDQvNC10YHQviDQvtC0INCx0YDQvtC60YPQu9Cw',
	'8R+KjQ==',
]

USER_DN = 'cn=test_user,' + DEFAULT_SUFFIX

def test_ticket49781(topology_st):
    """
        Test that four-byte UTF-8 characters are accepted by the
        directory string syntax.
    """

    # Add a test user
    try:
        topology_st.standalone.add_s(Entry((USER_DN,
                                            {'objectclass': ['top', 'person'],
                                             'sn': 'sn',
                                             'description': 'Four-byte UTF8 test',
                                             'cn': 'test_user'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add test user')
        assert False

    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'description', b'something else')])
    except ldap.LDAPError as e:
        log.fatal('trivial test failed!')
        assert False

    # Iterate over valid tests
    for s in VALID_STRINGS:
        decoded = base64.b64decode(s)
        try:
            topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'description', decoded)])
        except ldap.LDAPError as e:
            log.fatal('description: ' + decoded.decode('UTF-8') + ' failed')
            assert False

    # Iterate over invalid tests
    for s in INVALID_STRINGS:
        decoded = base64.b64decode(s)
        try:
            topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'description', decoded)])
            log.fatal('base64-decoded string ' + s + " was accepted, when it shouldn't have been!")
            assert False
        except ldap.LDAPError as e:
            pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
