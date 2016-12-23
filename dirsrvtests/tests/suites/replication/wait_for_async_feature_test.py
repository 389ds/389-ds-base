# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from collections import Counter

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

WAITFOR_ASYNC_ATTR = "nsDS5ReplicaWaitForAsyncResults"


@pytest.fixture(params=[(None, (4, 11)),
                        ('2000', (0, 2)),
                        ('0', (4, 11)),
                        ('-5', (4, 11))])
def waitfor_async_attr(topology_m2, request):
    """Sets attribute on all replicas"""

    attr_value = request.param[0]
    expected_result = request.param[1]

    # Run through all masters
    for num in range(1, 3):
        master = topology_m2.ms["master{}".format(num)]
        agmt = topology_m2.ms["master{}_agmts".format(num)].values()[0]
        try:
            if attr_value:
                log.info("Set %s: %s on %s" % (
                    WAITFOR_ASYNC_ATTR, attr_value, master.serverid))
                mod = [(ldap.MOD_REPLACE, WAITFOR_ASYNC_ATTR, attr_value)]
            else:
                log.info("Delete %s from %s" % (
                    WAITFOR_ASYNC_ATTR, master.serverid))
                mod = [(ldap.MOD_DELETE, WAITFOR_ASYNC_ATTR, None)]
            master.modify_s(agmt, mod)
        except ldap.LDAPError as e:
            log.error('Failed to set or delete %s attribute: (%s)' % (
                WAITFOR_ASYNC_ATTR, e.message['desc']))

    return (attr_value, expected_result)


@pytest.fixture
def entries(topology_m2, request):
    """Adds entries to the master1"""

    master1 = topology_m2.ms["master1"]

    TEST_OU = "test"
    test_dn = SUFFIX
    test_list = []

    log.info("Add 100 nested entries under replicated suffix on %s" % master1.serverid)
    for i in range(100):
        test_dn = 'ou=%s%s,%s' % (TEST_OU, i, test_dn)
        test_list.insert(0, test_dn)
        try:
            master1.add_s(Entry((test_dn,
                                 {'objectclass': 'top',
                                  'objectclass': 'organizationalUnit',
                                  'ou': TEST_OU})))
        except ldap.LDAPError as e:
            log.error('Failed to add entry (%s): error (%s)' % (test_dn,
                                                                e.message['desc']))
            assert False

    log.info("Delete created entries")
    for test_dn in test_list:
        try:
            master1.delete_s(test_dn)
        except ldap.LDAPError as e:
            log.error('Failed to delete entry (%s): error (%s)' % (test_dn,
                                                                   e.message['desc']))
            assert False

    def fin():
        log.info("Clear the errors log in the end of the test case")
        with open(master1.errlog, 'w') as errlog:
            errlog.writelines("")

    request.addfinalizer(fin)


def test_not_int_value(topology_m2):
    """Tests not integer value"""

    master1 = topology_m2.ms["master1"]
    agmt = topology_m2.ms["master1_agmts"]["m1_m2"]

    log.info("Try to set %s: wv1" % WAITFOR_ASYNC_ATTR)
    try:
        mod = [(ldap.MOD_REPLACE, WAITFOR_ASYNC_ATTR, "wv1")]
        master1.modify_s(agmt, mod)
    except ldap.LDAPError as e:
        assert e.message['desc'] == 'Invalid syntax'


def test_multi_value(topology_m2):
    """Tests multi value"""

    master1 = topology_m2.ms["master1"]
    agmt = topology_m2.ms["master1_agmts"]["m1_m2"]
    log.info("agmt: %s" % agmt)

    log.info("Try to set %s: 100 and 101 in the same time (multi value test)" % (
        WAITFOR_ASYNC_ATTR))
    try:
        mod = [(ldap.MOD_ADD, WAITFOR_ASYNC_ATTR, "100")]
        master1.modify_s(agmt, mod)
        mod = [(ldap.MOD_ADD, WAITFOR_ASYNC_ATTR, "101")]
        master1.modify_s(agmt, mod)
    except ldap.LDAPError as e:
        assert e.message['desc'] == 'Object class violation'


def test_value_check(topology_m2, waitfor_async_attr):
    """Checks that value has been set correctly"""

    attr_value = waitfor_async_attr[0]

    for num in range(1, 3):
        master = topology_m2.ms["master{}".format(num)]
        agmt = topology_m2.ms["master{}_agmts".format(num)].values()[0]

        log.info("Check attr %s on %s" % (WAITFOR_ASYNC_ATTR, master.serverid))
        try:
            if attr_value:
                entry = master.search_s(agmt, ldap.SCOPE_BASE, "%s=%s" % (
                    WAITFOR_ASYNC_ATTR, attr_value))
                assert entry
            else:
                entry = master.search_s(agmt, ldap.SCOPE_BASE, "%s=*" % WAITFOR_ASYNC_ATTR)
                assert not entry
        except ldap.LDAPError as e:
            log.fatal('Search failed, error: ' + e.message['desc'])
            assert False


def test_behavior_with_value(topology_m2, waitfor_async_attr, entries):
    """Tests replication behavior with valid
    nsDS5ReplicaWaitForAsyncResults attribute values
    """

    master1 = topology_m2.ms["master1"]
    master2 = topology_m2.ms["master2"]

    log.info("Set Replication Debugging loglevel for the errorlog")
    master1.setLogLevel(LOG_REPLICA)
    master2.setLogLevel(LOG_REPLICA)

    master1.modify_s("cn=config", [(ldap.MOD_REPLACE,
                                    'nsslapd-logging-hr-timestamps-enabled', "off")])
    master2.modify_s("cn=config", [(ldap.MOD_REPLACE,
                                    'nsslapd-logging-hr-timestamps-enabled', "off")])

    sync_dict = Counter()
    min_ap = waitfor_async_attr[1][0]
    max_ap = waitfor_async_attr[1][1]

    time.sleep(20)

    log.info("Gather all sync attempts within Counter dict, group by timestamp")
    with open(master1.errlog, 'r') as errlog:
        errlog_filtered = filter(lambda x: "waitfor_async_results" in x, errlog)

        # Watch only over unsuccessful sync attempts
        for line in errlog_filtered:
            if line.split()[3] != line.split()[4]:
                timestamp = line.split(']')[0]
                sync_dict[timestamp] += 1

    log.info("Take the most common timestamp and assert it has appeared " \
             "in the range from %s to %s times" % (min_ap, max_ap))
    most_common_val = sync_dict.most_common(1)[0][1]
    assert min_ap <= most_common_val <= max_ap


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
