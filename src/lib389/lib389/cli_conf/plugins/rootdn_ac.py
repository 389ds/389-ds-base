# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap

from lib389.plugins import RootDNAccessControlPlugin
from lib389.cli_conf.plugin import add_generic_plugin_parsers


def display_time(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    val = plugin.get_open_time_formatted()
    if not val:
        log.info("rootdn-open-time is not set")
    else:
        log.info(val)
    val = plugin.get_close_time_formatted()
    if not val:
        log.info("rootdn-close-time is not set")
    else:
        log.info(val)

def set_open_time(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    plugin.set_open_time(args.value)
    log.info('rootdn-open-time set to "{}"'.format(args.value))

def set_close_time(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    plugin.set_close_time(args.value)
    log.info('rootdn-close-time set to "{}"'.format(args.value))

def clear_time(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    plugin.remove_open_time()
    plugin.remove_close_time()
    log.info('time-based policy was cleared')

def display_ips(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    allowed_ips = plugin.get_allow_ip_formatted()
    denied_ips = plugin.get_deny_ip_formatted()
    if not allowed_ips and not denied_ips:
        log.info("No ip-based access control policy has been configured")
    else:
        log.info(allowed_ips)
        log.info(denied_ips)

def allow_ip(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)

    # remove ip from denied ips
    try:
        plugin.remove_deny_ip(args.value)
    except ldap.NO_SUCH_ATTRIBUTE:
        pass

    try:
        plugin.add_allow_ip(args.value)
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    log.info('{} added to rootdn-allow-ip'.format(args.value))

def deny_ip(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)

    # remove ip from allowed ips
    try:
        plugin.remove_allow_ip(args.value)
    except ldap.NO_SUCH_ATTRIBUTE:
        pass

    try:
        plugin.add_deny_ip(args.value)
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    log.info('{} added to rootdn-deny-ip'.format(args.value))

def clear_all_ips(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    plugin.remove_all_allow_ip()
    plugin.remove_all_deny_ip()
    log.info('ip-based policy was cleared')

def display_hosts(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    allowed_hosts = plugin.get_allow_host_formatted()
    denied_hosts = plugin.get_deny_host_formatted()
    if not allowed_hosts and not denied_hosts:
        log.info("No host-based access control policy has been configured")
    else:
        log.info(allowed_hosts)
        log.info(denied_hosts)

def allow_host(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)

    # remove host from denied hosts
    try:
        plugin.remove_deny_host(args.value)
    except ldap.NO_SUCH_ATTRIBUTE:
        pass

    try:
        plugin.add_allow_host(args.value)
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    log.info('{} added to rootdn-allow-host'.format(args.value))

def deny_host(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)

    # remove host from allowed hosts
    try:
        plugin.remove_allow_host(args.value)
    except ldap.NO_SUCH_ATTRIBUTE:
        pass

    try:
        plugin.add_deny_host(args.value)
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    log.info('{} added to rootdn-deny-host'.format(args.value))

def clear_all_hosts(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    plugin.remove_all_allow_host()
    plugin.remove_all_deny_host()
    log.info('host-based policy was cleared')

def display_days(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    days = plugin.get_days_allowed_formatted()
    if not days:
        log.info("No day-based access control policy has been configured")
    else:
        log.info(days)

def allow_day(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    args.value = args.value[0:3]
    plugin.add_allow_day(args.value)
    log.info('{} added to rootdn-days-allowed'.format(args.value))

def deny_day(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    args.value = args.value[0:3]
    plugin.remove_allow_day(args.value)
    log.info('{} removed from rootdn-days-allowed'.format(args.value))

def clear_all_days(inst, basedn, log, args):
    plugin = RootDNAccessControlPlugin(inst)
    plugin.remove_days_allowed()
    log.info('day-based policy was cleared')


def create_parser(subparsers):
    rootdnac_parser = subparsers.add_parser('rootdn', help='Manage and configure RootDN Access Control plugin')
    subcommands = rootdnac_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, RootDNAccessControlPlugin)

    time_parser = subcommands.add_parser('time', help='get or set rootdn open and close times')
    time_parser.set_defaults(func=display_time)

    time_subcommands = time_parser.add_subparsers(help='action')

    open_time_parser = time_subcommands.add_parser('open', help='set open time value')
    open_time_parser.set_defaults(func=set_open_time)
    open_time_parser.add_argument('value', help='Value to set as open time')

    close_time_parser = time_subcommands.add_parser('close', help='set close time value')
    close_time_parser.set_defaults(func=set_close_time)
    close_time_parser.add_argument('value', help='Value to set as close time')

    time_clear_parser = time_subcommands.add_parser('clear', help='reset time-based access policy')
    time_clear_parser.set_defaults(func=clear_time)

    ip_parser = subcommands.add_parser('ip', help='get or set ip access policy')
    ip_parser.set_defaults(func=display_ips)

    ip_subcommands = ip_parser.add_subparsers(help='action')

    ip_allow_parser = ip_subcommands.add_parser('allow', help='allow IP addr or IP addr range')
    ip_allow_parser.set_defaults(func=allow_ip)
    ip_allow_parser.add_argument('value', help='IP addr or IP addr range')

    ip_deny_parser = ip_subcommands.add_parser('deny', help='deny IP addr or IP addr range')
    ip_deny_parser.set_defaults(func=deny_ip)
    ip_deny_parser.add_argument('value', help='IP addr or IP addr range')

    ip_clear_parser = ip_subcommands.add_parser('clear', help='reset IP-based access policy')
    ip_clear_parser.set_defaults(func=clear_all_ips)

    host_parser = subcommands.add_parser('host', help='get or set host access policy')
    host_parser.set_defaults(func=display_hosts)

    host_subcommands = host_parser.add_subparsers(help='action')

    host_allow_parser = host_subcommands.add_parser('allow', help='allow host address')
    host_allow_parser.set_defaults(func=allow_host)
    host_allow_parser.add_argument('value', help='host address')

    host_deny_parser = host_subcommands.add_parser('deny', help='deny host address')
    host_deny_parser.set_defaults(func=deny_host)
    host_deny_parser.add_argument('value', help='host address')

    host_clear_parser = host_subcommands.add_parser('clear', help='reset host-based access policy')
    host_clear_parser.set_defaults(func=clear_all_hosts)

    day_parser = subcommands.add_parser('day', help='get or set days access policy')
    day_parser.set_defaults(func=display_days)

    day_subcommands = day_parser.add_subparsers(help='action')

    day_allow_parser = day_subcommands.add_parser('allow', help='allow day of the week')
    day_allow_parser.set_defaults(func=allow_day)
    day_allow_parser.add_argument('value', type=str.capitalize, help='day of the week')

    day_deny_parser = day_subcommands.add_parser('deny', help='deny day of the week')
    day_deny_parser.set_defaults(func=deny_day)
    day_deny_parser.add_argument('value', type=str.capitalize, help='day of the week')

    day_clear_parser = day_subcommands.add_parser('clear', help='reset day-based access policy')
    day_clear_parser.set_defaults(func=clear_all_days)
