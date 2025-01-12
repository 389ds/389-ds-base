# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import DNAPlugin, DNAPluginConfig, DNAPluginConfigs, DNAPluginSharedConfig, DNAPluginSharedConfigs
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add, _args_to_attrs
from lib389.cli_base import CustomHelpFormatter

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
    'HOSTNAME': 'dnaHostname',
    'PORT': 'dnaPortNum',
    'secure_port': 'dnaSecurePortNum',
    'remaining_values': 'dnaRemainingValues',
    'remote_bind_method': 'dnaRemoteBindMethod',
    'remote_conn_protocol': 'dnaRemoteConnProtocol'
}


def _get_shared_config_dn(inst, args):
    configs = DNAPluginConfigs(inst)
    config = configs.get(args.NAME)
    if config.present('dnaSharedCfgDN'):
        basedn = config.get_attr_val_utf8_l('dnaSharedCfgDN')
    else:
        raise ValueError('dnaSharedCfgDN should be set at the "%s" config entry' % args.NAME)
    if ':' not in args.SHARED_CFG:
       raise ValueError("The shared config entry identifier must be HOSTNAME:PORT")
    (hostname, port) = args.SHARED_CFG.split(":", 1)
    decomposed_dn = [[('dnaHostname', hostname, 1),
                      ('dnaPortNum', port, 1)]] + ldap.dn.str2dn(basedn)
    return ldap.dn.dn2str(decomposed_dn)


def dna_list(inst, basedn, log, args):
    log = log.getChild('dna_list')
    configs = DNAPluginConfigs(inst)
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
            log.info("No DNA configurations were found")


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
    result = []
    result_json = []
    parent_config_entries = []

    parent_configs = DNAPluginConfigs(inst)
    for config in parent_configs.list():
        if config.present("dnaSharedCfgDN") and config.get_attr_val_utf8("dnaSharedCfgDN") == args.BASEDN:
            parent_config_entries.append(config.rdn)

    for config in configs.list():
        if args.json:
            result_json.append(json.loads(config.get_all_attrs_json()))
        else:
            port = config.get_attr_val_utf8('dnaPortNum')
            shared_config = f"{config.rdn}:{port}"
            result.append(shared_config)
    if args.json:
        log.info(json.dumps({"type": "list", "items": result_json}, indent=4))
    else:
        if len(result) > 0:
            for i in result:
                log.info(i)
            if parent_config_entries:
                log.info("DNA plugin configs which have the shared config entry set as a dnaSharedCfgDN attribute: " + " ".join(parent_config_entries))
            else:
                log.info("No DNA plugin configs have the shared config entry set as a dnaSharedCfgDN attribute.")
        else:
            log.info("No DNA shared configurations were found")


def dna_config_edit(inst, basedn, log, args):
    log = log.getChild('dna_config_edit')
    targetdn = _get_shared_config_dn(inst, args)

    shared_config = DNAPluginSharedConfig(inst, targetdn)
    generic_object_edit(shared_config, log, args, arg_to_attr_config)


def dna_config_show(inst, basedn, log, args):
    log = log.getChild('dna_config_show')
    targetdn = _get_shared_config_dn(inst, args)

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
    targetdn = _get_shared_config_dn(inst, args)
    shared_config = DNAPluginSharedConfig(inst, targetdn)
    shared_config.delete()
    log.info("Successfully deleted the %s", targetdn)


def _add_parser_args(parser):
    parser.add_argument('--type', nargs='+',
                        help='Sets which attributes have unique numbers being generated for them (dnaType)')
    parser.add_argument('--prefix', help='Defines a prefix that can be prepended to the generated '
                                         'number values for the attribute (dnaPrefix)')
    parser.add_argument('--next-value', help='Sets the next available number which can be assigned (dnaNextValue)')
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
                        help='Sets a timeout period, in seconds, for range requests so that the server '
                             'does not stall waiting on a new range from one server and '
                             'can request a range from a new server (dnaRangeRequestTimeout)')

def create_parser(subparsers):
    dna = subparsers.add_parser('dna', help='Manage and configure DNA plugin', formatter_class=CustomHelpFormatter)
    subcommands = dna.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, DNAPlugin)

    list = subcommands.add_parser('list', help='List available plugin configs', formatter_class=CustomHelpFormatter)
    subcommands_list = list.add_subparsers(help='action')
    list_configs = subcommands_list.add_parser('configs', help='List main DNA plugin config entries', formatter_class=CustomHelpFormatter)
    list_configs.set_defaults(func=dna_list)
    list_shared_configs = subcommands_list.add_parser('shared-configs', help='List DNA plugin shared config entries', formatter_class=CustomHelpFormatter)
    list_shared_configs.add_argument('BASEDN', help='The search DN')
    list_shared_configs.set_defaults(func=dna_config_list)

    config = subcommands.add_parser('config', help='Manage plugin configs', formatter_class=CustomHelpFormatter)
    config.add_argument('NAME', help='The DNA configuration name')
    config_subcommands = config.add_subparsers(help='action')
    add = config_subcommands.add_parser('add', help='Add the config entry', formatter_class=CustomHelpFormatter)
    add.set_defaults(func=dna_add)
    _add_parser_args(add)
    edit = config_subcommands.add_parser('set', help='Edit the config entry', formatter_class=CustomHelpFormatter)
    edit.set_defaults(func=dna_edit)
    _add_parser_args(edit)
    show = config_subcommands.add_parser('show', help='Display the config entry', formatter_class=CustomHelpFormatter)
    show.set_defaults(func=dna_show)
    delete = config_subcommands.add_parser('delete', help='Delete the config entry', formatter_class=CustomHelpFormatter)
    delete.set_defaults(func=dna_del)

    shared_config = config_subcommands.add_parser('shared-config-entry', help='Manage the shared config entry', formatter_class=CustomHelpFormatter)
    shared_config.add_argument('SHARED_CFG',
                               help='Use HOSTNAME:PORT for this argument to identify the host name and port of a server in a shared range, as part of the DNA range '
                                    'configuration for that specific host in multi-supplier replication.  (dnaHostname+dnaPortNum)')
    shared_config_subcommands = shared_config.add_subparsers(help='action')
    edit_config = shared_config_subcommands.add_parser('set', help='Edit the shared config entry', formatter_class=CustomHelpFormatter)
    edit_config.set_defaults(func=dna_config_edit)
    edit_config.add_argument('--remote-bind-method', help='Specifies the remote bind method "SIMPLE", "SSL" (for SSL client auth), "SASL/GSSAPI", or "SASL/DIGEST-MD5" (dnaRemoteBindMethod)')
    edit_config.add_argument('--remote-conn-protocol', help='Specifies the remote connection protocol "LDAP", or "TLS" (dnaRemoteConnProtocol)')

    show_config_parser = shared_config_subcommands.add_parser('show', help='Display the shared config entry', formatter_class=CustomHelpFormatter)
    show_config_parser.set_defaults(func=dna_config_show)
    del_config_parser = shared_config_subcommands.add_parser('delete', help='Delete the shared config entry', formatter_class=CustomHelpFormatter)
    del_config_parser.set_defaults(func=dna_config_del)
