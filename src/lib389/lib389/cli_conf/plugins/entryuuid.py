# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 William Brown <william@blackhats.net.au>
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


from lib389.plugins import EntryUUIDPlugin, EntryUUIDFixupTasks
from lib389.cli_conf import add_generic_plugin_parsers
from lib389.utils import get_task_status
from lib389.cli_base import CustomHelpFormatter


def do_fixup(inst, basedn, log, args):
    plugin = EntryUUIDPlugin(inst)
    log.info('Attempting to add task entry...')
    if not plugin.status():
        log.error("'%s' is disabled. Fix up task can't be executed" % plugin.rdn)
        return
    fixup_task = plugin.fixup(args.DN, args.filter)
    if args.wait:
        log.info(f'Waiting for fixup task "{fixup_task.dn}" to complete.  You can safely exit by pressing Control C ...')
        fixup_task.wait(timeout=args.timeout)
        exitcode = fixup_task.get_exit_code()
        if exitcode != 0:
            if exitcode is None:
                raise ValueError(f'EntryUUID fixup task "{fixup_task.dn}" for {args.DN} has not completed. Please, check logs')
            else:
                raise ValueError(f'EntryUUID fixup task "{fixup_task.dn}" for {args.DN} has failed (error {exitcode}). Please, check logs')
        else:
            log.info('Fixup task successfully completed')
    else:
        log.info(f'Successfully added task entry "{fixup_task.dn}". This task is running in the background. To track its progress you can use the "fixup-status" command.')


def do_fixup_status(inst, basedn, log, args):
    get_task_status(inst, log, EntryUUIDFixupTasks, dn=args.dn, show_log=args.show_log,
                    watch=args.watch, use_json=args.json)


def create_parser(subparsers):
    referint = subparsers.add_parser('entryuuid', help='Manage and configure EntryUUID plugin', formatter_class=CustomHelpFormatter)
    subcommands = referint.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, EntryUUIDPlugin)

    fixup = subcommands.add_parser('fixup', help='Run the fix-up task for EntryUUID plugin', formatter_class=CustomHelpFormatter)
    fixup.set_defaults(func=do_fixup)
    fixup.add_argument('DN', help="Base DN that contains entries to fix up")
    fixup.add_argument('-f', '--filter',
                       help='Filter for entries to fix up.\n If omitted, all entries under base DN'
                            'will have their EntryUUID attribute regenerated if not present.')
    fixup.add_argument('--wait', action='store_true',
                       help="Wait for the task to finish, this could take a long time")
    fixup.add_argument('--timeout', type=int, default=0,
                       help="Sets the task timeout. Default is 0 (no timeout)")

    fixup_status = subcommands.add_parser('fixup-status', help='Check the status of a fix-up task', formatter_class=CustomHelpFormatter)
    fixup_status.set_defaults(func=do_fixup_status)
    fixup_status.add_argument('--dn', help="The task entry's DN")
    fixup_status.add_argument('--show-log', action='store_true', help="Display the task log")
    fixup_status.add_argument('--watch', action='store_true',
                       help="Watch the task's status and wait for it to finish")
