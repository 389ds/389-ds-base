# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# JC Work around for missing dependency on https://github.com/389ds/389-ds-base/pull/4344
import ldap

from lib389.plugins import RetroChangelogPlugin
# JC Work around for missing dependency https://github.com/389ds/389-ds-base/pull/4344
# from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add_attr
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, _args_to_attrs

arg_to_attr = {
    'is_replicated': 'isReplicated',
    'attribute': 'nsslapd-attribute',
    'directory': 'nsslapd-changelogdir',
    'max_age': 'nsslapd-changelogmaxage',
    'exclude_suffix': 'nsslapd-exclude-suffix',
    'exclude_attrs': 'nsslapd-exclude-attrs'
}

def retrochangelog_edit(inst, basedn, log, args):
    log = log.getChild('retrochangelog_edit')
    plugin = RetroChangelogPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)

# JC Work around for missing dependency https://github.com/389ds/389-ds-base/pull/4344
def retrochangelog_add_attr(inst, basedn, log, args):
    log = log.getChild('retrochangelog_add_attr')
    plugin = RetroChangelogPlugin(inst)
    generic_object_add_attr(plugin, log, args, arg_to_attr)

# JC Work around for missing dependency https://github.com/389ds/389-ds-base/pull/4344
def generic_object_add_attr(dsldap_object, log, args, arg_to_attr):
    """Add an attribute to the entry. This differs to 'edit' as edit uses replace,
    and this allows multivalues to be added.

    dsldap_object should be a single instance of DSLdapObject with a set dn
    """
    log = log.getChild('generic_object_add_attr')
    # Gather the attributes
    attrs = _args_to_attrs(args, arg_to_attr)

    modlist = []
    for attr, value in attrs.items():
        if not isinstance(value, list):
            value = [value]
        modlist.append((ldap.MOD_ADD, attr, value))
    if len(modlist) > 0:
        dsldap_object.apply_mods(modlist)
        log.info("Successfully changed the %s", dsldap_object.dn)
    else:
        raise ValueError("There is nothing to set in the %s plugin entry" % dsldap_object.dn)

def retrochangelog_add(inst, basedn, log, args):
    log = log.getChild('retrochangelog_add')
    plugin = RetroChangelogPlugin(inst)
    generic_object_add_attr(plugin, log, args, arg_to_attr)


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
                        help='This attribute specifies the maximum age of any entry '
                             'in the changelog (nsslapd-changelogmaxage)')
    parser.add_argument('--exclude-suffix',
                        help='This attribute specifies the suffix which will be excluded '
                             'from the scope of the plugin (nsslapd-exclude-suffix)')
    parser.add_argument('--exclude-attrs',
                        help='This attribute specifies the attributes which will be excluded '
                             'from the scope of the plugin (nsslapd-exclude-attrs)')


def create_parser(subparsers):
    retrochangelog = subparsers.add_parser('retro-changelog', help='Manage and configure Retro Changelog plugin')
    subcommands = retrochangelog.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, RetroChangelogPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin')
    edit.set_defaults(func=retrochangelog_edit)
    _add_parser_args(edit)

    addp = subcommands.add_parser('add', help='Add attributes to the plugin')
    addp.set_defaults(func=retrochangelog_add)
    _add_parser_args(addp)

