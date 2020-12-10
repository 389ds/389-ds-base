# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import ContentSyncPlugin
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add_attr

arg_to_attr = {
    'allow_openldap': 'syncrepl-allow-openldap',
}

def contentsync_edit(inst, basedn, log, args):
    log = log.getChild('contentsync_edit')
    plugin = ContentSyncPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def contentsync_add(inst, basedn, log, args):
    log = log.getChild('contentsync_add')
    plugin = ContentSyncPlugin(inst)
    generic_object_add_attr(plugin, log, args, arg_to_attr)


def _add_parser_args(parser):
    parser.add_argument('--allow-openldap', choices=['on', 'off'], type=str.lower,
                        help='Allows openldap servers to act as read only consumers of this server via syncrepl')

def create_parser(subparsers):
    contentsync_parser = subparsers.add_parser('contentsync', help='Manage and configure Content Sync Plugin (aka syncrepl)')
    subcommands = contentsync_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, ContentSyncPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin')
    edit.set_defaults(func=contentsync_edit)
    _add_parser_args(edit)

    addp = subcommands.add_parser('add', help='Add attributes to the plugin')
    addp.set_defaults(func=contentsync_add)
    _add_parser_args(addp)
