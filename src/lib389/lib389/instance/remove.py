# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import shutil

def remove_ds_instance(dirsrv):
    """
    This will delete the instance as it is define. This must be a local instance.
    """
    _log = dirsrv.log.getChild('remove_ds')
    _log.debug("Removing instance %s" % dirsrv.serverid)
    # Stop the instance (if running)
    _log.debug("Stopping instance %s" % dirsrv.serverid)
    dirsrv.stop()
    # Copy all the paths we are about to tamp with
    remove_paths = {}
    remove_paths['backup_dir'] = dirsrv.ds_paths.backup_dir
    remove_paths['cert_dir'] = dirsrv.ds_paths.cert_dir
    remove_paths['config_dir'] = dirsrv.ds_paths.config_dir
    remove_paths['db_dir'] = dirsrv.ds_paths.db_dir
    remove_paths['ldif_dir'] = dirsrv.ds_paths.ldif_dir
    remove_paths['lock_dir'] = dirsrv.ds_paths.lock_dir
    remove_paths['log_dir'] = dirsrv.ds_paths.log_dir
    remove_paths['run_dir'] = dirsrv.ds_paths.run_dir

    marker_path = "%s/sysconfig/dirsrv-%s" % (dirsrv.ds_paths.sysconf_dir, dirsrv.serverid)

    # Check the marker exists. If it *does not* warn about this, and say that to
    # force removal you should touch this file.

    _log.debug("Checking for instance marker at %s" % marker_path)
    assert os.path.exists(marker_path)

    # Remove these paths:
    # for path in ('backup_dir', 'cert_dir', 'config_dir', 'db_dir',
    #             'ldif_dir', 'lock_dir', 'log_dir', 'run_dir'):
    for path_k in remove_paths:
        if os.path.exists(remove_paths[path_k]):
            _log.debug("Removing %s" % remove_paths[path_k])
            shutil.rmtree(remove_paths[path_k])

    # Finally remove the sysconfig marker.
    os.remove(marker_path)
    _log.debug("Removing %s" % marker_path)

    # Done!
    _log.debug("Complete")

