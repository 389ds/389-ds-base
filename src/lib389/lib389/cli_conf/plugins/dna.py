# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import DNAPlugin, DNAPluginConfig, DNAPluginConfigs, DNAPluginSharedConfig, DNAPluginSharedConfigs
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add, _args_to_attrs

arg_to_attr = {
    'type': 'dnaType',
    'prefix': 'dnaPrefix',
    'next_value': 'dnaNextValue',
    'max_value': 'dnaMaxValue',
    'interval': 'dnaInterval',
    'magic_regen': 'dnaMagicRegen',
    'filter': 'dnaFilter',
    'scope': 'dnaScope',
    'remote_bind_dn': 'dnaRemoteBindDN',
    'remote_bind_cred': 'dnaRemoteBindCred',
    'shared_config_entry': 'dnaSharedCfgDN',
    'threshold': 'dnaThreshold',
    'next_range': 'dnaNextRange',
    'range_request_timeout': 'dnaRangeRequestTimeout'
}

arg_to_attr_config = {
    'hostname': 'dnaHostname',
    'port': 'dnaPortNum',
    'secure_port': 'dnaSecurePortNum',
    'remaining_values': 'dnaRemainingValues',
    'remote_bind_method': 'dnaRemoteBindMethod',
    'remote_conn_protocol': 'dnaRemoteConnProtocol'
}


def dna_list(inst, basedn, log, args):
    log = log.getChild('dna_list')
    configs = DNAPluginConfigs(inst)
    config_list = configs.list()
    if args.json:
        result = {'type': 'list', 'items': []}
    if len(config_list) > 0:
        for config in config_list:
            if args.json:
                result['items'].append(config)
            else:
                log.info(config.rdn)
    else:
        log.info("No DNA configurations were found")

    if args.json:
        log.info(json.dumps(result))


def dna_add(inst, basedn, log, args):
    log = log.getChild('dna_add')
    plugin = DNAPlugin(inst)
    props = {'cn': args.NAME}
    generic_object_add(DNAPluginConfig, inst, log, args, arg_to_attr, basedn=plugin.dn, props=props)


def dna_edit(inst, basedn, log, args):
    log = log.getChild('dna_edit')
    configs = DNAPluginConfigs(inst)
    config = configs.get(args.NAME)
    generic_object_edit(config, log, args, arg_to_attr)


def dna_show(inst, basedn, log, args):
    log = log.getChild('dna_show')
    configs = DNAPluginConfigs(inst)
    config = configs.get(args.NAME)

    if not config.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.NAME)
    if args and args.json:
        o_str = config.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(config.display())


def dna_del(inst, basedn, log, args):
    log = log.getChild('dna_del')
    configs = DNAPluginConfigs(inst)
    config = configs.get(args.NAME)
    config.delete()
    log.info("Successfully deleted the %s", config.dn)


def dna_config_list(inst, basedn, log, args):
    log = log.getChild('dna_list')
    configs = DNAPluginSharedConfigs(inst, args.BASEDN)
    config_list = configs.list()
    if args.json:
        result = {'type': 'list', 'items': []}
    if len(config_list) > 0:
        for config in config_list:
            if args.json:
                result['items'].append(config.get_all_attrs_json())
            else:
                log.info(config.dn)
    else:
        log.info("No DNA shared configurations were found")

    if args.json:
        log.info(json.dumps(result))


def dna_config_add(inst, basedn, log, args):
    log = log.getChild('dna_config_add')
    targetdn = args.BASEDN

    shared_configs = DNAPluginSharedConfigs(inst, targetdn)
    attrs = _args_to_attrs(args, arg_to_attr_config)
    props = {attr: value for (attr, value) in attrs.items() if value != ""}

    shared_config = shared_configs.create(properties=props)
    log.info("Successfully created the %s" % shared_config.dn)

    configs = DNAPluginConfigs(inst)
    config = configs.get(args.NAME)
    config.replace('dnaSharedCfgDN', config.dn)
    log.info('DNA attribute dnaSharedCfgDN (shared-config-entry) '
             'was set in the %s plugin config' % config.rdn)


def dna_config_edit(inst, basedn, log, args):
    log = log.getChild('dna_config_edit')
    targetdn = args.DN
    shared_config = DNAPluginSharedConfig(inst, targetdn)
    generic_object_edit(shared_config, log, args, arg_to_attr_config)


def dna_config_show(inst, basedn, log, args):
    log = log.getChild('dna_config_show')
    targetdn = args.DN
    shared_config = DNAPluginSharedConfig(inst, targetdn)

    if not shared_config.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % targetdn)
    if args and args.json:
        o_str = shared_config.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(shared_config.display())


def dna_config_del(inst, basedn, log, args):
    log = log.getChild('dna_config_del')
    targetdn = args.DN
    shared_config = DNAPluginSharedConfig(inst, targetdn)
    shared_config.delete()
    log.info("Successfully deleted the %s", targetdn)


def _add_parser_args(parser):
    parser.add_argument('--type', nargs='+',
                        help='Sets which attributes have unique numbers being generated for them (dnaType)')
    parser.add_argument('--prefix', help='Defines a prefix that can be prepended to the generated '
                                         'number values for the attribute (dnaPrefix)')
    parser.add_argument('--next-value', help='Gives the next available number which can be assigned (dnaNextValue)')
    parser.add_argument('--max-value', help='Sets the maximum value that can be assigned for the range (dnaMaxValue)')
    parser.add_argument('--interval', help='Sets an interval to use to increment through numbers in a range (dnaInterval)')
    parser.add_argument('--magic-regen', help='Sets a user-defined value that instructs the plug-in '
                                              'to assign a new value for the entry (dnaMagicRegen)')
    parser.add_argument('--filter', help='Sets an LDAP filter to use to search for and identify the entries '
                                         'to which to apply the distributed numeric assignment range (dnaFilter)')
    parser.add_argument('--scope', help='Sets the base DN to search for entries to which '
                                        'to apply the distributed numeric assignment (dnaScope)')
    parser.add_argument('--remote-bind-dn', help='Specifies the Replication Manager DN (dnaRemoteBindDN)')
    parser.add_argument('--remote-bind-cred', help='Specifies the Replication Manager\'s password (dnaRemoteBindCred)')
    parser.add_argument('--shared-config-entry', help='Defines a shared identity that the servers can use '
                                                      'to transfer ranges to one another (dnaSharedCfgDN)')
    parser.add_argument('--threshold', help='Sets a threshold of remaining available numbers in the range. When the '
                                            'server hits the threshold, it sends a request for a new range (dnaThreshold)')
    parser.add_argument('--next-range',
                        help='Defines the next range to use when the current range is exhausted (dnaNextRange)')
    parser.add_argument('--range-request-timeout',
                        help='sets a timeout period, in seconds, for range requests so that the server '
                             'does not stall waiting on a new range from one server and '
                             'can request a range from a new server (dnaRangeRequestTimeout)')


def _add_parser_args_config(parser):
    parser.add_argument('--hostname',
                        help='Identifies the host name of a server in a shared range, as part of the DNA '
                             'range configuration for that specific host in multi-master replication (dnaHostname)')
    parser.add_argument('--port', help='Gives the standard port number to use to connect to '
                                       'the host identified in dnaHostname (dnaPortNum)')
    parser.add_argument('--secure-port', help='Gives the secure (TLS) port number to use to connect '
                                              'to the host identified in dnaHostname (dnaSecurePortNum)')
    parser.add_argument('--remote-bind-method', help='Specifies the remote bind method (dnaRemoteBindMethod)')
    parser.add_argument('--remote-conn-protocol', help='Specifies the remote connection protocol (dnaRemoteConnProtocol)')
    parser.add_argument('--remaining-values', help='Contains the number of values that are remaining and '
                                                   'available to a server to assign to entries (dnaRemainingValues)')


def create_parser(subparsers):
    dna = subparsers.add_parser('dna', help='Manage and configure DNA plugin')
    subcommands = dna.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, DNAPlugin)

    list = subcommands.add_parser('list', help='List available plugin configs')
    subcommands_list = list.add_subparsers(help='action')
    list_configs = subcommands_list.add_parser('configs', help='List main DNA plugin config entries')
    list_configs.set_defaults(func=dna_list)
    list_shared_configs = subcommands_list.add_parser('shared-configs', help='List DNA plugin shared config entries')
    list_shared_configs.add_argument('BASEDN', help='The search DN')
    list_shared_configs.set_defaults(func=dna_config_list)

    config = subcommands.add_parser('config', help='Manage plugin configs')
    config.add_argument('NAME', help='The DNA configuration name')
    config_subcommands = config.add_subparsers(help='action')
    add = config_subcommands.add_parser('add', help='Add the config entry')
    add.set_defaults(func=dna_add)
    _add_parser_args(add)
    edit = config_subcommands.add_parser('set', help='Edit the config entry')
    edit.set_defaults(func=dna_edit)
    _add_parser_args(edit)
    show = config_subcommands.add_parser('show', help='Display the config entry')
    show.set_defaults(func=dna_show)
    delete = config_subcommands.add_parser('delete', help='Delete the config entry')
    delete.set_defaults(func=dna_del)
    shared_config = config_subcommands.add_parser('shared-config-entry', help='Manage the shared config entry')
    shared_config_subcommands = shared_config.add_subparsers(help='action')

    add_config = shared_config_subcommands.add_parser('add', help='Add the shared config entry')
    add_config.add_argument('BASEDN', help='The shared config entry BASE DN. The new DN will be constructed with '
                                           'dnaHostname and dnaPortNum')
    add_config.set_defaults(func=dna_config_add)
    _add_parser_args_config(add_config)
    edit_config = shared_config_subcommands.add_parser('edit', help='Edit the shared config entry')
    edit_config.add_argument('DN', help='The shared config entry DN')
    edit_config.set_defaults(func=dna_config_edit)
    _add_parser_args_config(edit_config)
    show_config_parser = shared_config_subcommands.add_parser('show', help='Display the shared config entry')
    show_config_parser.add_argument('DN', help='The shared config entry DN')
    show_config_parser.set_defaults(func=dna_config_show)
    del_config_parser = shared_config_subcommands.add_parser('delete', help='Delete the shared config entry')
    del_config_parser.add_argument('DN', help='The shared config entry DN')
    del_config_parser.set_defaults(func=dna_config_del)
