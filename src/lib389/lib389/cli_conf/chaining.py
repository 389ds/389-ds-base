# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from lib389.chaining import (
    ChainingLinks, ChainingConfig, ChainingDefault)
from lib389.cli_base import (
    _generic_list,
    _generic_get,
    _get_arg,
    CustomHelpFormatter
    )
from lib389.cli_conf.monitor import _format_status
from lib389.utils import get_passwd_from_file

arg_to_attr = {
        'conn_bind_limit': 'nsbindconnectionslimit',
        'conn_op_limit': 'nsoperationconnectionslimit',
        'abandon_check_interval': 'nsabandonedsearchcheckinterval',
        'bind_limit': 'nsconcurrentbindlimit',
        'op_limit': 'nsconcurrentoperationslimit',
        'proxied_auth': 'nsproxiedauthorization',
        'conn_lifetime': 'nsconnectionlife',
        'bind_timeout': 'nsbindtimeout',
        'return_ref': 'nsreferralonscopedsearch',
        'check_aci': 'nschecklocalaci',
        'bind_attempts': 'nsbindretrylimit',
        'size_limit': 'nsslapd-sizelimit',
        'time_limit': 'nsslapd-timelimit',
        'hop_limit': 'nshoplimit',
        'response_delay': 'nsmaxresponsedelay',
        'test_response_delay': 'nsmaxtestresponsedelay',
        'use_starttls': 'nsusestarttls',
        'server_url': 'nsfarmserverurl',
        'bind_mech': 'nsbindmechanism',
        'bind_dn': 'nsmultiplexorbinddn',
        'bind_pw': 'nsmultiplexorcredentials',
        'suffix': 'nsslapd-suffix'
    }


def _args_to_attrs(args):
    attrs = {}
    for arg in vars(args):
        val = getattr(args, arg)
        if arg in arg_to_attr and val is not None:
            attrs[arg_to_attr[arg]] = val
    return attrs


def _get_link(inst, rdn):
    found = False
    links = ChainingLinks(inst).list()
    for link in links:
        cn = link.get_attr_val_utf8_l('cn')
        suffix = link.get_attr_val_utf8_l('nsslapd-suffix')
        if cn == rdn.lower() or suffix == rdn.lower():
            found = True
            return link
    if not found:
        raise ValueError("Could not find database link '{}'".format(rdn))


def config_get(inst, basedn, log, args):
    chain_cfg = ChainingConfig(inst)
    if args.avail_controls or args.avail_comps:
        if args.avail_controls:
            ctrls = chain_cfg.get_controls()
            if args.json:
                print(json.dumps({"type": "list", "items": ctrls}, indent=4))
            else:
                print("Available Components:")
                for ctrl in ctrls:
                    print(ctrl)
        if args.avail_comps:
            comps = chain_cfg.get_comps()
            if args.json:
                print(json.dumps({"type": "list", "items": comps}, indent=4))
            else:
                print("Available Controls:")
                for comp in comps:
                    print(comp)
    else:
        if args.json:
            print(chain_cfg.get_all_attrs_json())
        else:
            print(chain_cfg.display())


def config_set(inst, basedn, log, args):
    chain_cfg = ChainingConfig(inst)
    did_something = False

    # Add control
    if args.add_control is not None:
        for ctrl in args.add_control:
            chain_cfg.add('nstransmittedcontrols', ctrl)
        did_something = True

    # Delete control
    if args.del_control is not None:
        for ctrl in args.del_control:
            chain_cfg.remove('nstransmittedcontrols', ctrl)
        did_something = True

    # Add component
    if args.add_comp is not None:
        for comp in args.add_comp:
            chain_cfg.add('nsactivechainingcomponents', comp)
        did_something = True

    # Del component
    if args.del_comp is not None:
        for comp in args.del_comp:
            chain_cfg.remove('nsactivechainingcomponents', comp)
        did_something = True

    if did_something:
        print("Successfully updated chaining configuration")
    else:
        raise ValueError('There are no changes to set for the chaining configuration')


def def_config_get(inst, basedn, log, args):
    def_chain_cfg = ChainingDefault(inst)
    if args and args.json:
        print(def_chain_cfg.get_all_attrs_json())
    else:
        print(def_chain_cfg.display())


def def_config_set(inst, basedn, log, args):
    chain_cfg = ChainingDefault(inst)
    attrs = _args_to_attrs(args)
    did_something = False
    replace_list = []

    for attr, value in list(attrs.items()):
        if value is False:
            value = "off"
        elif value is True:
            value = "on"
        if value == "":
            # Delete value
            chain_cfg.remove_all(attr)
            did_something = True
        else:
            replace_list.append((attr, value))
    if len(replace_list) > 0:
        chain_cfg.replace_many(*replace_list)
    elif not did_something:
        raise ValueError("There are no changes to set in the chaining default instance creation configuration")

    print("Successfully updated chaining default instance creation configuration")


def create_link(inst, basedn, log, args, warn=True):
    if args.bind_pw_file is not None:
        args.bind_pw = get_passwd_from_file(args.bind_pw_file)
    elif args.bind_pw_prompt:
        args.bind_pw = _get_arg(None, msg="Enter remote bind DN password", hidden=True, confirm=True)
    attrs = _args_to_attrs(args)
    attrs['cn'] = args.CHAIN_NAME[0]
    links = ChainingLinks(inst)
    links.add_link(attrs)
    print('Successfully created database link')


def get_link(inst, basedn, log, args, warn=True):
    rdn = _get_arg(args.CHAIN_NAME[0], msg="Enter 'cn' to retrieve")
    _generic_get(inst, basedn, log.getChild('get_link'), ChainingLinks, rdn, args)


def edit_link(inst, basedn, log, args):
    chain_link = _get_link(inst, args.CHAIN_NAME[0])
    if args.bind_pw_file is not None:
        args.bind_pw = get_passwd_from_file(args.bind_pw_file)
    elif args.bind_pw_prompt:
        args.bind_pw = _get_arg(None, msg="Enter remote bind DN password", hidden=True, confirm=True)
    attrs = _args_to_attrs(args)
    did_something = False
    replace_list = []

    for attr, value in list(attrs.items()):
        if value == "":
            # Delete value
            chain_link.remove_all(attr)
            did_something = True
        else:
            replace_list.append((attr, value))
    if len(replace_list) > 0:
        chain_link.replace_many(*replace_list)
    elif not did_something:
        raise ValueError("There are no changes to set in the database chaining link")

    print("Successfully updated database chaining link")


def delete_link(inst, basedn, log, args, warn=True):
    chain_link = _get_link(inst, args.CHAIN_NAME[0])
    chain_link.del_link()
    print('Successfully deleted database link')


def monitor_link(inst, basedn, log, args):
    chain_link = _get_link(inst, args.CHAIN_NAME[0])
    monitor = chain_link.get_monitor()
    if monitor is not None:
        _format_status(log, monitor, args.json)
    else:
        raise ValueError("There was no monitor found for link '{}'".format(args.CHAIN_NAME[0]))


def list_links(inst, basedn, log, args):
    _generic_list(inst, basedn, log.getChild('chaining_list'), ChainingLinks, args)


def create_parser(subparsers):
    chaining_parser = subparsers.add_parser('chaining', help="Manage database chaining and database links", formatter_class=CustomHelpFormatter)
    subcommands = chaining_parser.add_subparsers(help="action")

    config_get_parser = subcommands.add_parser('config-get', help='Display the chaining controls and server component lists', formatter_class=CustomHelpFormatter)
    config_get_parser.set_defaults(func=config_get)
    config_get_parser.add_argument('--avail-controls', action='store_true', help="Lists available chaining controls")
    config_get_parser.add_argument('--avail-comps', action='store_true', help="Lists available chaining plugin components")

    config_set_parser = subcommands.add_parser('config-set', help='Set the chaining controls and server component lists', formatter_class=CustomHelpFormatter)
    config_set_parser.set_defaults(func=config_set)
    config_set_parser.add_argument('--add-control', action='append', help="Adds a transmitted control OID")
    config_set_parser.add_argument('--del-control', action='append', help="Deletes a transmitted control OID")
    config_set_parser.add_argument('--add-comp', action='append', help="Adds a chaining component")
    config_set_parser.add_argument('--del-comp', action='append', help="Deletes a chaining component")

    def_config_get_parser = subcommands.add_parser('config-get-def', help='Display the default creation parameters for new database links', formatter_class=CustomHelpFormatter)
    def_config_get_parser.set_defaults(func=def_config_get)

    def_config_set_parser = subcommands.add_parser('config-set-def', help='Set the default creation parameters for new database links', formatter_class=CustomHelpFormatter)
    def_config_set_parser.set_defaults(func=def_config_set)
    def_config_set_parser.add_argument('--conn-bind-limit',
                                       help="Sets the maximum number of BIND connections the database link establishes "
                                            "with the remote server")
    def_config_set_parser.add_argument('--conn-op-limit',
                                       help="Sets the maximum number of LDAP connections the database link establishes "
                                            "with the remote server ")
    def_config_set_parser.add_argument('--abandon-check-interval',
                                       help="Sets the number of seconds that pass before the server checks for "
                                            "abandoned operations")
    def_config_set_parser.add_argument('--bind-limit',
                                       help="Sets the maximum number of concurrent bind operations per TCP connection")
    def_config_set_parser.add_argument('--op-limit', help="Sets the maximum number of concurrent operations allowed")
    def_config_set_parser.add_argument('--proxied-auth',
                                       help="Enables or disables proxied authorization. If set to \"off\", the server "
                                            "executes bind for chained operations as the user set in the "
                                            "nsMultiplexorBindDn attribute.")
    def_config_set_parser.add_argument('--conn-lifetime',
                                       help="Specifies connection lifetime in seconds. \"0\" keeps the connection open forever.")
    def_config_set_parser.add_argument('--bind-timeout', help="Sets the amount of time in seconds before a bind attempt times out")
    def_config_set_parser.add_argument('--return-ref', help="Enables or disables whether referrals are returned by scoped searches")
    def_config_set_parser.add_argument('--check-aci',
                                       help="Enables or disables whether the server evaluates ACIs on the database "
                                            "link as well as the remote data server")
    def_config_set_parser.add_argument('--bind-attempts', help="Sets the number of times the server tries to bind to the remote server")
    def_config_set_parser.add_argument('--size-limit', help="Sets the maximum number of entries to return from a search operation")
    def_config_set_parser.add_argument('--time-limit', help="Sets the maximum number of seconds allowed for an operation")
    def_config_set_parser.add_argument('--hop-limit',
                                       help="Sets the maximum number of times a database is allowed to chain. That is "
                                             "the number of times a request can be forwarded from one database link to another.")
    def_config_set_parser.add_argument('--response-delay',
                                       help="Sets the maximum amount of time it can take a remote server to respond to "
                                            "an LDAP operation request made by a database link before an error is suspected")
    def_config_set_parser.add_argument('--test-response-delay',
                                       help="Sets the duration of the test issued by the database link to check whether "
                                            "the remote server is responding")
    def_config_set_parser.add_argument('--use-starttls', help="Configured that database links use StartTLS if set to \"on\"")

    create_link_parser = subcommands.add_parser('link-create', add_help=False, conflict_handler='resolve',
                                                parents=[def_config_set_parser],
                                                help='Create a database link to a remote server')
    create_link_parser.set_defaults(func=create_link)
    create_link_parser.add_argument('CHAIN_NAME', nargs=1, help='The name of the database link')
    create_link_parser.add_argument('--suffix', required=True, help="Sets the suffix managed by the database link")
    create_link_parser.add_argument('--server-url', required=True, help="Sets the LDAP/LDAPS URL to the remote server")
    create_link_parser.add_argument('--bind-mech', required=True,
                                    help="Sets the authentication method to use to authenticate to the remote server. "
                                         "Valid values: \"SIMPLE\" (default), \"EXTERNAL\", \"DIGEST-MD5\", or \"GSSAPI\"")
    create_link_parser.add_argument('--bind-dn', required=True,
                                    help="Sets the DN of the administrative entry used to communicate with the remote server")
    create_link_parser.add_argument('--bind-pw', help="Sets the password of the administrative user")
    create_link_parser.add_argument('--bind-pw-file', help="File containing the password")
    create_link_parser.add_argument('--bind-pw-prompt', action='store_true', help="Prompt for password")

    get_link_parser = subcommands.add_parser('link-get', help='Displays chaining database links', formatter_class=CustomHelpFormatter)
    get_link_parser.set_defaults(func=get_link)
    get_link_parser.add_argument('CHAIN_NAME', nargs=1, help='The chaining link name or suffix to retrieve')

    edit_link_parser = subcommands.add_parser('link-set', add_help=False, conflict_handler='resolve',
        parents=[def_config_set_parser], help='Edit a database link to a remote server')
    edit_link_parser.set_defaults(func=edit_link)
    edit_link_parser.add_argument('CHAIN_NAME', nargs=1, help='The name of the database link')
    edit_link_parser.add_argument('--suffix', help="Sets the suffix managed by the database link")
    edit_link_parser.add_argument('--server-url', help="Sets the LDAP/LDAPS URL to the remote server")
    edit_link_parser.add_argument('--bind-mech',
                                  help="Sets the authentication method to use to authenticate to the remote server: "
                                       "Valid values: \"SIMPLE\" (default), \"EXTERNAL\", \"DIGEST-MD5\", or \"GSSAPI\"")
    edit_link_parser.add_argument('--bind-dn',
                                  help="Sets the DN of the administrative entry used to communicate with the remote server")
    edit_link_parser.add_argument('--bind-pw', help="Sets the password of the administrative user")
    edit_link_parser.add_argument('--bind-pw-file', help="File containing the password")
    edit_link_parser.add_argument('--bind-pw-prompt', action='store_true', help="Prompt for password")

    delete_link_parser = subcommands.add_parser('link-delete', help='Delete a database link', formatter_class=CustomHelpFormatter)
    delete_link_parser.set_defaults(func=delete_link)
    delete_link_parser.add_argument('CHAIN_NAME', nargs=1, help='The name of the database link')

    monitor_link_parser = subcommands.add_parser('monitor', help='Display monitor information for a database chaining link', formatter_class=CustomHelpFormatter)
    monitor_link_parser.set_defaults(func=monitor_link)
    monitor_link_parser.add_argument('CHAIN_NAME', nargs=1, help='The name of the database link')

    list_link_parser = subcommands.add_parser('link-list', help='List database links', formatter_class=CustomHelpFormatter)
    list_link_parser.set_defaults(func=list_links)
