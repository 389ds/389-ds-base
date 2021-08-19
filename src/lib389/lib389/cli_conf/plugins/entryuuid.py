# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389.plugins import EntryUUIDPlugin
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add

def do_fixup(inst, basedn, log, args):
    plugin = EntryUUIDPlugin(inst)
    log.info('Attempting to add task entry...')
    if not plugin.status():
        log.error("'%s' is disabled. Fix up task can't be executed" % plugin.rdn)
        return
    fixup_task = plugin.fixup(args.DN, args.filter)
    fixup_task.wait()
    exitcode = fixup_task.get_exit_code()
    if exitcode != 0:
        log.error('EntryUUID fixup task has failed. Please, check the error log for more - %s' % exitcode)
    else:
        log.info('Successfully added task entry')

def create_parser(subparsers):
    referint = subparsers.add_parser('entryuuid', help='Manage and configure EntryUUID plugin')
    subcommands = referint.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, EntryUUIDPlugin)

    fixup = subcommands.add_parser('fixup', help='Run the fix-up task for EntryUUID plugin')
    fixup.set_defaults(func=do_fixup)
    fixup.add_argument('DN', help="Base DN that contains entries to fix up")
    fixup.add_argument('-f', '--filter',
                       help='Filter for entries to fix up.\n If omitted, all entries under base DN'
                            'will have their EntryUUID attribute regenerated if not present.')

