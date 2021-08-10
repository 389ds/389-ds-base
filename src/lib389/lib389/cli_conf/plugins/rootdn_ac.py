# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import socket
from lib389.plugins import RootDNAccessControlPlugin
from lib389.utils import is_valid_hostname
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit

arg_to_attr = {
    'allow_host': 'rootdn-allow-host',
    'deny_host': 'rootdn-deny-host',
    'allow_ip': 'rootdn-allow-ip',
    'deny_ip': 'rootdn-deny-ip',
    'open_time': 'rootdn-open-time',
    'close_time': 'rootdn-close-time',
    'days_allowed': 'rootdn-days-allowed'
}


def validate_args(args):
    # validate the args
    if args.close_time is not None:
        try:
            int(args.close_time)
        except:
            raise ValueError("The close time must be a 4 digit number: HHMM")
        if len(args.close_time) != 4:
            raise ValueError("The close time must be a 4 digit number: HHMM")
        hour = int(args.close_time[:2])
        if hour < 0 or hour > 23:
            raise ValueError(f"The hour portion of the time is invalid: {hour}  Must be between 0 and 23")
        min = int(args.close_time[-2:])
        if min < 0 or min > 59:
            raise ValueError(f"The minute portion of the time is invalid: {min}  Must be between 1 and 59")

    if args.open_time is not None:
        try:
            int(args.open_time)
        except:
            raise ValueError("The open time must be a 4 digit number: HHMM")
        if len(args.open_time) != 4:
            raise ValueError("The open time must be a 4 digit number: HHMM")
        hour = int(args.open_time[:2])
        if hour < 0 or hour > 23:
            raise ValueError(f"The hour portion of the time is invalid: {hour}  Must be between 0 and 23")
        min = int(args.open_time[-2:])
        if min < 0 or min > 59:
            raise ValueError(f"The minute portion of the time is invalid: {min}  Must be between 1 and 59")

    if args.days_allowed is not None and args.days_allowed != "delete":
        valid_days = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun']
        choosen_days =  args.days_allowed.lower().replace(' ', '').split(',')
        for day in choosen_days:
            if day not in valid_days:
                raise ValueError(f"Invalid day entered ({day}), valid days are: Mon, Tue, Wed, Thu, Fri, Sat, Sun")

    if args.allow_ip is not None:
        for ip in args.allow_ip:
            if ip != "delete":
                try:
                    socket.inet_aton(ip)
                except socket.error:
                    raise ValueError(f"Invalid IP address ({ip}) for '--allow-ip'")

    if args.deny_ip is not None and args.deny_ip != "delete":
        for ip in args.deny_ip:
            if ip != "delete":
                try:
                    socket.inet_aton(ip)
                except socket.error:
                    raise ValueError(f"Invalid IP address ({ip}) for '--deny-ip'")

    if args.allow_host is not None:
        for hostname in args.allow_host:
            if hostname != "delete" and not is_valid_hostname(hostname):
                raise ValueError(f"Invalid hostname ({hostname}) for '--allow-host'")

    if args.deny_host is not None:
        for hostname in args.deny_host:
            if hostname != "delete" and not is_valid_hostname(hostname):
                raise ValueError(f"Invalid hostname ({hostname}) for '--deny-host'")


def rootdn_edit(inst, basedn, log, args):
    log = log.getChild('rootdn_edit')
    validate_args(args)
    plugin = RootDNAccessControlPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def _add_parser_args(parser):
    parser.add_argument('--allow-host', nargs='+',
                        help='Sets what hosts, by fully-qualified domain name, the root user is allowed to use '
                             'to access the Directory Server. Any hosts not listed are implicitly denied '
                             '(rootdn-allow-host)')
    parser.add_argument('--deny-host', nargs='+',
                        help='Sets what hosts, by fully-qualified domain name, the root user is not allowed to use '
                             'to access the Directory Server Any hosts not listed are implicitly allowed '
                             '(rootdn-deny-host). If an host address is listed in both the rootdn-allow-host and '
                             'rootdn-deny-host attributes, it is denied access.')
    parser.add_argument('--allow-ip', nargs='+',
                        help='Sets what IP addresses, either IPv4 or IPv6, for machines the root user is allowed '
                             'to use to access the Directory Server Any IP addresses not listed are implicitly '
                             'denied (rootdn-allow-ip)')
    parser.add_argument('--deny-ip', nargs='+',
                        help='Sets what IP addresses, either IPv4 or IPv6, for machines the root user is not allowed '
                             'to use to access the Directory Server. Any IP addresses not listed are implicitly '
                             'allowed (rootdn-deny-ip) If an IP address is listed in both the rootdn-allow-ip and '
                             'rootdn-deny-ip attributes, it is denied access.')
    parser.add_argument('--open-time',
                        help='Sets part of a time period or range when the root user is allowed to access '
                             'the Directory Server. This sets when the time-based access begins (rootdn-open-time)')
    parser.add_argument('--close-time',
                        help='Sets part of a time period or range when the root user is allowed to access '
                             'the Directory Server. This sets when the time-based access ends (rootdn-close-time)')
    parser.add_argument('--days-allowed',
                        help='Gives a comma-separated list of what days the root user is allowed to use to access '
                             'the Directory Server. Any days listed are implicitly denied (rootdn-days-allowed)')


def create_parser(subparsers):
    rootdnac_parser = subparsers.add_parser('root-dn', help='Manage and configure RootDN Access Control plugin')
    subcommands = rootdnac_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, RootDNAccessControlPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin')
    edit.set_defaults(func=rootdn_edit)
    _add_parser_args(edit)


