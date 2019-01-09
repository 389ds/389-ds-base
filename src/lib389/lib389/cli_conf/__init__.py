# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
import ldap
from lib389 import ensure_list_str


def _args_to_attrs(args, arg_to_attr):
    attrs = {}
    for arg in vars(args):
        val = getattr(args, arg)
        if arg in arg_to_attr and val is not None:
            attrs[arg_to_attr[arg]] = val
        elif arg == 'DN':
            # Extract RDN from the DN
            attribute = ldap.dn.str2dn(val)[0][0][0]
            value = ldap.dn.str2dn(val)[0][0][1]
            attrs[attribute] = value
    return attrs


def generic_object_add(dsldap_object, log, args, arg_to_attr, props={}):
    """Create an entry using DSLdapObject interface

    dsldap_object should be a single instance of DSLdapObject with a set dn
    """

    log = log.getChild('generic_object_add')
    # Gather the attributes
    attrs = _args_to_attrs(args, arg_to_attr)
    # Update the parameters (which should have at least 'cn') with arg attributes
    props.update({attr: value for (attr, value) in attrs.items() if value != ""})
    new_object = dsldap_object.create(properties=props)
    log.info("Successfully created the %s", new_object.dn)


def generic_object_edit(dsldap_object, log, args, arg_to_attr):
    """Create an entry using DSLdapObject interface

    dsldap_object should be a single instance of DSLdapObject with a set dn
    """

    log = log.getChild('generic_object_edit')
    # Gather the attributes
    attrs = _args_to_attrs(args, arg_to_attr)
    existing_attributes = dsldap_object.get_all_attrs()

    modlist = []
    for attr, value in attrs.items():
        # Delete the attribute only if the user set it to 'delete' value
        if value in ("delete", ["delete"]):
            if attr in existing_attributes:
                modlist.append((ldap.MOD_DELETE, attr))
        else:
            if not isinstance(value, list):
                value = [value]
            if not (attr in existing_attributes and value == ensure_list_str(existing_attributes[attr])):
                modlist.append((ldap.MOD_REPLACE, attr, value))
    if len(modlist) > 0:
        dsldap_object.apply_mods(modlist)
        log.info("Successfully changed the %s", dsldap_object.dn)
    else:
        raise ValueError("There is nothing to set in the %s plugin entry" % dsldap_object.dn)


def generic_show(inst, basedn, log, args):
    """Display plugin configuration."""
    plugin = args.plugin_cls(inst)
    print(plugin.display())


def generic_enable(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    if plugin.status():
        print("Plugin '%s' already enabled", plugin.rdn)
    else:
        plugin.enable()
        print("Enabled plugin '%s'", plugin.rdn)


def generic_disable(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    if not plugin.status():
        print("Plugin '%s' already disabled", plugin.rdn)
    else:
        plugin.disable()
        print("Disabled plugin '%s'", plugin.rdn)


def generic_status(inst, basedn, log, args):
    plugin = args.plugin_cls(inst)
    if plugin.status() is True:
        print("Plugin '%s' is enabled", plugin.rdn)
    else:
        print("Plugin '%s' is disabled", plugin.rdn)


def add_generic_plugin_parsers(subparser, plugin_cls):
    show_parser = subparser.add_parser('show', help='display plugin configuration')
    show_parser.set_defaults(func=generic_show, plugin_cls=plugin_cls)

    enable_parser = subparser.add_parser('enable', help='enable plugin')
    enable_parser.set_defaults(func=generic_enable, plugin_cls=plugin_cls)

    disable_parser = subparser.add_parser('disable', help='disable plugin')
    disable_parser.set_defaults(func=generic_disable, plugin_cls=plugin_cls)

    status_parser = subparser.add_parser('status', help='display plugin status')
    status_parser.set_defaults(func=generic_status, plugin_cls=plugin_cls)


