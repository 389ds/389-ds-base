# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import POSIXWinsyncPlugin
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit

arg_to_attr = {
    'create_memberof_task': 'posixWinsyncCreateMemberOfTask',
    'lower_case_uid': 'posixWinsyncLowerCaseUID',
    'map_member_uid': 'posixWinsyncMapMemberUID',
    'map_nested_grouping': 'posixWinsyncMapNestedGrouping',
    'ms_sfu_schema': 'posixWinsyncMsSFUSchema'
}


def winsync_edit(inst, basedn, log, args):
    log = log.getChild('winsync_edit')
    plugin = POSIXWinsyncPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def do_fixup(inst, basedn, log, args):
    plugin = POSIXWinsyncPlugin(inst)
    log.info('Attempting to add task entry...')
    if not plugin.status():
        log.error(f"'{plugin.rdn}' is disabled. Fix up task can't be executed")
        return
    fixup_task = plugin.fixup(args.DN, args.filter)
    fixup_task.wait()
    exitcode = fixup_task.get_exit_code()
    if exitcode != 0:
        log.error(f'MemberUID task for {args.DN} has failed. Please, check logs')
    else:
        log.info('Successfully added task entry')


def _add_parser_args(parser):
    parser.add_argument('--create-memberof-task', choices=['true', 'false'], type=str.lower,
                        help='Sets whether to run the memberUID fix-up task immediately after a sync run in order '
                             'to update group memberships for synced users (posixWinsyncCreateMemberOfTask)')
    parser.add_argument('--lower-case-uid', choices=['true', 'false'], type=str.lower,
                        help='Sets whether to store (and, if necessary, convert) the UID value in the memberUID '
                             'attribute in lower case.(posixWinsyncLowerCaseUID)')
    parser.add_argument('--map-member-uid', choices=['true', 'false'], type=str.lower,
                        help='Sets whether to map the memberUID attribute in an Active Directory group to '
                             'the uniqueMember attribute in a Directory Server group (posixWinsyncMapMemberUID)')
    parser.add_argument('--map-nested-grouping', choices=['true', 'false'], type=str.lower,
                        help='Manages if nested groups are updated when memberUID attributes in '
                             'an Active Directory POSIX group change (posixWinsyncMapNestedGrouping)')
    parser.add_argument('--ms-sfu-schema', choices=['true', 'false'], type=str.lower,
                        help='Sets whether to the older Microsoft System Services for Unix 3.0 (msSFU30) '
                             'schema when syncing Posix attributes from Active Directory (posixWinsyncMsSFUSchema)')


def create_parser(subparsers):
    winsync = subparsers.add_parser('posix-winsync', help='Manage and configure The Posix Winsync API plugin')
    subcommands = winsync.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, POSIXWinsyncPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin')
    edit.set_defaults(func=winsync_edit)
    _add_parser_args(edit)

    fixup = subcommands.add_parser('fixup', help='Run the memberOf fix-up task to correct mismatched member and uniquemember values for synced users')
    fixup.set_defaults(func=do_fixup)
    fixup.add_argument('DN', help="Base DN that contains entries to fix up")
    fixup.add_argument('-f', '--filter',
                       help='Filter for entries to fix up.\n If omitted, all entries with objectclass '
                            'inetuser/inetadmin/nsmemberof under the specified base will have '
                            'their memberOf attribute regenerated.')
