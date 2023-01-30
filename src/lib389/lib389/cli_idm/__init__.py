# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from getpass import getpass
import json


def _get_arg(args, msg=None):
    if args is not None and len(args) > 0:
        if type(args) is list:
            return args[0]
        else:
            return args
    else:
        return input("%s : " % msg)


def _get_args(args, kws):
    kwargs = {}
    while len(kws) > 0:
        kw, msg, priv = kws.pop(0)

        if args is not None and len(args) > 0:
            kwargs[kw] = args.pop(0)
        else:
            if priv:
                kwargs[kw] = getpass("%s : " % msg)
            else:
                kwargs[kw] = input("%s : " % msg)
    return kwargs


# This is really similar to get_args, but generates from an array
def _get_attributes(args, attrs):
    kwargs = {}
    for attr in attrs:
        # Python can't represent a -, so it replaces it to _
        # in many places, so we have to normalise this.
        attr_normal = attr.replace('-', '_')
        if args is not None and hasattr(args, attr_normal) and getattr(args, attr_normal) is not None:
            kwargs[attr] = getattr(args, attr_normal)
        else:
            if attr.lower() == 'userpassword':
                kwargs[attr] = getpass("Enter value for %s : " % attr)
            else:
                kwargs[attr] = input("Enter value for %s : " % attr)
    return kwargs


def _warn(data, msg=None):
    if msg is not None:
        print("%s :" % msg)
    if 'Yes I am sure' != input("Type 'Yes I am sure' to continue: "):
        raise Exception("Not sure if I want to")
    return data


def _generic_list(inst, basedn, log, manager_class, args=None):
    mc = manager_class(inst, basedn)
    ol = mc.list()
    if len(ol) == 0:
        if args and args.json:
            log.info(json.dumps({"type": "list", "items": []}, indent=4))
        else:
            log.info("No entries to display")
    elif len(ol) > 0:
        # We might sort this in the future
        if args and args.json:
            json_result = {"type": "list", "items": []}
        for o in ol:
            o_str = o.__unicode__()
            if args and args.json:
                json_result['items'].append(o_str)
            else:
                log.info(o_str)
        if args and args.json:
            log.info(json.dumps(json_result, indent=4))


# Display these entries better!
def _generic_get(inst, basedn, log, manager_class, selector, args=None):
    mc = manager_class(inst, basedn)
    if args and args.json:
        try:
            o = mc.get(selector, json=True)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError(f'Could not find the entry under "{mc._basedn}"')
        log.info(o)
    else:
        try:
            o = mc.get(selector)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError(f'Could not find the entry under "{mc._basedn}"')
        o_str = o.display()
        log.info(o_str)


def _generic_get_dn(inst, basedn, log, manager_class, dn, args=None):
    mc = manager_class(inst, basedn)
    o = mc.get(dn=dn)
    o_str = o.__unicode__()
    log.info(o_str)


def _generic_create(inst, basedn, log, manager_class, kwargs, args=None):
    mc = manager_class(inst, basedn)
    try:
        o = mc.create(properties=kwargs)
    except ldap.NO_SUCH_OBJECT:
        raise ValueError(f'The base DN "{mc._basedn}" does not exist')

    o_str = o.__unicode__()
    log.info('Successfully created %s' % o_str)


def _generic_delete(inst, basedn, log, object_class, dn, args=None):
    # Load the oc direct
    o = object_class(inst, dn)
    try:
        o.delete()
    except ldap.NO_SUCH_OBJECT:
        raise ValueError(f'The entry does not exist')
    log.info('Successfully deleted %s' % dn)


def _generic_rename_inner(log, o, new_rdn, newsuperior=None, deloldrdn=None):
    # The default argument behaviour is defined in _mapped_object.py
    arguments = {'new_rdn': new_rdn}
    if newsuperior is not None:
        arguments['newsuperior'] = newsuperior
    if deloldrdn is not None:
        arguments['deloldrdn'] = deloldrdn
    o.rename(**arguments)
    log.info('Successfully renamed to %s' % o.dn)


def _generic_rename(inst, basedn, log, manager_class, selector, args=None):
    if not args or not args.new_name:
        raise ValueError("Missing a new name argument.")
    # Here, we should have already selected the type etc. mc should be a
    # type of DSLdapObjects (plural)
    mc = manager_class(inst, basedn)
    # Get the object singular by selector
    try:
        o = mc.get(selector)
    except ldap.NO_SUCH_OBJECT:
        raise ValueError(f'The entry does not exist')
    rdn_attr = ldap.dn.str2dn(o.dn)[0][0][0]
    arguments = {'new_rdn': f'{rdn_attr}={args.new_name}'}
    if args.keep_old_rdn:
        arguments['deloldrdn'] = False
    try:
        _generic_rename_inner(log, o, **arguments)
    except ldap.NO_SUCH_OBJECT:
        raise ValueError(f'The base DN "{mc._basedn}" does not exist')


def _generic_rename_dn(inst, basedn, log, manager_class, dn, args=None):
    if not args or not args.new_dn:
        raise ValueError("Missing a new DN argument.")
    if not ldap.dn.is_dn(args.new_dn):
        raise ValueError(f"Specified DN '{args.new_dn}' is not a valid DN")
    # Here, we should have already selected the type etc. mc should be a
    # type of DSLdapObjects (plural)
    mc = manager_class(inst, basedn)
    # Get the object singular by dn
    o = mc.get(dn=dn)
    old_parent = ",".join(ldap.dn.explode_dn(o.dn.lower())[1:])
    new_parent = ",".join(ldap.dn.explode_dn(args.new_dn.lower())[1:])
    new_rdn = ldap.dn.explode_dn(args.new_dn.lower())[0]
    arguments = {'new_rdn': new_rdn}
    if old_parent != new_parent:
        arguments['newsuperior'] = new_parent
    if args.keep_old_rdn:
        arguments['deloldrdn'] = False
    try:
        _generic_rename_inner(log, o, **arguments)
    except ldap.NO_SUCH_OBJECT:
        raise ValueError(f'The base DN "{mc._basedn}" does not exist')

# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
