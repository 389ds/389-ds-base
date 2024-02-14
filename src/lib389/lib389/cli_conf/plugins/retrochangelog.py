# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
from lib389.plugins import RetroChangelogPlugin
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add_attr, generic_object_del_attr
from lib389.cli_base import CustomHelpFormatter

arg_to_attr = {
    'is_replicated': 'isReplicated',
    'attribute': 'nsslapd-attribute',
    'directory': 'nsslapd-changelogdir',
    'max_age': 'nsslapd-changelogmaxage',
    'trim_interval': 'nsslapd-changelog-trim-interval',
    'exclude_suffix': 'nsslapd-exclude-suffix',
    'exclude_attrs': 'nsslapd-exclude-attrs',
}

def retrochangelog_edit(inst, basedn, log, args):
    log = log.getChild('retrochangelog_edit')
    plugin = RetroChangelogPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def retrochangelog_add(inst, basedn, log, args):
    log = log.getChild('retrochangelog_add')
    plugin = RetroChangelogPlugin(inst)
    generic_object_add_attr(plugin, log, args, arg_to_attr)

def retrochangelog_del(inst, basedn, log, args):
    log = log.getChild('retrochangelog_add')
    plugin = RetroChangelogPlugin(inst)
    generic_object_del_attr(plugin, log, args, arg_to_attr)

def _add_parser_args(parser):
    parser.add_argument('--is-replicated', choices=['TRUE', 'FALSE'], type=str.upper,
                        help='Sets a flag to indicate on a change in the changelog whether the change is newly made '
                             'on that server or whether it was replicated over from another server (isReplicated)')
    parser.add_argument('--attribute',
                        help='Specifies another Directory Server attribute which must be included in '
                             'the retro changelog entries (nsslapd-attribute)')
    parser.add_argument('--directory',
                        help='Specifies the name of the directory in which the changelog database '
                             'is created the first time the plug-in is run')
    parser.add_argument('--max-age',
                        help='Specifies the maximum age of any entry in the changelog.  Used to trim the '
                            'changelog (nsslapd-changelogmaxage)')
    parser.add_argument('--trim-interval',
                        help='. nsslapd-changelog-trim-interval)')
    parser.add_argument('--exclude-suffix', nargs='*',
                        help='Specifies the suffix which will be excluded from the scope of the plugin '
                            '(nsslapd-exclude-suffix)')
    parser.add_argument('--exclude-attrs', nargs='*',
                        help='Specifies the attributes which will be excluded from the scope of the plugin '
                            '(nsslapd-exclude-attrs)')


def create_parser(subparsers):
    retrochangelog = subparsers.add_parser('retro-changelog', help='Manage and configure Retro Changelog plugin', formatter_class=CustomHelpFormatter)
    subcommands = retrochangelog.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, RetroChangelogPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin', formatter_class=CustomHelpFormatter)
    edit.set_defaults(func=retrochangelog_edit)
    _add_parser_args(edit)

    addp = subcommands.add_parser('add', help='Add attributes to the plugin', formatter_class=CustomHelpFormatter)
    addp.set_defaults(func=retrochangelog_add)
    _add_parser_args(addp)

    delp = subcommands.add_parser('del', help='Delete an attribute from plugin scope', formatter_class=CustomHelpFormatter)
    delp.set_defaults(func=retrochangelog_del)
    _add_parser_args(delp)
