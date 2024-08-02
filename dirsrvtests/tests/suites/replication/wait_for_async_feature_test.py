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

from lib389._constants import SUFFIX, DEFAULT_SUFFIX, ErrorLog

from lib389.agreement import Agreements
from lib389.idm.organizationalunit import OrganizationalUnits

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

# Expected minimum and maximum number of async result in usual cases
USUAL_MIN_AP = 3
USUAL_MAX_AP = 11

@pytest.fixture(params=[(None, (USUAL_MIN_AP, USUAL_MAX_AP)),
                        ('2000', (0, 2)),
                        ('0', (USUAL_MIN_AP, USUAL_MAX_AP)),
                        ('-5', (USUAL_MIN_AP, USUAL_MAX_AP))])
def waitfor_async_attr(topology_m2, request):
    """Sets attribute on all replicas"""

    attr_value = request.param[0]
    expected_result = request.param[1]

    # Run through all suppliers

    for supplier in topology_m2.ms.values():
        agmt = Agreements(supplier).list()[0]

        if attr_value:
            agmt.set_wait_for_async_results(attr_value)
        else:
            try:
                # Sometimes we can double remove this.
                agmt.remove_wait_for_async_results()
            except ldap.NO_SUCH_ATTRIBUTE:
                pass

    return (attr_value, expected_result)


@pytest.fixture
def entries(topology_m2, request):
    """Adds entries to the supplier1"""

    supplier1 = topology_m2.ms["supplier1"]

    test_list = []

    log.info("Add 100 nested entries under replicated suffix on %s" % supplier1.serverid)
    ous = OrganizationalUnits(supplier1, DEFAULT_SUFFIX)
    for i in range(100):
        ou = ous.create(properties={
            'ou' : 'test_ou_%s' % i,
        })
        test_list.append(ou)

    log.info("Delete created entries")
    for test_ou in test_list:
        test_ou.delete()

    def fin():
        log.info("Clear the errors log in the end of the test case")
        with open(supplier1.errlog, 'w') as errlog:
            errlog.writelines("")

    request.addfinalizer(fin)


def test_not_int_value(topology_m2):
    """Tests not integer value

    :id: 67c9994f-9251-425a-8197-8d12ad9beafc
    :setup: Replication with two suppliers
    :steps:
        1. Try to set some string value
           to nsDS5ReplicaWaitForAsyncResults
    :expectedresults:
        1. Invalid syntax error should be raised
    """
    supplier1 = topology_m2.ms["supplier1"]
    agmt = Agreements(supplier1).list()[0]

    with pytest.raises(ldap.INVALID_SYNTAX):
        agmt.set_wait_for_async_results("ws2")

def test_multi_value(topology_m2):
    """Tests multi value

    :id: 1932301a-db29-407e-b27e-4466a876d1d3
    :setup: Replication with two suppliers
    :steps:
        1. Set nsDS5ReplicaWaitForAsyncResults to some int
        2. Try to add one more int value
           to nsDS5ReplicaWaitForAsyncResults
    :expectedresults:
        1. nsDS5ReplicaWaitForAsyncResults should be set
        2. Object class violation error should be raised
    """

    supplier1 = topology_m2.ms["supplier1"]
    agmt = Agreements(supplier1).list()[0]

    agmt.set_wait_for_async_results('100')
    with pytest.raises(ldap.OBJECT_CLASS_VIOLATION):
        agmt.add('nsDS5ReplicaWaitForAsyncResults', '101')

def test_value_check(topology_m2, waitfor_async_attr):
    """Checks that value has been set correctly

    :id: 3e81afe9-5130-410d-a1bb-d798d8ab8519
    :parametrized: yes
    :setup: Replication with two suppliers,
        wait for async set on all suppliers, try:
        None, '2000', '0', '-5'
    :steps:
        1. Search for nsDS5ReplicaWaitForAsyncResults on supplier 1
        2. Search for nsDS5ReplicaWaitForAsyncResults on supplier 2
    :expectedresults:
        1. nsDS5ReplicaWaitForAsyncResults should be set correctly
        2. nsDS5ReplicaWaitForAsyncResults should be set correctly
    """

    attr_value = waitfor_async_attr[0]

    for supplier in topology_m2.ms.values():
        agmt = Agreements(supplier).list()[0]

        server_value = agmt.get_wait_for_async_results_utf8()
        assert server_value == attr_value

def test_behavior_with_value(topology_m2, waitfor_async_attr, entries):
    """Tests replication behavior with valid
    nsDS5ReplicaWaitForAsyncResults attribute values

    :id: 117b6be2-cdab-422e-b0c7-3b88bbeec036
    :parametrized: yes
    :setup: Replication with two suppliers,
        wait for async set on all suppliers, try:
        None, '2000', '0', '-5'
    :steps:
        1. Set Replication Debugging loglevel for the errorlog
        2. Set nsslapd-logging-hr-timestamps-enabled to 'off' on both suppliers
        3. Gather all sync attempts,  group by timestamp
        4. Take the most common timestamp and assert it has appeared
           in the set range
    :expectedresults:
        1. Replication Debugging loglevel should be set
        2. nsslapd-logging-hr-timestamps-enabled  should be set
        3. Operation should be successful
        4. Errors log should have all timestamp appear
    """

    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]

    log.info("Set Replication Debugging loglevel for the errorlog")
    supplier1.config.loglevel((ErrorLog.REPLICA,))
    supplier2.config.loglevel((ErrorLog.REPLICA,))

    sync_dict = Counter()
    min_ap = waitfor_async_attr[1][0]
    max_ap = waitfor_async_attr[1][1]

    time.sleep(20)

    log.info("Gather all sync attempts within Counter dict, group by timestamp")
    with open(supplier1.errlog, 'r') as errlog:
        errlog_filtered = filter(lambda x: "waitfor_async_results" in x, errlog)

        # Watch only over unsuccessful sync attempts
        for line in errlog_filtered:
            if line.split()[3] != line.split()[4]:
                # A timestamp looks like:
                # [03/Jan/2018:14:35:15.806396035 +1000] LOGMESSAGE HERE
                # We want to assert a range of "seconds", so we need to reduce
                # this to a reasonable amount. IE:
                #   [03/Jan/2018:14:35:15
                # So to achieve this we split on ] and . IE.
                # [03/Jan/2018:14:35:15.806396035 +1000] LOGMESSAGE HERE
                #                                      ^ split here first
                #                      ^ now split here
                # [03/Jan/2018:14:35:15
                # ^ final result
                timestamp = line.split(']')[0].split('.')[0]
                sync_dict[timestamp] += 1

    log.info("Take the most common timestamp and assert it has appeared " \
             "in the range from %s to %s times" % (min_ap, max_ap))
    most_common_val = sync_dict.most_common(1)[0][1]
    log.debug("%s <= %s <= %s" % (min_ap, most_common_val, max_ap))
    assert min_ap <= most_common_val <= max_ap


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
