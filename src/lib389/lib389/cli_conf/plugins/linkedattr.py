# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import LinkedAttributesPlugin, LinkedAttributesConfig, LinkedAttributesConfigs, LinkedAttributesFixupTasks
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add
from lib389.cli_base import CustomHelpFormatter
from lib389.utils import get_task_status

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
            result_json.append(json.loads(config.get_all_attrs_json()))
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

    if args.wait:
        log.info(f'Waiting for fixup task "{fixup_task.dn}" to complete.  You can safely exit by pressing Control C ...')
        fixup_task.wait(timeout=None)
        exitcode = fixup_task.get_exit_code()
        if exitcode != 0:
            log.error(f'LinkedAttributes fixup "{fixup_task.dn}" for {args.linkdn} has failed (error {exitcode}). Please, check logs')
        else:
            log.info('Fixup task successfully completed')
    else:
        log.info(f'Successfully added task entry "{fixup_task.dn}". This task is running in the background. To track its progress you can use the "fixup-status" command.')


def do_fixup_status(inst, basedn, log, args):
    get_task_status(inst, log, LinkedAttributesFixupTasks, dn=args.dn, show_log=args.show_log,
                    watch=args.watch, use_json=args.json)


def _add_parser_args(parser):
    parser.add_argument('--link-type',
                        help='Sets the attribute that is managed manually by administrators (linkType)')
    parser.add_argument('--managed-type',
                        help='Sets the attribute that is created dynamically by the plugin (managedType)')
    parser.add_argument('--link-scope',
                        help='Sets the scope that restricts the plugin to a specific part of the directory tree (linkScope)')


def create_parser(subparsers):
    linkedattr_parser = subparsers.add_parser('linked-attr', help='Manage and configure Linked Attributes plugin', formatter_class=CustomHelpFormatter)
    subcommands = linkedattr_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, LinkedAttributesPlugin)

    fixup_parser = subcommands.add_parser('fixup', help='Run the fix-up task for linked attributes plugin', formatter_class=CustomHelpFormatter)
    fixup_parser.add_argument('-l', '--linkdn', help="Sets the base DN that contains entries to fix up")
    fixup_parser.add_argument('--wait', action='store_true',
                              help="Wait for the task to finish, this could take a long time")
    fixup_parser.set_defaults(func=fixup)

    fixup_status = subcommands.add_parser('fixup-status', help='Check the status of a fix-up task', formatter_class=CustomHelpFormatter)
    fixup_status.set_defaults(func=do_fixup_status)
    fixup_status.add_argument('--dn', help="The task entry's DN")
    fixup_status.add_argument('--show-log', action='store_true', help="Display the task log")
    fixup_status.add_argument('--watch', action='store_true',
                       help="Watch the task's status and wait for it to finish")

    list = subcommands.add_parser('list', help='List available plugin configs', formatter_class=CustomHelpFormatter)
    list.set_defaults(func=linkedattr_list)

    config = subcommands.add_parser('config', help='Manage plugin configs', formatter_class=CustomHelpFormatter)
    config.add_argument('NAME', help='The Linked Attributes configuration name')
    config_subcommands = config.add_subparsers(help='action')
    add = config_subcommands.add_parser('add', help='Add the config entry', formatter_class=CustomHelpFormatter)
    add.set_defaults(func=linkedattr_add)
    _add_parser_args(add)
    edit = config_subcommands.add_parser('set', help='Edit the config entry', formatter_class=CustomHelpFormatter)
    edit.set_defaults(func=linkedattr_edit)
    _add_parser_args(edit)
    show = config_subcommands.add_parser('show', help='Display the config entry', formatter_class=CustomHelpFormatter)
    show.set_defaults(func=linkedattr_show)
    delete = config_subcommands.add_parser('delete', help='Delete the config entry', formatter_class=CustomHelpFormatter)
    delete.set_defaults(func=linkedattr_del)
