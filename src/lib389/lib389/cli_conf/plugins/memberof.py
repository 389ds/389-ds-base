# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import MemberOfPlugin, MemberOfSharedConfig, MemberOfFixupTasks
from lib389.utils import get_task_status
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add

arg_to_attr = {
    'initfunc': 'nsslapd-pluginInitfunc',
    'attr': 'memberOfAttr',
    'groupattr': 'memberOfGroupAttr',
    'allbackends': 'memberOfAllBackends',
    'skipnested': 'memberOfSkipNested',
    'scope': 'memberOfEntryScope',
    'exclude': 'memberOfEntryScopeExcludeSubtree',
    'autoaddoc': 'memberOfAutoAddOC',
    'config_entry': 'nsslapd-pluginConfigArea'
}


def memberof_edit(inst, basedn, log, args):
    log = log.getChild('memberof_edit')
    plugin = MemberOfPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def memberof_add_config(inst, basedn, log, args):
    log = log.getChild('memberof_add_config')
    targetdn = args.DN
    config = generic_object_add(MemberOfSharedConfig, inst, log, args, arg_to_attr, dn=targetdn)
    plugin = MemberOfPlugin(inst)
    plugin.replace('nsslapd-pluginConfigArea', config.dn)
    log.info('MemberOf attribute nsslapd-pluginConfigArea (config-entry) '
             'was set in the main plugin config')


def memberof_edit_config(inst, basedn, log, args):
    log = log.getChild('memberof_edit_config')
    targetdn = args.DN
    config = MemberOfSharedConfig(inst, targetdn)
    generic_object_edit(config, log, args, arg_to_attr)


def memberof_show_config(inst, basedn, log, args):
    log = log.getChild('memberof_show_config')
    targetdn = args.DN
    config = MemberOfSharedConfig(inst, targetdn)

    if not config.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % targetdn)
    if args and args.json:
        o_str = config.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(config.display())


def memberof_del_config(inst, basedn, log, args):
    log = log.getChild('memberof_del_config')
    targetdn = args.DN
    config = MemberOfSharedConfig(inst, targetdn)
    config.delete()
    log.info("Successfully deleted the %s", targetdn)


def do_fixup(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    log.info('Adding fixup task entry...')
    if not plugin.status():
        log.error("'%s' is disabled. Fix up task can't be executed" % plugin.rdn)
        return
    fixup_task = plugin.fixup(args.DN, args.filter)
    if args.wait:
        log.info(f'Waiting for fixup task "{fixup_task.dn}" to complete.  You can safely exit by pressing Control C ...')
        fixup_task.wait(timeout=args.timeout)
        exitcode = fixup_task.get_exit_code()
        if exitcode != 0:
            if existcode is None:
                raise ValueError(f'MemberOf fixup task "{fixup_task.dn}" for {args.DN} has not completed. Please, check logs')
            else:
                raise ValueError(f'MemberOf fixup task "{fixup_task.dn}" for {args.DN} has failed (error {exitcode}). Please, check logs')
        else:
            log.info('Fixup task successfully completed')
    else:
        log.info(f'Successfully added task entry "{fixup_task.dn}". This task is running in the background. To track its progress you can use the "fixup-status" command.')


def do_fixup_status(inst, basedn, log, args):
    get_task_status(inst, log, MemberOfFixupTasks, dn=args.dn, show_log=args.show_log,
                    watch=args.watch, use_json=args.json)


def _add_parser_args(parser):
    parser.add_argument('--attr',
                        help='Specifies the attribute in the user entry for the Directory Server '
                             'to manage to reflect group membership (memberOfAttr)')
    parser.add_argument('--groupattr', nargs='+',
                        help='Specifies the attribute in the group entry to use to identify '
                             'the DNs of group members (memberOfGroupAttr)')
    parser.add_argument('--allbackends', choices=['on', 'off'], type=str.lower,
                        help='Specifies whether to search the local suffix for user entries on '
                             'all available suffixes (memberOfAllBackends)')
    parser.add_argument('--skipnested', choices=['on', 'off'], type=str.lower,
                        help='Specifies whether to skip nested groups or not (memberOfSkipNested)')
    parser.add_argument('--scope', nargs='+', help='Specifies backends or multiple-nested suffixes '
                                                   'for the MemberOf plug-in to work on (memberOfEntryScope)')
    parser.add_argument('--exclude', nargs='+', help='Specifies backends or multiple-nested suffixes '
                                                     'for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)')
    parser.add_argument('--autoaddoc', type=str.lower,
                        help='If an entry does not have an object class that allows the memberOf attribute '
                             'then the memberOf plugin will automatically add the object class listed '
                             'in the memberOfAutoAddOC parameter')


def create_parser(subparsers):
    memberof = subparsers.add_parser('memberof', help='Manage and configure MemberOf plugin')

    subcommands = memberof.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, MemberOfPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin settings')
    edit.set_defaults(func=memberof_edit)
    _add_parser_args(edit)
    edit.add_argument('--config-entry', help='The value to set as nsslapd-pluginConfigArea')

    config = subcommands.add_parser('config-entry', help='Manage the config entry')
    config_subcommands = config.add_subparsers(help='action')
    add_config = config_subcommands.add_parser('add', help='Add the config entry')
    add_config.set_defaults(func=memberof_add_config)
    add_config.add_argument('DN', help='The config entry full DN')
    _add_parser_args(add_config)
    edit_config = config_subcommands.add_parser('set', help='Edit the config entry')
    edit_config.set_defaults(func=memberof_edit_config)
    edit_config.add_argument('DN', help='The config entry full DN')
    _add_parser_args(edit_config)
    show_config = config_subcommands.add_parser('show', help='Display the config entry')
    show_config.set_defaults(func=memberof_show_config)
    show_config.add_argument('DN', help='The config entry full DN')
    del_config_ = config_subcommands.add_parser('delete', help='Delete the config entry')
    del_config_.set_defaults(func=memberof_del_config)
    del_config_.add_argument('DN', help='The config entry full DN')

    fixup = subcommands.add_parser('fixup', help='Run the fix-up task for memberOf plugin')
    fixup.set_defaults(func=do_fixup)
    fixup.add_argument('DN', help="Base DN that contains entries to fix up")
    fixup.add_argument('-f', '--filter',
                       help='Filter for entries to fix up.\n If omitted, all entries with objectclass '
                            'inetuser/inetadmin/nsmemberof under the specified base will have '
                            'their memberOf attribute regenerated.')
    fixup.add_argument('--wait', action='store_true',
                       help="Wait for the task to finish, this could take a long time")
    fixup.add_argument('--timeout', type=int, default=0,
                        help="Sets the task timeout. ,Default is 0 (no timeout)")

    fixup_status = subcommands.add_parser('fixup-status', help='Check the status of a fix-up task')
    fixup_status.set_defaults(func=do_fixup_status)
    fixup_status.add_argument('--dn', help="The task entry's DN")
    fixup_status.add_argument('--show-log', action='store_true', help="Display the task log")
    fixup_status.add_argument('--watch', action='store_true',
                       help="Watch the task's status and wait for it to finish")
