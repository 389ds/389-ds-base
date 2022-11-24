# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# Copyright (C) 2022 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.plugins import (PassThroughAuthenticationPlugin)

from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add, generic_show, generic_enable, generic_disable, generic_status


def _get_url_next_num(url_attrs):
    existing_nums = list(map(lambda url: int(url.split('nsslapd-pluginarg')[1]),
                             [i for i, _ in url_attrs.items()]))
    if len(existing_nums) > 0:
        existing_nums.sort()
        full_num_list = list(range(existing_nums[-1]+2))
        if not full_num_list:
            next_num_list = ["0"]
        else:
            next_num_list = list(filter(lambda x: x not in existing_nums, full_num_list))
    else:
        next_num_list = ["0"]

    return next_num_list[0]

def _get_url_attr(url_attrs,  this_url):
    for attr, val in url_attrs.items():
        if val.lower() == this_url.lower():
            return attr

    return "nsslapd-pluginarg0"

def _validate_url(url):
    failed = False
    if len(url.split(" ")) == 2:
        link = url.split(" ")[0]
        params = url.split(" ")[1]
    else:
        link = url
        params = ""

    if (":" not in link) or ("//" not in link) or ("/" not in link) or (params and "," not in params):
        failed = False

    if not ldap.dn.is_dn(link.split("/")[-1]):
        raise ValueError("Subtree is an invalid DN")

    if params and len(params.split(",")) != 6 and not all(map(str.isdigit, params.split(","))):
        failed = False

    if failed:
        raise ValueError("URL should be in one of the next formats (all parameters after a space should be digits): "
                         "'ldap|ldaps://authDS/subtree maxconns,maxops,timeout,ldver,connlifetime,startTLS' or "
                         "'ldap|ldaps://authDS/subtree'")
    return url


def pta_list(inst, basedn, log, args):
    log = log.getChild('pta_list')
    plugin = PassThroughAuthenticationPlugin(inst)
    urls = plugin.get_urls()
    if args.json:
        log.info(json.dumps({"type": "list",
                             "items": [{"id": id, "url": value} for id, value in urls.items()]},
                            indent=4))
    else:
        if len(urls) > 0:
            for _, value in urls.items():
                log.info(value)
        else:
            log.info("No Pass Through Auth URLs were found")


def pta_add(inst, basedn, log, args):
    log = log.getChild('pta_add')
    new_url_l = _validate_url(args.URL.lower())
    plugin = PassThroughAuthenticationPlugin(inst)
    url_attrs = plugin.get_urls()
    urls = list(map(lambda url: url.lower(),
                    [i for _, i in url_attrs.items()]))
    next_num = _get_url_next_num(url_attrs)
    if new_url_l in urls:
        raise ldap.ALREADY_EXISTS("Entry %s already exists" % args.URL)
    plugin.add("nsslapd-pluginarg%s" % next_num, args.URL)


def pta_edit(inst, basedn, log, args):
    log = log.getChild('pta_edit')
    plugin = PassThroughAuthenticationPlugin(inst)
    url_attrs = plugin.get_urls()
    urls = list(map(lambda url: url.lower(),
                    [i for _, i in url_attrs.items()]))
    _validate_url(args.NEW_URL.lower())
    old_url_l = args.OLD_URL.lower()
    if old_url_l not in urls:
        raise ValueError("URL %s doesn't exist." % args.OLD_URL)
    else:
        for attr, value in url_attrs.items():
            if value.lower() == old_url_l:
                plugin.remove(attr, old_url_l)
                break
    plugin.add("%s" % _get_url_attr(url_attrs, old_url_l), args.NEW_URL)


def pta_del(inst, basedn, log, args):
    log = log.getChild('pta_del')
    plugin = PassThroughAuthenticationPlugin(inst)
    url_attrs = plugin.get_urls()
    urls = list(map(lambda url: url.lower(),
                    [i for _, i in url_attrs.items()]))
    old_url_l = args.URL.lower()
    if old_url_l not in urls:
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % args.URL)

    plugin.remove_all("%s" % _get_url_attr(url_attrs, old_url_l))
    log.info("Successfully deleted %s", args.URL)


def create_parser(subparsers):
    passthroughauth_parser = subparsers.add_parser('ldap-pass-through-auth',
                                                   help='Manage and configure LDAP Pass-Through Authentication Plugin')
    subcommands = passthroughauth_parser.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, PassThroughAuthenticationPlugin)

    list_urls = subcommands.add_parser('list', help='Lists LDAP URLs')
    list_urls.set_defaults(func=pta_list)

    # url = subcommands.add_parser('url', help='Manage PTA LDAP URL configurations')
    # subcommands_url = url.add_subparsers(help='action')

    add_url = subcommands.add_parser('add', help='Add an LDAP url to the config entry')
    add_url.add_argument('URL',
                         help='The full LDAP URL in format '
                              '"ldap|ldaps://authDS/subtree maxconns,maxops,timeout,ldver,connlifetime,startTLS". '
                              'If one optional parameter is specified the rest should be specified too')
    add_url.set_defaults(func=pta_add)

    edit_url = subcommands.add_parser('modify', help='Edit the LDAP pass through config entry')
    edit_url.add_argument('OLD_URL', help='The full LDAP URL you get from the "list" command')
    edit_url.add_argument('NEW_URL',
                          help='Sets the full LDAP URL in format '
                               '"ldap|ldaps://authDS/subtree maxconns,maxops,timeout,ldver,connlifetime,startTLS". '
                               'If one optional parameter is specified the rest should be specified too.')
    edit_url.set_defaults(func=pta_edit)

    delete_url = subcommands.add_parser('delete', help='Delete a URL from the config entry')
    delete_url.add_argument('URL', help='The full LDAP URL you get from the "list" command')
    delete_url.set_defaults(func=pta_del)

