# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
from os.path import expanduser
from lib389.cli_base import FakeArgs
from lib389.cli_ctl.dsrc import create_dsrc, modify_dsrc, delete_dsrc, display_dsrc, replmon_dsrc
from lib389._constants import DEFAULT_SUFFIX, DN_DM
from lib389.topologies import topology_st as topo

log = logging.getLogger(__name__)


def get_fake_args():
    # Setup our args
    args = FakeArgs()
    args.basedn = DEFAULT_SUFFIX
    args.groups_rdn = None
    args.people_rdn = None
    args.binddn = DN_DM
    args.json = None
    args.uri = None
    args.saslmech = None
    args.tls_cacertdir = None
    args.tls_cert = None
    args.tls_key = None
    args.tls_reqcert = None
    args.starttls = None
    args.cancel_starttls = None
    args.pwdfile = None
    args.do_it = True
    args.add_conn = None
    args.del_conn = None
    args.add_alias = None
    args.del_alias = None

    return args


@pytest.fixture(scope="function")
def setup(topo, request):
    """Preserve any existing .dsrc file"""

    dsrc_file = f'{expanduser("~")}/.dsrc'
    backup_file = dsrc_file + ".original"
    if os.path.exists(dsrc_file):
        os.rename(dsrc_file, backup_file)

    def fin():
        if os.path.exists(backup_file):
            os.rename(backup_file, dsrc_file)

    request.addfinalizer(fin)


def test_dsrc(topo, setup):
    """Test "dsctl dsrc" command

    :id: 0610de6c-e167-4761-bdab-3e677b2d44bb
    :setup: Standalone Instance
    :steps:
        1. Test creation works
        2. Test creating duplicate section
        3. Test adding an additional inst config works
        4. Test removing an instance works
        5. Test modify works
        6. Test delete works
        7. Test display fails when no file is present

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    inst = topo.standalone
    serverid = inst.serverid
    second_inst_name = "Second"
    second_inst_basedn = "o=second"
    different_suffix = "o=different"

    # Setup our args
    args = get_fake_args()

    # Create a dsrc configuration entry
    create_dsrc(inst, log, args)
    display_dsrc(inst, topo.logcap.log, args)
    assert topo.logcap.contains("basedn = " + args.basedn)
    assert topo.logcap.contains("binddn = " + args.binddn)
    assert topo.logcap.contains("[" + serverid + "]")
    topo.logcap.flush()

    # Attempt to add duplicate instance section
    with pytest.raises(ValueError):
        create_dsrc(inst, log, args)

    # Test adding a second instance works correctly
    inst.serverid = second_inst_name
    args.basedn = second_inst_basedn
    create_dsrc(inst, log, args)
    display_dsrc(inst, topo.logcap.log, args)
    assert topo.logcap.contains("basedn = " + args.basedn)
    assert topo.logcap.contains("[" + second_inst_name + "]")
    topo.logcap.flush()

    # Delete second instance
    delete_dsrc(inst, log, args)
    inst.serverid = serverid  # Restore original instance name
    display_dsrc(inst, topo.logcap.log, args)
    assert not topo.logcap.contains("[" + second_inst_name + "]")
    assert not topo.logcap.contains("basedn = " + args.basedn)
    # Make sure first instance config is still present
    assert topo.logcap.contains("[" + serverid + "]")
    assert topo.logcap.contains("binddn = " + args.binddn)
    topo.logcap.flush()

    # Modify the config
    args.basedn = different_suffix
    modify_dsrc(inst, log, args)
    display_dsrc(inst, topo.logcap.log, args)
    assert topo.logcap.contains(different_suffix)
    topo.logcap.flush()

    # Remove an arg from the config
    args.basedn = ""
    modify_dsrc(inst, log, args)
    display_dsrc(inst, topo.logcap.log, args)
    assert not topo.logcap.contains(different_suffix)
    topo.logcap.flush()

    # Remove the last entry, which should delete the file
    delete_dsrc(inst, log, args)
    dsrc_file = f'{expanduser("~")}/.dsrc'
    assert not os.path.exists(dsrc_file)

    # Make sure display fails
    with pytest.raises(ValueError):
        display_dsrc(inst, log, args)


def test_dsrc_repl_mon(topo, setup):
    """Test "dsctl dsrc repl-mon" command, add & remove creds and aliases

    :id: 33007d01-f11c-456b-bb16-fcd7920c9fc8
    :setup: Standalone Instance
    :steps:
        1. Add connection
        2. Add same connection - should fail
        3. Delete connection
        4. Delete same connection - should fail
        5. Add alias
        6. Add same alias - should fail
        7. Delete alias
        8. Delete same alias again 0 should fail

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """

    inst = topo.standalone
    args = get_fake_args()
    create_dsrc(inst, log, args)

    # Add replica connection
    assert not topo.logcap.contains("repl-monitor-connections")
    repl_conn = "replica_1:localhost:5555:cn=directory manager:password"
    args.add_conn = [repl_conn,]
    replmon_dsrc(inst, log, args)
    display_dsrc(inst, topo.logcap.log, args)
    assert  topo.logcap.contains("repl-monitor-connections")
    assert  topo.logcap.contains("replica_1 = localhost:5555:cn=directory manager:password")
    topo.logcap.flush()
    args.add_conn = None

    # Add duplicate replica connection
    args.add_conn = [repl_conn, ]
    try:
        replmon_dsrc(inst, log, args)
        assert False
    except ValueError:
        pass
    args.add_conn = None

    # Delete replica connection
    args.del_conn = ["replica_1"]
    replmon_dsrc(inst, log, args)
    display_dsrc(inst, topo.logcap.log, args)
    assert not topo.logcap.contains("replica_1 = localhost:5555:cn=directory manager:password")
    assert not topo.logcap.contains("repl-monitor-connections")
    topo.logcap.flush()
    args.del_conn = None

    # Delete replica connection (already deleted)
    args.del_conn = ["replica_1"]
    try:
        replmon_dsrc(inst, log, args)
        assert False
    except ValueError:
        pass
    args.del_conn = None

    # Add Alias
    assert not topo.logcap.contains("repl-monitor-aliases")
    repl_alias = "my_alias:localhost:4444"
    args.add_alias = [repl_alias,]
    replmon_dsrc(inst, log, args)
    display_dsrc(inst, topo.logcap.log, args)
    assert topo.logcap.contains("repl-monitor-aliases")
    assert topo.logcap.contains("my_alias = localhost:4444")
    topo.logcap.flush()
    args.add_alias = None

    # Add Duplicate Alias
    args.add_alias = [repl_alias,]
    try:
        replmon_dsrc(inst, log, args)
        assert False
    except ValueError:
        pass
    args.add_alias = None

    # Delete Alias
    args.del_alias = ["my_alias",]
    replmon_dsrc(inst, log, args)
    display_dsrc(inst, topo.logcap.log, args)
    assert not topo.logcap.contains("my_alias = localhost:4444")
    assert not topo.logcap.contains("repl-monitor-aliases")
    topo.logcap.flush()
    args.del_alias = None

    # Delete alias (already deleted)
    args.del_alias = ["my_alias", ]
    try:
        replmon_dsrc(inst, log, args)
        assert False
    except ValueError:
        pass
    args.del_alias = None


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
