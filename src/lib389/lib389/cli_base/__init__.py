# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import sys

from getpass import getpass
from lib389 import DirSrv
from lib389.properties import SER_LDAP_URL, SER_ROOT_DN, SER_ROOT_PW

MAJOR, MINOR, _, _, _ = sys.version_info

# REALLY PYTHON 3? REALLY???
def _input(msg):
    if MAJOR >= 3:
        return input(msg)
    else:
        return raw_input(msg)


def _get_arg(args, msg=None):
    if args is not None and len(args) > 0:
        if type(args) is list:
            return args[0]
        else:
            return args
    else:
        return _input("%s : " % msg)

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
                kwargs[kw] = _input("%s : " % msg)
    return kwargs

# This is really similar to get_args, but generates from an array
def _get_attributes(args, attrs):
    kwargs = {}
    for attr in attrs:
        if args is not None and len(args) > 0:
            kwargs[attr] = args.pop(0)
        else:
            if attr.lower() == 'userpassword':
                kwargs[attr] = getpass("Enter value for %s : " % attr)
            else:
                kwargs[attr] = _input("Enter value for %s : " % attr)
    return kwargs


def _warn(data, msg=None):
    if msg is not None:
        print("%s :" % msg)
    if 'Yes I am sure' != _input("Type 'Yes I am sure' to continue: "):
        raise Exception("Not sure if want")
    return data

# We'll need another of these that does a "connect via instance name?"
def connect_instance(ldapurl, binddn, verbose, starttls):
    dsargs = {
        SER_LDAP_URL: ldapurl,
        SER_ROOT_DN: binddn
    }
    ds = DirSrv(verbose=verbose)
    ds.allocate(dsargs)
    if not ds.can_autobind() and binddn is not None:
        dsargs[SER_ROOT_PW] = getpass("Enter password for %s on %s : " % (binddn, ldapurl))
    elif binddn is None:
        raise Exception("Must provide a binddn to connect with")
    ds.allocate(dsargs)
    ds.open(starttls=starttls, connOnly=True)
    print("")
    return ds

def disconnect_instance(inst):
    if inst is not None:
        inst.close()

def _generic_list(inst, basedn, log, manager_class, **kwargs):
    mc = manager_class(inst, basedn)
    ol = mc.list()
    if len(ol) == 0:
        log.info("No objects to display")
    elif len(ol) > 0:
        # We might sort this in the future
        for o in ol:
            o_str = o.__unicode__()
            log.info(o_str)

# Display these entries better!
def _generic_get(inst, basedn, log, manager_class, selector):
    mc = manager_class(inst, basedn)
    o = mc.get(selector)
    o_str = o.display()
    log.info(o_str)

def _generic_get_dn(inst, basedn, log, manager_class, dn):
    mc = manager_class(inst, basedn)
    o = mc.get(dn=dn)
    o_str = o.display()
    log.info(o_str)

def _generic_create(inst, basedn, log, manager_class, kwargs):
    mc = manager_class(inst, basedn)
    o = mc.create(properties=kwargs)
    o_str = o.__unicode__()
    log.info('Sucessfully created %s' % o_str)

def _generic_delete(inst, basedn, log, object_class, dn):
    # Load the oc direct
    o = object_class(inst, dn)
    o.delete()
    log.info('Sucessfully deleted %s' % dn)


class LogCapture(logging.Handler):
    """
    This useful class is for intercepting logs, and then making assertions about
    the outputs provided. Used by the cli unit tests
    """

    def __init__(self):
        """
        Create a log instance and primes the output capture.
        """
        super(LogCapture, self).__init__()
        self.outputs = []
        self.log = logging.getLogger("LogCapture")
        self.log.addHandler(self)

    def emit(self, record):
        self.outputs.append(record)

    def contains(self, query):
        """
        Assert that the query string listed is in some logged Record.
        """
        result = False
        for rec in self.outputs:
            if query in rec.message:
                result = True
        return result

    def flush(self):
        self.outputs = []

class FakeArgs(object):
    def __init__(self):
        pass


# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
