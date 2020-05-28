# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from lib389.plugins import Plugin, Plugins
from lib389.utils import ensure_dict_str
from lib389.cli_base import (
    _generic_get,
    _get_arg,
)
from lib389.cli_conf import generic_object_edit
from lib389.cli_conf.plugins import memberof as cli_memberof
from lib389.cli_conf.plugins import usn as cli_usn
from lib389.cli_conf.plugins import rootdn_ac as cli_rootdn_ac
from lib389.cli_conf.plugins import referint as cli_referint
from lib389.cli_conf.plugins import accountpolicy as cli_accountpolicy
from lib389.cli_conf.plugins import attruniq as cli_attruniq
from lib389.cli_conf.plugins import dna as cli_dna
from lib389.cli_conf.plugins import linkedattr as cli_linkedattr
from lib389.cli_conf.plugins import managedentries as cli_managedentries
from lib389.cli_conf.plugins import passthroughauth as cli_passthroughauth
from lib389.cli_conf.plugins import retrochangelog as cli_retrochangelog
from lib389.cli_conf.plugins import automember as cli_automember
from lib389.cli_conf.plugins import posix_winsync as cli_posix_winsync
from lib389.cli_conf.plugins import contentsync as cli_contentsync

SINGULAR = Plugin
MANY = Plugins
RDN = 'cn'

arg_to_attr = {
    'initfunc': 'nsslapd-pluginInitfunc',
    'enabled': 'nsslapd-pluginEnabled',
    'path': 'nsslapd-pluginPath',
    'type': 'nsslapd-pluginType',
    'id': 'nsslapd-pluginId',
    'version': 'nsslapd-pluginVersion',
    'vendor': 'nsslapd-pluginVendor',
    'description': 'nsslapd-pluginDescription',
    'depends_on_type': 'nsslapd-plugin-depends-on-type',
    'depends_on_named': 'nsslapd-plugin-depends-on-named',
    'precedence': 'nsslapd-pluginPrecedence'
}


def plugin_list(inst, basedn, log, args):
    plugin_log = log.getChild('plugin_list')
    mc = MANY(inst, basedn)
    plugins = mc.list()
    if len(plugins) == 0:
        if args and args.json:
            print(json.dumps({"type": "list", "items": []}, indent=4))
        else:
            plugin_log.info("No objects to display")
    elif len(plugins) > 0:
        # We might sort this in the future
        if args and args.json:
            json_result = {"type": "list", "items": []}
        for plugin in plugins:
            plugin_data = ensure_dict_str(dict(plugin.get_all_attrs()))
            if args and args.json:
                json_result['items'].append(plugin_data)
            else:
                plugin_log.info(plugin_data["cn"][0])
        if args and args.json:
            print(json.dumps(json_result, indent=4))


def plugin_get(inst, basedn, log, args):
    rdn = _get_arg(args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('plugin_get'), MANY, rdn, args)


def vaidate_args(plugin, attr_arg_list):
    """Check if the attribute needs to be changed
    Return mods for the replace_many() method
    """

    mods = []
    for attr_name, arg in attr_arg_list.items():
        if arg is not None and plugin.get_attr_val_utf8_l(attr_name) != arg.lower():
            mods.append((attr_name, arg))
    return mods


def plugin_edit(inst, basedn, log, args):
    log = log.getChild('plugin_edit')
    rdn = _get_arg(args.selector, msg="Enter %s to retrieve" % RDN)
    plugins = Plugins(inst)
    plugin = plugins.get(rdn)
    generic_object_edit(plugin, log, args, arg_to_attr)


def create_parser(subparsers):
    plugin_parser = subparsers.add_parser('plugin', help="Manage plugins available on the server")

    subcommands = plugin_parser.add_subparsers(help="Plugins")

    cli_memberof.create_parser(subcommands)
    cli_automember.create_parser(subcommands)
    cli_referint.create_parser(subcommands)
    cli_rootdn_ac.create_parser(subcommands)
    cli_usn.create_parser(subcommands)
    cli_accountpolicy.create_parser(subcommands)
    cli_attruniq.create_parser(subcommands)
    cli_dna.create_parser(subcommands)
    cli_linkedattr.create_parser(subcommands)
    cli_managedentries.create_parser(subcommands)
    cli_passthroughauth.create_parser(subcommands)
    cli_retrochangelog.create_parser(subcommands)
    cli_posix_winsync.create_parser(subcommands)
    cli_contentsync.create_parser(subcommands)

    list_parser = subcommands.add_parser('list', help="List current configured (enabled and disabled) plugins")
    list_parser.set_defaults(func=plugin_list)

    get_parser = subcommands.add_parser('show', help='Show the plugin data')
    get_parser.set_defaults(func=plugin_get)
    get_parser.add_argument('selector', nargs='?', help='The plugin to search for')

    edit_parser = subcommands.add_parser('set', help='Edit the plugin')
    edit_parser.set_defaults(func=plugin_edit)
    edit_parser.add_argument('selector', nargs='?', help='The plugin to edit')
    edit_parser.add_argument('--type', help='The type of plugin.')
    edit_parser.add_argument('--enabled', choices=['on', 'off'],
                             help='Identifies whether or not the plugin is enabled.')
    edit_parser.add_argument('--path', help='The plugin library name (without the library suffix).')
    edit_parser.add_argument('--initfunc', help='An initialization function of the plugin.')
    edit_parser.add_argument('--id', help='The plugin ID.')
    edit_parser.add_argument('--vendor', help='The vendor of plugin.')
    edit_parser.add_argument('--version', help='The version of plugin.')
    edit_parser.add_argument('--description', help='The description of the plugin.')
    edit_parser.add_argument('--depends-on-type',
                             help='All plug-ins with a type value which matches one of the values '
                                  'in the following valid range will be started by the server prior to this plug-in.')
    edit_parser.add_argument('--depends-on-named',
                             help='The plug-in name matching one of the following values will be '
                                  'started by the server prior to this plug-in')
    edit_parser.add_argument('--precedence', help='The priority it has in the execution order of plug-ins')
