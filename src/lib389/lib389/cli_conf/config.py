# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from enum import Enum
from lib389.config import Config
from lib389.cli_base import (
    _generic_get_entry,
    _generic_get_attr,
    _generic_replace_attr,
    )

OpType = Enum("OpType", "add delete")


def _config_display_ldapimaprootdn_warning(log, args):
    """If we update the rootdn we need to update the ldapi settings too"""

    for attr in args.attr:
        if attr.lower().startswith('nsslapd-ldapimaprootdn='):
            log.warning("The \"nsslapd-ldapimaprootdn\" setting is obsolete and kept for compatibility reasons. "
                        "For LDAPI configuration, \"nsslapd-rootdn\" is used instead.")


def _config_get_existing_attrs(conf, args, op_type):
    """Get the existing attribute from the server and return them in a dict
    so we can add them back after the operation is done.

    For op_type == OpType.delete, we delete them from the server so we can add
    back only those that are not specified in the command line.
    (i.e delete nsslapd-haproxy-trusted-ip="192.168.0.1", but
    nsslapd-haproxy-trusted-ip has 192.168.0.1 and 192.168.0.2 values.
    So we want only 192.168.0.1 to be deleted in the end)
    """

    existing_attrs = {}
    if args and args.attr:
        for attr in args.attr:
            if "=" in attr:
                [attr_name, val] = attr.split("=", 1)
                # We should process only multi-valued attributes this way
                if attr_name.lower() == "nsslapd-haproxy-trusted-ip" or \
                   attr_name.lower() == "nsslapd-referral":
                    if attr_name not in existing_attrs.keys():
                        existing_attrs[attr_name] = conf.get_attr_vals_utf8(attr_name)
                    existing_attrs[attr_name] = [x for x in existing_attrs[attr_name] if x != val]

                    if op_type == OpType.add:
                        if existing_attrs[attr_name] == []:
                            del existing_attrs[attr_name]

                    if op_type == OpType.delete:
                        conf.remove_all(attr_name)
            else:
                if op_type == OpType.delete:
                    conf.remove_all(attr)
                else:
                    raise ValueError("You must specify a value to add for the attribute ({})".format(attr_name))
        return existing_attrs
    else:
        # Missing value
        raise ValueError(f"Missing attribute to {op_type.name}")


def config_get(inst, basedn, log, args):
    if args and args.attrs:
        _generic_get_attr(inst, basedn, log.getChild('config_get'), Config, args)
    else:
        # Get the entire cn=config entry
        _generic_get_entry(inst, basedn, log.getChild('config_get'), Config, args)


def config_replace_attr(inst, basedn, log, args):
    _generic_replace_attr(inst, basedn, log.getChild('config_replace_attr'), Config, args)

    _config_display_ldapimaprootdn_warning(log, args)


def config_add_attr(inst, basedn, log, args):
    conf = Config(inst, basedn)
    final_mods = []

    existing_attrs = _config_get_existing_attrs(conf, args, OpType.add)

    if args and args.attr:
        for attr in args.attr:
            if "=" in attr:
                [attr_name, val] = attr.split("=", 1)
                if attr_name in existing_attrs:
                    for v in existing_attrs[attr_name]:
                        final_mods.append((attr_name, v))
                final_mods.append((attr_name, val))
                try:
                    conf.add_many(*set(final_mods))
                except ldap.TYPE_OR_VALUE_EXISTS:
                    pass
            else:
                raise ValueError("You must specify a value to add for the attribute ({})".format(attr_name))
    else:
        # Missing value
        raise ValueError("Missing attribute to add")    

    _config_display_ldapimaprootdn_warning(log, args)


def config_del_attr(inst, basedn, log, args):
    conf = Config(inst, basedn)
    final_mods = []

    existing_attrs = _config_get_existing_attrs(conf, args, OpType.delete)

    # Then add the attributes back all except the one we need to remove
    for attr_name in existing_attrs.keys():
        for val in existing_attrs[attr_name]:
            final_mods.append((attr_name, val))
        conf.add_many(*set(final_mods))


def create_parser(subparsers):
    config_parser = subparsers.add_parser('config', help="Manage the server configuration")

    subcommands = config_parser.add_subparsers(help="action")

    get_parser = subcommands.add_parser('get', help='get')
    get_parser.set_defaults(func=config_get)
    get_parser.add_argument('attrs', nargs='*', help='Configuration attribute(s) to get')

    add_attr_parser = subcommands.add_parser('add', help='Add attribute value to configuration')
    add_attr_parser.set_defaults(func=config_add_attr)
    add_attr_parser.add_argument('attr', nargs='*', help='Configuration attribute to add')

    replace_attr_parser = subcommands.add_parser('replace', help='Replace attribute value in configuration')
    replace_attr_parser.set_defaults(func=config_replace_attr)
    replace_attr_parser.add_argument('attr', nargs='*', help='Configuration attribute to replace')

    del_attr_parser = subcommands.add_parser('delete', help='Delete attribute value in configuration')
    del_attr_parser.set_defaults(func=config_del_attr)
    del_attr_parser.add_argument('attr', nargs='*', help='Configuration attribute to delete')
