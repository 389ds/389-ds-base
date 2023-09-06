# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import json
import logging
import re
import time
import ldap
import pytest
from lib389._constants import SUFFIX, ReplicaRole, DEFAULT_SUFFIX
from lib389.topologies import create_topology
from lib389.replica import Agreements, ReplicationManager
from lib389.schema import Schema
from lib389.idm.user import UserAccounts
from lib389.cli_base import LogCapture
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

def pattern_errorlog(file, log_pattern):
    """Check for a pattern in the error log file."""

    try:
        pattern_errorlog.last_pos += 1
    except AttributeError:
        pattern_errorlog.last_pos = 0

    found = None
    log.debug("_pattern_errorlog: start at offset %d" % pattern_errorlog.last_pos)
    file.seek(pattern_errorlog.last_pos)

    # Use a while true iteration because 'for line in file: hit a
    # python bug that break file.tell()
    while True:
        line = file.readline()
        log.debug("_pattern_errorlog: [%d] %s" % (file.tell(), line))
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break

    log.debug("_pattern_errorlog: end at offset %d" % file.tell())
    pattern_errorlog.last_pos = file.tell()
    return found


def trigger_update(topology, user_rdn, num):
    """It triggers an update on the supplier. This will start a replication
    session and a schema push
    """

    users_s = UserAccounts(topology.ms["supplier1"], DEFAULT_SUFFIX)
    user = users_s.get(user_rdn)
    user.replace('telephonenumber', str(num))

    #  wait until the update is replicated (until up to x seconds)
    users_c = UserAccounts(topology.cs["consumer1"], DEFAULT_SUFFIX)
    for _ in range(30):
        try:
            user = users_c.get(user_rdn)
            val = user.get_attr_val_int('telephonenumber')
            if val == num:
                return
            # the expected value is not yet replicated. try again
            time.sleep(1)
            log.debug(f"trigger_update: receive {val} (expected {num})")
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)


def trigger_schema_push(topology, user_rdn, num):
    """Triggers a schema push from the supplier to the consumer or hub."""

    supplier = topology['topology'].ms["supplier1"]
    if topology['type'] == "m1h1c1":
        consumer = topology['topology'].hs["hub1"]
    else:
        consumer = topology['topology'].cs["consumer1"]

    agreements = supplier.agreement.list(suffix=SUFFIX,
                                         consumer_host=consumer.host,
                                         consumer_port=consumer.port)
    assert (len(agreements) == 1)
    ra = agreements[0]
    trigger_update(topology['topology'], user_rdn, num)
    supplier.agreement.pause(ra.dn)
    supplier.agreement.resume(ra.dn)
    trigger_update(topology['topology'], user_rdn, num)


def add_attributetype(inst, num, at_name, x_origin):
    """Adds a new attribute type to the schema."""

    schema = Schema(inst)
    # Add new attribute
    parameters = {
        'names': [at_name],
        'oid': str(9000 + num),
        'desc': 'Test extra parenthesis in X-ORIGIN',
        # 'x_origin': [x_origin],
        'x_origin': None,
        'syntax': '1.3.6.1.4.1.1466.115.121.1.15',
        'syntax_len': None,
        'x_ordered': None,
        'collective': None,
        'obsolete': None,
        'single_value': None,
        'no_user_mod': None,
        'equality': None,
        'substr': None,
        'ordering': None,
        'usage': None,
        'sup': None
    }
    schema.add_attributetype(parameters)


@pytest.fixture(scope="function", params=["m1c1", "m1h1c1"])
def topology(request):
    """Create Replication Deployment based on the params"""

    if request.param == "m1c1":
        topo_roles = {ReplicaRole.SUPPLIER: 1, ReplicaRole.CONSUMER: 1}
    elif request.param == "m1h1c1":
        topo_roles = {ReplicaRole.SUPPLIER: 1, ReplicaRole.HUB: 1, ReplicaRole.CONSUMER: 1}

    topology = create_topology(topo_roles, request=request)

    topology.logcap = LogCapture()
    return {
        'topology': topology, 
        'type': request.param
    }


@pytest.fixture(scope="function")
def schema_replication_init(topology):
    """Initialize the test environment """

    supplier = topology['topology'].ms["supplier1"]
    supplier.errorlog_file = open(supplier.errlog, "r")
    users = UserAccounts(supplier, DEFAULT_SUFFIX)
    user = users.create_test_user()
    user.replace('telephonenumber', '0')
    
    return user


@pytest.mark.parametrize("xorigin", ['user defined', 'custom xorigin'])
def test_schema_xorigin_repl(topology, schema_replication_init, xorigin):
    """Check consumer schema is a superset (one extra OC) of supplier schema, then
    schema is pushed and there is a message in the error log

    :id: 2b29823b-3e83-4b25-954a-8a081dbc15ee
    :setup: Supplier and consumer topology, with one user entry;
            Supplier, hub and consumer topology, with one user entry
    :steps:
        1. Push the schema from the supplier to the consumer (an error should not be generated)
        2. Update the schema of the consumer, so it will be a superset of the supplier's schema
        3. Update the schema of the supplier to make its nsSchemaCSN larger than the consumer's
        4. Push the schema from the supplier to the consumer (an error should be generated)
        5. Check if the supplier learns the missing definition
        6. Check the error logs for any issues
        7. Check the startup and final state of the schema replication process
    :expectedresults:
        1. The supplier's schema update should be successful
        2. The consumer's schema update should be successful
        3. The supplier's schema update should be successful
        4. The schema push operation should be successful
        5. The supplier should successfully learn the missing definition
        6. There should be no error messages in the logs
        7. The startup and final state of the schema replication process should be as expected
    """

    repl = ReplicationManager(DEFAULT_SUFFIX)
    user = schema_replication_init
    hub = None
    supplier = topology['topology'].ms["supplier1"]
    consumer = topology['topology'].cs["consumer1"]
    if topology['type'] == "m1h1c1":
        hub = topology['topology'].hs["hub1"]

    add_attributetype(supplier, 1, 'testAttribute', xorigin)

    # Search for attribute with JSON option
    schema = Schema(supplier)
    attr_result = schema.query_attributetype('testAttribute', json=True)
    # Verify the x-origin value is correct
    assert attr_result['at']['x_origin'][0] == "user defined"

    trigger_schema_push(topology, user.rdn, 1)
    repl.wait_for_replication(supplier, consumer)
    supplier_schema_csn = supplier.schema.get_schema_csn()
    consumer_schema_csn = consumer.schema.get_schema_csn()
    assert supplier_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    regex = re.compile(r"must not be overwritten \(set replication log for additional info\)")
    res = pattern_errorlog(supplier.errorlog_file, regex)
    if res is not None:
        assert False

    # add a new OC on the supplier so that its nsSchemaCSN is larger than the consumer (wait 2s)
    add_attributetype(consumer, 2, 'testAttributeCA', xorigin)
    time.sleep(2)
    add_attributetype(supplier, 3, 'testAttributeSA', xorigin)

    # now push the scheam
    trigger_schema_push(topology, user.rdn, 2)
    repl.wait_for_replication(supplier, consumer)
    supplier_schema_csn = supplier.schema.get_schema_csn()
    consumer_schema_csn = consumer.schema.get_schema_csn()
    assert supplier_schema_csn == consumer_schema_csn

    # Check the error log of the supplier does not contain an error
    # This message may happen during the learning phase
    regex = re.compile(r"must not be overwritten \(set replication log for additional info\)")
    pattern_errorlog(supplier.errorlog_file, regex)

    # Check that standard schema was not rewritten to be "user defined' on the consumer
    cn_attrs = json.loads(consumer.schema.query_attributetype("cn", json=True))
    cn_attr = cn_attrs['at']
    assert cn_attr['x_origin'][0].lower() != "user defined"
    if len(cn_attr['x_origin']) > 1:
        assert cn_attr['x_origin'][1].lower() != "user defined"

    # Check that the new OC "supplierNewOCB" was written to be "user defined' on the consumer
    ocs = json.loads(consumer.schema.query_attributetype("testAttributeSA", json=True))
    new_oc = ocs['at']
    assert new_oc['x_origin'][0].lower() == "user defined"
