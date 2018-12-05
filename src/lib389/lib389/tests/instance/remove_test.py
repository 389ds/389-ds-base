# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import subprocess
import pytest
import logging
from lib389 import DirSrv
from lib389.instance.remove import remove_ds_instance
from lib389._constants import ReplicaRole
from lib389.topologies import create_topology


@pytest.fixture(scope="function")
def topology_st(request):
    """Create DS standalone instance"""

    topology = create_topology({ReplicaRole.STANDALONE: 1})

    def fin():
        if topology.standalone.exists():
            topology.standalone.delete()
    request.addfinalizer(fin)

    return topology


@pytest.mark.parametrize("simple_allocate", (True, False))
def test_basic(topology_st, simple_allocate):
    """Check that all DS directories and systemd items were removed"""

    inst = topology_st.standalone

    # FreeIPA uses local_simple_allocate for the removal process
    if simple_allocate:
        inst = DirSrv(verbose=inst.verbose)
        inst.local_simple_allocate(topology_st.standalone.serverid)

    remove_ds_instance(inst)

    paths = [inst.ds_paths.backup_dir,
             inst.ds_paths.cert_dir,
             inst.ds_paths.config_dir,
             inst.ds_paths.db_dir,
             inst.get_changelog_dir(),
             inst.ds_paths.ldif_dir,
             inst.ds_paths.lock_dir,
             inst.ds_paths.log_dir,
             "{}/sysconfig/dirsrv-{}".format(inst.ds_paths.sysconf_dir, inst.serverid)]
    for path in paths:
        assert not os.path.exists(path)

    try:
        subprocess.check_output(['systemctl', 'is-enabled', 'dirsrv@{}'.format(inst.serverid)], encoding='utf-8')
    except subprocess.CalledProcessError as ex:
        assert "disabled" in ex.output


