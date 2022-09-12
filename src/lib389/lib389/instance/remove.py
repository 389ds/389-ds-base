# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import shutil
import subprocess
from lib389.nss_ssl import NssSsl
from lib389.utils import (
    assert_c,
    ensure_list_str,
    ensure_str,
    selinux_clean_files_label,
    selinux_clean_ports_label,
    selinux_label_file,
    selinux_label_port,
)


######################## WARNING #############################
# DO NOT CHANGE THIS FILE OR ITS CONTENTS WITHOUT READING
# ALL OF THE COMMENTS FIRST. THERE ARE VERY DELICATE
# AND DETAILED INTERACTIONS OF COMPONENTS IN THIS FILE.
#
# IF IN DOUBT CONTACT WILLIAM BROWN <william@blackhats.net.au>


def remove_ds_instance(dirsrv, force=False):
    """
    This will delete the instance as it is define. This must be a local instance. This is
    designed to raise exceptions quickly and often if *any* error is hit. However, this can
    be run repeatedly, and only when the instance is truely removed, will this program fail
    to run further.

    :param dirsrv: A directory server instance
    :type dirsrv: DirSrv
    :param force: A psychological aid, for people who think force means do something, harder. Does
        literally nothing in this program because state machines are a thing.
    :type force: bool
    """
    _log = dirsrv.log.getChild('remove_ds')
    _log.debug("Removing instance %s" % dirsrv.serverid)

    # Copy all the paths we are about to tamper with
    remove_paths = {}
    remove_paths['backup_dir'] = dirsrv.ds_paths.backup_dir
    remove_paths['cert_dir'] = dirsrv.ds_paths.cert_dir
    remove_paths['config_dir'] = dirsrv.ds_paths.config_dir
    remove_paths['db_dir'] = dirsrv.ds_paths.db_dir
    remove_paths['db_home_dir'] = dirsrv.ds_paths.db_home_dir
    remove_paths['db_dir_parent'] = dirsrv.ds_paths.db_dir + "/../"
    ### WARNING: The changelogdb isn't removed. we assume it's in:
    # db_dir ../changelogdb. So remove that too!
    # abspath will resolve the ".." down.
    remove_paths['changelogdb_dir'] = dirsrv.get_changelog_dir()
    remove_paths['ldif_dir'] = dirsrv.ds_paths.ldif_dir
    remove_paths['lock_dir'] = dirsrv.ds_paths.lock_dir
    remove_paths['log_dir'] = dirsrv.ds_paths.log_dir
    remove_paths['inst_dir'] = dirsrv.ds_paths.inst_dir
    remove_paths['etc_sysconfig'] = "%s/sysconfig/dirsrv-%s" % (dirsrv.ds_paths.sysconf_dir, dirsrv.serverid)
    remove_paths['ldapi'] = dirsrv.ds_paths.ldapi

    tmpfiles_d_path = dirsrv.ds_paths.tmpfiles_d + "/dirsrv-" + dirsrv.serverid + ".conf"

    # These are handled in a special way.
    dse_ldif_path = os.path.join(dirsrv.ds_paths.config_dir, 'dse.ldif')

    # Check the marker exists. If it *does not* warn about this, and say that to
    # force removal you should touch this file.

    _log.debug("Checking for instance marker at %s" % dse_ldif_path)
    if not os.path.exists(dse_ldif_path):
        _log.info("Instance configuration not found, no action will be taken")
        _log.info("If you want us to cleanup anyway, recreate '%s'" % dse_ldif_path)
        return
    _log.debug("Found instance marker at %s! Proceeding to remove ..." % dse_ldif_path)

    # Stop the instance (if running) and now we know it really does exist
    # and hopefully have permission to access it ...
    _log.debug("Stopping instance %s" % dirsrv.serverid)
    dirsrv.stop()

    _log.debug("Found instance marker at %s! Proceeding to remove ..." % dse_ldif_path)

    ### ANY NEW REMOVAL ACTION MUST BE BELOW THIS LINE!!!

    # Remove these paths:
    # for path in ('backup_dir', 'cert_dir', 'config_dir', 'db_dir',
    #             'ldif_dir', 'lock_dir', 'log_dir', 'run_dir'):
    for path_k in remove_paths:
        _log.debug("Removing %s" % remove_paths[path_k])
        shutil.rmtree(remove_paths[path_k], ignore_errors=True)

    # Remove parent (/var/lib/dirsrv/slapd-INST)
    shutil.rmtree(remove_paths['db_dir'].replace('db', ''), ignore_errors=True)

    # Remove /run/slapd-isntance
    try:
        os.remove(f'/run/slapd-{dirsrv.serverid}.socket')
    except OSError as e:
        _log.debug("Failed to remove socket file: " + str(e))

    # We can not assume we have systemd ...
    if dirsrv.ds_paths.with_systemd:
        # Remove the systemd symlink
        _log.debug("Removing the systemd symlink")

        result = subprocess.run(["systemctl", "disable", "dirsrv@{}".format(dirsrv.serverid)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        args = ' '.join(ensure_list_str(result.args))
        stdout = ensure_str(result.stdout)
        stderr = ensure_str(result.stderr)
        _log.debug(f"CMD: {args} ; STDOUT: {stdout} ; STDERR: {stderr}")

        _log.debug("Removing %s" % tmpfiles_d_path)
        try:
            os.remove(tmpfiles_d_path)
        except OSError as e:
            _log.debug("Failed to remove tmpfile: " + str(e))

    # Nor can we assume we have selinux. Try docker sometime ;)
    if dirsrv.ds_paths.with_selinux:
        # Remove selinux port label
        _log.debug("Removing the port labels")
        selinux_label_port(dirsrv.port, remove_label=True)

        # This is a compatability with ancient installs, all modern install have tls port
        if dirsrv.sslport is not None:
            selinux_label_port(dirsrv.sslport, remove_label=True)

    # If this was the last instance, remove the ssca instance
    # and all ds related selinux customizations
    insts = dirsrv.list(all=True)
    if len(insts) == 0:
        ssca = NssSsl(dbpath=dirsrv.get_ssca_dir())
        ssca.remove_db()
        selinux_clean_ports_label()
        selinux_clean_files_label(all=True)
    else:
        selinux_clean_files_label()

    ### ANY NEW REMOVAL ACTIONS MUST BE ABOVE THIS LINE!!!

    # Finally means FINALLY, the last thing, the LAST LAST thing. By doing this absolutely
    # last, it means that we can have any failure above, and continue to re-run until resolved
    # because this instance marker (dse.ldif) continues to exist!
    # Move the config_dir to config_dir.removed
    config_dir = dirsrv.ds_paths.config_dir
    config_dir_rm = "{}.removed".format(config_dir)

    if os.path.exists(config_dir_rm):
        _log.debug("Removing previously existed %s" % config_dir_rm)
        shutil.rmtree(config_dir_rm)

    assert_c(not os.path.exists(config_dir_rm))
    selinux_label_file(config_dir_rm, None)

    # That's it, everything before this MUST have suceeded, so now we can move the
    # config dir (containing dse.ldif, the marker) out of the way.
    _log.debug("Moving %s to %s" % (config_dir, config_dir_rm))
    try:
        shutil.move(config_dir, config_dir_rm)
    except FileNotFoundError:
        pass

    # DO NOT PUT ANY CODE BELOW THIS COMMENT BECAUSE THAT WOULD VIOLATE THE ASSERTIONS OF THE
    # ABOVE CODE.

    # Done!
    _log.debug("Complete")
