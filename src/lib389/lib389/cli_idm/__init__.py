# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

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
        raise Exception("Not sure if want")
    return data


def _generic_list(inst, basedn, log, manager_class, args=None):
    mc = manager_class(inst, basedn)
    ol = mc.list()
    if len(ol) == 0:
        if args and args.json:
            print(json.dumps({"type": "list", "items": []}))
        else:
            log.info("No objects to display")
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
            print(json.dumps(json_result))


# Display these entries better!
def _generic_get(inst, basedn, log, manager_class, selector, args=None):
    mc = manager_class(inst, basedn)
    if args and args.json:
        o = mc.get(selector, json=True)
        print(o)
    else:
        o = mc.get(selector)
        o_str = o.display()
        log.info(o_str)


def _generic_get_dn(inst, basedn, log, manager_class, dn, args=None):
    mc = manager_class(inst, basedn)
    o = mc.get(dn=dn)
    o_str = o.__unicode__()
    print(o_str)


def _generic_create(inst, basedn, log, manager_class, kwargs, args=None):
    mc = manager_class(inst, basedn)
    o = mc.create(properties=kwargs)
    o_str = o.__unicode__()
    log.info('Successfully created %s' % o_str)


def _generic_delete(inst, basedn, log, object_class, dn, args=None):
    # Load the oc direct
    o = object_class(inst, dn)
    o.delete()
    log.info('Successfully deleted %s' % dn)

# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
