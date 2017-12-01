# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import ldap
import shutil
import sys
import pwd
import grp
import re
import socket
import subprocess

from lib389 import _ds_shutil_copytree
from lib389._constants import *
from lib389.properties import *
from lib389.passwd import password_hash, password_generate

from lib389.configurations import get_config

from lib389.instance.options import General2Base, Slapd2Base

# The poc backend api
from lib389.backend import Backends
from lib389.utils import (
    is_a_dn,
    ensure_bytes,
    ensure_str,
    socket_check_open,)

try:
    # There are too many issues with this on EL7
    # Out of the box, it's just outright broken ...
    import six.moves.urllib.request
    import six.moves.urllib.parse
    import six.moves.urllib.error
    import six
except ImportError:
    pass

MAJOR, MINOR, _, _, _ = sys.version_info

if MAJOR >= 3:
    import configparser
else:
    import ConfigParser as configparser


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
        if self.verbose:
            self.log.info('Running setup with verbose')
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
            self.log.info("%s not specified:setting to default - %s" % (attr, value))
            return value

    def _validate_config_2(self, config):
        pass

    def _prepare(self, extra):
        pass

    def _install(self, extra):
        pass

    def _validate_ds_2_config(self, config):
        assert config.has_section('slapd')
        # Extract them in a way that create can understand.

        general_options = General2Base(self.log)
        general_options.parse_inf_config(config)
        general_options.verify()
        general = general_options.collect()

        if self.verbose:
            self.log.info("Configuration general %s" % general)

        slapd_options = Slapd2Base(self.log)
        slapd_options.parse_inf_config(config)
        slapd_options.verify()
        slapd = slapd_options.collect()

        if self.verbose:
            self.log.info("Configuration slapd %s" % slapd)

        backends = []
        for section in config.sections():
            if section.startswith('backend-'):
                be = {}
                # TODO: Add the other BACKEND_ types
                be[BACKEND_NAME] = section.replace('backend-', '')
                be[BACKEND_SUFFIX] = config.get(section, 'suffix')
                be[BACKEND_SAMPLE_ENTRIES] = config.get(section, 'sample_entries')
                backends.append(be)

        if self.verbose:
            self.log.info("Configuration backends %s" % backends)

        return (general, slapd, backends)

    def _validate_ds_config(self, config):
        # This will move to lib389 later.
        # Check we have all the sections.
        # Make sure we have needed keys.
        assert(config.has_section('general'))
        assert(config.has_option('general', 'config_version'))
        assert(config.get('general', 'config_version') >= '2')
        if config.get('general', 'config_version') == '2':
            # Call our child api to validate itself from the inf.
            self._validate_config_2(config)
            return self._validate_ds_2_config(config)
        else:
            self.log.info("Failed to validate configuration version.")
            assert(False)

    def create_from_inf(self, inf_path):
        """
        Will trigger a create from the settings stored in inf_path
        """
        # Get the inf file
        if self.verbose:
            self.log.info("Using inf from %s" % inf_path)
        if not os.path.isfile(inf_path):
            self.log.error("%s is not a valid file path" % inf_path)
            return False
        config = None
        try:
            config = configparser.SafeConfigParser()
            config.read([inf_path])
        except Exception as e:
            self.log.error("Exception %s occured" % e)
            return False

        if self.verbose:
            self.log.info("Configuration %s" % config.sections())

        (general, slapd, backends) = self._validate_ds_config(config)

        # Actually do the setup now.
        self.create_from_args(general, slapd, backends, self.extra)

        return True

    def _prepare_ds(self, general, slapd, backends):

        assert(general['defaults'] is not None)
        if self.verbose:
            self.log.info("PASSED: using config settings %s" % general['defaults'])
        # Validate our arguments.
        assert(slapd['user'] is not None)
        # check the user exists
        assert(pwd.getpwnam(slapd['user']))
        slapd['user_uid'] = pwd.getpwnam(slapd['user']).pw_uid
        assert(slapd['group'] is not None)
        assert(grp.getgrnam(slapd['group']))
        slapd['group_gid'] = grp.getgrnam(slapd['group']).gr_gid
        # check this group exists
        # Check that we are running as this user / group, or that we are root.
        assert(os.geteuid() == 0 or getpass.getuser() == slapd['user'])

        if self.verbose:
            self.log.info("PASSED: user / group checking")

        assert(general['full_machine_name'] is not None)
        assert(general['strict_host_checking'] is not None)
        if general['strict_host_checking'] is True:
            # Check it resolves with dns
            assert(socket.gethostbyname(general['full_machine_name']))
            if self.verbose:
                self.log.info("PASSED: Hostname strict checking")

        assert(slapd['prefix'] is not None)
        if (slapd['prefix'] != ""):
            assert(os.path.exists(slapd['prefix']))
        if self.verbose:
            self.log.info("PASSED: prefix checking")

        # We need to know the prefix before we can do the instance checks
        assert(slapd['instance_name'] is not None)
        # Check if the instance exists or not.
        # Should I move this import? I think this prevents some recursion
        from lib389 import DirSrv
        ds = DirSrv(verbose=self.verbose)
        ds.containerised = self.containerised
        ds.prefix = slapd['prefix']
        insts = ds.list(serverid=slapd['instance_name'])
        assert(len(insts) == 0)

        if self.verbose:
            self.log.info("PASSED: instance checking")

        assert(slapd['root_dn'] is not None)
        # Assert this is a valid DN
        assert(is_a_dn(slapd['root_dn']))
        assert(slapd['root_password'] is not None)
        # Check if pre-hashed or not.
        # !!!!!!!!!!!!!!

        # Right now, the way that rootpw works on ns-slapd works, it force hashes the pw
        # see https://fedorahosted.org/389/ticket/48859
        if not re.match('^\{[A-Z0-9]+\}.*$', slapd['root_password']):
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

        if self.verbose:
            self.log.info("INFO: temp root password set to %s" % self._raw_secure_password)
            self.log.info("PASSED: root user checking")

        assert(slapd['port'] is not None)
        assert(socket_check_open('::1', slapd['port']) is False)
        ## This causes some problems in tests :(
        # assert(slapd['secure_port'] is not None)
        if slapd['secure_port'] is not None:
            assert(socket_check_open('::1', slapd['secure_port']) is False)
        if self.verbose:
            self.log.info("PASSED: network avaliability checking")

        # Make assertions of the paths?

        # Make assertions of the backends?

    def create_from_args(self, general, slapd, backends=[], extra=None):
        """
        Actually does the setup. this is what you want to call as an api.
        """
        # Check we have privs to run

        if self.verbose:
            self.log.info("READY: preparing installation for %s" % slapd['instance_name'])
        self._prepare_ds(general, slapd, backends)
        # Call our child api to prepare itself.
        self._prepare(extra)

        if self.verbose:
            self.log.info("READY: beginning installation for %s" % slapd['instance_name'])

        if self.dryrun:
            self.log.info("NOOP: dry run requested")
        else:
            # Actually trigger the installation.
            self._install_ds(general, slapd, backends)
            # Call the child api to do anything it needs.
            self._install(extra)
        self.log.info("FINISH: completed installation for %s" % slapd['instance_name'])

    def _install_ds(self, general, slapd, backends):
        """
        Actually install the Ds from the dicts provided.

        You should never call this directly, as it bypasses assertions.
        """
        # register the instance to /etc/sysconfig
        # We do this first so that we can trick remove-ds.pl if needed.
        # There may be a way to create this from template like the dse.ldif ...
        initconfig = ""
        with open("%s/dirsrv/config/template-initconfig" % slapd['sysconf_dir']) as template_init:
            for line in template_init.readlines():
                initconfig += line.replace('{{', '{', 1).replace('}}', '}', 1).replace('-', '_')
        try:
            os.makedirs("%s/sysconfig" % slapd['sysconf_dir'], mode=0o775)
        except FileExistsError:
            pass
        with open("%s/sysconfig/dirsrv-%s" % (slapd['sysconf_dir'], slapd['instance_name']), 'w') as f:
            f.write(initconfig.format(
                SERVER_DIR=slapd['lib_dir'],
                SERVERBIN_DIR=slapd['sbin_dir'],
                CONFIG_DIR=slapd['config_dir'],
                INST_DIR=slapd['inst_dir'],
                RUN_DIR=slapd['run_dir'],
                DS_ROOT='',
                PRODUCT_NAME='slapd',
            ))

        # Create all the needed paths
        # we should only need to make bak_dir, cert_dir, config_dir, db_dir, ldif_dir, lock_dir, log_dir, run_dir? schema_dir,
        for path in ('backup_dir', 'cert_dir', 'config_dir', 'db_dir', 'ldif_dir', 'lock_dir', 'log_dir', 'run_dir'):
            if self.verbose:
                self.log.info("ACTION: creating %s" % slapd[path])
            try:
                os.makedirs(slapd[path], mode=0o775)
            except OSError:
                pass
            os.chown(slapd[path], slapd['user_uid'], slapd['group_gid'])
        ### Warning! We need to down the directory under db too for .restore to work.
        # See dblayer.c for more!
        db_parent = os.path.join(slapd['db_dir'], '..')
        os.chown(db_parent, slapd['user_uid'], slapd['group_gid'])

        # Copy correct data to the paths.
        # Copy in the schema
        #  This is a little fragile, make it better.
        # It won't matter when we move schema to usr anyway ...

        _ds_shutil_copytree(os.path.join(slapd['sysconf_dir'], 'dirsrv/schema'), slapd['schema_dir'])
        os.chown(slapd['schema_dir'], slapd['user_uid'], slapd['group_gid'])

        # Copy in the collation
        srcfile = os.path.join(slapd['sysconf_dir'], 'dirsrv/config/slapd-collations.conf')
        dstfile = os.path.join(slapd['config_dir'], 'slapd-collations.conf')
        shutil.copy2(srcfile, dstfile)
        os.chown(slapd['schema_dir'], slapd['user_uid'], slapd['group_gid'])

        # If we are on the correct platform settings, systemd
        if general['systemd'] and not self.containerised:
            # Should create the symlink we need, but without starting it.
            subprocess.check_call(["/usr/bin/systemctl",
                                    "enable",
                                    "dirsrv@%s" % slapd['instance_name']])
        # Else we need to detect other init scripts?

        # Bind sockets to our type?

        # Create certdb in sysconfidir
        if self.verbose:
            self.log.info("ACTION: Creating certificate database is %s" % slapd['cert_dir'])

        # Create dse.ldif with a temporary root password.
        # The template is in slapd['data_dir']/dirsrv/data/template-dse.ldif
        # Variables are done with %KEY%.
        # You could cheat and read it in, do a replace of % to { and } then use format?
        if self.verbose:
            self.log.info("ACTION: Creating dse.ldif")
        dse = ""
        with open(os.path.join(slapd['data_dir'], 'dirsrv', 'data', 'template-dse.ldif')) as template_dse:
            for line in template_dse.readlines():
                dse += line.replace('%', '{', 1).replace('%', '}', 1)

        with open(os.path.join(slapd['config_dir'], 'dse.ldif'), 'w') as file_dse:
            file_dse.write(dse.format(
                schema_dir=slapd['schema_dir'],
                lock_dir=slapd['lock_dir'],
                tmp_dir=slapd['tmp_dir'],
                cert_dir=slapd['cert_dir'],
                ldif_dir=slapd['ldif_dir'],
                bak_dir=slapd['backup_dir'],
                run_dir=slapd['run_dir'],
                inst_dir="",
                log_dir=slapd['log_dir'],
                fqdn=general['full_machine_name'],
                ds_port=slapd['port'],
                ds_user=slapd['user'],
                rootdn=slapd['root_dn'],
                # ds_passwd=slapd['root_password'],
                ds_passwd=self._secure_password,  # We set our own password here, so we can connect and mod.
                ds_suffix='',
                config_dir=slapd['config_dir'],
                db_dir=slapd['db_dir'],
            ))

        # open the connection to the instance.

        # Should I move this import? I think this prevents some recursion
        from lib389 import DirSrv
        ds_instance = DirSrv(self.verbose)
        ds_instance.containerised = self.containerised
        args = {
            SER_PORT: slapd['port'],
            SER_SERVERID_PROP: slapd['instance_name'],
            SER_ROOT_DN: slapd['root_dn'],
            SER_ROOT_PW: self._raw_secure_password,
            SER_DEPLOYED_DIR: slapd['prefix']
        }

        ds_instance.allocate(args)
        # Does this work?
        assert(ds_instance.exists())
        # Create the nssdb
        assert(ds_instance.nss_ssl.reinit())
        # Do we want to selfsign a CA and cert?

        ## LAST CHANCE, FIX PERMISSIONS.
        # Selinux fixups?
        # Restorecon of paths?

        # Start the server
        ds_instance.start(timeout=60)
        ds_instance.open()

        # In some cases we may want to change log settings
        # ds_instance.config.enable_log('audit')

        # Create the configs related to this version.
        base_config = get_config(general['defaults'])
        base_config_inst = base_config(ds_instance)
        base_config_inst.apply_config(install=True)

        # Create the backends as listed
        # Load example data if needed.
        for backend in backends:
            ds_instance.backends.create(properties=backend)

        # Make changes using the temp root
        # Change the root password finally

        # Initialise ldapi socket information. IPA expects this ....
        ds_instance.config.set('nsslapd-ldapifilepath', ds_instance.get_ldapi_path())
        ds_instance.config.set('nsslapd-ldapilisten', 'on')

        # Complete.
        ds_instance.config.set('nsslapd-rootpw',
                               ensure_str(slapd['root_password']))

        # In a container build we need to stop DirSrv at the end
        if self.containerised:
            ds_instance.stop()

