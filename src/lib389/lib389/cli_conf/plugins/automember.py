# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import json
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinitions
from lib389.cli_conf import add_generic_plugin_parsers


def list_definition(inst, basedn, log, args):
    """List automember definition if instance name
    is given else show all automember definitions.

    :param name: An instance
    :type name: lib389.DirSrv
    """
    
    automembers = AutoMembershipDefinitions(inst)

    if args.name is not None:
        if args.json:
            print(automembers.get_all_attrs_json(args.name))
        else:
            automember = automembers.get(args.name)
            log.info(automember.display())
    else:    
        all_definitions = automembers.list()
        if args.json:
            result = {'type': 'list', 'items': []}
        if len(all_definitions) > 0:
            for definition in all_definitions:
                if args.json:
                    result['items'].append(definition)
                else:
                    log.info(definition.display())
        else:
            log.info("No automember definitions were found")

        if args.json:
            print(json.dumps(result))


def create_definition(inst, basedn, log, args):
    """
        Create automember definition.

        :param name: An instance
        :type name: lib389.DirSrv
        :param groupattr: autoMemberGroupingAttr value
        :type groupattr: str
        :param defaultgroup: autoMemberDefaultGroup value
        :type defaultgroup: str
        :param scope: autoMemberScope value
        :type scope: str
        :param filter: autoMemberFilter value
        :type filter: str

    """
    automember_prop = {
        'cn': args.name,
        'autoMemberScope': args.scope,
        'autoMemberFilter': args.filter,
        'autoMemberDefaultGroup': args.defaultgroup,
        'autoMemberGroupingAttr': args.groupattr,
    }

    plugin = AutoMembershipPlugin(inst)
    plugin.enable()

    automembers = AutoMembershipDefinitions(inst)
    
    try:
        automember = automembers.create(properties=automember_prop)
        log.info("Automember definition created successfully!")
    except Exception as e:
        log.info("Failed to create Automember definition: {}".format(str(e)))
        raise e


def edit_definition(inst, basedn, log, args):
    """
        Edit automember definition
        
        :param name: An instance
        :type name: lib389.DirSrv
        :param groupattr: autoMemberGroupingAttr value
        :type groupattr: str
        :param defaultgroup: autoMemberDefaultGroup value
        :type defaultgroup: str
        :param scope: autoMemberScope value
        :type scope: str
        :param filter: autoMemberFilter value
        :type filter: str

    """
    automembers = AutoMembershipDefinitions(inst)
    automember = automembers.get(args.name)

    if args.scope is not None:
        automember.replace("automemberscope", args.scope)
    if args.filter is not None:
        automember.replace("automemberfilter", args.filter)
    if args.defaultgroup is not None:
        automember.replace("automemberdefaultgroup", args.defaultgroup)
    if args.groupattr is not None:
        automember.replace("automembergroupingattr", args.groupattr)
    log.info("Definition updated successfully.")


def remove_definition(inst, basedn, log, args):
    """
        Remove automember definition for the given
        instance.

        :param name: An instance
        :type name: lib389.DirSrv

    """
    automembers = AutoMembershipDefinitions(inst)
    automember = automembers.get(args.name)

    automember.delete()
    log.info("Definition deleted successfully.")


def create_parser(subparsers):
    automember_parser = subparsers.add_parser('automember', help="Manage and configure automember plugin")

    subcommands = automember_parser.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, AutoMembershipPlugin)

    create_parser = subcommands.add_parser('create', help='Create automember definition.')
    create_parser.set_defaults(func=create_definition)
    
    create_parser.add_argument("name", help='Set cn for group entry.')

    create_parser.add_argument("--groupattr", help='Set member attribute in group entry.', default='member:dn')

    create_parser.add_argument('--defaultgroup', required=True, help='Set default group to add member to.')

    create_parser.add_argument('--scope', required=True, help='Set automember scope.')

    create_parser.add_argument('--filter', help='Set automember filter.', default= '(objectClass=*)')

    show_parser = subcommands.add_parser('list', help='List automember definition.')
    show_parser.set_defaults(func=list_definition)

    show_parser.add_argument("--name", help='Set cn for group entry. If not specified show all automember definitions.')

    edit_parser = subcommands.add_parser('edit', help='Edit automember definition.')
    edit_parser.set_defaults(func=edit_definition)

    edit_parser.add_argument("name", help='Set cn for group entry.')

    edit_parser.add_argument("--groupattr", help='Set member attribute in group entry.')

    edit_parser.add_argument('--defaultgroup', help='Set default group to add member to.')

    edit_parser.add_argument('--scope', help='Set automember scope.')

    edit_parser.add_argument('--filter', help='Set automember filter.')

    remove_parser = subcommands.add_parser('remove', help='Remove automember definition.')
    remove_parser.set_defaults(func=remove_definition)

    remove_parser.add_argument("name", help='Set cn for group entry.')
