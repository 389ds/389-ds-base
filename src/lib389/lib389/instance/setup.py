# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import sys
import shutil
import pwd
import grp
import re
import socket
import subprocess
import getpass
import configparser
from lib389 import _ds_shutil_copytree, DirSrv
from lib389._constants import *
from lib389.properties import *
from lib389.passwd import password_hash, password_generate
from lib389.nss_ssl import NssSsl
from lib389.configurations import get_config
from lib389.configurations.sample import (
    create_base_domain,
    create_base_org,
    create_base_orgunit,
    create_base_cn,
    create_base_c,
)
from lib389.instance.options import General2Base, Slapd2Base, Backend2Base
from lib389.paths import Paths
from lib389.saslmap import SaslMappings
from lib389.instance.remove import remove_ds_instance
from lib389.index import Indexes
from lib389.replica import Replicas, BootstrapReplicationManager, Changelog
from lib389.utils import (
    assert_c,
    is_a_dn,
    ensure_str,
    ensure_list_str,
    get_default_db_lib,
    normalizeDN,
    socket_check_open,
    selinux_label_file,
    selinux_label_port,
    selinux_restorecon,
    selinux_present)

ds_paths = Paths()

# We need this to decide if we should remove after a failed install - useful
# for tests ONLY which is why it's the env flag still.
DEBUGGING = os.getenv('DEBUGGING', default=False)


def get_port(port, default_port, secure=False):
    # Get the port number for the interactive installer and validate it
    while 1:
        if secure:
            val = input('\nEnter secure port number [{}]: '.format(default_port)).rstrip()
        else:
            val = input('\nEnter port number [{}]: '.format(default_port)).rstrip()

        if val != "" or default_port == "":
            # Validate port is number and in a valid range
            try:
                port = int(val)
                if port < 1 or port > 65535:
                    print("Port number {} is not in range (1 thru 65535)".format(port))
                    continue
            except ValueError:
                # not a number
                print('Port number {} is not a number'.format(val))
                continue

            # Validate port is available
            if socket_check_open('::1', port):
                print('Port number {} is already in use, please choose a different port number'.format(port))
                continue

            # It's a good port
            return port
        elif val == "" and default_port == "":
            print('You must specify a port number')
            continue
        else:
            # Use default
            return default_port


class SetupDs(object):
    """
    Implements the Directory Server installer.

    This maybe subclassed, and a number of well known steps will be called.
    This allows the inf to be shared, and for other installers to work in lock
    step with this.

    If you are subclassing you want to derive:

    _validate_config_2(self, config):
    _prepare(self, extra):
    _install(self, extra):

    If you are calling this from an INF, you can pass the config in
    _validate_config, then stash the result into self.extra

    If you have anything you need passed to your install helpers, this can
    be given in create_from_args(extra) if you are calling as an api.

    If you use create_from_inf, self.extra is passed to create_from_args for
    you. You only need to over-load the three methods above.

    A logging interface is provided to self.log that you should call.
    """

    def __init__(self, verbose=False, dryrun=False, log=None, containerised=False):
        self.verbose = verbose
        self.extra = None
        self.dryrun = dryrun
        # Expose the logger to our children.
        self.log = log.getChild('SetupDs')
        self.log.debug('Running setup with verbose')
        # This will indicate that start / stop / status should bypass systemd.
        self.containerised = containerised

    # Could be nicer if we did self._get_config_fallback_<type>?
    def _get_config_fallback(self, config, group, attr, value, boolean=False, num=False):
        try:
            if boolean:
                return config.getboolean(group, attr)
            elif num:
                return config.getint(group, attr)
            else:
                return config.get(group, attr)
        except ValueError:
            return value
        except configparser.NoOptionError:
            self.log.info("%s not specified:setting to default - %s", attr, value)
            return value

    def _validate_config_2(self, config):
        pass

    def _prepare(self, extra):
        pass

    def _install(self, extra):
        pass

    def _validate_ds_2_config(self, config):
        assert_c(config.has_section('slapd'), "Missing configuration section [slapd]")
        # Extract them in a way that create can understand.

        general_options = General2Base(self.log)
        general_options.parse_inf_config(config)
        general_options.verify()
        general = general_options.collect()

        self.log.debug("Configuration general %s", general)

        slapd_options = Slapd2Base(self.log)
        slapd_options.parse_inf_config(config)
        slapd_options.verify()
        slapd = slapd_options.collect()

        self.log.debug("Configuration slapd %s", slapd)

        backends = []
        for section in config.sections():
            if section.startswith('backend-'):
                backend_options = Backend2Base(self.log, section)
                backend_options.parse_inf_config(config)
                suffix = config.get(section, 'suffix', fallback='')
                if suffix != '':
                    # Suffix
                    be = {}
                    be[BACKEND_NAME] = section.replace('backend-', '')
                    be[BACKEND_SUFFIX] = suffix
                    be['create_suffix_entry'] = config.get(section, 'create_suffix_entry', fallback=False)

                    # Sample entries
                    sample_entries = config.get(section, 'sample_entries', fallback='no')
                    if sample_entries.lower() != 'no':
                        if sample_entries.lower() == 'yes':
                            be[BACKEND_SAMPLE_ENTRIES] = INSTALL_LATEST_CONFIG
                        elif (sample_entries != '001003006' and sample_entries != '001004000'):
                            # invalid value
                            raise ValueError('Invalid value for sample_entries ({}), you must use "yes", "no", "001003006", or "001004000"'.format(sample_entries))
                        else:
                            be[BACKEND_SAMPLE_ENTRIES] = sample_entries

                    # Require index
                    req_idx = config.getboolean(section, 'require_index', fallback=False)
                    if req_idx:
                        be[BACKEND_REQ_INDEX] = "on"

                    # Replication settings
                    be[BACKEND_REPL_ENABLED] = False
                    if config.get(section, BACKEND_REPL_ENABLED, fallback=False):
                        be[BACKEND_REPL_ENABLED] = True
                        role = config.get(section, BACKEND_REPL_ROLE, fallback="supplier")
                        be[BACKEND_REPL_ROLE] = role
                        rid = config.get(section, BACKEND_REPL_ID, fallback="1")
                        be[BACKEND_REPL_ID] = rid
                        binddn = config.get(section, BACKEND_REPL_BINDDN, fallback=None)
                        be[BACKEND_REPL_BINDDN] = binddn
                        bindpw = config.get(section, BACKEND_REPL_BINDPW, fallback=None)
                        be[BACKEND_REPL_BINDPW] = bindpw
                        bindgrp = config.get(section, BACKEND_REPL_BINDGROUP, fallback=None)
                        be[BACKEND_REPL_BINDGROUP] = bindgrp
                        cl_max_entries = config.get(section, BACKEND_REPL_CL_MAX_ENTRIES, fallback="-1")
                        be[BACKEND_REPL_CL_MAX_ENTRIES] = cl_max_entries
                        cl_max_age = config.get(section, BACKEND_REPL_CL_MAX_AGE, fallback="7d")
                        be[BACKEND_REPL_CL_MAX_AGE] = cl_max_age

                    # Add this backend to the list
                    backends.append(be)

        self.log.debug("Configuration backends %s", backends)

        return (general, slapd, backends)

    def _validate_ds_config(self, config):
        # This will move to lib389 later.
        # Check we have all the sections.
        # Make sure we have needed keys.
        assert_c(config.has_section('general'), "Missing configuration section [general]")
        if config.get('general', 'config_version', fallback='2') == '2':
            # Call our child api to validate itself from the inf.
            self._validate_config_2(config)
            return self._validate_ds_2_config(config)
        else:
            assert_c(False, "Unsupported config_version in section [general]")

    def _remove_failed_install(self, serverid):
        """The install failed, remove the scraps
        :param serverid - The server ID of the instance
        """
        inst = DirSrv()

        # Allocate the instance based on name
        insts = []
        insts = inst.list(serverid=serverid)

        if len(insts) != 1:
            self.log.error("No such instance to remove {}".format(serverid))
            return
        inst.allocate(insts[0])
        remove_ds_instance(inst, force=True)

    def _server_id_taken(self, serverid, prefix='/usr'):
        """Check if instance name is already taken
        :param serverid - name of the server instance
        :param prefix - name of prefix build location
        :return True - if the server id is already in use
                False - if the server id is available
        """
        if prefix != "/usr":
            inst_dir = prefix + "/etc/dirsrv/slapd-" + serverid
        else:
            inst_dir = "/etc/dirsrv/slapd-" + serverid

        return os.path.isdir(inst_dir)

    def create_from_cli(self):
        # Ask questions to generate general, slapd, and backends
        print('Install Directory Server (interactive mode)')
        print('===========================================')

        # Set the defaults
        general = {'config_version': 2,
                   'full_machine_name': socket.getfqdn(),
                   'strict_host_checking': False,
                   'selinux': True,
                   'systemd': ds_paths.with_systemd,
                   'defaults': '999999999', 'start': True}

        slapd = {'self_sign_cert_valid_months': 24,
                 'group': ds_paths.group,
                 'root_dn': ds_paths.root_dn,
                 'initconfig_dir': ds_paths.initconfig_dir,
                 'self_sign_cert': True,
                 'root_password': '',
                 'port': 389,
                 'instance_name': 'localhost',
                 'user': ds_paths.user,
                 'secure_port': 636,
                 'prefix': ds_paths.prefix,
                 'bin_dir': ds_paths.bin_dir,
                 'sbin_dir': ds_paths.sbin_dir,
                 'sysconf_dir': ds_paths.sysconf_dir,
                 'data_dir': ds_paths.data_dir,
                 'local_state_dir': ds_paths.local_state_dir,
                 'ldapi': ds_paths.ldapi,
                 'lib_dir': ds_paths.lib_dir,
                 'run_dir': ds_paths.run_dir,
                 'tmp_dir': ds_paths.tmp_dir,
                 'cert_dir': ds_paths.cert_dir,
                 'config_dir': ds_paths.config_dir,
                 'inst_dir': ds_paths.inst_dir,
                 'backup_dir': ds_paths.backup_dir,
                 'db_dir': ds_paths.db_dir,
                 'db_home_dir': ds_paths.db_home_dir,
                 'db_lib': get_default_db_lib(),
                 'ldif_dir': ds_paths.ldif_dir,
                 'lock_dir': ds_paths.lock_dir,
                 'log_dir': ds_paths.log_dir,
                 'schema_dir': ds_paths.schema_dir}

        # Let them know about the selinux status
        if not selinux_present():
            val = input('\nSelinux support will be disabled, continue? [yes]: ')
            if val.strip().lower().startswith('n'):
                return

        # Start asking questions, beginning with the hostname...
        val = input('\nEnter system\'s hostname [{}]: '.format(general['full_machine_name'])).rstrip()
        if val != "":
            general['full_machine_name'] = val

        # Instance name - adjust defaults once set
        while 1:
            slapd['instance_name'] = general['full_machine_name'].split('.', 1)[0]

            # Check if default server id is taken
            if self._server_id_taken(slapd['instance_name'], prefix=slapd['prefix']):
                slapd['instance_name'] = ""

            val = input('\nEnter the instance name [{}]: '.format(slapd['instance_name'])).rstrip()
            if val != "":
                if len(val) > 80:
                    print("Server identifier should not be longer than 80 symbols")
                    continue
                if not all(ord(c) < 128 for c in val):
                    print("Server identifier can not contain non ascii characters")
                    continue
                if ' ' in val:
                    print("Server identifier can not contain a space")
                    continue
                if val == 'admin':
                    print("Server identifier \"admin\" is reserved, please choose a different identifier")
                    continue

                # Check that valid characters are used
                safe = re.compile(r'^[#%:\w@_-]+$').search
                if not bool(safe(val)):
                    print("Server identifier has invalid characters, please choose a different value")
                    continue

                # Check if server id is taken
                if self._server_id_taken(val, prefix=slapd['prefix']):
                    print("Server identifier \"{}\" is already taken, please choose a new name".format(val))
                    continue

                # instance name is good
                slapd['instance_name'] = val
                break
            elif slapd['instance_name'] == "":
                continue
            else:
                # Check if default server id is taken
                if self._server_id_taken(slapd['instance_name'], prefix=slapd['prefix']):
                    print("Server identifier \"{}\" is already taken, please choose a new name".format(slapd['instance_name']))
                    continue
                break

        # Finally have a good server id, adjust the default paths
        for key, value in slapd.items():
            if isinstance(value, str):
                slapd[key] = value.format(instance_name=slapd['instance_name'])

        # Non-Secure Port
        if not socket_check_open('::1', slapd['port']):
            port = get_port(slapd['port'], slapd['port'])
        else:
            # Port 389 is already taken, pick another port
            port = get_port(slapd['port'], "")
        slapd['port'] = port

        # Self-Signed Cert DB
        while 1:
            val = input('\nCreate self-signed certificate database [yes]: ').rstrip().lower()
            if val != "":
                if val== 'no' or val == "n":
                    slapd['self_sign_cert'] = False
                    break
                elif val == "yes" or val == "y":
                    # Default value is already yes
                    break
                else:
                    print('Invalid value "{}", please use "yes" or "no"')
                    continue
            else:
                # use default
                break

        # Secure Port (only if using self signed cert)
        if slapd['self_sign_cert']:
            if not socket_check_open('::1', slapd['secure_port']):
                port = get_port(slapd['secure_port'], slapd['secure_port'], secure=True)
            else:
                # Port 636 is already taken, pick another port
                port = get_port(slapd['secure_port'], "", secure=True)
            slapd['secure_port'] = port
        else:
            slapd['secure_port'] = False

        # Root DN
        while 1:
            val = input('\nEnter Directory Manager DN [{}]: '.format(slapd['root_dn'])).rstrip()
            if val != '':
                # Validate value is a DN
                if is_a_dn(val, allow_anon=False):
                    slapd['root_dn'] = val
                    break
                else:
                    print('The value "{}" is not a valid DN'.format(val))
                    continue
            else:
                # use default
                break

        # Root DN Password
        while 1:
            rootpw1 = getpass.getpass('\nEnter the Directory Manager password: ').rstrip()
            if rootpw1 == '':
                print('Password can not be empty')
                continue

            if len(rootpw1) < 8:
                print('Password must be at least 8 characters long')
                continue


            rootpw2 = getpass.getpass('Confirm the Directory Manager Password: ').rstrip()
            if rootpw1 != rootpw2:
                print('Passwords do not match')
                continue

            # Passwords match, set it
            slapd['root_password'] = rootpw1
            break

        # Backend   [{'name': 'userroot', 'suffix': 'dc=example,dc=com'}]
        backend = {'name': 'userroot', 'suffix': ''}
        backends = [backend]
        suffix = ''
        domain_comps = general['full_machine_name'].split('.')
        for comp in domain_comps:
            if suffix == '':
                suffix = "dc=" + comp
            else:
                suffix += ",dc=" + comp

        while 1:
            val = input("\nEnter the database suffix (or enter \"none\" to skip) [{}]: ".format(suffix)).rstrip()
            if val != '':
                if val.lower() == "none":
                    # No database, no problem
                    backends = []
                    break
                if is_a_dn(val, allow_anon=False):
                    backend['suffix'] = val
                    break
                else:
                    print("The suffix \"{}\" is not a valid DN".format(val))
                    continue
            else:
                backend['suffix'] = suffix
                break

        # Add sample entries or root suffix entry?
        if len(backends) > 0:
            while 1:
                val = input("\nCreate sample entries in the suffix [no]: ").rstrip().lower()
                if val != "":
                    if val == "no" or val == "n":
                        break
                    if val == "yes" or val == "y":
                        backend['sample_entries'] = INSTALL_LATEST_CONFIG
                        break

                    # Unknown value
                    print ("Value \"{}\" is invalid, please use \"yes\" or \"no\"".format(val))
                    continue
                else:
                    break

            if 'sample_entries' not in backend:
                # Check if they want to create the root node entry instead
                while 1:
                    val = input("\nCreate just the top suffix entry [no]: ").rstrip().lower()
                    if val != "":
                        if val == "no" or val == "n":
                            break
                        if val == "yes" or val == "y":
                            backend['create_suffix_entry'] = True
                            break

                        # Unknown value
                        print ("Value \"{}\" is invalid, please use \"yes\" or \"no\"".format(val))
                        continue
                    else:
                        break

        # Start the instance?
        while 1:
            val = input('\nDo you want to start the instance after the installation? [yes]: ').rstrip().lower()
            if val == '' or val == 'yes' or val == 'y':
                # Default behaviour
                break
            elif val == "no" or val == 'n':
                general['start'] = False
                break
            else:
                print('Invalid value, please use \"yes\" or \"no\"')
                continue

        # Are you ready?
        while 1:
            val = input('\nAre you ready to install? [no]: ').rstrip().lower()
            if val == '' or val == "no" or val == 'n':
                print('Aborting installation...')
                sys.exit(0)
            elif val == 'yes' or val == 'y':
                # lets do it!
                break
            else:
                print('Invalid value, please use \"yes\" or \"no\"')
                continue

        self.create_from_args(general, slapd, backends, self.extra)

        return True

    def create_from_inf(self, inf_path):
        """
        Will trigger a create from the settings stored in inf_path
        """
        # Get the inf file
        self.log.debug("Using inf from %s" % inf_path)
        if not os.path.isfile(inf_path):
            self.log.error("%s is not a valid file path", inf_path)
            return False
        config = None
        try:
            config = configparser.ConfigParser()
            config.read([inf_path])
        except Exception as e:
            self.log.error("Exception %s occured", e)
            return False

        self.log.debug("Configuration %s" % config.sections())
        (general, slapd, backends) = self._validate_ds_config(config)

        # Actually do the setup now.
        self.create_from_args(general, slapd, backends, self.extra)

        return True

    def _prepare_ds(self, general, slapd, backends):
        self.log.info("Validate installation settings ...")
        assert_c(general['defaults'] is not None, "Configuration defaults in section [general] not found")
        self.log.debug("PASSED: using config settings %s" % general['defaults'])
        # Validate our arguments.
        assert_c(slapd['user'] is not None, "Configuration user in section [slapd] not found")
        # check the user exists
        assert_c(pwd.getpwnam(slapd['user']), "user %s not found on system" % slapd['user'])
        slapd['user_uid'] = pwd.getpwnam(slapd['user']).pw_uid
        assert_c(slapd['group'] is not None, "Configuration group in section [slapd] not found")
        assert_c(grp.getgrnam(slapd['group']), "group %s not found on system" % slapd['group'])
        slapd['group_gid'] = grp.getgrnam(slapd['group']).gr_gid
        # check this group exists
        # Check that we are running as this user / group, or that we are root.
        assert_c(os.geteuid() == 0 or getpass.getuser() == slapd['user'], "Not running as user root or %s, may not have permission to continue" % slapd['user'])

        self.log.debug("PASSED: user / group checking")

        assert_c(general['full_machine_name'] is not None, "Configuration full_machine_name in section [general] not found")
        assert_c(general['strict_host_checking'] is not None, "Configuration strict_host_checking in section [general] not found")
        if general['strict_host_checking'] is True:
            # Check it resolves with dns
            assert_c(socket.gethostbyname(general['full_machine_name']), "Strict hostname check failed. Check your DNS records for %s" % general['full_machine_name'])
            self.log.debug("PASSED: Hostname strict checking")

        assert_c(slapd['prefix'] is not None, "Configuration prefix in section [slapd] not found")
        if (slapd['prefix'] != ""):
            assert_c(os.path.exists(slapd['prefix']), "Prefix location '%s' not found" % slapd['prefix'])
        self.log.debug("PASSED: prefix checking")

        # We need to know the prefix before we can do the instance checks
        assert_c(slapd['instance_name'] is not None, "Configuration instance_name in section [slapd] not found")
        assert_c(len(slapd['instance_name']) <= 80, "Server identifier should not be longer than 80 symbols")
        assert_c(all(ord(c) < 128 for c in slapd['instance_name']), "Server identifier can not contain non ascii characters")
        assert_c(' ' not in slapd['instance_name'], "Server identifier can not contain a space")
        assert_c(slapd['instance_name'] != 'admin', "Server identifier \"admin\" is reserved, please choose a different identifier")

        # Check that valid characters are used
        safe = re.compile(r'^[:\w@_-]+$').search
        assert_c(bool(safe(slapd['instance_name'])), "Server identifier has invalid characters, please choose a different value")

        # Check if the instance exists or not.
        # Should I move this import? I think this prevents some recursion
        from lib389 import DirSrv
        ds = DirSrv(verbose=self.verbose)
        ds.containerised = self.containerised
        ds.prefix = slapd['prefix']
        insts = ds.list(serverid=slapd['instance_name'])
        assert_c(len(insts) == 0, "Another instance named '%s' may already exist" % slapd['instance_name'])

        self.log.debug("PASSED: instance checking")

        assert_c(slapd['root_dn'] is not None, "Configuration root_dn in section [slapd] not found")
        # Assert this is a valid DN
        assert_c(is_a_dn(slapd['root_dn']), "root_dn in section [slapd] is not a well formed LDAP DN")
        assert_c(slapd['root_password'] is not None and slapd['root_password'] != '',
                 "Configuration attribute 'root_password' in section [slapd] not found")
        if len(slapd['root_password']) < 8:
            raise ValueError("root_password must be at least 8 characters long")

        # Check if pre-hashed or not.
        # !!!!!!!!!!!!!!

        # Right now, the way that rootpw works on ns-slapd works, it force hashes the pw
        # see https://fedorahosted.org/389/ticket/48859
        if not re.match('^([A-Z0-9]+).*$', slapd['root_password']):
            # We need to hash it. Call pwdhash-bin.
            # slapd['root_password'] = password_hash(slapd['root_password'], prefix=slapd['prefix'])
            pass
        else:
            pass

        # Create a random string
        # Hash it.
        # This will be our temporary rootdn password so that we can do
        # live mods and setup rather than static ldif manipulations.
        self._raw_secure_password = password_generate()
        self._secure_password = password_hash(self._raw_secure_password, bin_dir=slapd['bin_dir'])

        self.log.debug("INFO: temp root password set to %s" % self._raw_secure_password)
        self.log.debug("PASSED: root user checking")

        assert_c(slapd['port'] is not None, "Configuration port in section [slapd] not found")

        if self.containerised:
            if slapd['port'] <= 1024:
                self.log.warning("WARNING: slapd port %s may not work without NET_BIND_SERVICE in containers" % slapd['port'])
            if slapd['secure_port'] <= 1024:
                self.log.warning("WARNING: slapd secure_port %s may not work without NET_BIND_SERVICE in containers" % slapd['secure_port'])
        assert_c(socket_check_open('::1', slapd['port']) is False, "port %s is already in use, or missing NET_BIND_SERVICE" % slapd['port'])
        # We enable secure port by default.
        assert_c(slapd['secure_port'] is not None, "Configuration secure_port in section [slapd] not found")
        assert_c(socket_check_open('::1', slapd['secure_port']) is False, "secure_port %s is already in use, or missing NET_BIND_SERVICE" % slapd['secure_port'])
        self.log.debug("PASSED: network avaliability checking")

        # Make assertions of the paths?

        # Make assertions of the backends?
        # First fix some compat shenanigans. I hate legacy ...
        for be in backends:
            for k in BACKEND_PROPNAME_TO_ATTRNAME:
                if k in be:
                    be[BACKEND_PROPNAME_TO_ATTRNAME[k]] = be[k]
                    del(be[k])
        for be in backends:
            assert_c('nsslapd-suffix' in be)
            assert_c('cn' in be)
        # Add an assertion that we don't double suffix or double CN here ...

    def create_from_args(self, general, slapd, backends=[], extra=None):
        """
        Actually does the setup. this is what you want to call as an api.
        """

        self.log.debug("START: Starting installation ...")
        if not self.verbose:
            self.log.info("Starting installation ...")

        # Check we have privs to run
        self.log.debug("READY: Preparing installation for %s...", slapd['instance_name'])

        self._prepare_ds(general, slapd, backends)
        # Call our child api to prepare itself.
        self._prepare(extra)

        self.log.debug("READY: Beginning installation for %s...", slapd['instance_name'])

        if self.dryrun:
            self.log.info("NOOP: Dry run requested")
        else:
            # Actually trigger the installation.
            try:
                self._install_ds(general, slapd, backends)
            except ValueError as e:
                if DEBUGGING is False:
                    self._remove_failed_install(slapd['instance_name'])
                else:
                    self.log.fatal(f"Error: {str(e)}, preserving incomplete installation for analysis...")
                raise ValueError(f"Instance creation failed!  {str(e)}")

            # Call the child api to do anything it needs.
            self._install(extra)
        self.log.debug("FINISH: Completed installation for instance: slapd-%s", slapd['instance_name'])
        if not self.verbose:
            self.log.info("Completed installation for instance: slapd-%s", slapd['instance_name'])

        return True

    def _install_ds(self, general, slapd, backends):
        """
        Actually install the Ds from the dicts provided.

        You should never call this directly, as it bypasses assertions.
        """
        ######################## WARNING #############################
        # DO NOT CHANGE THIS FUNCTION OR ITS CONTENTS WITHOUT READING
        # ALL OF THE COMMENTS FIRST. THERE ARE VERY DELICATE
        # AND DETAILED INTERACTIONS OF COMPONENTS IN THIS FUNCTION.
        #
        # IF IN DOUBT CONTACT WILLIAM BROWN <william@blackhats.net.au>


        ### This first section is about creating the *minimal* required paths and config to get
        # directory server to start: After this, we then perform all configuration as online
        # changes from after this point.

        # Create dse.ldif with a temporary root password.
        # This is done first, because instances are found for removal and listing by detecting
        # the present of their dse.ldif!!!!
        # The template is in slapd['data_dir']/dirsrv/data/template-dse.ldif
        # Variables are done with %KEY%.
        self.log.debug("ACTION: Creating dse.ldif")
        try:
            os.umask(0o007)  # For parent dirs that get created -> sets 770 for perms
            os.makedirs(slapd['config_dir'], mode=0o770)
        except OSError:
            pass

        # Get suffix for some plugin defaults (if possible)
        # annoyingly for legacy compat backend takes TWO key types
        # and we have to now deal with that ....
        #
        # Create ds_suffix here else it won't be in scope ....
        ds_suffix = ''
        if len(backends) > 0:
            ds_suffix = normalizeDN(backends[0]['nsslapd-suffix'])

        dse = ""
        with open(os.path.join(slapd['data_dir'], 'dirsrv', 'data', 'template-dse.ldif')) as template_dse:
            for line in template_dse.readlines():
                dse += line.replace('%', '{', 1).replace('%', '}', 1)

        # Check if we are in a container, if so don't use /dev/shm for the db home dir
        # as containers typically don't allocate enough space for dev/shm and we don't
        # want to unexpectedly break the server after an upgrade
        #
        # If we know we are are in a container, we don't need to re-detect on systemd.
        # It actually turns out if you add systemd-detect-virt, that pulls in system
        # which subsequently breaks containers starting as instance.start then believes
        # it COULD check the ds status. The times we need to check for systemd are mainly
        # in other environments that use systemd natively in their containers.
        container_result = 1
        if not self.containerised:
            container_result = subprocess.run(["systemd-detect-virt", "-c"], stdout=subprocess.PIPE)
        if self.containerised or container_result.returncode == 0:
            # In a container, set the db_home_dir to the db path
            self.log.debug("Container detected setting db home directory to db directory.")
            slapd['db_home_dir'] = slapd['db_dir']

        with open(os.path.join(slapd['config_dir'], 'dse.ldif'), 'w') as file_dse:
            dse_fmt = dse.format(
                schema_dir=slapd['schema_dir'],
                lock_dir=slapd['lock_dir'],
                tmp_dir=slapd['tmp_dir'],
                cert_dir=slapd['cert_dir'],
                ldif_dir=slapd['ldif_dir'],
                bak_dir=slapd['backup_dir'],
                run_dir=slapd['run_dir'],
                inst_dir=slapd['inst_dir'],
                log_dir=slapd['log_dir'],
                fqdn=general['full_machine_name'],
                ds_port=slapd['port'],
                ds_user=slapd['user'],
                rootdn=slapd['root_dn'],
                instance_name=slapd['instance_name'],
                ds_passwd=self._secure_password,  # We set our own password here, so we can connect and mod.
                # This is because we never know the users input root password as they can validly give
                # us a *hashed* input.
                ds_suffix=ds_suffix,
                config_dir=slapd['config_dir'],
                db_dir=slapd['db_dir'],
                db_home_dir=slapd['db_home_dir'],
                db_lib=slapd['db_lib'],
                ldapi_enabled="on",
                ldapi=slapd['ldapi'],
                ldapi_autobind="on",
            )
            file_dse.write(dse_fmt)

        self.log.info("Create file system structures ...")
        # Create all the needed paths
        # we should only need to make bak_dir, cert_dir, config_dir, db_dir, ldif_dir, lock_dir, log_dir, run_dir?
        for path in ('backup_dir', 'cert_dir', 'db_dir', 'db_home_dir', 'ldif_dir', 'lock_dir', 'log_dir', 'run_dir'):
            self.log.debug("ACTION: creating %s", slapd[path])
            try:
                os.umask(0o007)  # For parent dirs that get created -> sets 770 for perms
                os.makedirs(slapd[path], mode=0o770)
            except OSError:
                pass
            os.chown(slapd[path], slapd['user_uid'], slapd['group_gid'])

        # /var/lock/dirsrv needs special attention...
        parentdir = os.path.abspath(os.path.join(slapd['lock_dir'], os.pardir))
        os.chown(parentdir, slapd['user_uid'], slapd['group_gid'])

        ### Warning! We need to down the directory under db too for .restore to work.
        # During a restore, the db dir is deleted and recreated, which is why we need
        # to own it for a restore.
        #
        # However, in a container, we can't always guarantee this due to how the volumes
        # work and are mounted. Specifically, if we have an anonymous volume we will
        # NEVER be able to own it, but in a true deployment it is reasonable to expect
        # we DO own it. Thus why we skip it in this specific context
        if not self.containerised:
            db_parent = os.path.join(slapd['db_dir'], '..')
            os.chown(db_parent, slapd['user_uid'], slapd['group_gid'])

        # Copy correct data to the paths.
        # Copy in the schema
        #  This is a little fragile, make it better.
        # It won't matter when we move schema to usr anyway ...

        _ds_shutil_copytree(os.path.join(slapd['sysconf_dir'], 'dirsrv/schema'), slapd['schema_dir'])
        os.chown(slapd['schema_dir'], slapd['user_uid'], slapd['group_gid'])
        os.chmod(slapd['schema_dir'], 0o770)

        # Copy in the collation
        srcfile = os.path.join(slapd['sysconf_dir'], 'dirsrv/config/slapd-collations.conf')
        dstfile = os.path.join(slapd['config_dir'], 'slapd-collations.conf')
        shutil.copy(srcfile, dstfile)
        os.chown(dstfile, slapd['user_uid'], slapd['group_gid'])
        os.chmod(dstfile, 0o440)

        # Copy in the certmap configuration
        srcfile = os.path.join(slapd['sysconf_dir'], 'dirsrv/config/certmap.conf')
        dstfile = os.path.join(slapd['config_dir'], 'certmap.conf')
        shutil.copy(srcfile, dstfile)
        os.chown(dstfile, slapd['user_uid'], slapd['group_gid'])
        os.chmod(dstfile, 0o440)

        # If we are on the correct platform settings, systemd
        if general['systemd']:
            # Should create the symlink we need, but without starting it.
            result = subprocess.run(["systemctl", "enable", "dirsrv@%s" % slapd['instance_name']],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            args = ' '.join(ensure_list_str(result.args))
            stdout = ensure_str(result.stdout)
            stderr = ensure_str(result.stderr)
            # Systemd encodes some odd charecters into it's symlink output on newer versions which
            # can trip up the logger.
            self.log.debug(f"CMD: {args} ; STDOUT: {stdout} ; STDERR: {stderr}".encode("utf-8"))

            # Setup tmpfiles_d
            tmpfile_d = ds_paths.tmpfiles_d + "/dirsrv-" + slapd['instance_name'] + ".conf"
            with open(tmpfile_d, "w") as TMPFILE_D:
                TMPFILE_D.write("d {} 0770 {} {}\n".format(slapd['run_dir'], slapd['user'], slapd['group']))
                TMPFILE_D.write("d {} 0770 {} {}\n".format(slapd['lock_dir'].replace("slapd-" + slapd['instance_name'], ""),
                                                           slapd['user'], slapd['group']))
                TMPFILE_D.write("d {} 0770 {} {}\n".format(slapd['lock_dir'], slapd['user'], slapd['group']))

        # Else we need to detect other init scripts?
        # WB: No, we just install and assume that docker will start us ...

        # Bind sockets to our type?

        # Create certdb in sysconfidir
        self.log.debug("ACTION: Creating certificate database is %s", slapd['cert_dir'])

        # BELOW THIS LINE - all actions are now ONLINE changes to the directory server.
        # if it all possible, ALWAYS ADD NEW INSTALLER CHANGES AS ONLINE ACTIONS.

        # Should I move this import? I think this prevents some recursion
        from lib389 import DirSrv
        ds_instance = DirSrv(self.verbose, containerised=self.containerised)
        if self.containerised:
            ds_instance.systemd_override = general['systemd']

        # By default SUSE does something extremely silly - it creates a hostname
        # that CANT be resolved by DNS. As a result this causes all installs to
        # fail. We need to guarantee that we only connect to localhost here, as
        # it's the only stable and guaranteed way to connect to the instance
        # at this point.
        #
        # Use ldapi which would prevent the need
        # to configure a temp root pw in the setup phase.
        args = {
            SER_HOST: "localhost",
            SER_PORT: slapd['port'],
            SER_SERVERID_PROP: slapd['instance_name'],
            SER_ROOT_DN: slapd['root_dn'],
            SER_ROOT_PW: self._raw_secure_password,
            SER_DEPLOYED_DIR: slapd['prefix'],
            SER_LDAPI_ENABLED: 'on',
            SER_LDAPI_SOCKET: slapd['ldapi'],
            SER_LDAPI_AUTOBIND: 'on'
        }

        ds_instance.allocate(args)
        # Does this work?
        assert_c(ds_instance.exists(), "Instance failed to install, does not exist when expected")

        # Create a certificate database.
        tlsdb = NssSsl(dirsrv=ds_instance, dbpath=slapd['cert_dir'])
        if not tlsdb._db_exists():
            tlsdb.reinit()

        if slapd['self_sign_cert']:
            self.log.info("Create self-signed certificate database ...")
            etc_dirsrv_path = os.path.join(slapd['sysconf_dir'], 'dirsrv/')
            ssca_path = os.path.join(etc_dirsrv_path, 'ssca/')
            ssca = NssSsl(dbpath=ssca_path)
            # If it doesn't exist, create a CA DB
            if not ssca._db_exists():
                ssca.reinit()
                ssca.create_rsa_ca(months=slapd['self_sign_cert_valid_months'])
            # If CA is expired or will expire soon,
            # Reissue it and resign the existing certs that were signed by the cert previously
            elif ssca.rsa_ca_needs_renew():
                ca = ssca.renew_rsa_ca(months=slapd['self_sign_cert_valid_months'])
                # Import CA to the existing instances except the one we install now (we import it later)
                for dir in os.listdir(etc_dirsrv_path):
                    if dir.startswith("slapd-") and dir != slapd['cert_dir']:
                        tlsdb_inst = NssSsl(dbpath=os.path.join(etc_dirsrv_path, dir))
                        tlsdb_inst.import_rsa_crt(ca)

            csr = tlsdb.create_rsa_key_and_csr(alt_names=[general['full_machine_name']])
            (ca, crt) = ssca.rsa_ca_sign_csr(csr)
            tlsdb.import_rsa_crt(ca, crt)
            if general['selinux']:
                # Set selinux port label
                selinux_label_port(slapd['secure_port'])

        # Do selinux fixups
        if general['selinux']:
            self.log.info("Perform SELinux labeling ...")
            # Since there may be some custom path, we must explicitly set the labels
            selinux_labels = {
                                'backup_dir': 'dirsrv_var_lib_t',
                                'cert_dir': 'dirsrv_config_t',
                                'config_dir': 'dirsrv_config_t',
                                'db_dir': 'dirsrv_var_lib_t',
                                'ldif_dir': 'dirsrv_var_lib_t',
                                'lock_dir': 'dirsrv_var_lock_t',
                                'log_dir': 'dirsrv_var_log_t',
                                'db_home_dir': 'dirsrv_tmpfs_t',
                                'run_dir': 'dirsrv_var_run_t',
                                'schema_dir': 'dirsrv_config_t',
                                'tmp_dir': 'tmp_t',
            }
            # Lets sort the paths to avoid overriding the labels if path are nested.
            selinux_sorted_labels = sorted( ( (slapd[key], label) for key, label in selinux_labels.items() ) )
            for path, label in selinux_sorted_labels:
                selinux_label_file(path, label)
                selinux_restorecon(path)

            selinux_label_port(slapd['port'])

        # Start the server
        # Make changes using the temp root
        self.log.debug(f"asan_enabled={ds_instance.has_asan()}")
        self.log.debug(f"libfaketime installed ={'libfaketime' in sys.modules}")
        assert_c(not ds_instance.has_asan() or 'libfaketime' not in sys.modules, "libfaketime python module is incompatible with ASAN build.")
        ds_instance.start(timeout=60)
        ds_instance.open()

        # In some cases we may want to change log settings
        # ds_instance.config.enable_log('audit')

        # Create the configs related to this version.
        base_config = get_config(general['defaults'])
        base_config_inst = base_config(ds_instance)
        base_config_inst.apply_config(install=True)

        # Setup TLS with the instance.

        # We *ALWAYS* set secure port, even if security is off, because it breaks
        # tests with standalone.enable_tls if we do not. It's only when security; on
        # that we actually start listening on it.
        if not slapd['secure_port']:
            slapd['secure_port'] = "636"
        ds_instance.config.set('nsslapd-secureport', '%s' % slapd['secure_port'])
        if slapd['self_sign_cert']:
            ds_instance.config.set('nsslapd-security', 'on')

        # Before we create any backends, create any extra default indexes that may be
        # dynamically provisioned, rather than from template-dse.ldif. Looking at you
        # entryUUID (requires rust enabled).
        #
        # Indexes defaults to default_index_dn
        indexes = Indexes(ds_instance)
        if ds_instance.ds_paths.rust_enabled:
            indexes.create(properties={
                'cn': 'entryUUID',
                'nsSystemIndex': 'false',
                'nsIndexType': ['eq', 'pres'],
            })

        # Create the backends as listed
        # Load example data if needed.
        for backend in backends:
            self.log.info(f"Create database backend: {backend['nsslapd-suffix']} ...")
            is_sample_entries_in_props = "sample_entries" in backend
            create_suffix_entry_in_props = backend.pop('create_suffix_entry', False)
            repl_enabled = backend.pop(BACKEND_REPL_ENABLED, False)
            rid = backend.pop(BACKEND_REPL_ID, False)
            role = backend.pop(BACKEND_REPL_ROLE, False)
            binddn = backend.pop(BACKEND_REPL_BINDDN, False)
            bindpw = backend.pop(BACKEND_REPL_BINDPW, False)
            bindgrp = backend.pop(BACKEND_REPL_BINDGROUP, False)
            cl_maxage = backend.pop(BACKEND_REPL_CL_MAX_AGE, False)
            cl_maxentries = backend.pop(BACKEND_REPL_CL_MAX_ENTRIES, False)

            ds_instance.backends.create(properties=backend)
            if not is_sample_entries_in_props and create_suffix_entry_in_props:
                # Set basic ACIs
                c_aci = '(targetattr="c || description || objectClass")(targetfilter="(objectClass=country)")(version 3.0; acl "Enable anyone c read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
                o_aci = '(targetattr="o || description || objectClass")(targetfilter="(objectClass=organization)")(version 3.0; acl "Enable anyone o read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
                dc_aci = '(targetattr="dc || description || objectClass")(targetfilter="(objectClass=domain)")(version 3.0; acl "Enable anyone domain read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
                ou_aci = '(targetattr="ou || description || objectClass")(targetfilter="(objectClass=organizationalUnit)")(version 3.0; acl "Enable anyone ou read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
                cn_aci = '(targetattr="cn || description || objectClass")(targetfilter="(objectClass=nscontainer)")(version 3.0; acl "Enable anyone cn read"; allow (read, search, compare)(userdn="ldap:///anyone");)'
                suffix_rdn_attr = backend['nsslapd-suffix'].split('=')[0].lower()
                if suffix_rdn_attr == 'dc':
                    domain = create_base_domain(ds_instance, backend['nsslapd-suffix'])
                    domain.add('aci', dc_aci)
                elif suffix_rdn_attr == 'o':
                    org = create_base_org(ds_instance, backend['nsslapd-suffix'])
                    org.add('aci', o_aci)
                elif suffix_rdn_attr == 'ou':
                    orgunit = create_base_orgunit(ds_instance, backend['nsslapd-suffix'])
                    orgunit.add('aci', ou_aci)
                elif suffix_rdn_attr == 'cn':
                    cn = create_base_cn(ds_instance, backend['nsslapd-suffix'])
                    cn.add('aci', cn_aci)
                elif suffix_rdn_attr == 'c':
                    c = create_base_c(ds_instance, backend['nsslapd-suffix'])
                    c.add('aci', c_aci)
                else:
                    # Unsupported rdn
                    raise ValueError("Suffix RDN '{}' in '{}' is not supported.  Supported RDN's are: 'c', 'cn', 'dc', 'o', and 'ou'".format(suffix_rdn_attr, backend['nsslapd-suffix']))

            if repl_enabled:
                # Okay enable replication....
                self.log.info(f"Enable replication for: {backend['nsslapd-suffix']} ...")
                repl_root = backend['nsslapd-suffix']
                if role == "supplier":
                    repl_type = '3'
                    repl_flag = '1'
                elif role == "hub":
                    repl_type = '2'
                    repl_flag = '1'
                elif role == "consumer":
                    repl_type = '2'
                    repl_flag = '0'
                else:
                    # error - unknown type
                    raise ValueError("Unknown replication role ({}), you must use \"supplier\", \"hub\", or \"consumer\"".format(role))

                # Start the propeties and update them as needed
                repl_properties = {
                    'cn': 'replica',
                    'nsDS5ReplicaRoot': repl_root,
                    'nsDS5Flags': repl_flag,
                    'nsDS5ReplicaType': repl_type,
                    'nsDS5ReplicaId': '65535'
                    }

                # Validate supplier settings
                if role == "supplier":
                    try:
                        rid_num = int(rid)
                        if rid_num < 1 or rid_num > 65534:
                            raise ValueError
                        repl_properties['nsDS5ReplicaId'] = rid
                    except ValueError:
                        raise ValueError("replica_id expects a number between 1 and 65534")

                    # rid is good add it to the props
                    repl_properties['nsDS5ReplicaId'] = rid

                # Bind DN & Group
                if binddn:
                    repl_properties['nsDS5ReplicaBindDN'] = binddn
                if bindgrp:
                    repl_properties['nsDS5ReplicaBindDNGroup'] = bindgrp

                # Enable replication
                replicas = Replicas(ds_instance)
                replicas.create(properties=repl_properties)

                # Create replication manager if password was provided
                if binddn is not None and bindpw:
                    rdn = binddn.split(",", 1)[0]
                    rdn_attr, rdn_val = rdn.split("=", 1)
                    manager = BootstrapReplicationManager(ds_instance, dn=binddn, rdn_attr=rdn_attr)
                    manager.create(properties={
                        'cn': rdn_val,
                        'uid': rdn_val,
                        'userPassword': bindpw
                    })

                # Changelog settings
                if role != "consumer":
                    cl = Changelog(ds_instance, repl_root)
                    cl.set_max_age(cl_maxage)
                    if cl_maxentries != "-1":
                        cl.set_max_entries(cl_maxentries)


        # Create all required sasl maps: if we have a single backend ...
        # our default maps are really really bad, and we should feel bad.
        # they basically only work with a single backend, and they'll break
        # GSSAPI in some cases too :(
        if len(backends) > 0:
            self.log.debug("Adding sasl maps for suffix %s" % backend['nsslapd-suffix'])
            backend = backends[0]
            saslmappings = SaslMappings(ds_instance)
            saslmappings.create(properties={
                'cn': 'rfc 2829 u syntax',
                'nsSaslMapRegexString': '^u:\\(.*\\)',
                'nsSaslMapBaseDNTemplate': backend['nsslapd-suffix'],
                'nsSaslMapFilterTemplate': '(uid=\\1)'
            })
            # I think this is for LDAPI
            saslmappings.create(properties={
                'cn': 'uid mapping',
                'nsSaslMapRegexString': '^[^:@]+$',
                'nsSaslMapBaseDNTemplate': backend['nsslapd-suffix'],
                'nsSaslMapFilterTemplate': '(uid=&)'
            })
        else:
            self.log.debug("Skipping default SASL maps - no backend found!")

        self.log.info("Perform post-installation tasks ...")
        # Change the root password finally
        ds_instance.config.set('nsslapd-rootpw', slapd['root_password'])

        # We need to log the password when containerised
        if self.containerised:
            self.log.debug("Root DN password: {}".format(slapd['root_password']))

        # Complete.
        if general['start']:
            # Restart for changes to take effect - this could be removed later
            ds_instance.restart(post_open=False)
        else:
            # Just stop the instance now.
            ds_instance.stop()

        self.log.debug(" 🎉 Instance setup complete")
