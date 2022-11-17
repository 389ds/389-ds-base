# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m3 as T

from lib389._constants import DEFAULT_SUFFIX, PLUGIN_MEMBER_OF

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_ticket49008(T):
    A = T.ms['supplier1']
    B = T.ms['supplier2']
    C = T.ms['supplier3']

    A.enableReplLogging()
    B.enableReplLogging()
    C.enableReplLogging()

    AtoB = A.agreement.list(suffix=DEFAULT_SUFFIX)[0].dn
    AtoC = A.agreement.list(suffix=DEFAULT_SUFFIX)[1].dn
    CtoA = C.agreement.list(suffix=DEFAULT_SUFFIX)[0].dn
    CtoB = C.agreement.list(suffix=DEFAULT_SUFFIX)[1].dn

    # we want replication in a line A <==> B <==> C
    A.agreement.pause(AtoC)
    C.agreement.pause(CtoA)

    # Enable memberOf on Supplier B
    B.plugins.enable(name=PLUGIN_MEMBER_OF)

    # Set the auto OC to an objectclass that does NOT allow memberOf
    B.modify_s('cn=MemberOf Plugin,cn=plugins,cn=config',
               [(ldap.MOD_REPLACE, 'memberofAutoAddOC', b'referral')])
    B.restart(timeout=10)

    # add a few entries allowing memberof
    for i in range(1, 6):
        name = "userX{}".format(i)
        dn = "cn={},{}".format(name, DEFAULT_SUFFIX)
        A.add_s(Entry((dn, {'objectclass': "top person inetuser".split(),
                            'sn': name, 'cn': name})))

        # add a few entries not allowing memberof
    for i in range(1, 6):
        name = "userY{}".format(i)
        dn = "cn={},{}".format(name, DEFAULT_SUFFIX)
        A.add_s(Entry((dn, {'objectclass': "top person".split(),
                            'sn': name, 'cn': name})))

    time.sleep(15)

    A_entries = A.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                           '(objectClass=person)')
    B_entries = B.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                           '(objectClass=person)')
    C_entries = C.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                           '(objectClass=person)')

    log.debug("A contains: %s", A_entries)
    log.debug("B contains: %s", B_entries)
    log.debug("C contains: %s", C_entries)

    assert len(A_entries) == len(B_entries)
    assert len(B_entries) == len(C_entries)

    # add a group with members allowing memberof
    dn = "cn=g1,{}".format(DEFAULT_SUFFIX)
    A.add_s(Entry((dn, {'objectclass': "top groupOfNames".split(),
                        'description': "Test Owned Group {}".format(name),
                        'member': "cn=userX1,{}".format(DEFAULT_SUFFIX),
                        'cn': "g1"})))

    # check ruv on m2 before applying failing op
    time.sleep(10)
    B_RUV = B.search_s("cn=config", ldap.SCOPE_SUBTREE,
                       "(&(objectclass=nsds5replica)(nsDS5ReplicaRoot={}))".format(DEFAULT_SUFFIX),
                       ['nsds50ruv'])
    elements = B_RUV[0].getValues('nsds50ruv')
    ruv_before = 'ruv_before'
    for ruv in elements:
        if b'replica 2' in ruv:
            ruv_before = ruv

    # add a group with members allowing memberof and members which don't
    # the op will fail on M2
    dn = "cn=g2,{}".format(DEFAULT_SUFFIX)
    A.add_s(Entry((dn, {'objectclass': "top groupOfNames".split(),
                        'description': "Test Owned Group {}".format(name),
                        'member': ["cn=userX1,{}".format(DEFAULT_SUFFIX),
                                   "cn=userX2,{}".format(DEFAULT_SUFFIX),
                                   "cn=userY1,{}".format(DEFAULT_SUFFIX)],
                        'cn': "g2"})))

    # check ruv on m2 after applying failing op
    time.sleep(10)
    B_RUV = B.search_s("cn=config", ldap.SCOPE_SUBTREE,
                       "(&(objectclass=nsds5replica)(nsDS5ReplicaRoot={}))".format(DEFAULT_SUFFIX),
                       ['nsds50ruv'])
    elements = B_RUV[0].getValues('nsds50ruv')
    ruv_after = 'ruv_after'
    for ruv in elements:
        if b'replica 2' in ruv:
            ruv_after = ruv

    log.info('ruv before fail: {}'.format(ruv_before))
    log.info('ruv after  fail: {}'.format(ruv_after))
    # the ruv should not have changed
    assert ruv_before == ruv_after


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
