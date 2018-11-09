# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from lib389.plugins import Plugin, Plugins
from lib389.utils import ensure_dict_str
from lib389.cli_base import (
    _generic_list,
    _generic_get,
    _generic_get_dn,
    _generic_create,
    _generic_delete,
    _get_arg,
    _get_args,
    _get_attributes,
    _warn,
    )

SINGULAR = Plugin
MANY = Plugins
RDN = 'cn'


def plugin_list(inst, basedn, log, args):
    plugin_log = log.getChild('plugin_list')
    mc = MANY(inst, basedn)
    plugins = mc.list()
    if len(plugins) == 0:
        if args and args.json:
            print(json.dumps({"type": "list", "items": []}))
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
                plugin_log.info(plugin_data)
        if args and args.json:
            print(json.dumps(json_result))


def plugin_get(inst, basedn, log, args):
    rdn = _get_arg(args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('plugin_get'), MANY, rdn, args)


def plugin_get_dn(inst, basedn, log, args):
    dn = _get_arg(args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('plugin_get_dn'), MANY, dn, args)


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

    if args.enabled is not None and args.enabled.lower() not in ["on", "off"]:
        raise ValueError("Plugin enabled argument should be 'on' or 'off'")

    plugin_args = {'nsslapd-pluginInitfunc': args.initfunc,
                   'nsslapd-pluginEnabled': args.enabled,
                   'nsslapd-pluginPath': args.path,
                   'nsslapd-pluginType': args.type,
                   'nsslapd-pluginId': args.id,
                   'nsslapd-pluginVersion': args.version,
                   'nsslapd-pluginVendor': args.vendor,
                   'nsslapd-pluginDescription': args.description}
    mods = vaidate_args(plugin, plugin_args)

    if len(mods) > 0:
        plugin.replace_many(*mods)
        log.info("Successfully changed the plugin %s", rdn)
    else:
        raise ValueError("Nothing to change")


# Plugin enable
def plugin_enable(inst, basedn, log, args):
    dn = _get_arg(args.dn, msg="Enter plugin dn to enable")
    mc = MANY(inst, basedn)
    o = mc.get(dn=dn)
    o.enable()
    o_str = o.display()
    print('Enabled %s', o_str)


# Plugin disable
def plugin_disable(inst, basedn, log, args, warn=True):
    dn = _get_arg(args.dn, msg="Enter plugin dn to disable")
    if warn:
        _warn(dn, msg="Disabling %s %s" % (SINGULAR.__name__, dn))
    mc = MANY(inst, basedn)
    o = mc.get(dn=dn)
    o.disable()
    o_str = o.display()
    print('Disabled %s', o_str)


# Plugin configure?
def plugin_configure(inst, basedn, log, args):
    pass


def generic_show(inst, basedn, log, args):
    """Display plugin configuration."""
    plugin = args.plugin_cls(inst)
    print(plugin.display())


def generic_enable(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    plugin.enable()
    print("Enabled %s", plugin.rdn)


def generic_disable(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    plugin.disable()
    print("Disabled %s", plugin.rdn)


def generic_status(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    if plugin.status() is True:
        print("%s is enabled", plugin.rdn)
    else:
        print("%s is disabled", plugin.rdn)


def add_generic_plugin_parsers(subparser, plugin_cls):
    show_parser = subparser.add_parser('show', help='display plugin configuration')
    show_parser.set_defaults(func=generic_show, plugin_cls=plugin_cls)

    enable_parser = subparser.add_parser('enable', help='enable plugin')
    enable_parser.set_defaults(func=generic_enable, plugin_cls=plugin_cls)

    disable_parser = subparser.add_parser('disable', help='disable plugin')
    disable_parser.set_defaults(func=generic_disable, plugin_cls=plugin_cls)

    status_parser = subparser.add_parser('status', help='display plugin status')
    status_parser.set_defaults(func=generic_status, plugin_cls=plugin_cls)


def create_parser(subparsers):
    plugin_parser = subparsers.add_parser('plugin', help="Manage plugins available on the server")

    subcommands = plugin_parser.add_subparsers(help="action")

    list_parser = subcommands.add_parser('list', help="List current configured (enabled and disabled) plugins")
    list_parser.set_defaults(func=plugin_list)

    get_parser = subcommands.add_parser('get', help='get')
    get_parser.set_defaults(func=plugin_get)
    get_parser.add_argument('selector', nargs='?', help='The plugin to search for')

    get_dn_parser = subcommands.add_parser('get_dn', help='get_dn')
    get_dn_parser.set_defaults(func=plugin_get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The plugin dn to get')

    edit_parser = subcommands.add_parser('edit', help='get')
    edit_parser.set_defaults(func=plugin_edit)
    edit_parser.add_argument('selector', nargs='?', help='The plugin to edit')
    edit_parser.add_argument('--type', help='The type of plugin.')
    edit_parser.add_argument('--enabled',
                             help='Identifies whether or not the plugin is enabled. It should have "on" or "off" values.')
    edit_parser.add_argument('--path', help='The plugin library name (without the library suffix).')
    edit_parser.add_argument('--initfunc', help='An initialization function of the plugin.')
    edit_parser.add_argument('--id', help='The plugin ID.')
    edit_parser.add_argument('--vendor', help='The vendor of plugin.')
    edit_parser.add_argument('--version', help='The version of plugin.')
    edit_parser.add_argument('--description', help='The description of the plugin.')

    enable_parser = subcommands.add_parser('enable', help='enable a plugin in the server')
    enable_parser.set_defaults(func=plugin_enable)
    enable_parser.add_argument('dn', nargs='?', help='The dn to enable')

    disable_parser = subcommands.add_parser('disable', help='disable the plugin configuration')
    disable_parser.set_defaults(func=plugin_disable)
    disable_parser.add_argument('dn', nargs='?', help='The dn to disable')
