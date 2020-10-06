# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import sys
import os
import ldap
from lib389.properties import (SER_LDAP_URL, SER_ROOT_DN, SER_LDAPI_ENABLED,
                               SER_LDAPI_SOCKET, SER_LDAPI_AUTOBIND)
from lib389._constants import DSRC_CONTAINER

MAJOR, MINOR, _, _, _ = sys.version_info

if MAJOR >= 3:
    import configparser


def dsrc_arg_concat(args, dsrc_inst):
    """
    Given a set of argparse args containing:

    instance
    binddn
    starttls

    and a dsrc_inst (as from dsrc_to_ldap)

    Using this, overlay the settings together to form an instance

    Generally, if dsrc_inst is None, we make a new instance.

    If dsrc_inst is populated we overlay args as needed on top.
    """
    if dsrc_inst is None:
        new_dsrc_inst = {
            'uri': args.instance,
            'basedn': args.basedn,
            'binddn': args.binddn,
            'bindpw': None,
            'saslmech': None,
            'tls_cacertdir': None,
            'tls_cert': None,
            'tls_key': None,
            'tls_reqcert': ldap.OPT_X_TLS_HARD,
            'starttls': args.starttls,
            'prompt': False,
            'pwdfile': None,
            'args': {}
        }
        # Now gather the args
        new_dsrc_inst['args'][SER_LDAP_URL] = new_dsrc_inst['uri']
        new_dsrc_inst['args'][SER_ROOT_DN] = new_dsrc_inst['binddn']
        if new_dsrc_inst['uri'][0:8] == 'ldapi://':
            new_dsrc_inst['args'][SER_LDAPI_ENABLED] = "on"
            new_dsrc_inst['args'][SER_LDAPI_SOCKET] = new_dsrc_inst['uri'][9:]
            new_dsrc_inst['args'][SER_LDAPI_AUTOBIND] = "on"

        # Make new
        return new_dsrc_inst
    # overlay things.
    if args.basedn is not None:
        dsrc_inst['basedn'] = args.basedn
    if args.binddn is not None:
        dsrc_inst['binddn'] = args.binddn
    if args.starttls is True:
        dsrc_inst['starttls'] = True
    return dsrc_inst


def _read_dsrc(path, log, case_sensetive=False):
    path = os.path.expanduser(path)
    log.debug("dsrc path: %s" % path)
    log.debug("dsrc container path: %s" % DSRC_CONTAINER)
    config = configparser.ConfigParser()
    if case_sensetive:
        config.optionxform = str
    # First read our container config if it exists
    # Then overlap the user config.
    config.read([DSRC_CONTAINER, path])

    log.debug("dsrc instances: %s" % config.sections())

    return config


def dsrc_to_ldap(path, instance_name, log):
    """
    Given a path to a file, return the required details for an instance.

    The file should be an ini file, and instance should identify a section.

    The ini file should have the content:

    [instance]
    uri = ldaps://hostname:port
    basedn = dc=example,dc=com
    binddn = uid=user,....
    saslmech = [EXTERNAL|PLAIN]
    tls_cacertdir = /path/to/cadir
    tls_cert = /path/to/user.crt
    tls_key = /path/to/user.key
    tls_reqcert = [never, hard, allow]
    starttls = [true, false]
    """
    config = _read_dsrc(path, log)
    server_id = instance_name

    # Do we have an instance name to work with?
    if instance_name is None:
        log.debug("No instance name provided")
        return None

    # Strip the prefix
    if instance_name.startswith("slapd-"):
        server_id = instance_name = instance_name.replace("slapd-", "", 1)

    if not config.has_section(instance_name):
        # instance_name does not have a prefix, but dsrc might, so add it
        instance_name = "slapd-" + instance_name
        if not config.has_section(instance_name):
            log.debug("dsrc no such section: %s" % instance_name)
            return None

    dsrc_inst = {}
    dsrc_inst['args'] = {}

    # Read all the values
    dsrc_inst['uri'] = config.get(instance_name, 'uri')
    dsrc_inst['basedn'] = config.get(instance_name, 'basedn', fallback=None)
    dsrc_inst['binddn'] = config.get(instance_name, 'binddn', fallback=None)
    dsrc_inst['saslmech'] = config.get(instance_name, 'saslmech', fallback=None)
    if dsrc_inst['saslmech'] is not None and dsrc_inst['saslmech'] not in ['EXTERNAL', 'PLAIN']:
        raise Exception("%s [%s] saslmech must be one of EXTERNAL or PLAIN" % (path, instance_name))

    dsrc_inst['tls_cacertdir'] = config.get(instance_name, 'tls_cacertdir', fallback=None)
    # At this point, we should check if the provided cacertdir is indeed, a dir. This can be a cause
    # of basic failures and confusion.
    if dsrc_inst['tls_cacertdir'] is not None and (os.path.exists(dsrc_inst['tls_cacertdir']) == False or os.path.isdir(dsrc_inst['tls_cacertdir']) == False):
        log.warning("Warning: dsrc tls_cacertdir path may not exist, or is not a directory")
        log.warning("Warning: This should be a directory, and you must run '/usr/bin/c_rehash path' for it to be a valid CA store")

    dsrc_inst['tls_cert'] = config.get(instance_name, 'tls_cert', fallback=None)
    dsrc_inst['tls_key'] = config.get(instance_name, 'tls_key', fallback=None)
    dsrc_inst['tls_reqcert'] = config.get(instance_name, 'tls_reqcert', fallback='hard')
    if dsrc_inst['tls_reqcert'] not in ['never', 'allow', 'hard']:
        raise Exception("dsrc tls_reqcert value invalid. %s [%s] tls_reqcert should be one of never, allow or hard" % (instance_name,
                                                                                                                       path))
    if dsrc_inst['tls_reqcert'] == 'never':
        dsrc_inst['tls_reqcert'] = ldap.OPT_X_TLS_NEVER
    elif dsrc_inst['tls_reqcert'] == 'allow':
        dsrc_inst['tls_reqcert'] = ldap.OPT_X_TLS_ALLOW
    else:
        dsrc_inst['tls_reqcert'] = ldap.OPT_X_TLS_HARD
    dsrc_inst['starttls'] = config.getboolean(instance_name, 'starttls', fallback=False)
    dsrc_inst['pwdfile'] = None
    dsrc_inst['prompt'] = False
    # Now gather the args
    dsrc_inst['args']['server-id'] = server_id
    dsrc_inst['args'][SER_LDAP_URL] = dsrc_inst['uri']
    dsrc_inst['args'][SER_ROOT_DN] = dsrc_inst['binddn']
    if dsrc_inst['uri'][0:8] == 'ldapi://':
        dsrc_inst['args'][SER_LDAPI_ENABLED] = "on"
        dsrc_inst['args'][SER_LDAPI_SOCKET] = dsrc_inst['uri'][9:]
        dsrc_inst['args'][SER_LDAPI_AUTOBIND] = "on"

    # Return the dict.
    log.debug("dsrc completed with %s" % dsrc_inst)
    return dsrc_inst


def dsrc_to_repl_monitor(path, log):
    """
    Given a path to a file, return the required details for an instance.

    The connection values for monitoring other not connected topologies. The format:
    'host:port:binddn:bindpwd'. You can use regex for host and port. You can set bindpwd
    to * and it will be requested at the runtime.

    If a host:port is assigned an alias, then the alias instead of host:port will be
    displayed in the output. The format: "alias=host:port".

    The file should be an ini file, and instance should identify a section.

    The ini fileshould have the content:

    [repl-monitor-connections]
    connection1 = server1.example.com:38901:cn=Directory manager:*
    connection2 = server2.example.com:38902:cn=Directory manager:[~/pwd.txt]
    connection3 = hub1.example.com:.*:cn=Directory manager:password

    [repl-monitor-aliases]
    M1 = server1.example.com:38901
    M2 = server2.example.com:38902
    """

    config = _read_dsrc(path, log, case_sensetive=True)
    dsrc_repl_monitor = {"connections": None,
                         "aliases": None}

    if config.has_section("repl-monitor-connections"):
        dsrc_repl_monitor["connections"] = [conn for _, conn in config.items("repl-monitor-connections")]

    if config.has_section("repl-monitor-aliases"):
        dsrc_repl_monitor["aliases"] = {alias: inst for alias, inst in config.items("repl-monitor-aliases")}

    log.debug(f"dsrc completed with {dsrc_repl_monitor}")
    return dsrc_repl_monitor
