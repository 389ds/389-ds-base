# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
from lib389.plugins import EntryUUIDPlugin
from lib389.cli_base.dsrc import dsrc_to_ldap, dsrc_arg_concat
from lib389._constants import DSRC_HOME
from lib389.cli_base import connect_instance, disconnect_instance


def get_connected_inst(inst, log, args):
    # update the args for connect_instance()
    args.basedn = None
    args.binddn = None
    args.bindpw = None
    args.starttls = None
    args.pwdfile = None
    args.prompt = False
    new_inst = inst
    dsrc_inst = dsrc_to_ldap(DSRC_HOME, args.instance, log.getChild('dsrc'))
    dsrc_inst = dsrc_arg_concat(args, dsrc_inst)
    try:
        new_inst = connect_instance(dsrc_inst=dsrc_inst, verbose=args.verbose, args=args)
    except Exception:
        # just assume offline
        pass
    return new_inst

def install_plugin(inst, log, args):
    """Add the plugin entries to cn=config"""

    # This can only be done if the plugin package is installed
    plugin_dir = inst.get_plugin_dir()
    if not os.path.exists(plugin_dir + "/libentryuuid-plugin.so"):
        # Not found
        raise ValueError("The '389-ds-base-entryuuid' package must be installed before the plugin configuration can be added to the server.")

    inst = get_connected_inst(inst, log, args)
    entryuuid_plugin = EntryUUIDPlugin(inst)
    entryuuid_plugin.install()

    disconnect_instance(inst)

    if args.restart:
        inst.restart()

    log.info("Successfully added the Entry UUID plugin to the configuration.")


def uninstall_plugin(inst, log, args):
    """Remove the entryuuid plugin entries"""
    inst = get_connected_inst(inst, log, args)
    entryuuid_plugin = EntryUUIDPlugin(inst)
    entryuuid_plugin.uninstall()
    if args.restart:
        inst.restart()

    log.info("Successfully removed the Entry UUID plugin from the configuration.")


def create_parser(subparsers):
    entryuuid_parser = subparsers.add_parser('entryuuid', help="Manage the EntryUUID plugins")
    subcommands = entryuuid_parser.add_subparsers(help="action")

    entryuuid_install = subcommands.add_parser('install', help="Create the EntryUUID plugin entry under cn=plugins,cn=config")
    entryuuid_install.add_argument('--restart', help="Restart the server after adding the Entry UUID plugin",
                                   default=False, action='store_true')
    entryuuid_install.set_defaults(func=install_plugin)

    entryuuid_uninstall = subcommands.add_parser('uninstall', help="Remove the EntryUUID plugin entry under cn=plugins,cn=config")
    entryuuid_uninstall.add_argument('--restart', help="Restart the server after removing the Entry UUID plugin",
                                     default=False, action='store_true')
    entryuuid_uninstall.set_defaults(func=uninstall_plugin)
