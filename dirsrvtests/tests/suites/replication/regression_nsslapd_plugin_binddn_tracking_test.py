# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
from lib389.tasks import *
from lib389.topologies import topology_m2 as topo_m2
from lib389.utils import *
from lib389.replica import *
from lib389._constants import *
from lib389.idm.user import UserAccounts
from lib389.idm.domain import Domain

log = logging.getLogger(__name__)


@pytest.mark.DS47950
def test_nsslapd_plugin_binddn_tracking(topo_m2):
    """
        Testing nsslapd-plugin-binddn-tracking does not cause issues around
        access control and reconfiguring replication/repl agmt.
        :id: f5ba7b64-fe04-11e8-a298-8c16451d917b
        :setup: Replication with two masters.
        :steps:
            1. Turn on bind dn tracking
            2. Add two users
            3. Add an aci
            4. Make modification as user
            5. Setup replica and create a repl agmt
            6. Modify replica
            7. Modify repl agmt
        :expectedresults:
            1. Should Success.
            2. Should Success.
            3. Should Success.
            4. Should Success.
            5. Should Success.
            6. Should Success.
            7. Should Success.
    """

    log.info("Testing Ticket 47950 - Testing nsslapd-plugin-binddn-tracking")

    #
    # Turn on bind dn tracking
    #
    topo_m2.ms["master1"].config.replace("nsslapd-plugin-binddn-tracking", "on")
    #
    # Add two users
    #
    users = UserAccounts(topo_m2.ms["master1"], DEFAULT_SUFFIX)
    test_user_1 = users.create_test_user(uid=1)
    test_user_2 = users.create_test_user(uid=2)
    test_user_1.set('userPassword', 'password')
    test_user_2.set('userPassword', 'password')
    #
    # Add an aci
    #
    USER1_DN = users.list()[0].dn
    USER2_DN = users.list()[1].dn
    acival = (
        '(targetattr ="cn")(version 3.0;acl "Test bind dn tracking"'
        + ';allow (all) (userdn = "ldap:///%s");)' % USER1_DN
    )
    Domain(topo_m2.ms["master1"], DEFAULT_SUFFIX).add("aci", acival)

    #
    # Make modification as user
    #
    assert topo_m2.ms["master1"].simple_bind_s(USER1_DN, "password")
    test_user_2.replace("cn", "new value")
    #
    # Setup replica and create a repl agmt
    #
    repl = ReplicationManager(DEFAULT_SUFFIX)
    assert topo_m2.ms["master1"].simple_bind_s(DN_DM, PASSWORD)
    repl.test_replication(topo_m2.ms["master1"], topo_m2.ms["master2"], 30)
    repl.test_replication(topo_m2.ms["master2"], topo_m2.ms["master1"], 30)
    properties = {
        "cn": "test_agreement",
        "nsDS5ReplicaRoot": "dc=example,dc=com",
        "nsDS5ReplicaHost": "localhost.localdomain",
        "nsDS5ReplicaPort": "5555",
        "nsDS5ReplicaBindDN": "uid=tester",
        "nsds5ReplicaCredentials": "password",
        "nsDS5ReplicaTransportInfo": "LDAP",
        "nsDS5ReplicaBindMethod": "SIMPLE",
    }
    replicas = Replicas(topo_m2.ms["master1"])
    replica = replicas.get(DEFAULT_SUFFIX)
    agmts = Agreements(topo_m2.ms["master1"], basedn=replica.dn)
    repl_agreement = agmts.create(properties=properties)
    #
    # modify replica
    #
    replica.replace("nsDS5ReplicaId", "7")
    assert replica.present("nsDS5ReplicaId", "7")
    #
    # modify repl agmt
    #
    repl_agreement.replace('nsDS5ReplicaPort', "8888")
    assert repl_agreement.present('nsDS5ReplicaPort', "8888")


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
