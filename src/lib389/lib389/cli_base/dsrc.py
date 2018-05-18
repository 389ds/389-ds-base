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
from lib389.properties import *

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
            'saslmech': None,
            'tls_cacertdir': None,
            'tls_cert': None,
            'tls_key': None,
            'tls_reqcert': ldap.OPT_X_TLS_HARD,
            'starttls': args.starttls,
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

def dsrc_to_ldap(path, instance_name, log):
    """
    Given a path to a file, return the required details for an instance.

    The file should be an ini file, and instance should identify a section.

    The ini fileshould have the content:

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
    path = os.path.expanduser(path)
    log.debug("dsrc path: %s" % path)
    # First read our config
    # No such file?
    config = configparser.SafeConfigParser()
    config.read([path])

    log.debug("dsrc instances: %s" % config.sections())

    # Does our section exist?
    if not config.has_section(instance_name):
        # If not, return none.
        log.debug("dsrc no such section %s" % instance_name)
        return None

    dsrc_inst = {}
    dsrc_inst['args'] = {}

    # Read all the values
    dsrc_inst['uri'] = config.get(instance_name, 'uri')
    dsrc_inst['basedn'] = config.get(instance_name, 'basedn', fallback=None)
    dsrc_inst['binddn'] = config.get(instance_name, 'binddn', fallback=None)
    dsrc_inst['saslmech'] = config.get(instance_name, 'saslmech', fallback=None)
    if dsrc_inst['saslmech'] is not None and dsrc_inst['saslmech'] not in ['EXTERNAL', 'PLAIN']:
        raise Exception("~/.dsrc [%s] saslmech must be one of EXTERNAL or PLAIN" % instance_name)

    dsrc_inst['tls_cacertdir'] = config.get(instance_name, 'tls_cacertdir', fallback=None)
    dsrc_inst['tls_cert'] = config.get(instance_name, 'tls_cert', fallback=None)
    dsrc_inst['tls_key'] = config.get(instance_name, 'tls_key', fallback=None)
    dsrc_inst['tls_reqcert'] = config.get(instance_name, 'tls_reqcert', fallback='hard')
    if dsrc_inst['tls_reqcert'] not in ['never', 'allow', 'hard']:
        raise Exception("dsrc tls_reqcert value invalid. ~/.dsrc [%s] tls_reqcert should be one of never, allow or hard" % instance_name)
    if dsrc_inst['tls_reqcert'] == 'never':
        dsrc_inst['tls_reqcert'] = ldap.OPT_X_TLS_NEVER
    elif dsrc_inst['tls_reqcert'] == 'allow':
        dsrc_inst['tls_reqcert'] = ldap.OPT_X_TLS_ALLOW
    else:
        dsrc_inst['tls_reqcert'] = ldap.OPT_X_TLS_HARD
    dsrc_inst['starttls'] = config.getboolean(instance_name, 'starttls', fallback=False)

    # Now gather the args
    dsrc_inst['args'][SER_LDAP_URL] = dsrc_inst['uri']
    dsrc_inst['args'][SER_ROOT_DN] = dsrc_inst['binddn']
    if dsrc_inst['uri'][0:8] == 'ldapi://':
        dsrc_inst['args'][SER_LDAPI_ENABLED] = "on"
        dsrc_inst['args'][SER_LDAPI_SOCKET] = dsrc_inst['uri'][9:]
        dsrc_inst['args'][SER_LDAPI_AUTOBIND] = "on"


    # Return the dict.
    log.debug("dsrc completed with %s" % dsrc_inst)
    return dsrc_inst


