# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import socket
import sys
import os
from lib389.paths import Paths

MAJOR, MINOR, _, _, _ = sys.version_info

if MAJOR >= 3:
    import configparser
else:
    import ConfigParser as configparser

format_keys = [
    'prefix',
    'bin_dir',
    'sbin_dir',
    'sysconf_dir',
    'data_dir',
    'local_state_dir',
    'lib_dir',
    'cert_dir',
    'config_dir',
    'inst_dir',
    'backup_dir',
    'db_dir',
    'ldif_dir',
    'lock_dir',
    'log_dir',
    'run_dir',
    'schema_dir',
    'tmp_dir',
]

ds_paths = Paths()

class Options2(object):
    # This stores the base options in a self._options dict.
    # It provides a number of options for:
    # - dict overlay
    # - parsing the config parser types.
    def __init__(self, log):
        # 'key' : (default, helptext, valid_func )
        self._options = {} # this takes the default
        self._type = {} # Lists the type the item should be.
        self._helptext = {} # help text for the option, MANDATORY.
        self._valid_func = {} # options verification function.
        self._section = None
        self.log = log

    def parse_inf_config(self, config):
        v = None
        for k in self._options.keys():
            try:
                if self._type[k] == int:
                    v = config.getint(self._section, k)
                elif self._type[k] == bool:
                    v = config.getboolean(self._section, k)
                elif self._type[k] == str:
                    v = config.get(self._section, k)
                # How does this handle wrong types?
            except ValueError:
                # Should we raise an assertion error?
                # No, the sectons don't exist, continue
                self.log.debug('%s:%s not in inf, or incorrect type, using default' % (self._section, k))
                continue
            except configparser.NoOptionError:
                self.log.debug('%s:%s not in inf, or incorrect type, using default' % (self._section, k))
                continue
            self._options[k] = v

    def set(self, option, value):
        self._options[option] = value

    def verify(self):
        pass

    def collect(self):
        return self._options

    def collect_help(self):
        helptext = "[%s]\n" % self._section
        for k in self._options.keys():
            helptext += "# %s: %s\n" % (k, self._helptext[k])
            helptext += "# type: %s\n" % (self._type[k].__name__)
            helptext += "; %s = %s\n\n" % (k, self._options[k])
        return helptext

#
# Base, example dicts of the general, backend (userRoot) options.
#

class General2Base(Options2):
    def __init__(self, log):
        super(General2Base, self).__init__(log)
        self._section = 'general'

        self._options['config_version'] = 2
        self._type['config_version'] = int
        self._helptext['config_version'] = "The format version of the inf answer file."

        self._options['full_machine_name'] = socket.gethostname()
        self._type['full_machine_name'] = str
        self._helptext['full_machine_name'] = "The fully qualified hostname of this system."

        self._options['strict_host_checking'] = True
        self._type['strict_host_checking'] = bool
        self._helptext['strict_host_checking'] = "If true, will validate forward and reverse dns names for full_machine_name"

        self._options['selinux'] = True
        self._type['selinux'] = bool
        self._helptext['selinux'] = "Enable SELinux detection and integration. Normally, this should always be True, and will correctly detect when SELinux is not present."

        self._options['defaults'] = '99999'
        self._type['defaults'] = str
        self._helptext['defaults'] = "Set the configuration defaults version. If set to 99999, always use the latest values available for the slapd section. This allows pinning default values in cn=config to specific Directory Server releases."


#
# This module contains the base options and configs for Director Server
# setup and install. This allows 
#

class Slapd2Base(Options2):
    def __init__(self, log):
        super(Slapd2Base, self).__init__(log)
        self._section = 'slapd'

        self._options['instance_name'] = None
        self._type['instance_name'] = str
        self._helptext['instance_name'] = "The name of the instance. Cannot be changed post installation."

        self._options['user'] = ds_paths.user
        self._type['user'] = str
        self._helptext['user'] = "The user account ns-slapd will drop privileges to during operation."

        self._options['group'] = ds_paths.group
        self._type['group'] = str
        self._helptext['group'] = "The group ns-slapd will drop privilleges to during operation."

        self._options['root_dn'] = ds_paths.root_dn
        self._type['root_dn'] = str
        self._helptext['root_dn'] = "The Distinquished Name of the Administrator account. This is equivalent to root of your Directory Server."

        self._options['root_password'] = None
        self._type['root_password'] = str
        self._helptext['root_password'] = "The password for the root_dn account. "

        self._options['prefix'] = ds_paths.prefix
        self._type['prefix'] = str
        self._helptext['prefix'] = "The filesystem prefix for all other locations. Unless you are developing DS, you likely never need to set this. This value can be reffered to in other fields with {prefix}, and can be set with the environment variable PREFIX."

        self._options['port'] = 389
        self._type['port'] = int
        self._helptext['port'] = "The TCP port that Directory Server will listen on for LDAP connections."

        self._options['secure_port'] = 636
        self._type['secure_port'] = int
        self._helptext['secure_port'] = "The TCP port that Directory Server will listen on for TLS secured LDAP connections."

        # In the future, make bin and sbin /usr/[s]bin, but we may need autotools assistance from Ds
        self._options['bin_dir'] = ds_paths.bin_dir
        self._type['bin_dir'] = str
        self._helptext['bin_dir'] = "The location Directory Server can find binaries. You should not need to alter this value."

        self._options['sbin_dir'] = ds_paths.sbin_dir
        self._type['sbin_dir'] = str
        self._helptext['sbin_dir'] = "The location Directory Server can find systemd administration binaries. You should not need to alter this value."

        self._options['sysconf_dir'] = ds_paths.sysconf_dir
        self._type['sysconf_dir'] = str
        self._helptext['sysconf_dir'] = "The location of the system configuration directory. You should not need to alter this value."

        self._options['initconfig_dir'] = ds_paths.initconfig_dir
        self._type['initconfig_dir'] = str
        self._helptext['initconfig_dir'] = "The location of the system rc configuration directory. You should not need to alter this value."

        # In the future, make bin and sbin /usr/[s]bin, but we may need autotools assistance from Ds
        self._options['data_dir'] = ds_paths.data_dir
        self._type['data_dir'] = str
        self._helptext['data_dir'] = "The location of shared static data. You should not need to alter this value."

        self._options['local_state_dir'] = ds_paths.local_state_dir
        self._type['local_state_dir'] = str
        self._helptext['local_state_dir'] = "The location prefix to variable data. You should not need to alter this value."

        self._options['lib_dir'] = ds_paths.lib_dir
        self._type['lib_dir'] = str
        self._helptext['lib_dir'] = "The location to Directory Server shared libraries. You should not need to alter this value."

        self._options['cert_dir'] = ds_paths.cert_dir
        self._type['cert_dir'] = str
        self._helptext['cert_dir'] = "The location where NSS will store certificates."

        self._options['config_dir'] = ds_paths.config_dir
        self._type['config_dir'] = str
        self._helptext['config_dir'] = "The location where dse.ldif and other configuration will be stored. You should not need to alter this value."

        self._options['inst_dir'] = ds_paths.inst_dir
        self._type['inst_dir'] = str
        self._helptext['inst_dir'] = "The location of the Directory Server databases, ldif and backups. You should not need to alter this value."

        self._options['backup_dir'] = ds_paths.backup_dir
        self._type['backup_dir'] = str
        self._helptext['backup_dir'] = "The location where Directory Server will export and import backups from. You should not need to alter this value."

        self._options['db_dir'] = ds_paths.db_dir
        self._type['db_dir'] = str
        self._helptext['db_dir'] = "The location where Directory Server will store databases. You should not need to alter this value."

        self._options['ldif_dir'] = ds_paths.ldif_dir
        self._type['ldif_dir'] = str
        self._helptext['ldif_dir'] = "The location where Directory Server will export and import ldif from. You should not need to alter this value."

        self._options['lock_dir'] = ds_paths.lock_dir
        self._type['lock_dir'] = str
        self._helptext['lock_dir'] = "The location where Directory Server will store lock files. You should not need to alter this value."

        self._options['log_dir'] = ds_paths.log_dir
        self._type['log_dir'] = str
        self._helptext['log_dir'] = "The location where Directory Server will write log files. You should not need to alter this value."

        self._options['run_dir'] = ds_paths.run_dir
        self._type['run_dir'] = str
        self._helptext['run_dir'] = "The location where Directory Server will create pid files. You should not need to alter this value."

        self._options['schema_dir'] = ds_paths.schema_dir
        self._type['schema_dir'] = str
        self._helptext['schema_dir'] = "The location where Directory Server will store and write schema. You should not need to alter this value."

        self._options['tmp_dir'] = ds_paths.tmp_dir
        self._type['tmp_dir'] = str
        self._helptext['tmp_dir'] = "The location where Directory Server will write temporary files. You should not need to alter this value."

    def _format(self, d):
        new_d = {}
        ks = d.keys()
        no_format_keys = ks - format_keys

        for k in no_format_keys:
            new_d[k] = d[k]
        for k in format_keys:
            # Will these be done in correct order?
            if self._type[k] == str:
                new_d[k] = d[k].format(**new_d)
            else:
                new_d[k] = d[k]
        return new_d

    def collect(self):
        # This does the final format and return of options.
        return self._format(self._options)

# We use inheritence to "overlay" from base types and options, and we can then
# stack progressive versions "options" on top.
# This class is for 

