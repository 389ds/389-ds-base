# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389.plugins import MemberOfPlugin, MemberOfSharedConfig, MemberOfFixupTasks
from lib389.utils import get_task_status
from lib389.cli_conf import (
    add_generic_plugin_parsers,
    generic_object_edit,
    generic_object_add,
    generic_object_add_attr,
    generic_object_del_attr,
)
from lib389.cli_base import CustomHelpFormatter

arg_to_attr = {
    'initfunc': 'nsslapd-pluginInitfunc',
    'attr': 'memberOfAttr',
    'groupattr': 'memberOfGroupAttr',
    'allbackends': 'memberOfAllBackends',
    'skipnested': 'memberOfSkipNested',
    'scope': 'memberOfEntryScope',
    'exclude': 'memberOfEntryScopeExcludeSubtree',
    'autoaddoc': 'memberOfAutoAddOC',
    'deferredupdate': 'memberOfDeferredUpdate',
    'launchfixup': 'memberOfLaunchFixup',
    'config_entry': 'nsslapd-pluginConfigArea',
    'specific_group_filter': 'memberOfSpecificGroupFilter',
    'exclude_specific_group_filter': 'memberOfExcludeSpecificGroupFilter',
    'specific_group_oc': 'memberOfSpecificGroupOC',
}


def memberof_edit(inst, basedn, log, args):
    log = log.getChild('memberof_edit')
    plugin = MemberOfPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def memberof_add_attr(inst, basedn, log, args):
    log = log.getChild('memberof_add_attr')
    plugin = MemberOfPlugin(inst)
    generic_object_add_attr(plugin, log, args, arg_to_attr)


def memberof_del_attr(inst, basedn, log, args):
    log = log.getChild('memberof_del_attr')
    plugin = MemberOfPlugin(inst)
    generic_object_del_attr(plugin, log, args, arg_to_attr)


def memberof_add_attr_config(inst, basedn, log, args):
    log = log.getChild('memberof_add_attr_config')
    config = MemberOfSharedConfig(inst, args.DN)
    del args.DN
    generic_object_add_attr(config, log, args, arg_to_attr)


def memberof_del_attr_config(inst, basedn, log, args):
    log = log.getChild('memberof_del_attr_config')
    config = MemberOfSharedConfig(inst, args.DN)
    del args.DN
    generic_object_del_attr(config, log, args, arg_to_attr)


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
    # Now remove the attribute from the memberOf plugin
    plugin = MemberOfPlugin(inst)
    plugin.remove_all('nsslapd-pluginConfigArea')
    log.info("Successfully deleted %s", targetdn)


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
            if exitcode is None:
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


def _set_attr_parser_args(parser):
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
    parser.add_argument('--deferredupdate', choices=['on', 'off'], type=str.lower,
                        help='Specifies that the updates of the members are done after the completion '
                             'of the update of the target group. In addition each update (group/members) '
                             'uses its own transaction')
    parser.add_argument('--launchfixup', choices=['on', 'off'], type=str.lower,
                        help='Specify that if the server disorderly shutdown (crash, kill,..) then '
                             'at restart the memberof fixup task is launched automatically')
    parser.add_argument('--specific-group-oc', nargs='+',
                        help='Set objectclasses for the specific groups to include/exclude. '
                              'Otherwise all other groups will be excluded. Note, this replaces '
                              'all existing values. To add a new value, use the "add-attr" command. '
                              '(memberOfSpecificGroupOC)')
    parser.add_argument('--specific-group-filter', nargs='+',
                        help='Specifies a filter for groups to include. Otherwise all other groups will be excluded (memberOfSpecificGroupFilter)')
    parser.add_argument('--exclude-specific-group-filter', nargs='+',
                        help='Specifies a filter for groups to exclude. Otherwise all other groups will be included. (memberOfExcludeSpecificGroupFilter)')

def _add_attr_parser_args(parser):
    parser.add_argument('--specific-group-filter', nargs='+',
                        help='Specifies a filter for groups to include. This adds to the existing values. Otherwise all other groups will be excluded (memberOfSpecificGroupFilter)')
    parser.add_argument('--exclude-specific-group-filter', nargs='+',
                        help='Specifies a filter for groups to exclude. This adds to the existing values. Otherwise all other groups will be included (memberOfExcludeSpecificGroupFilter)')

def _del_attr_parser_args(parser):
    parser.add_argument('--specific-group-filter', nargs='+',
                        help='Removes a filter for groups to include. This removes the value from the existing values. (memberOfSpecificGroupFilter)')
    parser.add_argument('--exclude-specific-group-filter', nargs='+',
                        help="Removes a filter for groups to exclude. This removes the value from the existing values. (memberOfExcludeSpecificGroupFilter)")

def create_parser(subparsers):
    memberof = subparsers.add_parser('memberof', help='Manage and configure MemberOf plugin', formatter_class=CustomHelpFormatter)

    subcommands = memberof.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, MemberOfPlugin)

    # Core plugin
    edit = subcommands.add_parser('set', help='Edit the plugin settings', formatter_class=CustomHelpFormatter)
    edit.set_defaults(func=memberof_edit)
    _set_attr_parser_args(edit)
    edit.add_argument('--config-entry', help='The value to set as nsslapd-pluginConfigArea')

    add_attr = subcommands.add_parser('add-attr', help='Add attributes to the plugin', formatter_class=CustomHelpFormatter)
    add_attr.set_defaults(func=memberof_add_attr)
    _add_attr_parser_args(add_attr)

    del_attr = subcommands.add_parser('del-attr', help='Delete attributes from the plugin', formatter_class=CustomHelpFormatter)
    del_attr.set_defaults(func=memberof_del_attr)
    _del_attr_parser_args(del_attr)

    # Shared config entry
    config = subcommands.add_parser('config-entry', help='Manage the config entry', formatter_class=CustomHelpFormatter)
    config_subcommands = config.add_subparsers(help='action')
    add_config = config_subcommands.add_parser('add', help='Add the config entry', formatter_class=CustomHelpFormatter)
    add_config.set_defaults(func=memberof_add_config)
    add_config.add_argument('DN', help='The config entry full DN')
    _set_attr_parser_args(add_config)
    edit_config = config_subcommands.add_parser('set', help='Edit the config entry', formatter_class=CustomHelpFormatter)
    edit_config.set_defaults(func=memberof_edit_config)
    edit_config.add_argument('DN', help='The config entry full DN')
    _set_attr_parser_args(edit_config)

    add_attr_config = config_subcommands.add_parser('add-attr', help='Add attributes to the config entry',
                                                    formatter_class=CustomHelpFormatter)
    add_attr_config.set_defaults(func=memberof_add_attr_config)
    add_attr_config.add_argument('DN', help='The config entry full DN')
    _add_attr_parser_args(add_attr_config)

    del_attr_config = config_subcommands.add_parser('del-attr', help='Delete attributes from the config entry',
                                                    formatter_class=CustomHelpFormatter)
    del_attr_config.set_defaults(func=memberof_del_attr_config)
    del_attr_config.add_argument('DN', help='The config entry full DN')
    _del_attr_parser_args(del_attr_config)

    show_config = config_subcommands.add_parser('show', help='Display the config entry', formatter_class=CustomHelpFormatter)
    show_config.set_defaults(func=memberof_show_config)
    show_config.add_argument('DN', help='The config entry full DN')
    del_config_ = config_subcommands.add_parser('delete',
                                                help='Delete the config entry and remove the reference in the plugin',
                                                formatter_class=CustomHelpFormatter)
    del_config_.set_defaults(func=memberof_del_config)
    del_config_.add_argument('DN', help='The config entry full DN')

    # Tasks
    fixup = subcommands.add_parser('fixup', help='Run the fix-up task for memberOf plugin', formatter_class=CustomHelpFormatter)
    fixup.set_defaults(func=do_fixup)
    fixup.add_argument('DN', help="Base DN that contains entries to fix up")
    fixup.add_argument('-f', '--filter',
                       help='Filter for entries to fix up.\n If omitted, all entries with objectclass '
                            'inetuser/inetadmin/nsmemberof under the specified base will have '
                            'their memberOf attribute regenerated.')
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
