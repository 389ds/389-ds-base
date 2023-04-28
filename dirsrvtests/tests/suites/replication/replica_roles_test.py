# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import itertools
import pytest
import ldap
from lib389._constants import SUFFIX
from lib389.topologies import topology_st as topo
from lib389.replica import Replicas
from lib389.utils import ensure_bytes


log = logging.getLogger(__name__)


ROLE_TO_CONFIG = {
            "None" : {},
            "supplier" : {
                    "nsDS5Flags": 1,
                    "nsDS5ReplicaType": 3,
                    "nsDS5ReplicaId": 1,
                },
            "hub" : {
                    "nsDS5Flags": 1,
                    "nsDS5ReplicaType": 2,
                    "nsDS5ReplicaId": 65535,
                },
            "consumer" : {
                    "nsDS5Flags": 0,
                    "nsDS5ReplicaType": 2,
                    "nsDS5ReplicaId": 65535,
                },

}

REPLICA_PROPERTIES = {
    'cn': 'replica',
    'nsDS5ReplicaRoot': SUFFIX,
    'nsDS5ReplicaBindDN': 'cn=replmgr,cn=config',
}


def verify_role(replicas, role):
    """Verify that instance has the right replica attrbutes."""
    log.info("Verify role '%s'", role)
    expected = ROLE_TO_CONFIG[role]
    rep = {}
    try:
        replica = replicas.get(SUFFIX)
        rep["nsDS5Flags"] = replica.get_attr_val_int("nsDS5Flags")
        rep["nsDS5ReplicaType"] = replica.get_attr_val_int("nsDS5ReplicaType")
        rep["nsDS5ReplicaId"] = replica.get_attr_val_int("nsDS5ReplicaId")
    except ldap.NO_SUCH_OBJECT:
        pass
    log.info('verify_role: role: %s expected: %s found: %s', role, expected, rep)
    assert rep == expected


def config_role(inst, replicas, role):
    """Configure replica role."""
    log.info("Set role to: '%s'", role)
    try:
        replica = replicas.get(SUFFIX)
    except ldap.NO_SUCH_OBJECT:
        replica = None
    properties = { key:str(val) for dct in (REPLICA_PROPERTIES,
                   ROLE_TO_CONFIG[role]) for key,val in dct.items() }
    if replica:
        if role == "None":
            replica.delete()
        else:
            # Cannot use replica.ensure_state here because:
            #  lib389 complains if nsDS5ReplicaRoot is not set
            #  389ds complains if nsDS5ReplicaRoot it is set
            # replica.ensure_state(rdn='cn=replica', properties=properties)
            mods = [ (ldap.MOD_REPLACE, key, ensure_bytes(str(val)))
                     for key,val in ROLE_TO_CONFIG[role].items()
                     if str(val).lower() != replica.get_attr_val_utf8_l(key) ]
            log.debug("LDAPMODIFY: dn: %s mods: %s",replica.dn, mods)
            inst.modify_s(replica.dn, mods, escapehatch='i am sure')
    elif role != "None":
        replicas.create(properties=properties)


@pytest.mark.parametrize(
    "from_role,to_role",
    itertools.permutations( ("None", "supplier", "hub", "consumer" ) , 2 )
)
def test_switching_roles(topo, from_role, to_role):
    """Test all transitions between roles/ CONSUMER/HUB/SUPPLIER/NONE

    :id: 6e9a697b-d5a0-45ff-b9c7-5fa14ea0c102
    :setup: Standalone Instance
    :steps:
        1. Set initial replica role
        2. Verify initial replica role
        3. Set final replica role
        4. Verify final replica role
    :expectedresults:
        1. No error
        2. No error
        3. No error
        4. No error
    """

    inst = topo.standalone
    replicas = Replicas(inst)
    inst.start()
    config_role(inst, replicas, from_role)
    verify_role(replicas, from_role)
    config_role(inst, replicas, to_role)
    verify_role(replicas, to_role)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
