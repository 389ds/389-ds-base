# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import sys
import os

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
    'backup_dir',
    'ldif_dir',
    'initconfig_dir',
]

SECTION = 'slapd'

class Paths(object):
    def __init__(self, serverid=None):
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
        self._defaults_cached = False
        self._config = None
        self._serverid = serverid

    def _get_defaults_loc(self, search_paths):
        for spath in search_paths:
            if os.path.isfile(spath):
                return spath
        raise IOError('defaults.inf not found in any wellknown location!')

    def _read_defaults(self):
        spath = self._get_defaults_loc(DEFAULTS_PATH)
        self._config = configparser.SafeConfigParser()
        self._config.read([spath])
        self._defaults_cached = True

    def _validate_defaults(self):
        if self._defaults_cached is False:
            return False
        for k in MUST:
            if self._config.has_option(SECTION, k) is False:
                raise KeyError('Invalid defaults.inf, missing key %s' % k)
        return True

    def __getattr__(self, name):
        if self._defaults_cached is False:
            self._read_defaults()
            self._validate_defaults()
        if self._serverid is not None:
            return self._config.get(SECTION, name).format(instance_name=self._serverid)
        else:
            return self._config.get(SECTION, name)

    @property
    def asan_enabled(self):
        if self._defaults_cached is False:
            self._read_defaults()
            self._validate_defaults()
        if self._config.has_option(SECTION, 'asan_enabled'):
            if self._config.get(SECTION, 'asan_enabled') == '1':
                return True
        return False

    @property
    def with_systemd(self):
        if self._defaults_cached is False:
            self._read_defaults()
            self._validate_defaults()
        if self._config.has_option(SECTION, 'with_systemd'):
            if self._config.get(SECTION, 'with_systemd') == '1':
                return True
        return False
