# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import PassThroughAuthenticationPlugin
from lib389.cli_conf import add_generic_plugin_parsers


def pta_list(inst, basedn, log, args):
    log = log.getChild('pta_list')
    plugin = PassThroughAuthenticationPlugin(inst)
    result = []
    urls = plugin.get_urls()
    if args.json:
        print(json.dumps({"type": "list", "items": urls}))
    else:
        if len(urls) > 0:
            for i in result:
                print(i)
        else:
            print("No Pass Through Auth attributes were found")


def pta_add(inst, basedn, log, args):
    log = log.getChild('pta_add')
    plugin = PassThroughAuthenticationPlugin(inst)
    urls = list(map(lambda url: url.lower(), plugin.get_urls()))
    if args.URL.lower() in urls:
        raise ldap.ALREADY_EXISTS("Entry %s already exists" % args.URL)
    plugin.add("nsslapd-pluginarg%s" % len(urls), args.URL)


def pta_edit(inst, basedn, log, args):
    log = log.getChild('pta_edit')
    plugin = PassThroughAuthenticationPlugin(inst)
    urls = list(map(lambda url: url.lower(), plugin.get_urls()))
    old_url_l = args.OLD_URL.lower()
    if old_url_l not in urls:
        log.info("Entry %s doesn't exists. Adding a new value." % args.OLD_URL)
        url_num = len(urls)
    else:
        url_num = urls.index(old_url_l)
        plugin.remove("nsslapd-pluginarg%s" % url_num, old_url_l)
    plugin.add("nsslapd-pluginarg%s" % url_num, args.NEW_URL)


def pta_del(inst, basedn, log, args):
    log = log.getChild('pta_del')
    plugin = PassThroughAuthenticationPlugin(inst)
    urls = list(map(lambda url: url.lower(), plugin.get_urls()))
    old_url_l = args.URL.lower()
    if old_url_l not in urls:
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.URL)

    plugin.remove_all("nsslapd-pluginarg%s" % urls.index(old_url_l))
    log.info("Successfully deleted %s", args.URL)


def create_parser(subparsers):
    passthroughauth_parser = subparsers.add_parser('pass-through-auth', help='Manage and configure Pass-Through Authentication plugin')
    subcommands = passthroughauth_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, PassThroughAuthenticationPlugin)

    list = subcommands.add_parser('list', help='List available plugin configs')
    list.set_defaults(func=pta_list)

    add = subcommands.add_parser('add', help='Add the config entry')
    add.add_argument('URL', help='The full LDAP URL in format '
                                  '"ldap|ldaps://authDS/subtree maxconns,maxops,timeout,ldver,connlifetime,startTLS". '
                                  'If one optional parameter is specified the rest should be specified too')
    add.set_defaults(func=pta_add)

    edit = subcommands.add_parser('modify', help='Edit the config entry')
    edit.add_argument('OLD_URL', help='The full LDAP URL you get from the "list" command')
    edit.add_argument('NEW_URL', help='The full LDAP URL in format '
                                 '"ldap|ldaps://authDS/subtree maxconns,maxops,timeout,ldver,connlifetime,startTLS". '
                                 'If one optional parameter is specified the rest should be specified too')
    edit.set_defaults(func=pta_edit)

    delete = subcommands.add_parser('delete', help='Delete the config entry')
    delete.add_argument('URL', help='The full LDAP URL you get from the "list" command')
    delete.set_defaults(func=pta_del)
