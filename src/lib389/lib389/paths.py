# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import sys
import os

from lib389._constants import DIRSRV_STATE_ONLINE, DSRC_CONTAINER

MAJOR, MINOR, _, _, _ = sys.version_info

if MAJOR >= 3:
    import configparser
else:
    import ConfigParser as configparser

# Read the paths from default.inf

# Create a lazy eval class for paths. When we first access, we re-read
# the inf. This way, if we never request a path, we never need to read
# this file. IE remote installs, we shouldn't need to read this.

# Could this actually become a defaults module, and merge with instance/options?
# Would it handle the versioning requirements and diff we need?

DEFAULTS_PATH = [
    '/usr/share/dirsrv/inf/defaults.inf',
    '/usr/local/share/dirsrv/inf/defaults.inf',
    '/opt/dirsrv/share/dirsrv/inf/defaults.inf',
    '/opt/local/share/dirsrv/inf/defaults.inf',
    '/opt/share/dirsrv/inf/defaults.inf',
]

MUST = [
    'product',
    'version',
    'user',
    'group',
    'root_dn',
    'prefix',
    'bin_dir',
    'sbin_dir',
    'lib_dir',
    'data_dir',
    'tmp_dir',
    'sysconf_dir',
    'config_dir',
    'schema_dir',
    'cert_dir',
    'local_state_dir',
    'run_dir',
    'lock_dir',
    'log_dir',
    'inst_dir',
    'db_dir',
    'db_home_dir',
    'backup_dir',
    'ldif_dir',
    'initconfig_dir',
    'tmpfiles_d',
]

# will need to add the access, error, audit log later.

# This maps a config to (entry, attr).
# This can be used online, or while the server is offline and we can parse dse.ldif

CONFIG_MAP = {
    'user' : ('cn=config', 'nsslapd-localuser'),
    'group' : ('cn=config','nsslapd-localuser'), # Is this correct?
    'schema_dir' : ('cn=config','nsslapd-schemadir'),
    'cert_dir' : ('cn=config','nsslapd-certdir'),
    'lock_dir' : ('cn=config','nsslapd-lockdir'),
    'inst_dir' : ('cn=config','nsslapd-instancedir'),
    'db_dir' : ('cn=config,cn=ldbm database,cn=plugins,cn=config', 'nsslapd-directory'),
    'db_home_dir' : ('cn=bdb,cn=config,cn=ldbm database,cn=plugins,cn=config', 'nsslapd-db-home-directory'),
    'backup_dir': ('cn=config','nsslapd-bakdir'),
    'ldif_dir': ('cn=config','nsslapd-ldifdir'),
    'error_log' : ('cn=config', 'nsslapd-errorlog'),
    'access_log' : ('cn=config', 'nsslapd-accesslog'),
    'audit_log' : ('cn=config', 'nsslapd-auditlog'),
    'ldapi': ('cn=config', 'nsslapd-ldapifilepath'),
    'version': ('', 'vendorVersion'),
}

SECTION = 'slapd'


class Paths(object):
    def __init__(self, serverid=None, instance=None, local=True):
        """
        Parses and uses a set of default paths from wellknown locations. The list
        of keys available is from the MUST attribute in this module.

        To use this module:

        p = Paths()
        p.bindir

        If the defaults.inf is NOT in a wellknown path, this will throw IOError
        on the first attribute access. If this does not have a value defaults.inf
        it will raise KeyError that the defaults.inf is not capable of supporting
        this tool.

        This is lazy evaluated, so the file is read at the "last minute" then
        the contents are cached. This means that remote tools that don't need
        to know about paths, shouldn't need to have a copy of 389-ds-base
        installed to remotely admin a server.
        """
        self._is_container = os.path.exists(DSRC_CONTAINER)
        self._defaults_cached = False
        self._config = None
        self._serverid = serverid
        self._instance = instance
        self._islocal = local

    def _get_defaults_loc(self, search_paths):
        ## THIS IS HOW WE HANDLE A PREFIX INSTALL
        prefix = os.getenv('PREFIX')
        if prefix is not None:
            spath = os.path.join(prefix, 'share/dirsrv/inf/defaults.inf')
            if os.path.isfile(spath):
                return spath
            else:
                raise IOError('defaults.inf not found in prefixed location %s' % spath)
        for spath in search_paths:
            if os.path.isfile(spath):
                return spath
        raise IOError('defaults.inf not found in any well known location!')

    def _read_defaults(self):
        spath = self._get_defaults_loc(DEFAULTS_PATH)
        self._config = configparser.ConfigParser()
        self._config.read([spath])
        if self._is_container:
            # Load some values over the top that are container specific
            self._config.set(SECTION, "pid_file", "/data/run/slapd-localhost.pid")
            self._config.set(SECTION, "ldapi", "/data/run/slapd-localhost.socket")
        self._defaults_cached = True

    def _validate_defaults(self):
        if self._defaults_cached is False:
            return False
        for k in MUST:
            if self._config.has_option(SECTION, k) is False:
                raise KeyError('Invalid defaults.inf, missing key %s' % k)
        return True

    def __getattr__(self, name):
        from lib389.utils import ensure_str
        if self._defaults_cached is False and self._islocal:
            self._read_defaults()
            self._validate_defaults()
        # Are we online? Is our key in the config map?
        if name in CONFIG_MAP and self._instance is not None and self._instance.state == DIRSRV_STATE_ONLINE:
            # Get the online value.
            (dn, attr) = CONFIG_MAP[name]
            ent = self._instance.getEntry(dn, attrlist=[attr,])
            # If the server doesn't have it, fall back to our configuration.
            if attr is not None:
                v = ensure_str(ent.getValue(attr))
            # Do we need to post-process the value?
            if name == 'version':
                # We need to post process this - it's 389-Directory/1.4.2.2.20191031git8166d8345 B2019.304.19
                # but we need a string like: 1.4.2.2.20191031git8166d8345
                v = v.split('/')[1].split()[0]
            return v
        # Else get from the config
        if self._serverid is not None:
            return ensure_str(self._config.get(SECTION, name).format(instance_name=self._serverid))
        else:
            return ensure_str(self._config.get(SECTION, name))

    @property
    def asan_enabled(self):
        if self._defaults_cached is False and self._islocal:
            self._read_defaults()
            self._validate_defaults()
        if self._config.has_option(SECTION, 'asan_enabled'):
            if self._config.get(SECTION, 'asan_enabled') == '1':
                return True
        return False

    @property
    def with_systemd(self):
        if self._defaults_cached is False and self._islocal:
            self._read_defaults()
            self._validate_defaults()
        if self._is_container:
            # We never have systemd in a container, so check the marker.
            return False
        if self._config.has_option(SECTION, 'with_systemd'):
            if self._config.get(SECTION, 'with_systemd') == '1':
                return True
        return False

    @property
    def perl_enabled(self):
        if self._defaults_cached is False and self._islocal:
            self._read_defaults()
            self._validate_defaults()
        if self._config.has_option(SECTION, 'enable_perl'):
            if self._config.get(SECTION, 'enable_perl') == 'yes':
                return True
        return False

    @property
    def rust_enabled(self):
        if self._defaults_cached is False:
            self._read_defaults()
            self._validate_defaults()
        if self._config.has_option(SECTION, 'enable_rust'):
            if self._config.get(SECTION, 'enable_rust') == 'no':
                return False
        return True
