# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import USNPlugin
from lib389.cli_conf import add_generic_plugin_parsers


def display_usn_mode(inst, basedn, log, args):
    plugin = USNPlugin(inst)
    if plugin.is_global_mode_set():
        log.info("USN global mode is enabled")
    else:
        log.info("USN global mode is disabled")

def enable_global_mode(inst, basedn, log, args):
    plugin = USNPlugin(inst)
    plugin.enable_global_mode()
    log.info("USN global mode enabled")

def disable_global_mode(inst, basedn, log, args):
    plugin = USNPlugin(inst)
    plugin.disable_global_mode()
    log.info("USN global mode disabled")

def tombstone_cleanup(inst, basedn, log, args):
    plugin = USNPlugin(inst)
    log.info('Attempting to add task entry... This will fail if replication is enabled or if USN plug-in is disabled.')
    task = plugin.cleanup(args.suffix, args.backend, args.maxusn)
    log.info('Successfully added task entry ' + task.dn)

def create_parser(subparsers):
    usn_parser = subparsers.add_parser('usn', help='Manage and configure USN plugin')

    subcommands = usn_parser.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, USNPlugin)

    global_mode_parser = subcommands.add_parser('global', help='get or manage global usn mode')
    global_mode_parser.set_defaults(func=display_usn_mode)
    global_mode_subcommands = global_mode_parser.add_subparsers(help='action')
    on_global_mode_parser = global_mode_subcommands.add_parser('on', help='enable usn global mode')
    on_global_mode_parser.set_defaults(func=enable_global_mode)
    off_global_mode_parser = global_mode_subcommands.add_parser('off', help='disable usn global mode')
    off_global_mode_parser.set_defaults(func=disable_global_mode)

    cleanup_parser = subcommands.add_parser('cleanup', help='run the USN tombstone cleanup task')
    cleanup_parser.set_defaults(func=tombstone_cleanup)
    cleanup_group = cleanup_parser.add_mutually_exclusive_group(required=True)
    cleanup_group.add_argument('-s', '--suffix', help="suffix where USN tombstone entries are cleaned up")
    cleanup_group.add_argument('-n', '--backend', help="backend instance in which USN tombstone entries are cleaned up (alternative to suffix)")
    cleanup_parser.add_argument('-m', '--maxusn', type=int, help="USN tombstone entries are deleted up to the entry with maxusn")
