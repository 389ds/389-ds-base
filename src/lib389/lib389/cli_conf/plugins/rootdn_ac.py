# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import RootDNAccessControlPlugin
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


def rootdn_edit(inst, basedn, log, args):
    log = log.getChild('rootdn_edit')
    plugin = RootDNAccessControlPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def _add_parser_args(parser):
    parser.add_argument('--allow-host',
                        help='Sets what hosts, by fully-qualified domain name, the root user is allowed to use '
                             'to access the Directory Server. Any hosts not listed are implicitly denied '
                             '(rootdn-allow-host)')
    parser.add_argument('--deny-host',
                        help='Sets what hosts, by fully-qualified domain name, the root user is not allowed to use '
                             'to access the Directory Server Any hosts not listed are implicitly allowed '
                             '(rootdn-deny-host). If an host address is listed in both the rootdn-allow-host and '
                             'rootdn-deny-host attributes, it is denied access.')
    parser.add_argument('--allow-ip',
                        help='Sets what IP addresses, either IPv4 or IPv6, for machines the root user is allowed '
                             'to use to access the Directory Server Any IP addresses not listed are implicitly '
                             'denied (rootdn-allow-ip)')
    parser.add_argument('--deny-ip',
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


