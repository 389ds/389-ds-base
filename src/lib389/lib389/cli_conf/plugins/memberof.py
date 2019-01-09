# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389.plugins import MemberOfPlugin, Plugins, MemberOfSharedConfig
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
    plugins = Plugins(inst)
    plugin = plugins.get("MemberOf Plugin")
    generic_object_edit(plugin, log, args, arg_to_attr)


def memberof_add_config(inst, basedn, log, args):
    log = log.getChild('memberof_add_config')
    targetdn = args.DN
    config = MemberOfSharedConfig(inst, targetdn)
    generic_object_add(config, log, args, arg_to_attr)
    plugins = Plugins(inst)
    plugin = plugins.get("MemberOf Plugin")
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
        print(o_str)
    else:
        print(config.display())


def memberof_del_config(inst, basedn, log, args):
    log = log.getChild('memberof_del_config')
    targetdn = args.DN
    config = MemberOfSharedConfig(inst, targetdn)
    config.delete()
    log.info("Successfully deleted the %s", targetdn)


def fixup(inst, basedn, log, args):
    plugin = MemberOfPlugin(inst)
    log.info('Attempting to add task entry... This will fail if MemberOf plug-in is not enabled.')
    assert plugin.status(), "'%s' is disabled. Fix up task can't be executed" % plugin.rdn
    fixup_task = plugin.fixup(args.DN, args.filter)
    fixup_task.wait()
    exitcode = fixup_task.get_exit_code()
    assert exitcode == 0, 'MemberOf fixup task for %s has failed. Please, check logs'
    log.info('Successfully added task entry for %s', args.DN)


def _add_parser_args(parser):
    parser.add_argument('--attr', nargs='+', help='The value to set as memberOfAttr')
    parser.add_argument('--groupattr', nargs='+', help='The value to set as memberOfGroupAttr')
    parser.add_argument('--allbackends', choices=['on', 'off'], type=str.lower,
                        help='The value to set as memberOfAllBackends')
    parser.add_argument('--skipnested', choices=['on', 'off'], type=str.lower,
                        help='The value to set as memberOfSkipNested')
    parser.add_argument('--scope', help='The value to set as memberOfEntryScope')
    parser.add_argument('--exclude', help='The value to set as memberOfEntryScopeExcludeSubtree')
    parser.add_argument('--autoaddoc', type=str.lower, help='The value to set as memberOfAutoAddOC')


def create_parser(subparsers):
    memberof_parser = subparsers.add_parser('memberof', help='Manage and configure MemberOf plugin')

    subcommands = memberof_parser.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, MemberOfPlugin)

    edit_parser = subcommands.add_parser('edit', help='Edit the plugin')
    edit_parser.set_defaults(func=memberof_edit)
    _add_parser_args(edit_parser)
    edit_parser.add_argument('--config-entry', help='The value to set as nsslapd-pluginConfigArea')

    config_parser = subcommands.add_parser('config-entry', help='Manage the config entry')
    config_subcommands = config_parser.add_subparsers(help='action')
    add_config_parser = config_subcommands.add_parser('add', help='Add the config entry')
    add_config_parser.set_defaults(func=memberof_add_config)
    add_config_parser.add_argument('DN', help='The config entry full DN')
    _add_parser_args(add_config_parser)
    edit_config_parser = config_subcommands.add_parser('edit', help='Edit the config entry')
    edit_config_parser.set_defaults(func=memberof_edit_config)
    edit_config_parser.add_argument('DN', help='The config entry full DN')
    _add_parser_args(edit_config_parser)
    show_config_parser = config_subcommands.add_parser('show', help='Display the config entry')
    show_config_parser.set_defaults(func=memberof_show_config)
    show_config_parser.add_argument('DN', help='The config entry full DN')
    del_config_parser = config_subcommands.add_parser('delete', help='Delete the config entry')
    del_config_parser.set_defaults(func=memberof_del_config)
    del_config_parser.add_argument('DN', help='The config entry full DN')

    fixup_parser = subcommands.add_parser('fixup', help='Run the fix-up task for memberOf plugin')
    fixup_parser.set_defaults(func=fixup)
    fixup_parser.add_argument('DN', help="base DN that contains entries to fix up")
    fixup_parser.add_argument('-f', '--filter',
                              help='Filter for entries to fix up.\n If omitted, all entries with objectclass '
                                   'inetuser/inetadmin/nsmemberof under the specified base will have '
                                   'their memberOf attribute regenerated.')
