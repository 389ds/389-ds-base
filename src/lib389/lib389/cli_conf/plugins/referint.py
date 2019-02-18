# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import ReferentialIntegrityPlugin
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit

arg_to_attr = {
    'update_delay': 'referint-update-delay',
    'membership_attr': 'referint-membership-attr',
    'entry_scope': 'nsslapd-pluginEntryScope',
    'exclude_entry_scope': 'nsslapd-pluginExcludeEntryScope',
    'container_scope': 'nsslapd-pluginContainerScope',
}


def referint_edit(inst, basedn, log, args):
    log = log.getChild('referint_edit')
    plugin = ReferentialIntegrityPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def _add_parser_args(parser):
    parser.add_argument('--update-delay',
                        help='Sets the update interval. Special values: 0 - The check is performed immediately, '
                             '-1 - No check is performed (referint-update-delay)')
    parser.add_argument('--membership-attr', nargs='+',
                        help='Specifies attributes to check for and update (referint-membership-attr)')
    parser.add_argument('--entry-scope',
                        help='Defines the subtree in which the plug-in looks for the delete '
                             'or rename operations of a user entry (nsslapd-pluginEntryScope)')
    parser.add_argument('--exclude-entry-scope',
                        help='Defines the subtree in which the plug-in ignores any operations '
                             'for deleting or renaming a user (nsslapd-pluginExcludeEntryScope)')
    parser.add_argument('--container_scope',
                        help='Specifies which branch the plug-in searches for the groups to which the user belongs. '
                             'It only updates groups that are under the specified container branch, '
                             'and leaves all other groups not updated (nsslapd-pluginContainerScope)')


def create_parser(subparsers):
    referint = subparsers.add_parser('referential-integrity',
                                      help='Manage and configure Referential Integrity Postoperation plugin')

    subcommands = referint.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, ReferentialIntegrityPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin')
    edit.set_defaults(func=referint_edit)
    _add_parser_args(edit)


