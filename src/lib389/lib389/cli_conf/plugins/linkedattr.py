# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import LinkedAttributesPlugin, LinkedAttributesConfig, LinkedAttributesConfigs
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add

arg_to_attr = {
    'link_type': 'linkType',
    'managed_type': 'managedType',
    'link_scope': 'linkScope',
}


def linkedattr_list(inst, basedn, log, args):
    log = log.getChild('linkedattr_list')
    configs = LinkedAttributesConfigs(inst)
    result = []
    result_json = []
    for config in configs.list():
        if args.json:
            result_json.append(config.get_all_attrs_json())
        else:
            result.append(config.rdn)
    if args.json:
        log.info(json.dumps({"type": "list", "items": result_json}, indent=4))
    else:
        if len(result) > 0:
            for i in result:
                log.info(i)
        else:
            log.info("No Linked Attributes plugin config instances")


def linkedattr_add(inst, basedn, log, args):
    log = log.getChild('linkedattr_add')
    plugin = LinkedAttributesPlugin(inst)
    props = {'cn': args.NAME}
    generic_object_add(LinkedAttributesConfig, inst, log, args, arg_to_attr, basedn=plugin.dn, props=props)


def linkedattr_edit(inst, basedn, log, args):
    log = log.getChild('linkedattr_edit')
    configs = LinkedAttributesConfigs(inst)
    config = configs.get(args.NAME)
    generic_object_edit(config, log, args, arg_to_attr)


def linkedattr_show(inst, basedn, log, args):
    log = log.getChild('linkedattr_show')
    configs = LinkedAttributesConfigs(inst)
    config = configs.get(args.NAME)

    if not config.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.name)
    if args and args.json:
        o_str = config.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(config.display())


def linkedattr_del(inst, basedn, log, args):
    log = log.getChild('linkedattr_del')
    configs = LinkedAttributesConfigs(inst)
    config = configs.get(args.NAME)
    config.delete()
    log.info("Successfully deleted the %s", config.dn)


def fixup(inst, basedn, log, args):
    plugin = LinkedAttributesPlugin(inst)
    log.info('Attempting to add task entry... This will fail if LinkedAttributes plug-in is not enabled.')
    if not plugin.status():
        log.error("'%s' is disabled. Fix up task can't be executed" % plugin.rdn)
    fixup_task = plugin.fixup(args.linkdn)
    fixup_task.wait()
    exitcode = fixup_task.get_exit_code()
    if exitcode != 0:
        log.error('LinkedAttributes fixup task for %s has failed. Please, check logs')
    else:
        log.info('Successfully added fixup task')


def _add_parser_args(parser):
    parser.add_argument('--link-type',
                        help='Sets the attribute that is managed manually by administrators (linkType)')
    parser.add_argument('--managed-type',
                        help='Sets the attribute that is created dynamically by the plugin (managedType)')
    parser.add_argument('--link-scope',
                        help='Sets the scope that restricts the plugin to a specific part of the directory tree (linkScope)')


def create_parser(subparsers):
    linkedattr_parser = subparsers.add_parser('linked-attr', help='Manage and configure Linked Attributes plugin')
    subcommands = linkedattr_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, LinkedAttributesPlugin)

    fixup_parser = subcommands.add_parser('fixup', help='Run the fix-up task for linked attributes plugin')
    fixup_parser.add_argument('-l', '--linkdn', help="Base DN that contains entries to fix up")
    fixup_parser.set_defaults(func=fixup)

    list = subcommands.add_parser('list', help='List available plugin configs')
    list.set_defaults(func=linkedattr_list)

    config = subcommands.add_parser('config', help='Manage plugin configs')
    config.add_argument('NAME', help='The Linked Attributes configuration name')
    config_subcommands = config.add_subparsers(help='action')
    add = config_subcommands.add_parser('add', help='Add the config entry')
    add.set_defaults(func=linkedattr_add)
    _add_parser_args(add)
    edit = config_subcommands.add_parser('set', help='Edit the config entry')
    edit.set_defaults(func=linkedattr_edit)
    _add_parser_args(edit)
    show = config_subcommands.add_parser('show', help='Display the config entry')
    show.set_defaults(func=linkedattr_show)
    delete = config_subcommands.add_parser('delete', help='Delete the config entry')
    delete.set_defaults(func=linkedattr_del)
