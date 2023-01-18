# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import socket
import sys
import os
import random
from lib389.paths import Paths
from lib389._constants import INSTALL_LATEST_CONFIG
from lib389.utils import get_default_db_lib, socket_check_bind

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
    'db_home_dir',
    'db_lib',
    'ldapi',
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

    def get_default():
        # Get option default values if these values are dynamic
        # Concerned options are:
        #   - selinux
        #   - systemd
        #   - port
        #   - secure_port
        if os.geteuid() == 0:
            return { 'selinux': True,
                     'systemd': True,
                     'port': 389,
                     'secure_port': 636 }
        values = { 'selinux': False, 'systemd': False }
        # Try first to select a couple of port like (n*1000+389, n*1000+636)
        for delta in range(1000, 65000, 1000):
            values['port'] = 389+delta
            values['secure_port'] = 636+delta
            if socket_check_bind(values['port']) and socket_check_bind(values['secure_port']):
                return values;
        # Nothing found, so lets use a couple of random values
        for tryid in range(10000):
            values['port'], values['secure_port'] = random.choices(range(1000,65535), k=2)
            if socket_check_bind(values['port']) and socket_check_bind(values['secure_port']):
                return values;
        # Still nothing found.
        # Cannot not raise an exception as this code is also triggered while building 389ds
        # So lets silently choose a couple of ports (probably busy) and get a failure later
        # on (when starting the instance if in dscreate case).
        values['port'], values['secure_port'] = random.choices(range(1000,65535), k=2)
        return values;

    default_values = get_default()

    def get_systemd_default():
        # Cannot use ds_paths.with_systemd in get_default() because dse template
        # is not available in build. So a function is used instead.
        if Options2.default_values['systemd']:
            return ds_paths.with_systemd
        else:
            return False


    def __init__(self, log):
        # 'key' : (default, helptext, valid_func )
        self._options = {}  # this takes the default
        self._type = {}  # Lists the type the item should be.
        self._helptext = {}  # help text for the option, MANDATORY.
        self._advanced = {} # Is the option advanced/developer?
        self._valid_func = {}  # options verification function.
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
            except ValueError as e:
                # Invalid type
                err_msg = ('Invalid value in section "%s", "%s" has incorrect type (%s)' % (self._section, k, str(e)))
                raise ValueError(err_msg)
            except configparser.NoOptionError:
                self.log.debug('%s:%s not in inf, using default', self._section, k)
                continue
            self._options[k] = v

    def set(self, option, value):
        self._options[option] = value

    def verify(self):
        pass

    def collect(self):
        return self._options

    def collect_help(self, comment=False, advanced=False):
        helptext = "[%s]\n" % self._section
        for k in sorted(self._options.keys()):
            if self._advanced.get(k, False) is True and advanced is False:
                # Skip this option because we haven't reqed advanced.
                continue
            helptext += "# %s (%s)\n" % (k, self._type[k].__name__)
            helptext += "# Description: %s\n" % (self._helptext[k])
            helptext += "# Default value: %s \n" % (self._options[k])
            helptext += ";%s = %s\n\n" % (k, self._options[k])
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
        self._helptext['config_version'] = "Sets the format version of this INF file. To use the INF file with dscreate, you must set this parameter to \"2\"."
        self._advanced['config_version'] = True

        self._options['full_machine_name'] = socket.getfqdn()
        self._type['full_machine_name'] = str
        self._helptext['full_machine_name'] = "Sets the fully qualified hostname (FQDN) of this system. When installing this instance with GSSAPI authentication behind a load balancer, set this parameter to the FQDN of the load balancer and, additionally, set \"strict_host_checking\" to \"false\"."

        self._options['strict_host_checking'] = False
        self._type['strict_host_checking'] = bool
        self._helptext['strict_host_checking'] = "Sets whether the server verifies the forward and reverse record set in the \"full_machine_name\" parameter. When installing this instance with GSSAPI authentication behind a load balancer, set this parameter to \"false\". Container installs imply \"false\"."

        self._options['selinux'] = Options2.default_values['selinux']
        self._type['selinux'] = bool
        self._helptext['selinux'] = "Enables SELinux detection and integration during the installation of this instance. If set to \"True\", dscreate auto-detects whether SELinux is enabled. Set this parameter only to \"False\" in a development environment."
        self._advanced['selinux'] = True

        self._options['systemd'] = Options2.get_systemd_default()
        self._type['systemd'] = bool
        self._helptext['systemd'] = "Enables systemd platform features. If set to \"True\", dscreate auto-detects whether systemd is installed. Set this only to \"False\" in a development environment."
        self._advanced['systemd'] = True

        self._options['start'] = True
        self._type['start'] = bool
        self._helptext['start'] = "Starts the instance after the install completes. If false, the instance is created but not started."

        self._options['defaults'] = INSTALL_LATEST_CONFIG
        self._type['defaults'] = str
        self._helptext['defaults'] = "Directory Server enables administrators to use the default values for cn=config entries from a specific version. If you set this parameter to \"{LATEST}\", which is the default, the instance always uses the default values of the latest version. For example, to configure that the instance uses default values from version 1.3.5, set this parameter to \"001003005\". The format of this value is XXXYYYZZZ, where X is the major version, Y the minor version, and Z the patch level. Note that each part of the value uses 3 digits and must be filled with leading zeros if necessary.".format(LATEST=INSTALL_LATEST_CONFIG)

#
# This module contains the base options and configs for Directory Server
# setup and install. This allows
#


class Slapd2Base(Options2):
    def __init__(self, log):
        super(Slapd2Base, self).__init__(log)
        self._section = 'slapd'

        self._options['instance_name'] = 'localhost'
        self._type['instance_name'] = str
        self._helptext['instance_name'] = "Sets the name of the instance. You can refer to this value in other parameters of this INF file using the \"{instance_name}\" variable. Note that this name cannot be changed after the installation!"

        self._options['user'] = ds_paths.user
        self._type['user'] = str
        self._helptext['user'] = "Sets the user name the ns-slapd process will use after the service started."
        self._advanced['user'] = True

        self._options['group'] = ds_paths.group
        self._type['group'] = str
        self._helptext['group'] = "Sets the group name the ns-slapd process will use after the service started."
        self._advanced['group'] = True

        self._options['root_dn'] = ds_paths.root_dn
        self._type['root_dn'] = str
        self._helptext['root_dn'] = "Sets the Distinquished Name (DN) of the administrator account for this instance. It is recommended that you do not change this value from the default \"cn=Directory Manager\""
        self._advanced['root_dn'] = True

        self._options['root_password'] = 'Directory_Manager_Password'
        self._type['root_password'] = str
        self._helptext['root_password'] = ("Sets the password of the \"cn=Directory Manager\" account (\"root_dn\" parameter)." +
                                           "You can either set this parameter to a plain text password dscreate hashes " +
                                           "during the installation or to a \"{algorithm}hash\" string generated by the " +
                                           "pwdhash utility. The password must be at least 8 characters long.  Note " +
                                           "that setting a plain text password can be a security risk if unprivileged " +
                                           "users can read this INF file!")

        self._options['prefix'] = ds_paths.prefix
        self._type['prefix'] = str
        self._helptext['prefix'] = "Sets the file system prefix for all other directories. You can refer to this value in other fields using the {prefix} variable or the $PREFIX environment variable. Only set this parameter in a development environment."
        self._advanced['prefix'] = True

        self._options['port'] = Options2.default_values['port']
        self._type['port'] = int
        self._helptext['port'] = "Sets the TCP port the instance uses for LDAP connections."

        self._options['secure_port'] = Options2.default_values['secure_port']
        self._type['secure_port'] = int
        self._helptext['secure_port'] = "Sets the TCP port the instance uses for TLS-secured LDAP connections (LDAPS)."

        self._options['self_sign_cert'] = True
        self._type['self_sign_cert'] = bool
        self._helptext['self_sign_cert'] = "Sets whether the setup creates a self-signed certificate and enables TLS encryption during the installation. The certificate is not suitable for production, but it enables administrators to use TLS right after the installation. You can replace the self-signed certificate with a certificate issued by a Certificate Authority. If set to False, you can enable TLS later by importing a CA/Certificate and enabling 'dsconf <instance_name> config replace nsslapd-security=on'"

        self._options['self_sign_cert_valid_months'] = 24
        self._type['self_sign_cert_valid_months'] = int
        self._helptext['self_sign_cert_valid_months'] = "Set the number of months the issued self-signed certificate will be valid."

        # In the future, make bin and sbin /usr/[s]bin, but we may need autotools assistance from Ds
        self._options['bin_dir'] = ds_paths.bin_dir
        self._type['bin_dir'] = str
        self._helptext['bin_dir'] = "Sets the location where the Directory Server binaries are stored. Only set this parameter in a development environment."
        self._advanced['bin_dir'] = True

        self._options['sbin_dir'] = ds_paths.sbin_dir
        self._type['sbin_dir'] = str
        self._helptext['sbin_dir'] = "Sets the location where the Directory Server administration binaries are stored. Only set this parameter in a development environment."
        self._advanced['sbin_dir'] = True

        self._options['sysconf_dir'] = ds_paths.sysconf_dir
        self._type['sysconf_dir'] = str
        self._helptext['sysconf_dir'] = "Sets the location of the system's configuration directory. Only set this parameter in a development environment."
        self._advanced['sysconf_dir'] = True

        self._options['initconfig_dir'] = ds_paths.initconfig_dir
        self._type['initconfig_dir'] = str
        self._helptext['initconfig_dir'] = "Sets the directory of the operating system's rc configuration directory. Only set this parameter in a development environment."
        self._advanced['initconfig_dir'] = True

        # In the future, make bin and sbin /usr/[s]bin, but we may need autotools assistance from Ds
        self._options['data_dir'] = ds_paths.data_dir
        self._type['data_dir'] = str
        self._helptext['data_dir'] = "Sets the location of Directory Server shared static data. Only set this parameter in a development environment."
        self._advanced['data_dir'] = True

        self._options['local_state_dir'] = ds_paths.local_state_dir
        self._type['local_state_dir'] = str
        self._helptext['local_state_dir'] = "Sets the location of Directory Server variable data. Only set this parameter in a development environment."
        self._advanced['local_state_dir'] = True

        self._options['ldapi'] = ds_paths.ldapi
        self._type['ldapi'] = str
        self._helptext['ldapi'] = "Sets the location of socket interface of the Directory Server."

        self._options['lib_dir'] = ds_paths.lib_dir
        self._type['lib_dir'] = str
        self._helptext['lib_dir'] = "Sets the location of Directory Server shared libraries. Only set this parameter in a development environment."
        self._advanced['lib_dir'] = True

        self._options['cert_dir'] = ds_paths.cert_dir
        self._type['cert_dir'] = str
        self._helptext['cert_dir'] = "Sets the directory of the instance's Network Security Services (NSS) database."
        self._advanced['cert_dir'] = True

        self._options['config_dir'] = ds_paths.config_dir
        self._type['config_dir'] = str
        self._helptext['config_dir'] = "Sets the configuration directory of the instance."
        self._advanced['config_dir'] = True

        self._options['inst_dir'] = ds_paths.inst_dir
        self._type['inst_dir'] = str
        self._helptext['inst_dir'] = "Sets the directory of instance-specific scripts."
        self._advanced['inst_dir'] = True

        self._options['backup_dir'] = ds_paths.backup_dir
        self._type['backup_dir'] = str
        self._helptext['backup_dir'] = "Set the backup directory of the instance."
        self._advanced['backup_dir'] = True

        self._options['db_dir'] = ds_paths.db_dir
        self._type['db_dir'] = str
        self._helptext['db_dir'] = "Sets the database directory of the instance."
        self._advanced['db_dir'] = True

        self._options['db_home_dir'] = ds_paths.db_home_dir
        self._type['db_home_dir'] = str
        self._helptext['db_home_dir'] = "Sets the memory-mapped database files location of the instance."
        self._advanced['db_home_dir'] = True

        self._options['db_lib'] = get_default_db_lib()
        self._type['db_lib'] = str
        self._helptext['db_lib'] = "Select the database implementation library (bdb or mdb)."
        self._advanced['db_lib'] = True

        self._options['ldif_dir'] = ds_paths.ldif_dir
        self._type['ldif_dir'] = str
        self._helptext['ldif_dir'] = "Sets the LDIF export and import directory of the instance."
        self._advanced['ldif_dir'] = True

        self._options['lock_dir'] = ds_paths.lock_dir
        self._type['lock_dir'] = str
        self._helptext['lock_dir'] = "Sets the lock directory of the instance."
        self._advanced['lock_dir'] = True

        self._options['log_dir'] = ds_paths.log_dir
        self._type['log_dir'] = str
        self._helptext['log_dir'] = "Sets the log directory of the instance."
        self._advanced['log_dir'] = True

        self._options['run_dir'] = ds_paths.run_dir
        self._type['run_dir'] = str
        self._helptext['run_dir'] = "Sets PID directory of the instance."
        self._advanced['run_dir'] = True

        self._options['schema_dir'] = ds_paths.schema_dir
        self._type['schema_dir'] = str
        self._helptext['schema_dir'] = "Sets schema directory of the instance."
        self._advanced['schema_dir'] = True

        self._options['tmp_dir'] = ds_paths.tmp_dir
        self._type['tmp_dir'] = str
        self._helptext['tmp_dir'] = "Sets the temporary directory of the instance."
        self._advanced['tmp_dir'] = True

    def _format(self, d):
        new_d = {}
        ks = d.keys()
        no_format_keys = set(ks) - set(format_keys)

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


class Backend2Base(Options2):
    def __init__(self, log, section):
        super(Backend2Base, self).__init__(log)
        self._section = section

        # Suffix settings
        self._options['suffix'] = ''
        self._type['suffix'] = str
        self._helptext['suffix'] = ("Sets the root suffix stored in this database.  If you do not uncomment and set the suffix " +
                                    "attribute the install process will NOT create the backend/suffix.  You can also " +
                                    "create multiple backends/suffixes by duplicating this section.")

        self._options['create_suffix_entry'] = False
        self._type['create_suffix_entry'] = bool
        self._helptext['create_suffix_entry'] = ("Set this parameter to \"True\" to create a generic root node " +
                                                 "entry for the suffix in the database.")

        self._options['sample_entries'] = "no"
        self._type['sample_entries'] = str
        self._helptext['sample_entries'] = ("Set this parameter to 'yes' to add latest version of sample " +
                                            "entries to this database.  Or, use '001003006' to use the " +
                                            "1.3.6 version sample entries.  Use this option, for example, " +
                                            "to create a database for testing purposes.")

        self._options['require_index'] = False
        self._type['require_index'] = bool
        self._helptext['require_index'] = "Set this parameter to \"True\" to refuse unindexed searches in this database."

        # Replication settings
        self._options['enable_replication'] = False
        self._type['enable_replication'] = bool
        self._helptext['enable_replication'] = ("Enable replication for this backend.  By default it will setup the backend as " +
                                                "a supplier, with replica ID 1, and \"cn=replication manager,cn=config\" as the " +
                                                "replication binddn.")

        self._options['replica_role'] = "supplier"
        self._type['replica_role'] = str
        self._helptext['replica_role'] = "Set the replication role.  Choose either 'supplier', 'hub', or 'consumer'"

        self._options['replica_id'] = "1"
        self._type['replica_id'] = str
        self._helptext['replica_id'] = "Set the unique replication identifier for this replica's database (suppliers only)"

        self._options['replica_binddn'] = "cn=replication manager,cn=config"
        self._type['replica_binddn'] = str
        self._helptext['replica_binddn'] = "Set the replication manager DN"

        self._options['replica_bindpw'] = ""
        self._type['replica_bindpw'] = str
        self._helptext['replica_bindpw'] = ("Sets the password of the Replication Manager account (\"replica_binddn\" parameter)." +
                                            "Note that setting a plain text password can be a security risk if unprivileged " +
                                            "users can read this INF file!")

        self._options['replica_bindgroup'] = ""
        self._type['replica_bindgroup'] = str
        self._helptext['replica_bindgroup'] = "Set the replication bind group DN"

        self._options['changelog_max_age'] = "7d"
        self._type['changelog_max_age'] = str
        self._helptext['changelog_max_age'] = ("How long an entry should remain in the replication changelog.  The default is 7 days, or '7d'. (requires that replication is enabled).")

        self._options['changelog_max_entries'] = "-1"
        self._type['changelog_max_entries'] = str
        self._helptext['changelog_max_entries'] = ("The maximum number of entries to keep in the replication changelog.  The default is '-1', which means unlimited. (requires that replication is enabled).")
