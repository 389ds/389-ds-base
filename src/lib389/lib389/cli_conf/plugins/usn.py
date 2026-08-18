# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import USNPlugin
from lib389.cli_conf import add_generic_plugin_parsers
from lib389.cli_base import CustomHelpFormatter


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
    log.info('Attempting to add task entry...')
    if not plugin.status():
        log.error("'%s' is disabled. Fix up task can't be executed" % plugin.rdn)
    task = plugin.cleanup(args.suffix, args.backend, args.max_usn)
    task.wait(timeout=args.timeout)
    exitcode = task.get_exit_code()
    if exitcode != 0:
        if exitcode is None:
            raise ValueError('USM tombstone cleanup task has not completed. Please, check logs')
        else:
            raise ValueError('USM tombstone cleanup task has failed. Please, check logs')
    else:
        log.info('Successfully added task entry')


def create_parser(subparsers):
    usn_parser = subparsers.add_parser('usn', help='Manage and configure USN plugin', formatter_class=CustomHelpFormatter)
    subcommands = usn_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, USNPlugin)

    global_mode_parser = subcommands.add_parser('global', help='Get or manage global USN mode (nsslapd-entryusn-global)', formatter_class=CustomHelpFormatter)
    global_mode_parser.set_defaults(func=display_usn_mode)
    global_mode_subcommands = global_mode_parser.add_subparsers(help='action')
    on_global_mode_parser = global_mode_subcommands.add_parser('on', help='Enables USN global mode', formatter_class=CustomHelpFormatter)
    on_global_mode_parser.set_defaults(func=enable_global_mode)
    off_global_mode_parser = global_mode_subcommands.add_parser('off', help='Disables USN global mode', formatter_class=CustomHelpFormatter)
    off_global_mode_parser.set_defaults(func=disable_global_mode)

    cleanup_parser = subcommands.add_parser('cleanup', help='Runs the USN tombstone cleanup task', formatter_class=CustomHelpFormatter)
    cleanup_parser.set_defaults(func=tombstone_cleanup)
    cleanup_group = cleanup_parser.add_mutually_exclusive_group(required=True)
    cleanup_group.add_argument('-s', '--suffix',
                               help='Sets the suffix or subtree in Directory Server to run the cleanup operation '
                                    'against. If the suffix is not specified, then the back end must be specified (suffix).')
    cleanup_group.add_argument('-n', '--backend',
                               help='Sets the Directory Server instance back end, or database, to run the cleanup '
                                    'operation against. If the back end is not specified, then the suffix must be '
                                    'specified. Backend instance in which USN tombstone entries (backend)')
    cleanup_parser.add_argument('-m', '--max-usn', type=int, help='Sets the highest USN value to delete when '
                                                                 'removing tombstone entries (max_usn_to_delete)')
    cleanup_parser.add_argument('--timeout', type=int, default=120,
                                help="Sets the cleanup task timeout.  Default is 120 seconds,")
