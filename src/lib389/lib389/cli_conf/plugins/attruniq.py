# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import AttributeUniquenessPlugin, AttributeUniquenessPlugins
from lib389.cli_conf import (generic_object_edit, generic_object_add)

arg_to_attr = {
    'enabled': 'nsslapd-pluginenabled',
    'attr_name': 'uniqueness-attribute-name',
    'subtree': 'uniqueness-subtrees',
    'across_all_subtrees': 'uniqueness-across-all-subtrees',
    'top_entry_oc': 'uniqueness-top-entry-oc',
    'subtree_entries_oc': 'uniqueness-subtree-entries-oc'
}

PLUGIN_DN = "cn=plugins,cn=config"

def attruniq_list(inst, basedn, log, args):
    log = log.getChild('attruniq_list')
    plugins = AttributeUniquenessPlugins(inst)
    result = []
    result_json = []
    for plugin in plugins.list():
        if args.json:
            result_json.append(json.loads(plugin.get_all_attrs_json()))
        else:
            result.append(plugin.rdn)
    if args.json:
        log.info(json.dumps({"type": "list", "items": result_json},  indent=4))
    else:
        if len(result) > 0:
            for i in result:
                log.info(i)
        else:
            log.info("No Attribute Uniqueness plugin instances")


def attruniq_add(inst, basedn, log, args):
    log = log.getChild('attruniq_add')
    props = {'cn': args.NAME}
    # We require a subtree, or a target Objectclass
    if args.subtree_entries_oc is None and args.subtree is None:
        raise ValueError("A attribute uniqueness configuration requires a 'subtree' or 'subtree-entries-oc' to be set")

    generic_object_add(AttributeUniquenessPlugin, inst, log, args, arg_to_attr, basedn=PLUGIN_DN, props=props)


def attruniq_edit(inst, basedn, log, args):
    log = log.getChild('attruniq_edit')
    plugins = AttributeUniquenessPlugins(inst)
    plugin = plugins.get(args.NAME)
    generic_object_edit(plugin, log, args, arg_to_attr)


def attruniq_show(inst, basedn, log, args):
    log = log.getChild('attruniq_show')
    plugins = AttributeUniquenessPlugins(inst)
    plugin = plugins.get(args.NAME)

    if not plugin.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.name)
    if args and args.json:
        log.info(plugin.get_all_attrs_json())
    else:
        log.info(plugin.display())


def attruniq_del(inst, basedn, log, args):
    log = log.getChild('attruniq_del')
    plugins = AttributeUniquenessPlugins(inst)
    plugin = plugins.get(args.NAME)
    plugin.delete()
    log.info("Successfully deleted the %s", plugin.dn)


def _add_parser_args(parser):
    parser.add_argument('NAME', help='Sets the name of the plug-in configuration record. (cn) You can use any string, '
                                     'but "attribute_name Attribute Uniqueness" is recommended.')
    parser.add_argument('--enabled', choices=['on', 'off'],
                        help='Identifies whether or not the config is enabled.')
    parser.add_argument('--attr-name', nargs='+',
                        help='Sets the name of the attribute whose values must be unique. '
                             'This attribute is multi-valued. (uniqueness-attribute-name)')
    parser.add_argument('--subtree', nargs='+',
                        help='Sets the DN under which the plug-in checks for uniqueness of '
                             'the attributes value. This attribute is multi-valued (uniqueness-subtrees)')
    parser.add_argument('--across-all-subtrees', choices=['on', 'off'], type=str.lower,
                        help='If enabled (on), the plug-in checks that the attribute is unique across all subtrees '
                             'set. If you set the attribute to off, uniqueness is only enforced within the subtree '
                             'of the updated entry (uniqueness-across-all-subtrees)')
    parser.add_argument('--top-entry-oc',
                        help='Verifies that the value of the attribute set in uniqueness-attribute-name '
                             'is unique in this subtree (uniqueness-top-entry-oc)')
    parser.add_argument('--subtree-entries-oc',
                        help='Verifies if an attribute is unique, if the entry contains the object class '
                             'set in this parameter (uniqueness-subtree-entries-oc)')


def create_parser(subparsers):
    attruniq = subparsers.add_parser('attr-uniq', help='Manage and configure Attribute Uniqueness plugin')
    subcommands = attruniq.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, AttributeUniquenessPlugin)

    list = subcommands.add_parser('list', help='List available plugin configs')
    list.set_defaults(func=attruniq_list)

    add = subcommands.add_parser('add', help='Add the config entry')
    add.set_defaults(func=attruniq_add)
    _add_parser_args(add)

    edit = subcommands.add_parser('set', help='Edit the config entry')
    edit.set_defaults(func=attruniq_edit)
    _add_parser_args(edit)

    show = subcommands.add_parser('show', help='Display the config entry')
    show.add_argument('NAME', help='The name of the plug-in configuration record')
    show.set_defaults(func=attruniq_show)

    delete = subcommands.add_parser('delete', help='Delete the config entry')
    delete.add_argument('NAME', help='Sets the name of the plug-in configuration record')
    delete.set_defaults(func=attruniq_del)

    enable = subcommands.add_parser('enable', help='enable plugin')
    enable.add_argument('NAME', help='Sets the name of the plug-in configuration record')
    enable.set_defaults(func=generic_enable)

    disable = subcommands.add_parser('disable', help='disable plugin')
    disable.add_argument('NAME', help='Sets the name of the plug-in configuration record')
    disable.set_defaults(func=generic_disable)

    status = subcommands.add_parser('status', help='display plugin status')
    status.add_argument('NAME', help='Sets the name of the plug-in configuration record')
    status.set_defaults(func=generic_status)
