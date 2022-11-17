# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import copy
import os
import ldap
from lib389._constants import *
from lib389.topologies import topology_st as topo

from lib389.replica import Replicas
from lib389.agreement import Agreements
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

notnum = 'invalid'
too_big = '9223372036854775807'
overflow = '9999999999999999999999999999999999999999999999999999999999999999999'

replica_dict = {'nsDS5ReplicaRoot': 'dc=example,dc=com',
                'nsDS5ReplicaType': '3',
                'nsDS5Flags': '1',
                'nsDS5ReplicaId': '65534',
                'nsds5ReplicaPurgeDelay': '604800',
                'nsDS5ReplicaBindDN': 'cn=u',
                'cn': 'replica'}

agmt_dict = {'cn': 'test_agreement',
             'nsDS5ReplicaRoot': 'dc=example,dc=com',
             'nsDS5ReplicaHost': 'localhost.localdomain',
             'nsDS5ReplicaPort': '5555',
             'nsDS5ReplicaBindDN': 'uid=tester',
             'nsds5ReplicaCredentials': 'password',
             'nsDS5ReplicaTransportInfo': 'LDAP',
             'nsDS5ReplicaBindMethod': 'SIMPLE'}


repl_add_attrs = [('nsDS5ReplicaType', '-1', '4', overflow, notnum, '1'),
                  ('nsDS5Flags', '-1', '2', overflow, notnum, '1'),
                  ('nsDS5ReplicaId', '0', '65536', overflow, notnum, '1'),
                  ('nsds5ReplicaPurgeDelay', '-2', too_big, overflow, notnum, '1'),
                  ('nsDS5ReplicaBindDnGroupCheckInterval', '-2', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaTombstonePurgeInterval', '-2', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaProtocolTimeout', '-1', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaReleaseTimeout', '-1', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaBackoffMin', '0', too_big, overflow, notnum, '3'),
                  ('nsds5ReplicaBackoffMax', '0', too_big, overflow, notnum, '6')]

repl_mod_attrs = [('nsDS5Flags', '-1', '2', overflow, notnum, '1'),
                  ('nsds5ReplicaPurgeDelay', '-2', too_big, overflow, notnum, '1'),
                  ('nsDS5ReplicaBindDnGroupCheckInterval', '-2', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaTombstonePurgeInterval', '-2', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaProtocolTimeout', '-1', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaReleaseTimeout', '-1', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaBackoffMin', '0', too_big, overflow, notnum, '3'),
                  ('nsds5ReplicaBackoffMax', '0', too_big, overflow, notnum, '6')]

agmt_attrs = [
              ('nsds5ReplicaPort', '0', '65535', overflow, notnum, '389'),
              ('nsds5ReplicaTimeout', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaBusyWaitTime', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaSessionPauseTime', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaFlowControlWindow', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaFlowControlPause', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaProtocolTimeout', '-1', too_big, overflow, notnum, '6')
              ]

def replica_reset(topo):
    """Purge all existing replica details"""
    replicas = Replicas(topo.standalone)
    for r in replicas.list():
        r.delete()

def replica_setup(topo):
    """Add a valid replica config entry to modify
    """
    replicas = Replicas(topo.standalone)
    for r in replicas.list():
        r.delete()
    return replicas.create(properties=replica_dict)

def agmt_reset(topo):
    """Purge all existing agreements for testing"""
    agmts = Agreements(topo.standalone)
    for a in agmts.list():
        a.delete()

def agmt_setup(topo):
    """Add a valid replica config entry to modify
    """
    # Reset the agreements too.
    replica = replica_setup(topo)
    agmts = Agreements(topo.standalone, basedn=replica.dn)
    for a in agmts.list():
        a.delete()
    return agmts.create(properties=agmt_dict)

def perform_invalid_create(many, properties, attr, value):
    my_properties = copy.deepcopy(properties)
    my_properties[attr] = value
    with pytest.raises(ldap.LDAPError) as ei:
        many.create(properties=my_properties)
    return ei.value

def perform_invalid_modify(o, attr, value):
    with pytest.raises(ldap.LDAPError) as ei:
        o.replace(attr, value)
    return ei.value

@pytest.mark.parametrize("attr, too_small, too_big, overflow, notnum, valid", repl_add_attrs)
def test_replica_num_add(topo, attr, too_small, too_big, overflow, notnum, valid):
    """Test all the number values you can set for a replica config entry

    :id: a8b47d4a-a089-4d70-8070-e6181209bf92
    :parametrized: yes
    :setup: standalone instance
    :steps:
        1. Use a value that is too small
        2. Use a value that is too big
        3. Use a value that overflows the int
        4. Use a value with character value (not a number)
        5. Use a valid value
    :expectedresults:
        1. Add is rejected
        2. Add is rejected
        3. Add is rejected
        4. Add is rejected
        5. Add is allowed
    """
    replica_reset(topo)

    replicas = Replicas(topo.standalone)

    # Test too small
    perform_invalid_create(replicas, replica_dict, attr, too_small)
    # Test too big
    perform_invalid_create(replicas, replica_dict, attr, too_big)
    # Test overflow
    perform_invalid_create(replicas, replica_dict, attr, overflow)
    # test not a number
    perform_invalid_create(replicas, replica_dict, attr, notnum)
    # Test valid value
    my_replica = copy.deepcopy(replica_dict)
    my_replica[attr] = valid
    replicas.create(properties=my_replica)

@pytest.mark.parametrize("attr, too_small, too_big, overflow, notnum, valid", repl_mod_attrs)
def test_replica_num_modify(topo, attr, too_small, too_big, overflow, notnum, valid):
    """Test all the number values you can set for a replica config entry

    :id: a8b47d4a-a089-4d70-8070-e6181209bf93
    :parametrized: yes
    :setup: standalone instance
    :steps:
        1. Replace a value that is too small
        2. Repalce a value that is too big
        3. Replace a value that overflows the int
        4. Replace a value with character value (not a number)
        5. Replace a vlue with a valid value
    :expectedresults:
        1. Value is rejected
        2. Value is rejected
        3. Value is rejected
        4. Value is rejected
        5. Value is allowed
    """
    replica = replica_setup(topo)

    # Value too small
    perform_invalid_modify(replica, attr, too_small)
    # Value too big
    perform_invalid_modify(replica, attr, too_big)
    # Value overflow
    perform_invalid_modify(replica, attr, overflow)
    # Value not a number
    perform_invalid_modify(replica, attr, notnum)
    # Value is valid
    replica.replace(attr, valid)


@pytest.mark.xfail(reason="Agreement validation current does not work.")
@pytest.mark.parametrize("attr, too_small, too_big, overflow, notnum, valid", agmt_attrs)
def test_agmt_num_add(topo, attr, too_small, too_big, overflow, notnum, valid):
    """Test all the number values you can set for a replica config entry

    :id: a8b47d4a-a089-4d70-8070-e6181209bf94
    :parametrized: yes
    :setup: standalone instance
    :steps:
        1. Use a value that is too small
        2. Use a value that is too big
        3. Use a value that overflows the int
        4. Use a value with character value (not a number)
        5. Use a valid value
    :expectedresults:
        1. Add is rejected
        2. Add is rejected
        3. Add is rejected
        4. Add is rejected
        5. Add is allowed
    """

    agmt_reset(topo)
    replica = replica_setup(topo)

    agmts = Agreements(topo.standalone, basedn=replica.dn)

    # Test too small
    perform_invalid_create(agmts, agmt_dict, attr, too_small)
    # Test too big
    perform_invalid_create(agmts, agmt_dict, attr, too_big)
    # Test overflow
    perform_invalid_create(agmts, agmt_dict, attr, overflow)
    # test not a number
    perform_invalid_create(agmts, agmt_dict, attr, notnum)
    # Test valid value
    my_agmt = copy.deepcopy(agmt_dict)
    my_agmt[attr] = valid
    agmts.create(properties=my_agmt)


@pytest.mark.xfail(reason="Agreement validation current does not work.")
@pytest.mark.parametrize("attr, too_small, too_big, overflow, notnum, valid", agmt_attrs)
def test_agmt_num_modify(topo, attr, too_small, too_big, overflow, notnum, valid):
    """Test all the number values you can set for a replica config entry

    :id: a8b47d4a-a089-4d70-8070-e6181209bf95
    :parametrized: yes
    :setup: standalone instance
    :steps:
        1. Replace a value that is too small
        2. Replace a value that is too big
        3. Replace a value that overflows the int
        4. Replace a value with character value (not a number)
        5. Replace a vlue with a valid value
    :expectedresults:
        1. Value is rejected
        2. Value is rejected
        3. Value is rejected
        4. Value is rejected
        5. Value is allowed
    """

    agmt = agmt_setup(topo)

    # Value too small
    perform_invalid_modify(agmt, attr, too_small)
    # Value too big
    perform_invalid_modify(agmt, attr, too_big)
    # Value overflow
    perform_invalid_modify(agmt, attr, overflow)
    # Value not a number
    perform_invalid_modify(agmt, attr, notnum)
    # Value is valid
    agmt.replace(attr, valid)


@pytest.mark.skipif(ds_is_older('1.4.1.4'), reason="Not implemented")
@pytest.mark.bz1546739
def test_same_attr_yields_same_return_code(topo):
    """Test that various operations with same incorrect attribute value yield same return code
    """
    attr = 'nsDS5ReplicaId'

    replica_reset(topo)
    replicas = Replicas(topo.standalone)
    e = perform_invalid_create(replicas, replica_dict, attr, too_big)
    assert type(e) is ldap.UNWILLING_TO_PERFORM

    replica = replica_setup(topo)
    e = perform_invalid_modify(replica, attr, too_big)
    assert type(e) is ldap.UNWILLING_TO_PERFORM


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
