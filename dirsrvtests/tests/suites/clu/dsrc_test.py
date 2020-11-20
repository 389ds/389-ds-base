import logging
import pytest
import os
from os.path import expanduser
from lib389.cli_base import FakeArgs
from lib389.cli_ctl.dsrc import create_dsrc, modify_dsrc, delete_dsrc, display_dsrc
from lib389._constants import DEFAULT_SUFFIX, DN_DM
from lib389.topologies import topology_st as topo

log = logging.getLogger(__name__)


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
    args = FakeArgs()
    args.basedn = DEFAULT_SUFFIX
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

