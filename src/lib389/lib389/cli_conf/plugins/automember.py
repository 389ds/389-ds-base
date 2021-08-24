# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import json
from lib389.plugins import (AutoMembershipPlugin, AutoMembershipDefinition, AutoMembershipDefinitions,
                            AutoMembershipRegexRule, AutoMembershipRegexRules)
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add


arg_to_attr_definition = {
    'default_group': 'autoMemberDefaultGroup',
    'filter': 'autoMemberFilter',
    'grouping_attr': 'autoMemberGroupingAttr',
    'scope': 'autoMemberScope'
}

arg_to_attr_regex = {
    'exclusive': 'autoMemberExclusiveRegex',
    'inclusive': 'autoMemberInclusiveRegex',
    'target_group': 'autoMemberTargetGroup'
}


def definition_list(inst, basedn, log, args):
    automembers = AutoMembershipDefinitions(inst)
    result = []
    result_json = []
    for definition in automembers.list():
        if args.json:
            result_json.append(json.loads(definition.get_all_attrs_json()))
        else:
            result.append(definition.rdn)
    if args.json:
        log.info(json.dumps({"type": "list", "items": result_json}, indent=4))
    else:
        if len(result) > 0:
            for i in result:
                log.info(i)
        else:
            log.info("No Automember definitions were found")


def definition_add(inst, basedn, log, args):
    log = log.getChild('definition_add')
    plugin = AutoMembershipPlugin(inst)
    props = {'cn': args.DEFNAME}
    generic_object_add(AutoMembershipDefinition, inst, log, args, arg_to_attr_definition, basedn=plugin.dn, props=props)


def definition_edit(inst, basedn, log, args):
    log = log.getChild('definition_edit')
    definitions = AutoMembershipDefinitions(inst)
    definition = definitions.get(args.DEFNAME)
    generic_object_edit(definition, log, args, arg_to_attr_definition)


def definition_show(inst, basedn, log, args):
    log = log.getChild('definition_show')
    definitions = AutoMembershipDefinitions(inst)
    definition = definitions.get(args.DEFNAME)

    if not definition.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.DEFNAME)
    if args and args.json:
        o_str = definition.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(definition.display())


def definition_del(inst, basedn, log, args):
    log = log.getChild('definition_del')
    definitions = AutoMembershipDefinitions(inst)
    definition = definitions.get(args.DEFNAME)
    definition.delete()
    log.info("Successfully deleted the %s definition", args.DEFNAME)


def regex_list(inst, basedn, log, args):
    definitions = AutoMembershipDefinitions(inst)
    definition = definitions.get(args.DEFNAME)
    regexes = AutoMembershipRegexRules(inst, definition.dn)
    result = []
    result_json = []
    for regex in regexes.list():
        if args.json:
            result_json.append(regex.get_all_attrs_json())
        else:
            result.append(regex.rdn)
    if args.json:
        log.info(json.dumps({"type": "list", "items": result_json}, indent=4))
    else:
        if len(result) > 0:
            for i in result:
                log.info(i)
        else:
            log.info("No Automember regexes were found")


def regex_add(inst, basedn, log, args):
    log = log.getChild('regex_add')
    definitions = AutoMembershipDefinitions(inst)
    definition = definitions.get(args.DEFNAME)
    props = {'cn': args.REGEXNAME}
    generic_object_add(AutoMembershipRegexRule, inst, log, args, arg_to_attr_regex, basedn=definition.dn, props=props)


def regex_edit(inst, basedn, log, args):
    log = log.getChild('regex_edit')
    definitions = AutoMembershipDefinitions(inst)
    definition = definitions.get(args.DEFNAME)
    regexes = AutoMembershipRegexRules(inst, definition.dn)
    regex = regexes.get(args.REGEXNAME)
    generic_object_edit(regex, log, args, arg_to_attr_regex)


def regex_show(inst, basedn, log, args):
    log = log.getChild('regex_show')
    definitions = AutoMembershipDefinitions(inst)
    definition = definitions.get(args.DEFNAME)
    regexes = AutoMembershipRegexRules(inst, definition.dn)
    regex = regexes.get(args.REGEXNAME)

    if not regex.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.REGEXNAME)
    if args and args.json:
        o_str = regex.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(regex.display())


def regex_del(inst, basedn, log, args):
    log = log.getChild('regex_del')
    definitions = AutoMembershipDefinitions(inst)
    definition = definitions.get(args.DEFNAME)
    regexes = AutoMembershipRegexRules(inst, definition.dn)
    regex = regexes.get(args.REGEXNAME)
    regex.delete()
    log.info("Successfully deleted the %s regex", args.REGEXNAME)


def fixup(inst, basedn, log, args):
    plugin = AutoMembershipPlugin(inst)
    log.info('Attempting to add task entry... This will fail if Automembership plug-in is not enabled.')
    if not plugin.status():
        log.error("'%s' is disabled. Rebuild membership task can't be executed" % plugin.rdn)
    fixup_task = plugin.fixup(args.DN, args.filter)
    fixup_task.wait()
    exitcode = fixup_task.get_exit_code()
    if exitcode != 0:
        log.error('Rebuild membership task for %s has failed. Please, check logs')
    else:
        log.info('Successfully added task entry')


def _add_parser_args_definition(parser):
    parser.add_argument('--grouping-attr',  required=True,
                        help='Specifies the name of the member attribute in the group entry and '
                             'the attribute in the object entry that supplies the member attribute value, '
                             'in the format group_member_attr:entry_attr (autoMemberGroupingAttr)')
    parser.add_argument('--default-group',
                        help='Sets default or fallback group to add the entry to as a member '
                             'attribute in group entry (autoMemberDefaultGroup)')
    parser.add_argument('--scope', required=True,
                        help='Sets the subtree DN to search for entries (autoMemberScope)')
    parser.add_argument('--filter', required=True,
                        help='Sets a standard LDAP search filter to use to search for '
                             'matching entries (autoMemberFilter)')


def _add_parser_args_regex(parser):
    parser.add_argument("--exclusive", nargs='+',
                        help='Sets a single regular expression to use to identify '
                             'entries to exclude (autoMemberExclusiveRegex)')
    parser.add_argument('--inclusive', nargs='+',
                        help='Sets a single regular expression to use to identify '
                             'entries to include (autoMemberInclusiveRegex)')
    parser.add_argument('--target-group', required=True,
                        help='Sets which group to add the entry to as a member, if it meets '
                             'the regular expression conditions (autoMemberTargetGroup)')


def create_parser(subparsers):
    automember = subparsers.add_parser('automember', help="Manage and configure Automembership plugin")
    subcommands = automember.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, AutoMembershipPlugin)

    list = subcommands.add_parser('list', help='List Automembership definitions or regex rules.')
    subcommands_list = list.add_subparsers(help='action')
    list_definitions = subcommands_list.add_parser('definitions', help='Lists Automembership definitions.')
    list_definitions.set_defaults(func=definition_list)
    list_regexes = subcommands_list.add_parser('regexes', help='List Automembership regex rules.')
    list_regexes.add_argument('DEFNAME', help='The definition entry CN')
    list_regexes.set_defaults(func=regex_list)

    definition = subcommands.add_parser('definition', help='Manage Automembership definition.')
    definition.add_argument('DEFNAME', help='The definition entry CN.')
    subcommands_definition = definition.add_subparsers(help='action')

    add_def = subcommands_definition.add_parser('add', help='Creates Automembership definition.')
    add_def.set_defaults(func=definition_add)
    _add_parser_args_definition(add_def)
    edit_def = subcommands_definition.add_parser('set', help='Edits Automembership definition.')
    edit_def.set_defaults(func=definition_edit)
    _add_parser_args_definition(edit_def)
    delete_def = subcommands_definition.add_parser('delete', help='Removes Automembership definition.')
    delete_def.set_defaults(func=definition_del)
    show_def = subcommands_definition.add_parser('show', help='Displays Automembership definition.')
    show_def.set_defaults(func=definition_show)

    regex = subcommands_definition.add_parser('regex', help='Manage Automembership regex rules.')
    regex.add_argument('REGEXNAME', help='The regex entry CN')
    subcommands_regex = regex.add_subparsers(help='action')

    add_regex = subcommands_regex.add_parser('add', help='Creates Automembership regex.')
    add_regex.set_defaults(func=regex_add)
    _add_parser_args_regex(add_regex)
    edit_regex = subcommands_regex.add_parser('set', help='Edits Automembership regex.')
    edit_regex.set_defaults(func=regex_edit)
    _add_parser_args_regex(edit_regex)
    delete_regex = subcommands_regex.add_parser('delete', help='Removes Automembership regex.')
    delete_regex.set_defaults(func=regex_del)
    show_regex = subcommands_regex.add_parser('show', help='Displays Automembership regex.')
    show_regex.set_defaults(func=regex_show)

    fixup = subcommands.add_parser('fixup', help='Run a rebuild membership task.')
    fixup.set_defaults(func=fixup)
    fixup.add_argument('DN', help="Base DN that contains entries to fix up")
    fixup.add_argument('-f', '--filter', required=True, help='Sets the LDAP filter for entries to fix up')
    fixup.add_argument('-s', '--scope', required=True, choices=['sub', 'base', 'one'], type=str.lower,
                       help='Sets the LDAP search scope for entries to fix up')

