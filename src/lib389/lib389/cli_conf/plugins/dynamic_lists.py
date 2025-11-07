# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389.plugins import DynamicListsPlugin, DynamicListsConfig
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add
from lib389.cli_base import CustomHelpFormatter

arg_to_attr = {
    'objectclass': 'dynamicListObjectclass',
    'url_attr': 'dynamicListUrlAttr',
    'list_attr': 'dynamicListAttr'
}


def dynamic_lists_edit(inst, basedn, log, args):
    log = log.getChild('dynamic_lists_edit')
    plugin = DynamicListsPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def dynamic_lists_add_config(inst, basedn, log, args):
    log = log.getChild('dynamic_lists_add_config')
    targetdn = args.DN
    config = generic_object_add(DynamicListsConfig, inst, log, args, arg_to_attr, dn=targetdn)
    plugin = DynamicListsPlugin(inst)
    plugin.replace('nsslapd-pluginConfigArea', config.dn)
    log.info('Dynamic Lists attribute nsslapd-pluginConfigArea (config-entry) '
             'was set in the main plugin config')


def dynamic_lists_edit_config(inst, basedn, log, args):
    log = log.getChild('dynamic_lists_edit_config')
    targetdn = args.DN
    config = DynamicListsConfig(inst, targetdn)
    generic_object_edit(config, log, args, arg_to_attr)


def dynamic_lists_show_config(inst, basedn, log, args):
    log = log.getChild('dynamic_lists_show_config')
    targetdn = args.DN
    config = DynamicListsConfig(inst, targetdn)

    if not config.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % targetdn)
    if args and args.json:
        o_str = config.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(config.display())


def dynamic_lists_del_config(inst, basedn, log, args):
    log = log.getChild('dynamic_lists_del_config')
    targetdn = args.DN
    config = DynamicListsConfig(inst, targetdn)
    config.delete()
    # Now remove the attribute from the plugin
    plugin = DynamicListsPlugin(inst)
    plugin.remove_all('nsslapd-pluginConfigArea')
    log.info("Successfully deleted the %s", targetdn)


def _add_parser_args(parser):
    parser.add_argument('--objectclass',
                        help='Specifies the objectclass to identify entry that has a dynamic list (dynamicListObjectclass)')
    parser.add_argument('--url-attr',
                        help='Specifies the attribute that contains the URL of the dynamic list (dynamicListUrlAttr)')
    parser.add_argument('--list-attr',
                        help='Specifies the attribute used to store the values of the dynamic list. '
                             'The attribute must have a DN syntax (dynamicListAttr)')


def create_parser(subparsers):
    dynamic_lists = subparsers.add_parser('dynamic-lists',
                                      help='Manage and configure Dynamic Lists plugin')

    subcommands = dynamic_lists.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, DynamicListsPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin settings', formatter_class=CustomHelpFormatter)
    edit.set_defaults(func=dynamic_lists_edit)
    _add_parser_args(edit)
    edit.add_argument('--config-entry', help='The value to set as nsslapd-pluginConfigArea')

    config = subcommands.add_parser('config-entry', help='Manage the config entry', formatter_class=CustomHelpFormatter)
    config_subcommands = config.add_subparsers(help='action')
    add_config = config_subcommands.add_parser('add', help='Add the config entry', formatter_class=CustomHelpFormatter)
    add_config.set_defaults(func=dynamic_lists_add_config)
    add_config.add_argument('DN', help='The config entry full DN')
    _add_parser_args(add_config)
    edit_config = config_subcommands.add_parser('set', help='Edit the config entry', formatter_class=CustomHelpFormatter)
    edit_config.set_defaults(func=dynamic_lists_edit_config)
    edit_config.add_argument('DN', help='The config entry full DN')
    _add_parser_args(edit_config)
    show_config = config_subcommands.add_parser('show', help='Display the config entry', formatter_class=CustomHelpFormatter)
    show_config.set_defaults(func=dynamic_lists_show_config)
    show_config.add_argument('DN', help='The shared config entry full DN')
    del_config_ = config_subcommands.add_parser('delete',
                                                help='Delete the config entry and remove the reference in the plugin entry',
                                                formatter_class=CustomHelpFormatter)
    del_config_.set_defaults(func=dynamic_lists_del_config)
    del_config_.add_argument('DN', help='The config entry full DN')
