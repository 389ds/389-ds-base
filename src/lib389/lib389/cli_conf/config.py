# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.config import Config
from lib389.cli_base import (
    _generic_get_entry,
    _generic_get_attr,
    _generic_add_attr,
    _generic_replace_attr,
    _generic_del_attr,
    )


def _config_display_ldapimaprootdn_warning(log, args):
    """If we update the rootdn we need to update the ldapi settings too"""

    for attr in args.attr:
        if attr.lower().startswith('nsslapd-ldapimaprootdn='):
            log.warning("The \"nsslapd-ldapimaprootdn\" setting is obsolete and kept for compatibility reasons. "
                        "For LDAPI configuration, \"nsslapd-rootdn\" is used instead.")


def config_get(inst, basedn, log, args):
    if args and args.attrs:
        _generic_get_attr(inst, basedn, log.getChild('config_get'), Config, args)
    else:
        # Get the entire cn=config entry
        _generic_get_entry(inst, basedn, log.getChild('config_get'), Config, args)


def config_add_attr(inst, basedn, log, args):
    _generic_add_attr(inst, basedn, log.getChild('config_add_attr'), Config, args)

    _config_display_ldapimaprootdn_warning(log, args)


def config_replace_attr(inst, basedn, log, args):
    _generic_replace_attr(inst, basedn, log.getChild('config_replace_attr'), Config, args)

    _config_display_ldapimaprootdn_warning(log, args)


def config_del_attr(inst, basedn, log, args):
    _generic_del_attr(inst, basedn, log.getChild('config_del_attr'), Config, args)


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
