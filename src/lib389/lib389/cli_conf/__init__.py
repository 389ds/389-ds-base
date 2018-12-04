# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

def generic_show(inst, basedn, log, args):
    """Display plugin configuration."""
    plugin = args.plugin_cls(inst)
    print(plugin.display())


def generic_enable(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    if plugin.status():
        print("Plugin '%s' already enabled" % plugin.rdn)
    else:
        plugin.enable()
        print("Enabled plugin '%s'" % plugin.rdn)


def generic_disable(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    if not plugin.status():
        print("Plugin '%s' already disabled " % plugin.rdn)
    else:
        plugin.disable()
        print("Disabled plugin '%s'" % plugin.rdn)


def generic_status(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    if plugin.status() is True:
        print("Plugin '%s' is enabled" % plugin.rdn)
    else:
        print("Plugin '%s' is disabled" % plugin.rdn)


def add_generic_plugin_parsers(subparser, plugin_cls):
    show_parser = subparser.add_parser('show', help='display plugin configuration')
    show_parser.set_defaults(func=generic_show, plugin_cls=plugin_cls)

    enable_parser = subparser.add_parser('enable', help='enable plugin')
    enable_parser.set_defaults(func=generic_enable, plugin_cls=plugin_cls)

    disable_parser = subparser.add_parser('disable', help='disable plugin')
    disable_parser.set_defaults(func=generic_disable, plugin_cls=plugin_cls)

    status_parser = subparser.add_parser('status', help='display plugin status')
    status_parser.set_defaults(func=generic_status, plugin_cls=plugin_cls)


