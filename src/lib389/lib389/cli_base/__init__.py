# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import sys
import json
import ldap

from getpass import getpass
from lib389 import DirSrv
from lib389.utils import assert_c, get_ldapurl_from_serverid
from lib389.properties import *


def _get_arg(args, msg=None, hidden=False, confirm=False):
    if args is not None and len(args) > 0:
        if type(args) is list:
            return args[0]
        else:
            return args
    else:
        if hidden:
            if confirm:
                x = getpass("%s : " % msg)
                y = getpass("CONFIRM - %s : " % msg)
                assert_c(x == y, "inputs do not match, aborting.")
                return y
            else:
                return getpass("%s : " % msg)
        else:
            return input("%s : " % msg)


def _get_args(args, kws):
    kwargs = {}
    while len(kws) > 0:
        kw, msg, priv = kws.pop(0)
        kw = kw.lower()
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
            attr = attr.lower()
            kwargs[attr] = getattr(args, attr_normal)
        else:
            if attr.lower() == 'userpassword':
                kwargs[attr] = getpass("Enter value for %s : " % attr)
            else:
                attr_normal = attr.lower()
                kwargs[attr_normal] = input("Enter value for %s : " % attr)

    return kwargs


def _warn(data, msg=None):
    if msg is not None:
        print("%s :" % msg)
    if 'Yes I am sure' != input("Type 'Yes I am sure' to continue: "):
        raise Exception("Not sure if want")
    return data


def connect_instance(dsrc_inst, verbose, args):
    dsargs = dsrc_inst['args']
    if '//' not in dsargs['ldapurl']:
        # Connecting to the local instance
        dsargs['server-id'] = dsargs['ldapurl']
        # We have an instance name - generate url from dse.ldif
        ldapurl, certdir = get_ldapurl_from_serverid(dsargs['ldapurl'])
        if ldapurl is not None:
            dsargs['ldapurl'] = ldapurl
            if 'ldapi://' in ldapurl:
                dsargs['ldapi_enabled'] = 'on'
                dsargs['ldapi_socket'] = ldapurl.replace('ldapi://', '')
                dsargs['ldapi_autobind'] = 'on'
            elif 'ldaps://' in ldapurl:
                dsrc_inst['tls_cert'] = certdir
        else:
            # The instance name does not match any instances
            raise ValueError("Could not find configuration for instance: " + dsargs['ldapurl'])

    ds = DirSrv(verbose=verbose)
    # We do an empty allocate here to determine if we can autobind ... (really
    # we should actually be inspect the URL ...)
    ds.allocate(dsargs)

    if args.pwdfile is not None or args.bindpw is not None or args.prompt is True:
        if args.pwdfile is not None:
            # Read password from file
            try:
                with open(args.pwdfile, "r") as f:
                    dsargs[SER_ROOT_PW] = f.readline().rstrip()
            except EnvironmentError as e:
                raise ValueError("Failed to open password file: " + str(e))
        elif args.bindpw is not None:
            # Password provided
            # This shouldn't be needed? dsrc already inherits the args ...
            dsargs[SER_ROOT_PW] = args.bindpw
        else:
            # No password or we chose to prompt
            dsargs[SER_ROOT_PW] = getpass("Enter password for {} on {}: ".format(dsrc_inst['binddn'], dsrc_inst['uri']))
    elif not ds.can_autobind():
        # No LDAPI, prompt for password
        dsargs[SER_ROOT_PW] = getpass("Enter password for {} on {}: ".format(dsrc_inst['binddn'], dsrc_inst['uri']))

    if 'binddn' in dsrc_inst:
        # Allocate is an awful interface that we should stop using, but for now
        # just directly map the dsrc_inst args in (remember, dsrc_inst DOES
        # overlay cli args into the map ...)
        dsargs[SER_ROOT_DN] = dsrc_inst['binddn']

    ds = DirSrv(verbose=verbose)
    ds.allocate(dsargs)
    ds.open(saslmethod=dsrc_inst['saslmech'],
            certdir=dsrc_inst['tls_cacertdir'],
            reqcert=dsrc_inst['tls_reqcert'],
            usercert=dsrc_inst['tls_cert'],
            userkey=dsrc_inst['tls_key'],
            starttls=dsrc_inst['starttls'], connOnly=True)
    return ds


def disconnect_instance(inst):
    if inst is not None:
        inst.close()


def populate_attr_arguments(parser, attributes):
    for attr in attributes:
        parser.add_argument('--%s' % attr, nargs='?', help="Value of %s" % attr)


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
                print(o_str)
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
        print(o_str)


def _generic_get_entry(inst, basedn, log, manager_class, args=None):
    mc = manager_class(inst, basedn)
    if args and args.json:
        print(mc.get_all_attrs_json())
    else:
        print(mc.display())


def _generic_get_attr(inst, basedn, log, manager_class, args=None):
    mc = manager_class(inst, basedn)
    vals = {}
    for attr in args.attrs:
        if args and args.json:
            vals[attr] = mc.get_attr_vals_utf8(attr)
        else:
            print(mc.display_attr(attr).rstrip())
    if args.json:
        print(json.dumps({"type": "entry", "dn": mc._dn, "attrs": vals}))


def _generic_get_dn(inst, basedn, log, manager_class, dn, args=None):
    mc = manager_class(inst, basedn)
    o = mc.get(dn=dn)
    o_str = o.display()
    print(o_str)


def _generic_create(inst, basedn, log, manager_class, kwargs, args=None):
    mc = manager_class(inst, basedn)
    o = mc.create(properties=kwargs)
    o_str = o.__unicode__()
    print('Successfully created %s' % o_str)


def _generic_delete(inst, basedn, log, object_class, dn, args=None):
    # Load the oc direct
    o = object_class(inst, dn)
    o.delete()
    print('Successfully deleted %s' % dn)


# Attr functions expect attribute values to be "attr=value"

def _generic_replace_attr(inst, basedn, log, manager_class, args=None):
    mc = manager_class(inst, basedn)
    if args and args.attr:
        for myattr in args.attr:
            if "=" in myattr:
                [attr, val] = myattr.split("=", 1)
                mc.replace(attr, val)
                print("Successfully replaced \"{}\"".format(attr))
            else:
                raise ValueError("You must specify a value to replace the attribute ({})".format(myattr))
    else:
        # Missing value
        raise ValueError("Missing attribute to replace")


def _generic_add_attr(inst, basedn, log, manager_class, args=None):
    mc = manager_class(inst, basedn)
    if args and args.attr:
        for myattr in args.attr:
            if "=" in myattr:
                [attr, val] = myattr.split("=", 1)
                mc.add(attr, val)
                print("Successfully added \"{}\"".format(attr))
            else:
                raise ValueError("You must specify a value to add for the attribute ({})".format(myattr))
    else:
        # Missing value
        raise ValueError("Missing attribute to add")


def _generic_del_attr(inst, basedn, log, manager_class, args=None):
    mc = manager_class(inst, basedn)
    if args and args.attr:
        for myattr in args.attr:
            if "=" in myattr:
                # we have a specific value
                [attr, val] = myattr.split("=", 1)
                mc.remove(attr, val)
            else:
                # remove all
                mc.remove_all(myattr)
                attr = myattr  # for logging
            print("Successfully removed \"{}\"".format(attr))
    else:
        # Missing value
        raise ValueError("Missing attribute to delete")


def _generic_modify_change_to_mod(change):
    values = change.split(":")
    if len(values) <= 2:
        raise ValueError("Not enough arguments in '%s'. action:attribute:value" % change)
    elif len(values) >= 4:
        raise ValueError("Too many arguments in '%s'. %s:attribute:value expected" % (change, values[0]))
    elif len(values[1]) == 0:
        raise ValueError("Invalid empty attribute name in '%s'." % change)
    # Return a tuple
    if values[0] == 'add':
        if len(values[2]) == 0:
            raise ValueError("Invalid empty value in '%s'." % change)
        return (ldap.MOD_ADD, values[1], values[2])
    elif values[0] == 'delete':
        if len(values[2]) == 0:
            return (ldap.MOD_DELETE, values[1])
        return (ldap.MOD_DELETE, values[1], values[2])
    elif values[0] == 'replace':
        if len(values[2]) == 0:
            raise ValueError("Invalid empty value in '%s'." % change)
        return (ldap.MOD_REPLACE, values[1], values[2])
    else:
        raise ValueError("Unknown action '%s'. Expected add, delete, replace" % change)


def _generic_modify_inner(log, o, changes):
    # Now parse the series of arguments.
    # Turn them into mod lists. See apply_mods.
    mods = [_generic_modify_change_to_mod(x) for x in changes]
    log.debug("Requested mods: %s" % mods)
    # Now push them to dsldapobject to modify
    o.apply_mods(mods)
    print('Successfully modified %s' % o.dn)


def _generic_modify(inst, basedn, log, manager_class, selector, args=None):
    if not args or not args.changes:
        raise ValueError("Missing modify actions to perform.")
    # Here, we should have already selected the type etc. mc should be a
    # type of DSLdapObjects (plural)
    mc = manager_class(inst, basedn)
    # Get the object singular by selector
    o = mc.get(selector)
    _generic_modify_inner(log, o, args.changes)


def _generic_modify_dn(inst, basedn, log, manager_class, dn, args=None):
    if not args or not args.changes:
        raise ValueError("Missing modify actions to perform.")
    # Here, we should have already selected the type etc. mc should be a
    # type of DSLdapObjects (plural)
    mc = manager_class(inst, basedn)
    # Get the object singular by dn
    o = mc.get(dn=dn)
    _generic_modify_inner(log, o, args.changes)


class LogCapture(logging.Handler):
    """
    This useful class is for intercepting logs, and then making assertions about
    the outputs provided. Used by the cli unit tests.
    """

    def __init__(self):
        """
        Create a log instance and primes the output capture.
        """
        super(LogCapture, self).__init__()
        self.outputs = []
        self.log = logging.getLogger("LogCapture")
        self.log.addHandler(self)
        self.log.setLevel(logging.INFO)

    def emit(self, record):
        self.outputs.append(record)

    def contains(self, query):
        """
        Assert that the query string listed is in some logged Record.
        """
        result = False
        for rec in self.outputs:
            if query in str(rec):
                result = True
        return result

    def flush(self):
        self.outputs = []


class FakeArgs(object):
    def __init__(self):
        pass

    def __len__(self):
        return len(self.__dict__.keys())


def setup_script_logger(name, verbose=False):
    """Reset the python logging system for STDOUT, and attach a new
    console logger with cli expected formatting.

    :param name: Name of the logger
    :type name: str
    :param verbose: Enable verbose format of messages
    :type verbose: bool
    :return: logging.logger
    """
    root = logging.getLogger()
    log = logging.getLogger(name)
    log_handler = logging.StreamHandler(sys.stdout)

    if verbose:
        log.setLevel(logging.DEBUG)
        log_format = '%(levelname)s: %(message)s'
    else:
        log.setLevel(logging.INFO)
        log_format = '%(message)s'

    log_handler.setFormatter(logging.Formatter(log_format))
    root.addHandler(log_handler)

    return log
