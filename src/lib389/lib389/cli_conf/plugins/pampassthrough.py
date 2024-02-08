# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import (PAMPassThroughAuthPlugin,
                            PAMPassThroughAuthConfigs, PAMPassThroughAuthConfig)
from lib389.cli_base import CustomHelpFormatter
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add


arg_to_attr_pam = {
    'exclude_suffix': 'pamExcludeSuffix',
    'include_suffix': 'pamIncludeSuffix',
    'missing_suffix': 'pamMissingSuffix',
    'filter': 'pamFilter',
    'id_attr': 'pamIDAttr',
    'id_map_method': 'pamIDMapMethod',
    'fallback': 'pamFallback',
    'secure': 'pamSecure',
    'service': 'pamService'
}


def pam_pta_list(inst, basedn, log, args):
    log = log.getChild('pam_pta_list')
    configs = PAMPassThroughAuthConfigs(inst)
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
            log.info("No PAM Pass Through Auth plugin config instances")


def pam_pta_add(inst, basedn, log, args):
    log = log.getChild('pam_pta_add')
    plugin = PAMPassThroughAuthPlugin(inst)
    props = {'cn': args.NAME}
    generic_object_add(PAMPassThroughAuthConfig, inst, log, args, arg_to_attr_pam, basedn=plugin.dn, props=props)


def pam_pta_edit(inst, basedn, log, args):
    log = log.getChild('pam_pta_edit')
    configs = PAMPassThroughAuthConfigs(inst)
    config = configs.get(args.NAME)
    generic_object_edit(config, log, args, arg_to_attr_pam)


def pam_pta_show(inst, basedn, log, args):
    log = log.getChild('pam_pta_show')
    configs = PAMPassThroughAuthConfigs(inst)
    config = configs.get(args.NAME)

    if not config.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.name)
    if args and args.json:
        o_str = config.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(config.display())


def pam_pta_del(inst, basedn, log, args):
    log = log.getChild('pam_pta_del')
    configs = PAMPassThroughAuthConfigs(inst)
    config = configs.get(args.NAME)
    config.delete()
    log.info("Successfully deleted the %s", config.dn)


def _add_parser_args_pam(parser):
    parser.add_argument('--exclude-suffix', nargs='+',
                        help='Specifies a suffix to exclude from PAM authentication (pamExcludeSuffix)')
    parser.add_argument('--include-suffix', nargs='+',
                        help='Sets a suffix to include for PAM authentication (pamIncludeSuffix)')
    parser.add_argument('--missing-suffix', choices=['ERROR', 'ALLOW', 'IGNORE', 'delete', ''],
                        help='Identifies how to handle missing include or exclude suffixes (pamMissingSuffix)')
    parser.add_argument('--filter',
                        help='Sets an LDAP filter to use to identify specific entries within '
                             'the included suffixes for which to use PAM pass-through authentication (pamFilter)')
    parser.add_argument('--id-attr',
                        help='Contains the attribute name which is used to hold the PAM user ID (pamIDAttr)')
    parser.add_argument('--id_map_method',
                        help='Sets the method to use to map the LDAP bind DN to a PAM identity (pamIDMapMethod)')
    parser.add_argument('--fallback', choices=['TRUE', 'FALSE'], type=str.upper,
                        help='Sets whether to fallback to regular LDAP authentication '
                             'if PAM authentication fails (pamFallback)')
    parser.add_argument('--secure', choices=['TRUE', 'FALSE'], type=str.upper,
                        help='Requires secure TLS connection for PAM authentication (pamSecure)')
    parser.add_argument('--service',
                        help='Contains the service name to pass to PAM (pamService)')


def create_parser(subparsers):
    passthroughauth_parser = subparsers.add_parser('pam-pass-through-auth',
                                                   help='Manage and configure Pass-Through Authentication plugins '
                                                        '(LDAP URLs and PAM)')
    subcommands = passthroughauth_parser.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, PAMPassThroughAuthPlugin)

    list_pam = subcommands.add_parser('list', help='Lists PAM configurations', formatter_class=CustomHelpFormatter)
    list_pam.set_defaults(func=pam_pta_list)

    pam = subcommands.add_parser('config', help='Manage PAM PTA configurations.', formatter_class=CustomHelpFormatter)
    pam.add_argument('NAME', help='The PAM PTA configuration name')
    subcommands_pam = pam.add_subparsers(help='action')

    add = subcommands_pam.add_parser('add', help='Add the config entry', formatter_class=CustomHelpFormatter)
    add.set_defaults(func=pam_pta_add)
    _add_parser_args_pam(add)
    edit = subcommands_pam.add_parser('set', help='Edit the config entry', formatter_class=CustomHelpFormatter)
    edit.set_defaults(func=pam_pta_edit)
    _add_parser_args_pam(edit)
    show = subcommands_pam.add_parser('show', help='Display the config entry', formatter_class=CustomHelpFormatter)
    show.set_defaults(func=pam_pta_show)
    delete = subcommands_pam.add_parser('delete', help='Delete the config entry', formatter_class=CustomHelpFormatter)
    delete.set_defaults(func=pam_pta_del)
