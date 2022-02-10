# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import json
from lib389.backend import Backends
from lib389.plugins import ManagedEntriesPlugin, MEPConfig, MEPConfigs, MEPTemplate, MEPTemplates
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add

arg_to_attr = {
    'config_area': 'nsslapd-pluginconfigarea'
}

arg_to_attr_config = {
    'scope': 'originScope',
    'filter': 'originFilter',
    'managed_base': 'managedBase',
    'managed_template': 'managedTemplate'
}

arg_to_attr_template = {
    'rdn_attr': 'meprdnattr',
    'static_attr': 'mepstaticattr',
    'mapped_attr': 'mepmappedattr'
}


def mep_edit(inst, basedn, log, args):
    log = log.getChild('mep_edit')
    plugin = ManagedEntriesPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def mep_config_list(inst, basedn, log, args):
    log = log.getChild('mep_config_list')
    plugin = ManagedEntriesPlugin(inst)
    config_area = plugin.get_attr_val_utf8_l('nsslapd-pluginConfigArea')
    configs = MEPConfigs(inst, config_area)
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
            log.info("No Managed Entry plugin instances")


def mep_config_add(inst, basedn, log, args):
    log = log.getChild('mep_config_add')
    plugin = ManagedEntriesPlugin(inst)
    config_area = plugin.get_attr_val_utf8_l('nsslapd-pluginConfigArea')
    if config_area is None:
        config_area = plugin.dn
    props = {'cn': args.NAME}
    generic_object_add(MEPConfig, inst, log, args, arg_to_attr_config, basedn=config_area, props=props)


def mep_config_edit(inst, basedn, log, args):
    log = log.getChild('mep_config_edit')
    plugin = ManagedEntriesPlugin(inst)
    config_area = plugin.get_attr_val_utf8_l('nsslapd-pluginConfigArea')
    configs = MEPConfigs(inst, config_area)
    config = configs.get(args.NAME)
    generic_object_edit(config, log, args, arg_to_attr_config)


def mep_config_show(inst, basedn, log, args):
    log = log.getChild('mep_config_show')
    plugin = ManagedEntriesPlugin(inst)
    config_area = plugin.get_attr_val_utf8_l('nsslapd-pluginConfigArea')
    configs = MEPConfigs(inst, config_area)
    config = configs.get(args.NAME)

    if not config.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.name)
    if args and args.json:
        o_str = config.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(config.display())


def mep_config_del(inst, basedn, log, args):
    log = log.getChild('mep_config_del')
    plugin = ManagedEntriesPlugin(inst)
    config_area = plugin.get_attr_val_utf8_l('nsslapd-pluginConfigArea')
    configs = MEPConfigs(inst, config_area)
    config = configs.get(args.NAME)
    config.delete()
    log.info("Successfully deleted the %s", config.dn)


def mep_template_list(inst, basedn, log, args):
    log = log.getChild('mep_template_list')
    if args.BASEDN is None:
        # Gather all the templates from all the suffixes
        templates = []
        backends = Backends(inst).list()
        for be in backends:
            temps = MEPTemplates(inst, be.get_suffix()).list()
            if len(temps) > 0:
                templates += MEPTemplates(inst, be.get_suffix()).list()
    else:
        templates = MEPTemplates(inst, args.BASEDN).list()

    result = []
    result_json = []
    for template in templates:
        if args.json:
            result_json.append(json.loads(template.get_all_attrs_json()))
        else:
            result.append(template.rdn)
    if args.json:
        log.info(json.dumps({"type": "list", "items": result_json}, indent=4))
    else:
        if len(result) > 0:
            for i in result:
                log.info(i)
        else:
            log.info("No Managed Entry template entries found")


def mep_template_add(inst, basedn, log, args):
    log = log.getChild('mep_template_add')
    targetdn = args.DN
    if not targetdn or not ldap.dn.is_dn(targetdn):
        raise ValueError("Specified DN is not a valid DN")
    generic_object_add(MEPTemplate, inst, log, args, arg_to_attr_template, dn=targetdn)
    log.info('Don\'t forget to assign the template to Managed Entry Plugin config '
             'attribute - managedTemplate')


def mep_template_edit(inst, basedn, log, args):
    log = log.getChild('mep_template_edit')
    targetdn = args.DN
    if not ldap.dn.is_dn(targetdn):
        raise ValueError("Specified DN is not a valid DN")
    template = MEPTemplate(inst, targetdn)
    generic_object_edit(template, log, args, arg_to_attr_template)


def mep_template_show(inst, basedn, log, args):
    log = log.getChild('mep_template_show')
    targetdn = args.DN
    if not ldap.dn.is_dn(targetdn):
        raise ValueError("Specified DN is not a valid DN")
    template = MEPTemplate(inst, targetdn)

    if not template.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % targetdn)
    if args and args.json:
        o_str = template.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(template.display())


def mep_template_del(inst, basedn, log, args):
    log = log.getChild('mep_template_del')
    targetdn = args.DN
    if not ldap.dn.is_dn(targetdn):
        raise ValueError("Specified DN is not a valid DN")
    template = MEPTemplate(inst, targetdn)
    template.delete()
    log.info("Successfully deleted the %s", targetdn)


def _add_parser_args_config(parser):
    parser.add_argument('--scope', help='Sets the scope of the search to use to see '
                                        'which entries the plug-in monitors (originScope)')
    parser.add_argument('--filter', help='Sets the search filter to use to search for and identify the entries '
                                         'within the subtree which require a managed entry (originFilter)')
    parser.add_argument('--managed-base', help='Sets the subtree under which to create '
                                               'the managed entries (managedBase)')
    parser.add_argument('--managed-template', help='Identifies the template entry to use to create '
                                                   'the managed entry (managedTemplate)')


def _add_parser_args_template(parser):
    parser.add_argument('--rdn-attr', help='Sets which attribute to use as the naming attribute '
                                           'in the automatically-generated entry (mepRDNAttr)')
    parser.add_argument('--static-attr', nargs='+',
                        help='Sets an attribute with a defined value that must be added '
                             'to the automatically-generated entry (mepStaticAttr)')
    parser.add_argument('--mapped-attr', nargs='+',
                        help='Sets attributes in the Managed Entries template entry which must exist '
                             'in the generated entry (mepMappedAttr)')


def create_parser(subparsers):
    mep = subparsers.add_parser('managed-entries', help='Manage and configure Managed Entries Plugin')
    subcommands = mep.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, ManagedEntriesPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin settings')
    edit.set_defaults(func=mep_edit)
    edit.add_argument('--config-area', help='Sets the value of the nsslapd-pluginConfigArea attribute')

    list = subcommands.add_parser('list', help='List Managed Entries Plugin configs and templates')
    subcommands_list = list.add_subparsers(help='action')
    list_configs = subcommands_list.add_parser('configs', help='List Managed Entries Plugin configs (list config-area '
                                                               'if specified in the main plugin entry)')
    list_configs.set_defaults(func=mep_config_list)
    list_templates = subcommands_list.add_parser('templates',
                                               help='List Managed Entries Plugin templates in the directory')
    list_templates.add_argument('BASEDN', nargs='?', help='The base DN where to search the templates')
    list_templates.set_defaults(func=mep_template_list)

    config = subcommands.add_parser('config', help='Handle Managed Entries Plugin configs')
    config.add_argument('NAME', help='The config entry CN')
    config_subcommands = config.add_subparsers(help='action')
    add = config_subcommands.add_parser('add', help='Add the config entry')
    add.set_defaults(func=mep_config_add)
    _add_parser_args_config(add)
    edit = config_subcommands.add_parser('set', help='Edit the config entry')
    edit.set_defaults(func=mep_config_edit)
    _add_parser_args_config(edit)
    show = config_subcommands.add_parser('show', help='Display the config entry')
    show.set_defaults(func=mep_config_show)
    delete = config_subcommands.add_parser('delete', help='Delete the config entry')
    delete.set_defaults(func=mep_config_del)

    template = subcommands.add_parser('template', help='Handle Managed Entries Plugin templates')
    template.add_argument('DN', help='The template entry DN.')
    template_subcommands = template.add_subparsers(help='action')
    add = template_subcommands.add_parser('add', help='Add the template entry')
    add.set_defaults(func=mep_template_add)
    _add_parser_args_template(add)
    edit = template_subcommands.add_parser('set', help='Edit the template entry')
    edit.set_defaults(func=mep_template_edit)
    _add_parser_args_template(edit)
    show = template_subcommands.add_parser('show', help='Display the template entry')
    show.set_defaults(func=mep_template_show)
    delete = template_subcommands.add_parser('delete', help='Delete the template entry')
    delete.set_defaults(func=mep_template_del)
