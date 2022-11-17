# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m3 as T
import socket

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_ticket49020(T):
    A = T.ms['supplier1']
    B = T.ms['supplier2']
    C = T.ms['supplier3']

    A.enableReplLogging()
    B.enableReplLogging()
    C.enableReplLogging()

    AtoB = A.agreement.list(suffix=DEFAULT_SUFFIX)[0].dn
    AtoC = A.agreement.list(suffix=DEFAULT_SUFFIX)[1].dn
    CtoB = C.agreement.list(suffix=DEFAULT_SUFFIX)[1].dn

    A.agreement.pause(AtoB)
    C.agreement.pause(CtoB)
    time.sleep(5)
    name = "userX"
    dn = "cn={},{}".format(name, DEFAULT_SUFFIX)
    A.add_s(Entry((dn, {'objectclass': "top person".split(),
                        'sn': name,'cn': name})))

    A.agreement.init(DEFAULT_SUFFIX, socket.gethostname(), PORT_SUPPLIER_3)
    time.sleep(5)
    for i in range(1,11):
        name = "userY{}".format(i)
        dn = "cn={},{}".format(name, DEFAULT_SUFFIX)
        A.add_s(Entry((dn, {'objectclass': "top person".split(),
                            'sn': name,'cn': name})))
    time.sleep(5)
    C.agreement.resume(CtoB)

    time.sleep(5)
    A_entries = A.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                         '(objectClass=person)')
    B_entries = B.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                         '(objectClass=person)')
    C_entries = C.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                         '(objectClass=person)')

    assert len(A_entries) == len(C_entries)
    assert len(B_entries) == len(A_entries) - 11


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
