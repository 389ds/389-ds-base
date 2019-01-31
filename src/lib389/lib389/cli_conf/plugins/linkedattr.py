# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import LinkedAttributesPlugin
from lib389.cli_conf import add_generic_plugin_parsers


def fixup(inst, basedn, log, args):
    plugin = LinkedAttributesPlugin(inst)
    log.info('Attempting to add task entry... This will fail if LinkedAttributes plug-in is not enabled.')
    if not plugin.status():
        log.error("'%s' is disabled. Fix up task can't be executed" % plugin.rdn)
    fixup_task = plugin.fixup(args.basedn, args.filter)
    fixup_task.wait()
    exitcode = fixup_task.get_exit_code()
    if exitcode != 0:
        log.error('LinkedAttributes fixup task for %s has failed. Please, check logs')
    else:
        log.info('Successfully added fixup task')


def create_parser(subparsers):
    linkedattr_parser = subparsers.add_parser('linkedattr', help='Manage and configure Linked Attributes plugin')
    subcommands = linkedattr_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, LinkedAttributesPlugin)

    fixup_parser = subcommands.add_parser('fixup', help='Run the fix-up task for linked attributes plugin')
    fixup_parser.add_argument('basedn', help="basedn that contains entries to fix up")
    fixup_parser.add_argument('-f', '--filter', help='Filter for entries to fix up linked attributes.')
    fixup_parser.set_defaults(func=fixup)
