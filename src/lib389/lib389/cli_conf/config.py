# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
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
    CustomHelpFormatter
    )


def _config_display_ldapimaprootdn_warning(log, args):
    """If we update the rootdn we need to update the ldapi settings too"""

    for attr in args.attr:
        if attr.lower().startswith('nsslapd-ldapimaprootdn='):
            log.warning("The \"nsslapd-ldapimaprootdn\" setting is obsolete and kept for compatibility reasons. "
                        "For LDAPI configuration, \"nsslapd-rootdn\" is used instead.")


def _format_values_for_display(values):
    """Format a set of values for display"""

    if not values:
        return ""
    if len(values) == 1:
        return f"'{next(iter(values))}'"
    return ', '.join(f"'{value}'" for value in sorted(values))


def _parse_attr_value_pairs(attrs, allow_no_value=False):
    """Parse attribute value pairs from a list of strings in the format 'attr=value'"""

    attr_values = {}
    attrs_without_values = set()

    for attr in attrs:
        if "=" in attr:
            attr_name, val = attr.split("=", 1)
            if attr_name not in attr_values:
                attr_values[attr_name] = set()
            attr_values[attr_name].add(val)
        elif allow_no_value:
            attrs_without_values.add(attr)
        else:
            raise ValueError(f"Invalid attribute format: {attr}. Must be in format 'attr=value'")

    return attr_values, attrs_without_values


def _handle_attribute_operation(conf, operation_type, attr_values, log):
    """Handle attribute operations (add, replace) with consistent error handling"""

    if operation_type == "add":
        conf.add_many(*[(k, v) for k, v in attr_values.items()])
    elif operation_type == "replace":
        conf.replace_many(*[(k, v) for k, v in attr_values.items()])

    for attr_name, values in attr_values.items():
        formatted_values = _format_values_for_display(values)
        operation_past = "added" if operation_type == "add" else f"{operation_type}d"
        log.info(f"Successfully {operation_past} value(s) for '{attr_name}': {formatted_values}")


def config_get(inst, basedn, log, args):
    if args and args.attrs:
        _generic_get_attr(inst, basedn, log.getChild('config_get'), Config, args)
    else:
        # Get the entire cn=config entry
        _generic_get_entry(inst, basedn, log.getChild('config_get'), Config, args)


def config_replace_attr(inst, basedn, log, args):
    if not args or not args.attr:
        raise ValueError("Missing attribute to replace")

    conf = Config(inst, basedn)
    attr_values, _ = _parse_attr_value_pairs(args.attr)
    _handle_attribute_operation(conf, "replace", attr_values, log)
    _config_display_ldapimaprootdn_warning(log, args)


def config_add_attr(inst, basedn, log, args):
    if not args or not args.attr:
        raise ValueError("Missing attribute to add")

    conf = Config(inst, basedn)
    attr_values, _ = _parse_attr_value_pairs(args.attr)

    # Validate no values already exist
    for attr_name, values in attr_values.items():
        existing_vals = set(conf.get_attr_vals_utf8(attr_name) or [])
        duplicate_vals = values & existing_vals
        if duplicate_vals:
            raise ldap.ALREADY_EXISTS(
                f"Values {duplicate_vals} already exist for attribute '{attr_name}'")

    _handle_attribute_operation(conf, "add", attr_values, log)
    _config_display_ldapimaprootdn_warning(log, args)


def config_del_attr(inst, basedn, log, args):
    if not args or not args.attr:
        raise ValueError("Missing attribute to delete")

    conf = Config(inst, basedn)
    attr_values, attrs_to_remove = _parse_attr_value_pairs(args.attr, allow_no_value=True)

    # Handle complete attribute removal
    for attr in attrs_to_remove:
        try:
            if conf.get_attr_vals_utf8(attr):
                conf.remove_all(attr)
                log.info(f"Removed attribute '{attr}' completely")
            else:
                log.warning(f"Attribute '{attr}' does not exist - skipping")
        except ldap.NO_SUCH_ATTRIBUTE:
            log.warning(f"Attribute '{attr}' does not exist - skipping")

    # Validate and handle value-specific deletion
    if attr_values:
        for attr_name, values in attr_values.items():
            try:
                existing_values = set(conf.get_attr_vals_utf8(attr_name) or [])
                values_to_delete = values & existing_values
                values_not_found = values - existing_values

                if values_not_found:
                    formatted_values = _format_values_for_display(values_not_found)
                    log.warning(f"Values {formatted_values} do not exist for attribute '{attr_name}' - skipping")

                if values_to_delete:
                    remaining_values = existing_values - values_to_delete
                    if not remaining_values:
                        conf.remove_all(attr_name)
                    else:
                        conf.replace_many((attr_name, remaining_values))
                    formatted_values = _format_values_for_display(values_to_delete)
                    log.info(f"Successfully deleted values {formatted_values} from '{attr_name}'")
            except ldap.NO_SUCH_ATTRIBUTE:
                log.warning(f"Attribute '{attr_name}' does not exist - skipping")

    _config_display_ldapimaprootdn_warning(log, args)


def create_parser(subparsers):
    config_parser = subparsers.add_parser('config', help="Manage the server configuration", formatter_class=CustomHelpFormatter)

    subcommands = config_parser.add_subparsers(help="action")

    get_parser = subcommands.add_parser('get', help='get', formatter_class=CustomHelpFormatter)
    get_parser.set_defaults(func=config_get)
    get_parser.add_argument('attrs', nargs='*', help='Configuration attribute(s) to get')

    add_attr_parser = subcommands.add_parser('add', help='Add attribute value to configuration', formatter_class=CustomHelpFormatter)
    add_attr_parser.set_defaults(func=config_add_attr)
    add_attr_parser.add_argument('attr', nargs='*', help='Configuration attribute to add')

    replace_attr_parser = subcommands.add_parser('replace', help='Replace attribute value in configuration', formatter_class=CustomHelpFormatter)
    replace_attr_parser.set_defaults(func=config_replace_attr)
    replace_attr_parser.add_argument('attr', nargs='*', help='Configuration attribute to replace')

    del_attr_parser = subcommands.add_parser('delete', help='Delete attribute value in configuration', formatter_class=CustomHelpFormatter)
    del_attr_parser.set_defaults(func=config_del_attr)
    del_attr_parser.add_argument('attr', nargs='*', help='Configuration attribute to delete')
