# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import shutil
import subprocess


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
    ### WARNING: The changelogdb isn't removed. we assume it's in:
    # db_dir ../changelogdb. So remove that too!
    # abspath will resolve the ".." down.
    remove_paths['changelogdb_dir'] = dirsrv.get_changelog_dir()
    remove_paths['ldif_dir'] = dirsrv.ds_paths.ldif_dir
    remove_paths['lock_dir'] = dirsrv.ds_paths.lock_dir
    remove_paths['log_dir'] = dirsrv.ds_paths.log_dir
    # remove_paths['run_dir'] = dirsrv.ds_paths.run_dir

    marker_path = "%s/sysconfig/dirsrv-%s" % (dirsrv.ds_paths.sysconf_dir, dirsrv.serverid)

    # Check the marker exists. If it *does not* warn about this, and say that to
    # force removal you should touch this file.

    _log.debug("Checking for instance marker at %s" % marker_path)
    assert os.path.exists(marker_path)

    # Move the config_dir to config_dir.removed
    config_dir = dirsrv.ds_paths.config_dir
    config_dir_rm = "{}.removed".format(config_dir)

    if os.path.exists(config_dir_rm):
        _log.debug("Removing previously existed %s" % config_dir_rm)
        shutil.rmtree(config_dir_rm)

    _log.debug("Copying %s to %s" % (config_dir, config_dir_rm))
    try:
        shutil.copytree(config_dir, config_dir_rm)
    except FileNotFoundError:
        pass

    # Remove these paths:
    # for path in ('backup_dir', 'cert_dir', 'config_dir', 'db_dir',
    #             'ldif_dir', 'lock_dir', 'log_dir', 'run_dir'):
    for path_k in remove_paths:
        _log.debug("Removing %s" % remove_paths[path_k])
        shutil.rmtree(remove_paths[path_k], ignore_errors=True)

    # Finally remove the sysconfig marker.
    os.remove(marker_path)
    _log.debug("Removing %s" % marker_path)

    # Remove the systemd symlink
    _log.debug("Removing the systemd symlink")
    subprocess.check_call(["systemctl", "disable", "dirsrv@{}".format(dirsrv.serverid)])

    # Done!
    _log.debug("Complete")

