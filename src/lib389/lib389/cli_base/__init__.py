# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import sys
import json

from getpass import getpass
from lib389 import DirSrv
from lib389.utils import assert_c
from lib389.properties import *

MAJOR, MINOR, _, _, _ = sys.version_info


# REALLY PYTHON 3? REALLY???
def _input(msg):
    if MAJOR >= 3:
        return input(msg)
    else:
        return input(msg)


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
        # Python can't represent a -, so it replaces it to _
        # in many places, so we have to normalise this.
        attr_normal = attr.replace('-', '_')
        if args is not None and hasattr(args, attr_normal) and getattr(args, attr_normal) is not None:
            kwargs[attr] = getattr(args, attr_normal)
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
def connect_instance(dsrc_inst, verbose):
    dsargs = dsrc_inst['args']
    ds = DirSrv(verbose=verbose)
    ds.allocate(dsargs)
    if not ds.can_autobind() and dsrc_inst['binddn'] is not None:
        dsargs[SER_ROOT_PW] = getpass("Enter password for %s on %s : " % (dsrc_inst['binddn'], dsrc_inst['uri']))
    elif not ds.can_autobind() and dsrc_inst['binddn'] is None:
        raise Exception("Must provide a binddn to connect with")
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
    o_str = o.display()
    log.info(o_str)


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

log_simple_handler = logging.StreamHandler()
log_simple_handler.setFormatter(
    logging.Formatter('%(message)s')
)

log_verbose_handler = logging.StreamHandler()
log_verbose_handler.setFormatter(
    logging.Formatter('%(levelname)s: %(message)s')
)


def reset_get_logger(name, verbose=False):
    """Reset the python logging system for STDOUT, and attach a new
    console logger with cli expected formatting.

    :param name: Name of the logger
    :type name: str
    :param verbose: Enable verbose format of messages
    :type verbose: bool
    :return: logging.logger
    """
    root = logging.getLogger()
    if root.handlers:
        for handler in root.handlers:
                root.removeHandler(handler)

    if verbose:
        root.addHandler(log_verbose_handler)
    else:
        root.addHandler(log_simple_handler)

    log = logging.getLogger(name)

    if verbose:
        log.setLevel(logging.DEBUG)
    else:
        log.setLevel(logging.INFO)

    return log

