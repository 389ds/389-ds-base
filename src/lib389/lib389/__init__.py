# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""The lib389 module.
    The lib389 functionalities are split in various classes
        defined in brookers.py

    TODO: reorganize method parameters according to SimpleLDAPObject
        naming: filterstr, attrlist
"""

import sys
import os
from urllib.parse import urlparse
import stat
import pwd
import grp
import os.path
import socket
import ldif
import re
import ldap
import ldapurl
import time
import shutil
from datetime import datetime
import logging
import glob
import tarfile
import subprocess
from collections.abc import Callable
import signal
import errno
import uuid
import json
from shutil import copy2

# Deprecation
import warnings
import inspect

from ldap.ldapobject import SimpleLDAPObject
# file in this package

from lib389._constants import *
from lib389.properties import *
from lib389._entry import Entry
from lib389._ldifconn import LDIFConn
from lib389.tools import DirSrvTools
from lib389.utils import (
    ds_is_older,
    isLocalHost,
    normalizeDN,
    escapeDNValue,
    ensure_bytes,
    ensure_str,
    ensure_list_str,
    format_cmd_list,
    selinux_present,
    selinux_label_port)
from lib389.paths import Paths
from lib389.nss_ssl import NssSsl
from lib389.tasks import BackupTask, RestoreTask
from lib389.dseldif import DSEldif

# mixin
# from lib389.tools import DirSrvTools

from lib389.exceptions import *

MAJOR, MINOR, _, _, _ = sys.version_info

if MAJOR >= 3 or (MAJOR == 2 and MINOR >= 7):
    from ldap.controls.simple import GetEffectiveRightsControl
    from lib389._controls import DereferenceControl

RE_DBMONATTR = re.compile(r'^([a-zA-Z]+)-([1-9][0-9]*)$')
RE_DBMONATTRSUN = re.compile(r'^([a-zA-Z]+)-([a-zA-Z]+)$')

# This controls pyldap debug levels
TRACE_LEVEL = 0

DEBUGGING = os.getenv('DEBUGGING', default=False)

# My logger
logger = logging.getLogger(__name__)


# Initiate the paths object here. Should this be part of the DirSrv class
# for submodules?
def wrapper(f, name):
    """
    Wrapper of all superclass methods using lib389.Entry.
        @param f - DirSrv method inherited from SimpleLDAPObject
        @param name - method to call

    This seems to need to be an unbound method, that's why it's outside of
    DirSrv.  Perhaps there is some way to do this with the new classmethod
    or staticmethod of 2.4.

    We replace every call to a method in SimpleLDAPObject (the superclass
    of DirSrv) with a call to inner.  The f argument to wrapper is the bound
    method of DirSrv (which is inherited from the superclass).  Bound means
    that it will implicitly be called with the self argument, it is not in
    the args list.  name is the name of the method to call.  If name is a
    method that returns entry objects (e.g. result), we wrap the data returned
    by an Entry class.  If name is a method that takes an entry argument, we
    extract the raw data from the entry object to pass in.
    """
    def inner(*args, **kwargs):
        if name in [
            'add_s',
            'bind_s',
            'delete_s',
            'modify_s',
            'modrdn_s',
            'rename_s',
            'sasl_interactive_bind_s',
            'search_s',
            'search_ext_s',
            'simple_bind_s',
            'unbind_s',
            'getEntry',
        ] and not ('escapehatch' in kwargs and kwargs['escapehatch'] == 'i am sure'):
            c_stack = inspect.stack()
            frame = c_stack[1]

            warnings.warn(DeprecationWarning("Use of raw ldap function %s. This will be removed in a future release. "
                                             "Found in: %s:%s" % (name, frame.filename, frame.lineno)))
            # Later, we will add a sleep here to make it even more painful.
            # Finally, it will raise an exception.
        elif 'escapehatch' in kwargs:
            kwargs.pop('escapehatch')

        if name == 'result':
            objtype, data = f(*args, **kwargs)
            # data is either a 2-tuple or a list of 2-tuples
            # print data
            if data:
                if isinstance(data, tuple):
                    return objtype, Entry(data)
                elif isinstance(data, list):
                    # AD sends back these search references
                    # if objtype == ldap.RES_SEARCH_RESULT and \
                    #    isinstance(data[-1],tuple) and \
                    #    not data[-1][0]:
                    #     print "Received search reference: "
                    #     pprint.pprint(data[-1][1])
                    #     data.pop() # remove the last non-entry element

                    return objtype, [Entry(x) for x in data]
                else:
                    raise TypeError("unknown data type %s returned by result" %
                                    type(data))
            else:
                return objtype, data
        elif name.startswith('add'):
            # the first arg is self
            # the second and third arg are the dn and the data to send
            # We need to convert the Entry into the format used by
            # python-ldap
            ent = args[0]
            if isinstance(ent, Entry):
                return f(ent.dn, ent.toTupleList(), *args[2:])
            else:
                return f(*args, **kwargs)
        else:
            return f(*args, **kwargs)
    return inner


def pid_exists(pid):
    if not pid:
        return False
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except OSError as err:
        if err.errno == errno.ESRCH:
            return False
        elif err.errno == errno.EPERM:
            return True
        else:
            raise
    # Tell the OS to reap this please ...
    try:
        os.waitpid(pid, os.WNOHANG)
    except ChildProcessError:
        pass
    return True

def pid_from_file(pidfile):
    pid = None
    try:
        with open(pidfile, 'rb') as f:
            for line in f.readlines():
                try:
                    pid = int(line.strip())
                    break
                except ValueError:
                    continue
    except IOError:
        pass
    return pid

def _ds_shutil_copytree(src, dst, symlinks=False, ignore=None, copy_function=copy2,
             ignore_dangling_symlinks=False):
    """Recursively copy a directory tree.
    This is taken from /usr/lib64/python3.5/shutil.py, but removes the
    copystat function at the end. Why? Because in a container without
    privileges, we don't have access to set xattr. But copystat attempts to
    set the xattr when we are root, which causes the copy to fail. Remove it!
    """
    names = os.listdir(src)
    if ignore is not None:
        ignored_names = ignore(src, names)
    else:
        ignored_names = set()

    os.makedirs(dst)
    errors = []
    for name in names:
        if name in ignored_names:
            continue
        srcname = os.path.join(src, name)
        dstname = os.path.join(dst, name)
        try:
            if os.path.islink(srcname):
                linkto = os.readlink(srcname)
                if symlinks:
                    # We can't just leave it to `copy_function` because legacy
                    # code with a custom `copy_function` may rely on copytree
                    # doing the right thing.
                    os.symlink(linkto, dstname)
                    shutil.copystat(
                        srcname, dstname, follow_symlinks=not symlinks
                    )
                else:
                    # ignore dangling symlink if the flag is on
                    if not os.path.exists(linkto) and ignore_dangling_symlinks:
                        continue
                    # otherwise let the copy occurs. copy2 will raise an error
                    if os.path.isdir(srcname):
                        _ds_shutil_copytree(srcname, dstname, symlinks, ignore,
                                 copy_function)
                    else:
                        copy_function(srcname, dstname)
            elif os.path.isdir(srcname):
                _ds_shutil_copytree(srcname, dstname, symlinks, ignore, copy_function)
            else:
                # Will raise a SpecialFileError for unsupported file types
                copy_function(srcname, dstname)
        # catch the Error from the recursive copytree so that we can
        # continue with other files
        except Error as err:
            errors.extend(err.args[0])
        except OSError as why:
            errors.append((srcname, dstname, str(why)))
    return dst


class DirSrv(SimpleLDAPObject, object):

    def __initPart2(self):
        """Initialize the DirSrv structure filling various fields, like:
                self.errlog          -> nsslapd-errorlog
                self.accesslog       -> nsslapd-accesslog
                self.auditlog        -> nsslapd-auditlog
                self.confdir         -> nsslapd-certdir
                self.inst            -> equivalent to self.serverid
                self.sroot/self.inst -> nsslapd-instancedir
                self.dbdir           -> dirname(nsslapd-directory)
                self.bakdir          -> nsslapd-bakdir
                self.ldifdir         -> nsslapd-ldifdir

            @param - self

            @return - None

            @raise ldap.LDAPError - if failure during initialization

        """
        self.errlog = self.ds_paths.error_log
        self.accesslog = self.ds_paths.access_log
        self.auditlog = self.ds_paths.audit_log
        self.confdir = self.ds_paths.config_dir
        self.schemadir = self.ds_paths.schema_dir
        self.bakdir = self.ds_paths.backup_dir
        self.ldifdir = self.ds_paths.ldif_dir
        self.instdir = self.ds_paths.inst_dir
        self.dbdir = self.ds_paths.db_dir
        self.changelogdir = os.path.join(os.path.dirname(self.dbdir), DEFAULT_CHANGELOG_DB)

    def rebind(self):
        """Reconnect to the DS

            @raise ldap.CONFIDENTIALITY_REQUIRED - missing TLS:
        """
        uri = self.toLDAPURL()
        if hasattr(ldap, 'PYLDAP_VERSION') and MAJOR >= 3:
            super(DirSrv, self).__init__(uri, bytes_mode=False, trace_level=TRACE_LEVEL)
        else:
            super(DirSrv, self).__init__(uri, trace_level=TRACE_LEVEL)
        # self.start_tls_s()
        self.simple_bind_s(ensure_str(self.binddn), self.bindpw, escapehatch='i am sure')

    def __add_brookers__(self):
        from lib389.config import Config
        from lib389.aci import Aci
        from lib389.config import RSA
        from lib389.config import Encryption
        from lib389.dirsrv_log import DirsrvAccessLog, DirsrvErrorLog, DirsrvAuditLog
        from lib389.ldclt import Ldclt
        from lib389.mappingTree import MappingTrees
        from lib389.mappingTree import MappingTreeLegacy as MappingTree
        from lib389.backend import Backends
        from lib389.backend import BackendLegacy as Backend
        from lib389.suffix import Suffix
        from lib389.replica import ReplicaLegacy as Replica
        from lib389.replica import Replicas
        from lib389.agreement import AgreementLegacy as Agreement
        from lib389.schema import SchemaLegacy as Schema
        from lib389.plugins import Plugins
        from lib389.tasks import Tasks
        from lib389.index import IndexLegacy as Index
        from lib389.monitor import Monitor, MonitorLDBM
        from lib389.rootdse import RootDSE
        from lib389.saslmap import SaslMapping, SaslMappings
        from lib389.pwpolicy import PwPolicyManager

        # Need updating
        self.agreement = Agreement(self)
        self.replica = Replica(self)
        self.backend = Backend(self)
        self.config = Config(self)
        self.index = Index(self)
        self.mappingtree = MappingTree(self)
        self.suffix = Suffix(self)
        self.schema = Schema(self)
        self.plugins = Plugins(self)
        self.tasks = Tasks(self)
        self.saslmap = SaslMapping(self)
        self.pwpolicy = PwPolicyManager(self)
        # Do we have a certdb path?
        # if MAJOR < 3:
        self.monitor = Monitor(self)
        self.monitorldbm = MonitorLDBM(self)
        self.rootdse = RootDSE(self)
        self.backends = Backends(self)
        self.mappingtrees = MappingTrees(self)
        self.replicas = Replicas(self)
        self.aci = Aci(self)
        self.rsa = RSA(self)
        self.encryption = Encryption(self)
        self.ds_access_log = DirsrvAccessLog(self)
        self.ds_error_log = DirsrvErrorLog(self)
        self.ds_audit_log = DirsrvAuditLog(self)
        self.ldclt = Ldclt(self)
        self.saslmaps = SaslMappings(self)

    def __init__(self, verbose=False, external_log=None, containerised=False):
        """
            This method does various initialization of DirSrv object:
            parameters:
                - 'state' to DIRSRV_STATE_INIT
                - 'verbose' flag for debug purpose
                - 'log' so that the use the module defined logger

            wrap the methods.

                - from SimpleLDAPObject
                - from agreement, backends, suffix...

            It just create a DirSrv object. To use it the user will likely do
            the following additional steps:
                - allocate
                - create
                - open
        """

        self.state = DIRSRV_STATE_INIT
        self.uuid = str(uuid.uuid4())
        self.verbose = verbose
        self.serverid = None
        # Are we a container? We get containerised in setup.py during SetupDs, and
        # in other cases we use the marker to tell us.
        self._containerised = os.path.exists(DSRC_CONTAINER) or containerised

        # If we have an external logger, use it!
        self.log = logger
        if external_log is None:
            if self.verbose:
                self.log.setLevel(logging.DEBUG)
            else:
                self.log.setLevel(logging.INFO)
        else:
            self.log = external_log

        self.confdir = None

        # We can't assume the paths state yet ...
        self.ds_paths = Paths(instance=self, local=False)
        # Set the default systemd status. This MAY be overidden in the setup utils
        # as required, generally for containers.
        self.systemd_override = None

        # Reset the args (py.test reuses the args_instance for each test case)
        # We allocate a "default" prefix here which allows an un-allocate or
        # un-instantiated DirSrv
        # instance to be able to do an an instance discovery. For example:
        #  ds = lib389.DirSrv()
        #  ds.list(all=True)
        # self.ds_paths.prefix = args_instance[SER_DEPLOYED_DIR]

        self.__wrapmethods()

    def __str__(self):
        """XXX and in SSL case?"""
        return self.host + ":" + str(self.port)

    def local_simple_allocate(self, serverid, ldapuri=None, binddn='cn=Directory Manager', password=None):
        """Allocate an instance and perform a simple bind. This is a local instance, so
        you can perform tasks like db2ldif etc. Note that you can use password=None and
        skip .open(), and still perform local tasks.

        :param serverid: The instance name to manipulate
        :type serverid: str
        :param ldapuri: The instance uri to connect to.
        :type ldapuri: str
        :param binddn: The dn to bind as.
        :type binddn: str
        :param password: The password for the dn to bind as.
        :type password: str
        """
        if self.state != DIRSRV_STATE_INIT and self.state != DIRSRV_STATE_ALLOCATED:
            raise ValueError("invalid state for calling allocate: %s" % self.state)

        # The lack of this value basically rules it out in most cases
        self.isLocal = True
        self.ds_paths = Paths(serverid, instance=self, local=self.isLocal)
        self.serverid = serverid
        self.userid = self.ds_paths.user

        # Do we have ldapi settings?
        self.ldapi_enabled = None
        self.ldapi_socket = None

        self.ldapuri = ldapuri

        # We must also alloc host and ports for some manipulation tasks
        self.host = socket.gethostname()

        dse_ldif = DSEldif(self)
        port = dse_ldif.get(DN_CONFIG, "nsslapd-port", single=True)
        sslport = dse_ldif.get(DN_CONFIG, "nsslapd-secureport", single=True)

        self.port = int(port) if port is not None else None
        self.sslport = int(sslport) if sslport is not None else None

        self.binddn = binddn
        self.bindpw = password
        self.state = DIRSRV_STATE_ALLOCATED
        self.log.debug("Allocate local instance %s with %s", self.__class__, self.ldapuri)

    def setup_ldapi(self):
        self.ldapi_enabled = "on"
        self.ldapi_socket = self.ds_paths.ldapi
        self.ldapi_autobind = "on"

    def remote_simple_allocate(self, ldapuri, binddn='cn=Directory Manager', password=None):
        """Allocate an instance, and perform a simple bind. This instance is remote, so
        local tasks will not operate.

        :param ldapuri: The instance uri to connect to.
        :type ldapuri: str
        :param binddn: The dn to bind as.
        :type binddn: str
        :param password: The password for the dn to bind as.
        :type password: str
        """
        if self.state != DIRSRV_STATE_INIT and self.state != DIRSRV_STATE_ALLOCATED:
            raise ValueError("invalid state for calling allocate: %s" % self.state)

        self.log.debug('SER_SERVERID_PROP not provided, assuming non-local instance')
        # The lack of this value basically rules it out in most cases
        self.isLocal = False
        self.ds_paths = Paths(instance=self, local=self.isLocal)

        # Do we have ldapi settings?
        # Do we really need .strip() on this?
        self.ldapi_enabled = None
        self.ldapi_socket = None

        self.ldapuri = ldapuri

        self.binddn = binddn
        self.bindpw = password
        self.state = DIRSRV_STATE_ALLOCATED
        self.log.debug("Allocate %s with %s", self.__class__, self.ldapuri)

    # Should there be an extra boolean to this function to determine to use
    #  ldapi or not? Or does the settings presence indicate intent?
    def allocate(self, args):
        '''
           Initialize a DirSrv object according to the provided args.
           The final state  -> DIRSRV_STATE_ALLOCATED
           @param args - dictionary that contains the DirSrv properties
               properties are
                   SER_SERVERID_PROP: used for offline op
                       (create/delete/backup/start/stop..) -> slapd-<serverid>
                   SER_HOST: hostname [LOCALHOST]
                   SER_PORT: normal ldap port [DEFAULT_PORT]
                   SER_SECURE_PORT: secure ldap port
                   SER_ROOT_DN: root DN [DN_DM]
                   SER_ROOT_PW: password of root DN [PW_DM]
                   SER_USER_ID: user id of the create instance [DEFAULT_USER]
                   SER_GROUP_ID: group id of the create instance [SER_USER_ID]
                   SER_DEPLOYED_DIR: directory where 389-ds is deployed
                   SER_BACKUP_INST_DIR: directory where instances will be
                                        backed up

           @return None

           @raise ValueError - if missing mandatory properties or invalid
                               state of DirSrv
        '''
        if self.state != DIRSRV_STATE_INIT and \
           self.state != DIRSRV_STATE_ALLOCATED:
            raise ValueError("invalid state for calling allocate: %s" %
                             self.state)

        # Do we have ldapi settings?
        # Do we really need .strip() on this?
        self.ldapi_enabled = args.get(SER_LDAPI_ENABLED, 'off')
        self.ldapi_socket = args.get(SER_LDAPI_SOCKET, None)
        self.ldapuri = args.get(SER_LDAP_URL, None)
        self.log.debug("Allocate %s with %s", self.__class__, self.ldapuri)
        # Still needed in setup, even if ldapuri over writes.
        if self.ldapuri is not None:
            ldapuri_parsed = urlparse(self.ldapuri)
            self.host = ldapuri_parsed.hostname
            try:
                self.port = ldapuri_parsed.port
            except ValueError:
                self.port = DEFAULT_PORT
        else:
            self.host = args.get(SER_HOST, socket.gethostname())
            self.port = args.get(SER_PORT, DEFAULT_PORT)
        self.sslport = args.get(SER_SECURE_PORT)

        self.inst_scripts = args.get(SER_INST_SCRIPTS_ENABLED, None)

        self.isLocal = False
        # Or do we have tcp / ip settings?
        if self.ldapi_enabled == 'on' and self.ldapi_socket is not None:
            self.ldapi_autobind = args.get(SER_LDAPI_AUTOBIND, 'off')
            self.isLocal = True
            self.log.debug("Allocate %s with %s", self.__class__, self.ldapi_socket)
        # Settings from args of server attributes
        self.strict_hostname = args.get(SER_STRICT_HOSTNAME_CHECKING, False)
        if self.strict_hostname is True:
            if self.host == LOCALHOST:
                DirSrvTools.testLocalhost()
            else:
                # Make sure our name is in hosts
                DirSrvTools.searchHostsFile(self.host, None)
        # Check if we are local only if we haven't found that yet
        if not self.isLocal:
            self.isLocal = isLocalHost(self.host)

        self.log.debug("Allocate %s with %s:%s", self.__class__, self.host, (self.sslport or self.port))

        if SER_SERVERID_PROP in args:
            self.ds_paths = Paths(serverid=args[SER_SERVERID_PROP], instance=self, local=self.isLocal)
            self.serverid = args.get(SER_SERVERID_PROP, None)
        else:
            self.ds_paths = Paths(instance=self, local=self.isLocal)

        self.binddn = args.get(SER_ROOT_DN, DN_DM)
        self.bindpw = args.get(SER_ROOT_PW, PW_DM)
        self.creation_suffix = args.get(SER_CREATION_SUFFIX, None)
        # These settings are only needed on a local connection.
        if self.isLocal:
            self.userid = args.get(SER_USER_ID)
            if not self.userid:
                if os.getuid() == 0:
                    # as root run as default user
                    self.userid = DEFAULT_USER
                else:
                    self.userid = pwd.getpwuid(os.getuid())[0]

            self.groupid = args.get(SER_GROUP_ID, self.userid)
            self.backupdir = args.get(SER_BACKUP_INST_DIR, DEFAULT_BACKUPDIR)

        # This will be externally populated in topologies.
        self.realm = None

        # additional settings
        self.suffixes = {}
        self.agmt = {}

        self.state = DIRSRV_STATE_ALLOCATED
        self.log.debug("Allocate %s with %s:%s",
                       self.__class__,
                       self.host,
                       (self.sslport or self.port))

    def clone(self, args_instance={}):
        """
        Open a new connection to our LDAP server
        *IMPORTANT*
        This is different to re-opening on the same dirsrv, as quirks in pyldap
        mean that ldap.set_option doesn't take effect! You need to use this
        to allow all of the start TLS options to work!
        """
        server = DirSrv(verbose=self.verbose)
        args_instance[SER_LDAP_URL] = self.ldapuri
        args_instance[SER_HOST] = self.host
        args_instance[SER_PORT] = self.port
        args_instance[SER_LDAP_URL] = self.ldapuri
        args_instance[SER_SECURE_PORT] = self.sslport
        args_instance[SER_SERVERID_PROP] = self.serverid
        args_standalone = args_instance.copy()
        server.allocate(args_standalone)

        return server

    def list(self, all=False, serverid=None):
        """
            Returns a list dictionary. For a created instance that is on the
            local file system (e.g. <prefix>/etc/dirsrv/slapd-*/dse.ldif).
            A dictionary is created with the following properties:
                CONF_SERVER_DIR
                CONF_SERVERBIN_DIR
                CONF_CONFIG_DIR
                CONF_INST_DIR
                CONF_RUN_DIR
                CONF_DS_ROOT
                CONF_PRODUCT_NAME
            If all=True it builds a list of dictionaries for all created
            instances.  Else (default), the list will only contain the
            dictionary of the calling instance

            @param all - True or False . default is [False]

            @param instance - The name of the instance to retrieve or None for
                              the current instance

            @return - list of dictionaries, each of them containing instance
                      properities

            @raise IOError - if the file containing the properties is not
                             foundable or readable
        """

        ### This inner function WILL BE REMOVED soon.
        def _parse_configfile(filename=None, serverid=None):
            '''
                This method read 'filename' and build a dictionary with
                CONF_* properties
            '''

            if not filename:
                raise IOError('filename is mandatory')
            if not os.path.isfile(filename) or \
               not os.access(filename, os.R_OK):
                raise IOError('invalid file name or rights: %s' % filename)

            prop = {}
            prop[CONF_SERVER_ID] = serverid
            prop[SER_SERVERID_PROP] = serverid

            inst_paths = Paths(serverid)

            # WARNING: This is not correct, but is a stop gap until: https://github.com/389ds/389-ds-base/issues/3266
            # Once that's done, this will "just work". Saying this, the whole prop dictionary
            # concept is fundamentally broken, and we should be using ds_paths anyway.
            prop[CONF_SERVER_DIR] = inst_paths.lib_dir
            prop[CONF_SERVERBIN_DIR] = inst_paths.sbin_dir
            prop[CONF_CONFIG_DIR] = inst_paths.config_dir
            prop[CONF_INST_DIR] = inst_paths.inst_dir
            prop[CONF_RUN_DIR] = inst_paths.run_dir
            prop[CONF_DS_ROOT] = ''
            prop[CONF_PRODUCT_NAME] = 'slapd'

            ldifconn = LDIFConn(filename)
            configentry = ldifconn.get(DN_CONFIG)
            for key in args_dse_keys:
                prop[key] = configentry.getValue(args_dse_keys[key])
                # SER_HOST            (host) nsslapd-localhost
                # SER_PORT            (port) nsslapd-port
                # SER_SECURE_PORT     (sslport) nsslapd-secureport
                # SER_ROOT_DN         (binddn) nsslapd-rootdn
                # SER_ROOT_PW         (bindpw) We can't do this
                # SER_CREATION_SUFFIX (creation_suffix)
                #                        nsslapd-defaultnamingcontext
                # SER_USER_ID         (userid) nsslapd-localuser
                # SER_SERVERID_PROP   (serverid) Already have this
                # SER_GROUP_ID        (groupid)
                # SER_DEPLOYED_DIR    (prefix) Already provided to for
                #                              discovery
                # SER_BACKUP_INST_DIR (backupdir) nsslapd-bakdir
                # We need to convert these two to int
                #  because other routines get confused if we don't
                for intkey in [SER_PORT, SER_SECURE_PORT]:
                    if intkey in prop and prop[intkey] is not None:
                        prop[intkey] = int(prop[intkey])
            return prop
            ### end _parse_configfile

        # Retrieves all instances under '<prefix>/etc/dirsrv'

        # Don't need a default value now since it's set in init.
        if serverid is None and hasattr(self, 'serverid'):
            serverid = self.serverid
        elif serverid is not None and serverid.startswith('slapd-'):
            serverid = serverid.replace('slapd-', '', 1)

        if self.serverid is None:
            # Need to set the Paths in case it does exist
            self.ds_paths = Paths(serverid)

        # list of the found instances
        instances = []

        # now prepare the list of instances properties
        if not all:
            # Don't use self.ds_paths here, because it has no server id : this
            # causes the config_dir to have a formatting issue.
            #
            # As dse.ldif is one of the only fixed locations in the server, this is
            # okay to use this without parsing of dse.ldif to add the other paths
            # required: yet.
            inst_paths = Paths(serverid)
            dse_ldif = os.path.join(inst_paths.config_dir, 'dse.ldif')
            # easy case we just look for the current instance
            if os.path.exists(dse_ldif):
                # It's real
                # Now just populate that instance dict (soon to be changed ...)
                instances.append(_parse_configfile(dse_ldif, serverid))
            else:
                # it's not=
                self.log.debug("list instance not found in {}: {}\n".format(dse_ldif, serverid))
        else:
            # For each dir that starts with slapd-*
            inst_path = self.ds_paths.sysconf_dir + "/dirsrv"
            potential_inst = [
                os.path.join(inst_path, f)
                for f in os.listdir(inst_path)
                if f.startswith('slapd-')
            ]

            # check it has dse.ldif
            for pi in potential_inst:
                pi_dse_ldif = os.path.join(pi, 'dse.ldif')
                # Takes /etc/dirsrv/slapd-instance -> slapd-instance -> instance
                pi_name = pi.split('/')[-1].split('slapd-')[-1]
                # parse + append
                if os.path.exists(pi_dse_ldif):
                    instances.append(_parse_configfile(pi_dse_ldif, pi_name))

        return instances

    def _createDirsrv(self, version):
        """
        Create a new dirsrv instance based on the new python installer, rather
        than setup-ds.pl

        version represents the config default and sample entry version to use.
        """
        from lib389.instance.setup import SetupDs
        from lib389.instance.options import General2Base, Slapd2Base
        # Import the new setup ds library.
        sds = SetupDs(verbose=self.verbose, dryrun=False, log=self.log)
        # Configure the options.
        general_options = General2Base(self.log)
        general_options.set('strict_host_checking', False)
        general_options.verify()
        general = general_options.collect()

        slapd_options = Slapd2Base(self.log)
        slapd_options.set('instance_name', self.serverid)
        slapd_options.set('port', self.port)
        slapd_options.set('secure_port', self.sslport)
        slapd_options.set('root_password', self.bindpw)
        slapd_options.set('root_dn', self.binddn)
        # We disable TLS during setup, we use a function in tests to enable instead.
        slapd_options.set('self_sign_cert', False)
        slapd_options.set('defaults', version)

        slapd_options.verify()
        slapd = slapd_options.collect()

        # In order to work by "default" for tests, we need to create a backend.
        backends = []
        if self.creation_suffix is not None:
            userroot = {
                'cn': 'userRoot',
                'nsslapd-suffix': self.creation_suffix,
                BACKEND_SAMPLE_ENTRIES: version,
            }
            backends = [userroot,]

        # Go!
        self.log.debug("DEBUG: creating with parameters:")
        self.log.debug(general)
        self.log.debug(slapd)
        self.log.debug(backends)
        sds.create_from_args(general, slapd, backends, None)

    def create(self, version=INSTALL_LATEST_CONFIG):
        """
            Creates an instance with the parameters sets in dirsrv
            The state change from  DIRSRV_STATE_ALLOCATED ->
                                   DIRSRV_STATE_OFFLINE

            @param - self

            @return - None

            @raise ValueError - if 'serverid' is missing or if it exist an
                                instance with the same 'serverid'
        """
        # check that DirSrv was in DIRSRV_STATE_ALLOCATED state
        self.log.debug("Server is in state %s", self.state)
        if self.state != DIRSRV_STATE_ALLOCATED:
            raise ValueError("invalid state for calling create: %s" %
                             self.state)

        if self.exists():
            raise ValueError("Error it already exists the instance (%s)" %
                             self.list()[0][CONF_INST_DIR])

        if not self.serverid:
            raise ValueError("SER_SERVERID_PROP is missing, " +
                             "it is required to create an instance")

        # Time to create the instance and retrieve the effective sroot
        self._createDirsrv(version)

        # Because of how this works, we force ldap:// only for now.
        # A real install will have ldaps, and won't go via this path.
        self.use_ldap_uri()

        # Retrieve sroot from the sys/priv config file
        assert(self.exists())
        self.sroot = self.list()[0][CONF_SERVER_DIR]

        # Now the instance is created but DirSrv is not yet connected to it
        self.state = DIRSRV_STATE_OFFLINE

    def get_db_lib(self):
        with suppress(AttributeError):
            return self._db_lib
        with suppress(Exception):
            from backend import DatabaseConfig
            self._db_lib = DatabaseConfig(self).get_db_lib()
            return self._db_lib
        with suppress(Exception):
            dse_ldif = DSEldif(None, self)
            self._db_lib = dse_ldif.get(DN_CONFIG_LDBM, "nsslapd-backend-implement", single=True)
            return self._db_lib
        return get_default_db_lib()

    def delete(self):
        # Time to create the instance and retrieve the effective sroot
        from lib389.instance.remove import remove_ds_instance
        remove_ds_instance(self)

        # Now, we are still an allocated ds object so we can be re-installed
        self.state = DIRSRV_STATE_ALLOCATED

    def open(self, uri=None, saslmethod=None, sasltoken=None, certdir=None, starttls=False, connOnly=False, reqcert=None,
                usercert=None, userkey=None):
        '''
            It opens a ldap bound connection to dirsrv so that online
            administrative tasks are possible.  It binds with the binddn
            property, then it initializes various fields from DirSrv
            (via __initPart2)

            The state changes  -> DIRSRV_STATE_ONLINE

            @param self
            @param saslmethod - None, or GSSAPI
            @param sasltoken - The ldap.sasl token type to bind with.
            @param certdir - Certificate directory for TLS
            @return None

            @raise LDAPError
        '''

        # Force our state offline to prevent paths from trying to search
        # cn=config while we startup.
        self.state = DIRSRV_STATE_OFFLINE

        if not uri:
            uri = self.toLDAPURL()

        self.log.debug('open(): Connecting to uri %s', uri)
        if hasattr(ldap, 'PYLDAP_VERSION') and MAJOR >= 3:
            super(DirSrv, self).__init__(uri, bytes_mode=False, trace_level=TRACE_LEVEL)
        else:
            super(DirSrv, self).__init__(uri, trace_level=TRACE_LEVEL)

        # Set new TLS context only if we changed some of the options
        new_tls_context = False

        if certdir is None and self.isLocal:
            certdir = self.get_cert_dir()
            # If we are trying to manage local instance and admin doesn't have access
            # to the instance certdir we shouldn't use it.
            # If we don't set it the python-ldap will pick up the policy from /etc/openldap/ldap.conf
            if not os.access(ensure_str(certdir), os.R_OK):
                certdir = None
            else:
                self.log.debug("Using dirsrv ca certificate %s", certdir)

        if certdir is not None:
            # Note this sets LDAP.OPT not SELF. Because once self has opened
            # it can NOT change opts AT ALL.
            self.log.debug("Using external ca certificate %s", certdir)
            self.set_option(ldap.OPT_X_TLS_CACERTDIR, ensure_str(certdir))
            new_tls_context = True

        if userkey is not None:
            # Note this sets LDAP.OPT not SELF. Because once self has opened
            # it can NOT change opts AT ALL.
            self.log.debug("Using user private key %s", userkey)
            self.set_option(ldap.OPT_X_TLS_KEYFILE, ensure_str(userkey))
            new_tls_context = True

        if usercert is not None:
            # Note this sets LDAP.OPT not SELF. Because once self has opened
            # it can NOT change opts AT ALL.
            self.log.debug("Using user certificate %s", usercert)
            self.set_option(ldap.OPT_X_TLS_CERTFILE, ensure_str(usercert))
            new_tls_context = True

        if certdir or starttls or uri.startswith('ldaps://'):
            try:
                # Note this sets LDAP.OPT not SELF. Because once self has opened
                # it can NOT change opts on reused (ie restart)
                if reqcert is not None:
                    self.log.debug("Using lib389 certificate policy %s", reqcert)
                    self.set_option(ldap.OPT_X_TLS_REQUIRE_CERT, reqcert)
                    new_tls_context = True
                else:
                    self.log.debug("Using /etc/openldap/ldap.conf certificate policy")
                self.log.debug("ldap.OPT_X_TLS_REQUIRE_CERT = %s", self.get_option(ldap.OPT_X_TLS_REQUIRE_CERT))
            except ldap.LDAPError as e:
                self.log.fatal('TLS negotiation failed: %s', e)
                raise e

        # Tell python ldap to make a new TLS context with this information.
        if new_tls_context:
            self.set_option(ldap.OPT_X_TLS_NEWCTX, 0)

        if starttls and not uri.startswith('ldaps'):
            self.start_tls_s(escapehatch='i am sure')

        if saslmethod and sasltoken is not None:
            # Just pass the sasltoken in!
            self.sasl_interactive_bind_s("", sasltoken, escapehatch='i am sure')
        elif saslmethod and saslmethod.lower() == 'gssapi':
            """
            Perform kerberos/gssapi authentication
            """
            sasl_auth = ldap.sasl.gssapi("")
            self.sasl_interactive_bind_s("", sasl_auth, escapehatch='i am sure')

        elif saslmethod == 'EXTERNAL':
            # Do nothing.
            sasl_auth = ldap.sasl.external()
            self.sasl_interactive_bind_s("", sasl_auth, escapehatch='i am sure')
        elif saslmethod:
            # Unknown or unsupported method
            self.log.debug('Unsupported SASL method: %s', saslmethod)
            raise ldap.UNWILLING_TO_PERFORM

        elif self.can_autobind():
            # Connect via ldapi, and autobind.
            # do nothing: the bind is complete.
            self.log.debug("open(): Using root autobind ...")
            sasl_auth = ldap.sasl.external()
            self.sasl_interactive_bind_s("", sasl_auth, escapehatch='i am sure')

        else:
            """
            Do a simple bind
            """
            try:
                self.simple_bind_s(ensure_str(self.binddn), self.bindpw, escapehatch='i am sure')
            except ldap.SERVER_DOWN as e:
                # TODO add server info in exception
                self.log.debug("Cannot connect to %r", uri)
                raise e
            except ldap.LDAPError as e:
                self.log.debug("Error: Failed to authenticate as %s: %s" % (self.binddn, e))
                raise e

        """
        Authenticated, now finish the initialization
        """
        self.log.debug("open(): bound as %s", self.binddn)
        if not connOnly and self.isLocal:
            self.__initPart2()
        self.state = DIRSRV_STATE_ONLINE
        # Now that we're online, some of our methods may try to query the version online.
        self.__add_brookers__()

    def close(self):
        '''
            It closes connection to dirsrv. Online administrative tasks are no
            longer possible.

            The state changes from DIRSRV_STATE_ONLINE -> DIRSRV_STATE_OFFLINE

            @param self

            @return None
            @raise ValueError - if the instance has not the right state
        '''
        # check that DirSrv was in DIRSRV_STATE_ONLINE state
        if self.state == DIRSRV_STATE_ONLINE:
            # Don't raise an error. Just move the state and return
            self.unbind_s(escapehatch='i am sure')

        self.state = DIRSRV_STATE_OFFLINE

    def start(self, timeout=120, post_open=True):
        '''
            It starts an instance and rebind it. Its final state after rebind
            (open) is DIRSRV_STATE_ONLINE

            @param self
            @param timeout (in sec) to wait for successful start

            @return None

            @raise ValueError
        '''
        if not self.isLocal:
            self.log.error("This is a remote instance!")
            input('Press Enter when the instance has started ...')
            return

        if self.status() is True:
            return

        if self.with_systemd():
            self.log.debug("systemd status -> True")
            # Do systemd things here ...
            subprocess.check_output(["systemctl", "start", "dirsrv@%s" % self.serverid], stderr=subprocess.STDOUT)
        else:
            self.log.debug("systemd status -> False")
            # Start the process.
            # Wait for it to terminate
            # This means the server is probably ready to go ....
            env = {}
            if self.has_asan():
                self.log.warning("WARNING: Starting instance with ASAN options. This is probably not what you want. Please contact support.")
                env.update(os.environ)
                env['ASAN_SYMBOLIZER_PATH'] = "/usr/bin/llvm-symbolizer"
                env['ASAN_OPTIONS'] = "%s symbolize=1 detect_deadlocks=1 log_path=%s/ns-slapd-%s.asan" % (env.get('ASAN_OPTIONS', ''), self.ds_paths.run_dir, self.serverid)
                self.log.debug("ASAN_SYMBOLIZER_PATH = %s" % env['ASAN_SYMBOLIZER_PATH'])
                self.log.debug("ASAN_OPTIONS = %s" % env['ASAN_OPTIONS'])
            output = None
            try:
                cmd = ["%s/ns-slapd" % self.get_sbin_dir(),
                        "-D",
                        self.ds_paths.config_dir,
                        "-i",
                        self.pid_file()],
                self.log.debug("DEBUG: starting with %s" % cmd)
                output = subprocess.check_output(*cmd, env=env, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                self.log.error('Failed to start ns-slapd: "%s"' % e.output.decode())
                self.log.error(e)
                raise ValueError('Failed to start DS')
            count = timeout
            pid = pid_from_file(self.pid_file())
            while (pid is None) and count > 0:
                count -= 1
                time.sleep(1)
                pid = pid_from_file(self.pid_file())
            if pid == 0 or pid is None:
                self.log.error("Unable to find pid (%s) of ns-slapd process" % self.pid_file())
                raise ValueError('Failed to start DS')
            # Wait
            while not pid_exists(pid) and count > 0:
                # It looks like DS changes the value in here at some point ...
                # It's probably a DS bug, but if we "keep checking" the file, eventually
                # we get the main server pid, and it's ready to go.
                pid = pid_from_file(self.pid_file())
                time.sleep(1)
                count -= 1
            if not pid_exists(pid):
                self.log.error("pid (%s) of ns-slapd process does not exist" % pid)
                raise ValueError("Failed to start DS")
        if post_open:
            self.open()

    def stop(self, timeout=120):
        '''
            It stops an instance.
            It changes the state  -> DIRSRV_STATE_OFFLINE

            @param self
            @param timeout (in sec) to wait for successful stop

            @return None

            @raise ValueError
        '''
        if not self.isLocal:
            self.log.error("This is a remote instance!")
            input('Press Enter when the instance has stopped ...')
            return

        if self.status() is False:
            return

        if self.with_systemd():
            self.log.debug("systemd status -> True")
            # Do systemd things here ...
            subprocess.check_output(["systemctl", "stop", "dirsrv@%s" % self.serverid], stderr=subprocess.STDOUT)
        else:
            self.log.debug("systemd status -> False")
            # TODO: Make the pid path in the files things
            # TODO: use the status call instead!!!!
            count = timeout
            pid = pid_from_file(self.pid_file())
            if pid == 0 or pid is None:
                raise ValueError("Failed to stop DS")
            os.kill(pid, signal.SIGTERM)
            # Wait
            while pid_exists(pid) and count > 0:
                time.sleep(1)
                count -= 1
            if pid_exists(pid):
                os.kill(pid, signal.SIGKILL)
        self.state = DIRSRV_STATE_OFFLINE

    def status(self):
        """
        Determine if an instance is running or not.

        Will update the self.state parameter.
        """
        if self.with_systemd():
            self.log.debug("systemd status -> True")
            # Do systemd things here ...
            rc = subprocess.call(["systemctl",
                                  "is-active", "--quiet",
                                  "dirsrv@%s" % self.serverid])
            if rc == 0:
                return True
                # We don't reset the state here because we don't know what state
                # we are in re shutdown. The state is for us internally anyway.
                # self.state = DIRSRV_STATE_RUNNING
            self.state = DIRSRV_STATE_OFFLINE
            return False
        else:
            self.log.debug("systemd status -> False")
            pid = pid_from_file(self.pid_file())
            self.log.debug("pid file %s -> %s" % (self.pid_file(), pid))
            if pid is None:
                self.log.debug("No pidfile found for %s", self.serverid)
                # No pidfile yet ...
                self.state = DIRSRV_STATE_OFFLINE
                return False
            if pid == 0:
                self.log.debug("Pid of 0 not valid for %s", self.serverid)
                self.state = DIRSRV_STATE_OFFLINE
                raise ValueError
            # Wait
            if not pid_exists(pid):
                self.log.debug("Pid of %s is not running for %s", pid, self.serverid)
                self.state = DIRSRV_STATE_OFFLINE
                return False
            self.log.debug("Pid of %s for %s and running", pid, self.serverid)
            return True

    def restart(self, timeout=120, post_open=True):
        '''
            It restarts an instance and rebind it. Its final state after rebind
            (open) is DIRSRV_STATE_ONLINE.

            @param self
            @param timeout (in sec) to wait for successful stop

            @return None

            @raise ValueError
        '''
        self.stop(timeout)
        time.sleep(1)
        self.start(timeout, post_open)

    def _infoBackupFS(self):
        """
            Return the information to retrieve the backup file of a given
            instance
            It returns:
                - Directory name containing the backup
                  (e.g. /tmp/slapd-standalone.bck)
                - The pattern of the backup files
                  (e.g. /tmp/slapd-standalone.bck/backup*.tar.gz)
        """
        backup_dir = "%s/slapd-%s.bck" % (self.backupdir, self.serverid)
        backup_pattern = os.path.join(backup_dir, "backup*.tar.gz")
        return backup_dir, backup_pattern

    def clearBackupFS(self, backup_file=None):
        """
            Remove a backup_file or all backup up of a given instance

            @param backup_file - optional

            @return None

            @raise None
        """
        if backup_file:
            if os.path.isfile(backup_file):
                try:
                    os.remove(backup_file)
                except:
                    self.log.info("clearBackupFS: fail to remove %s", backup_file)
                    pass
        else:
            backup_dir, backup_pattern = self._infoBackupFS()
            list_backup_files = glob.glob(backup_pattern)
            for f in list_backup_files:
                try:
                    os.remove(f)
                except:
                    self.log.info("clearBackupFS: fail to remove %s", backup_file)
                    pass

    def checkBackupFS(self):
        """
            If it exits a backup file, it returns it
            else it returns None

            @param None

            @return file name of the first backup. None if there is no backup

            @raise None
        """

        backup_dir, backup_pattern = self._infoBackupFS()
        list_backup_files = glob.glob(backup_pattern)
        if not list_backup_files:
            return None
        else:
            # returns the first found backup
            return list_backup_files[0]

    def backupFS(self):
        """
            Saves the files of an instance under:
                /tmp/slapd-<instance_name>.bck/backup_HHMMSS.tar.gz
            and return the archive file name.
            If it already exists a such file, it assums it is a valid backup
            and returns its name

            self.sroot : root of the instance  (e.g. /usr/lib64/dirsrv)
            self.inst  : instance name
                         (e.g. standalone for /etc/dirsrv/slapd-standalone)
            self.confdir : root of the instance config (e.g. /etc/dirsrv)
            self.dbdir: directory where is stored the database
                        (e.g. /var/lib/dirsrv/slapd-standalone/db)
            self.changelogdir: directory where is stored the changelog
                               (e.g. /var/lib/dirsrv/slapd-supplier/changelogdb)

            @param None

            @return file name of the backup

            @raise none
        """

        # First check it if already exists a backup file
        backup_dir, backup_pattern = self._infoBackupFS()
        if not os.path.exists(backup_dir):
                os.makedirs(backup_dir)
        # make the backup directory accessible for anybody so that any user can
        # run the tests even if it existed a backup created by somebody else
        os.chmod(backup_dir, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO)

        # Forget this: Just make a new backup!
        # backup_file = self.checkBackupFS()
        # if backup_file:
        #     return backup_file

        # goes under the directory where the DS is deployed
        listFilesToBackup = []
        here = os.getcwd()
        if self.ds_paths.prefix:
            os.chdir("%s/" % self.ds_paths.prefix)
            prefix_pattern = "%s/" % self.ds_paths.prefix
        else:
            os.chdir("/")
            prefix_pattern = None

        # build the list of directories to scan
        # THIS MUST BE FIXED, No guarantee of sroot!
        instroot = "%s/slapd-%s" % (self.sroot, self.serverid)
        ldir = [instroot]
        if hasattr(self, 'confdir'):
            ldir.append(self.confdir)
        if hasattr(self, 'dbdir'):
            ldir.append(self.dbdir)
        if hasattr(self, 'changelogdir'):
            ldir.append(self.changelogdir)
        if hasattr(self, 'errlog'):
            ldir.append(os.path.dirname(self.errlog))
        if hasattr(self, 'accesslog') and \
           os.path.dirname(self.accesslog) not in ldir:
            ldir.append(os.path.dirname(self.accesslog))
        if hasattr(self, 'auditlog') and os.path.dirname(self.auditlog) \
           not in ldir:
            ldir.append(os.path.dirname(self.auditlog))

        # now scan the directory list to find the files to backup
        for dirToBackup in ldir:
            for root, dirs, files in os.walk(dirToBackup):

                for b_dir in dirs:
                    name = os.path.join(root, b_dir)
                    self.log.debug("backupFS b_dir = %s (%s) [name=%s]",
                                   b_dir, self.ds_paths.prefix, name)
                    if prefix_pattern:
                        name = re.sub(prefix_pattern, '', name)

                    if os.path.isdir(name):
                        listFilesToBackup.append(name)
                        self.log.debug("backupFS add = %s (%s)",
                                       name, self.ds_paths.prefix)

                for file in files:
                    name = os.path.join(root, file)
                    if prefix_pattern:
                        name = re.sub(prefix_pattern, '', name)

                    if os.path.isfile(name):
                        listFilesToBackup.append(name)
                        self.log.debug("backupFS add = %s (%s)",
                                       name, self.ds_paths.prefix)

        # create the archive
        name = "backup_%s_%s.tar.gz" % (self.serverid, time.strftime("%m%d%Y_%H%M%S"))
        backup_file = os.path.join(backup_dir, name)
        tar = tarfile.open(backup_file, "w:gz")

        for name in listFilesToBackup:
            tar.add(name)
        tar.close()
        self.log.info("backupFS: archive done : %s", backup_file)

        # return to the directory where we were
        os.chdir(here)

        return backup_file

    def restoreFS(self, backup_file):
        """
            Restore a directory from a backup file

            @param backup_file - file name of the backup

            @return None

            @raise ValueError - if backup_file invalid file name
        """

        # First check the archive exists
        if backup_file is None:
            self.log.warning("Unable to restore the instance (missing backup)")
            raise ValueError("Unable to restore the instance (missing backup)")
        if not os.path.isfile(backup_file):
            self.log.warning("Unable to restore the instance (%s is not a file)",
                        backup_file)
            raise ValueError("Unable to restore the instance " +
                             "(%s is not a file)" % backup_file)

        #
        # Second do some clean up
        #

        # previous db (it may exists new db files not in the backup)
        self.log.debug("restoreFS: remove subtree %s/*", os.path.dirname(self.dbdir))
        for root, dirs, files in os.walk(os.path.dirname(self.dbdir)):
            for d in dirs:
                if d not in ("bak", "ldif"):
                    self.log.debug("restoreFS: before restore remove directory"
                                   " %s/%s", root, d)
                    shutil.rmtree("%s/%s" % (root, d))

        # previous error/access logs
        self.log.debug("restoreFS: remove error logs %s", self.errlog)
        for f in glob.glob("%s*" % self.errlog):
            self.log.debug("restoreFS: before restore remove file %s", f)
            os.remove(f)
        self.log.debug("restoreFS: remove access logs %s", self.accesslog)
        for f in glob.glob("%s*" % self.accesslog):
            self.log.debug("restoreFS: before restore remove file %s", f)
            os.remove(f)
        self.log.debug("restoreFS: remove audit logs %s" % self.auditlog)
        for f in glob.glob("%s*" % self.auditlog):
            self.log.debug("restoreFS: before restore remove file %s", f)
            os.remove(f)

        # Then restore from the directory where DS was deployed
        here = os.getcwd()
        if self.ds_paths.prefix:
            prefix_pattern = "%s/" % self.ds_paths.prefix
            os.chdir(prefix_pattern)
        else:
            prefix_pattern = "/"
            os.chdir(prefix_pattern)

        tar = tarfile.open(backup_file)
        for member in tar.getmembers():
            if os.path.isfile(member.name):
                #
                # restore only writable files
                # It could be a bad idea and preferably restore all.
                # Now it will be easy to enhance that function.
                if os.access(member.name, os.W_OK):
                    self.log.debug("restoreFS: restored %s", member.name)
                    tar.extract(member.name)
                else:
                    self.log.debug("restoreFS: not restored %s (no write access)",
                                   member.name)
            else:
                self.log.debug("restoreFS: restored %s", member.name)
                tar.extract(member.name)

        tar.close()

        #
        # Now be safe, triggers a recovery at restart
        #
        guardian_file = os.path.join(self.dbdir, "guardian")
        if os.path.isfile(guardian_file):
            try:
                self.log.debug("restoreFS: remove %s", guardian_file)
                os.remove(guardian_file)
            except:
                self.log.warning("restoreFS: fail to remove %s", guardian_file)
                pass

        os.chdir(here)

    def exists(self):
        '''
            Check if an instance exists.
            It checks that both exists:
                - configuration directory (<prefix>/etc/dirsrv/slapd-<servid>)
                - environment file (/etc/sysconfig/dirsrv-<serverid> or
                  $HOME/.dirsrv/dirsrv-<serverid>)

            @param None

            @return True of False if the instance exists or not

            @raise None

        '''
        return len(self.list()) == 1

    def toLDAPURL(self):
        """Return the uri ldap[s]://host:[ssl]port."""
        if self.ldapuri:
            return self.ldapuri
        elif self.ldapi_enabled == 'on' and self.ldapi_socket is not None:
            return "ldapi://%s" % (ldapurl.ldapUrlEscape(ensure_str(self.ldapi_socket)))
        elif self.sslport and not self.realm:
            # Gssapi can't use SSL so we have to nuke it here.
            return "ldaps://%s:%d/" % (ensure_str(self.host), self.sslport)
        else:
            return "ldap://%s:%d/" % (ensure_str(self.host), self.port)

    def can_autobind(self):
        """Check if autobind/LDAPI is enabled."""
        return self.ldapi_enabled == 'on' and self.ldapi_socket is not None and self.ldapi_autobind == 'on'

    def enable_tls(self, post_open=True):
        """ If it doesn't exist, create a self-signed system CA. Using that,
        we create certificates for our instance, as well as configuring the
        servers security settings. This is mainly used for test cases, if
        you want to enable_Tls on a real instance, there are better ways to
        achieve this.

        :param post_open: Open the server connection after restart.
        :type post_open: bool
        """
        if self.config.get_attr_val_utf8_l("nsslapd-security") == 'on':
            self.restart(post_open=post_open)
            return

        # If it doesn't exist, create a cadb.
        ssca = NssSsl(dbpath=self.get_ssca_dir())
        if not ssca._db_exists():
            ssca.reinit()
            ssca.create_rsa_ca()

        # Create certificate database.
        tlsdb = NssSsl(dirsrv=self)
        # Remember, DS breaks the db, so force reinit it.
        tlsdb.reinit()
        csr = tlsdb.create_rsa_key_and_csr()
        (ca, crt) = ssca.rsa_ca_sign_csr(csr)
        tlsdb.import_rsa_crt(ca, crt)

        self.config.set('nsslapd-security', 'on')
        self.use_ldaps_uri()

        if selinux_present():
            selinux_label_port(self.sslport)

        # If we are old, we don't have template dse, so enable manually.
        if ds_is_older('1.4.0'):
            if not self.encryption.exists():
                self.encryption.create()
            if not self.rsa.exists():
                self.rsa.create()

        # Restart the instance
        self.restart(post_open=post_open)

    def use_ldaps_uri(self):
        """Change this connection to use ldaps (TLS) on the next .open() call"""
        self.ldapuri = 'ldaps://%s:%s' % (self.host, self.sslport)

    def get_ldaps_uri(self):
        """Return what our ldaps (TLS) uri would be for this instance

        :returns: The string of the servers ldaps (TLS) uri.
        """
        return 'ldaps://%s:%s' % (self.host, self.sslport)

    def use_ldap_uri(self):
        """Change this connection to use ldap on the next .open() call"""
        self.ldapuri = 'ldap://%s:%s' % (self.host, self.port)

    def get_ldap_uri(self):
        """Return what our ldap uri would be for this instance

        :returns: The string of the servers ldap uri.
        """
        return 'ldap://%s:%s' % (self.host, self.port)

    def getServerId(self):
        """Return the server identifier."""
        return self.serverid

    def get_ldif_dir(self):
        """Return the server instance ldif directory."""
        return self.ds_paths.ldif_dir

    def get_bak_dir(self):
        """Return the server instance ldif directory."""
        return self.ds_paths.backup_dir

    def get_data_dir(self):
        """Return the server data path

        :returns: The string path of the data location.
        """
        return self.ds_paths.data_dir

    def get_local_state_dir(self):
        """Return the server data path

        :returns: The string path of the data location.
        """
        return self.ds_paths.local_state_dir

    def get_changelog_dir(self):
        """Return the server changelog path

        :returns: The string path of changelog location.
        """
        return os.path.abspath(os.path.join(self.ds_paths.db_dir, '../changelogdb'))

    def get_config_dir(self):
        """Return the server config directory

        :returns: The string path of config location.
        """
        return self.ds_paths.config_dir

    def get_cert_dir(self):
        return self.ds_paths.cert_dir

    def get_sysconf_dir(self):
        return self.ds_paths.sysconf_dir

    def get_ssca_dir(self):
        """Get the system self signed CA path.

        :returns: The path to the CA nss db
        """
        return os.path.join(self.ds_paths.sysconf_dir, 'dirsrv/ssca')

    def get_initconfig_dir(self):
        return self.ds_paths.initconfig_dir

    def get_sbin_dir(self):
        return self.ds_paths.sbin_dir

    def get_bin_dir(self):
        return self.ds_paths.bin_dir

    def get_run_dir(self):
        return self.ds_paths.run_dir

    def get_plugin_dir(self):
        return self.ds_paths.plugin_dir

    def get_tmp_dir(self):
        return self.ds_paths.tmp_dir

    def get_ldapi_path(self):
        return self.ds_paths.ldapi

    def get_user_uid(self):
        return pwd.getpwnam(self.ds_paths.user).pw_uid

    def get_group_gid(self):
        return grp.getgrnam(self.ds_paths.group).gr_gid

    def get_uuid(self):
        """Get the python dirsrv unique id.

        :returns: String of the object uuid
        """
        return self.uuid

    def has_asan(self):
        return self.ds_paths.asan_enabled

    def with_systemd(self):
        if self.systemd_override is not None:
            return self.systemd_override
        return self.ds_paths.with_systemd

    def pid_file(self):
        if self._containerised:
            return "/data/run/slapd-localhost.pid"
        return self.ds_paths.pid_file

    def get_server_tls_subject(self):
        """ Get the servers TLS subject line for enrollment purposes.

        :returns: String of the Server-Cert subject line.
        """
        tlsdb = NssSsl(dirsrv=self)
        return tlsdb.get_server_cert_subject()

    #
    # Get entries
    #
    def getEntry(self, *args, **kwargs):
        """Wrapper around SimpleLDAPObject.search. It is common to just get
           one entry.
            @param  - entry dn
            @param  - search scope, in ldap.SCOPE_BASE (default),
                      ldap.SCOPE_SUB, ldap.SCOPE_ONE
            @param filterstr - filterstr, default '(objectClass=*)' from
                               SimpleLDAPObject
            @param attrlist - list of attributes to retrieve. eg ['cn', 'uid']
            @oaram attrsonly - default None from SimpleLDAPObject
            eg. getEntry(dn, scope, filter, attributes)

            XXX This cannot return None
        """
        self.log.debug("Retrieving entry with %r", [args])
        if len(args) == 1 and 'scope' not in kwargs:
            args += (ldap.SCOPE_BASE, )

        res = self.search(*args, **kwargs)
        restype, obj = self.result(res)
        # TODO: why not test restype?
        if not obj:
            raise NoSuchEntryError("no such entry for %r", [args])

        self.log.debug("Retrieved entry %s", obj)
        if isinstance(obj, Entry):
            return obj
        else:  # assume list/tuple
            if obj[0] is None:
                raise NoSuchEntryError("Entry is None")
            return obj[0]

    def _test_entry(self, dn, scope=ldap.SCOPE_BASE):
        try:
            entry = self.getEntry(dn, scope)
            self.log.info("Found entry %s", entry)
            return entry
        except NoSuchEntryError:
            self.log.exception("Entry %s was added successfully, but I cannot ",
                               "search it" % dn)
            raise MissingEntryError("Entry %s was added successfully, but "
                                    "I cannot search it", dn)

    def __wrapmethods(self):
        """This wraps all methods of SimpleLDAPObject, so that we can intercept
        the methods that deal with entries.  Instead of using a raw list of
        tuples of lists of hashes of arrays as the entry object, we want to
        wrap entries in an Entry class that provides some useful methods"""
        for name in dir(self.__class__.__bases__[0]):
            attr = getattr(self, name)
            if isinstance(attr, Callable):
                setattr(self, name, wrapper(attr, name))

    def addLDIF(self, input_file, cont=False):
        class LDIFAdder(ldif.LDIFParser):
            def __init__(self, input_file, conn, cont=False,
                         ignored_attr_types=None, max_entries=0,
                         process_url_schemes=None
                         ):
                myfile = input_file
                if isinstance(input_file, str):
                    myfile = open(input_file, "r")
                self.conn = conn
                self.cont = cont
                ldif.LDIFParser.__init__(self, myfile, ignored_attr_types,
                                         max_entries, process_url_schemes)
                self.parse()
                if isinstance(input_file, str):
                    myfile.close()

            def handle(self, dn, entry):
                if not dn:
                    dn = ''
                newentry = Entry((dn, entry))
                try:
                    self.conn.add_s(newentry)
                except ldap.LDAPError as e:
                    if not self.cont:
                        raise e
                    self.log.exception("Error: could not add entry %s", dn)

        LDIFAdder(input_file, self, cont)

    def getDBStats(self, suffix, bename=''):
        if bename:
            dn = ','.join(("cn=monitor,cn=%s" % bename, DN_LDBM))
        else:
            entries_backend = self.backend.list(suffix=suffix)
            dn = "cn=monitor," + entries_backend[0].dn
        dbmondn = "cn=monitor," + DN_LDBM
        dbdbdn = "cn=database,cn=monitor," + DN_LDBM
        try:
            # entrycache and dncache stats
            ent = self.getEntry(dn, ldap.SCOPE_BASE)
            monent = self.getEntry(dbmondn, ldap.SCOPE_BASE)
            dbdbent = self.getEntry(dbdbdn, ldap.SCOPE_BASE)
            ret = "cache   available ratio    count unitsize\n"
            mecs = ent.maxentrycachesize or "0"
            cecs = ent.currententrycachesize or "0"
            rem = int(mecs) - int(cecs)
            ratio = ent.entrycachehitratio or "0"
            ratio = int(ratio)
            count = ent.currententrycachecount or "0"
            count = int(count)
            if count:
                size = int(cecs) / count
            else:
                size = 0
            ret += "entry % 11d   % 3d % 8d % 5d" % (rem, ratio, count, size)
            if ent.maxdncachesize:
                mdcs = ent.maxdncachesize or "0"
                cdcs = ent.currentdncachesize or "0"
                rem = int(mdcs) - int(cdcs)
                dct = ent.dncachetries or "0"
                tries = int(dct)
                if tries:
                    ratio = (100 * int(ent.dncachehits)) / tries
                else:
                    ratio = 0
                count = ent.currentdncachecount or "0"
                count = int(count)
                if count:
                    size = int(cdcs) / count
                else:
                    size = 0
                ret += "\ndn    % 11d   % 3d % 8d % 5d" % (
                    rem, ratio, count, size)

            if ent.hasAttr('entrycache-hashtables'):
                ret += "\n\n" + ent.getValue('entrycache-hashtables')

            # global db stats
            ret += "\n\nglobal db stats"
            dbattrs = ('dbcachehits dbcachetries dbcachehitratio ' +
                       'dbcachepagein dbcachepageout dbcacheroevict ' +
                       'dbcacherwevict'.split(' '))
            cols = {'dbcachehits': [len('cachehits'), 'cachehits'],
                    'dbcachetries': [10, 'cachetries'],
                    'dbcachehitratio': [5, 'ratio'],
                    'dbcachepagein': [6, 'pagein'],
                    'dbcachepageout': [7, 'pageout'],
                    'dbcacheroevict': [7, 'roevict'],
                    'dbcacherwevict': [7, 'rwevict']}
            dbrec = {}
            for attr, vals in monent.iterAttrs():
                if attr.startswith('dbcache'):
                    val = vals[0]
                    dbrec[attr] = val
                    vallen = len(val)
                    if vallen > cols[attr][0]:
                        cols[attr][0] = vallen
            # construct the format string based on the field widths
            fmtstr = ''
            ret += "\n"
            for attr in dbattrs:
                fmtstr += ' %%(%s)%ds' % (attr, cols[attr][0])
                ret += ' %*s' % tuple(cols[attr])
            ret += "\n" + (fmtstr % dbrec)

            # other db stats
            skips = {'nsslapd-db-cache-hit': 'nsslapd-db-cache-hit',
                     'nsslapd-db-cache-try': 'nsslapd-db-cache-try',
                     'nsslapd-db-page-write-rate':
                         'nsslapd-db-page-write-rate',
                     'nsslapd-db-page-read-rate': 'nsslapd-db-page-read-rate',
                     'nsslapd-db-page-ro-evict-rate':
                         'nsslapd-db-page-ro-evict-rate',
                     'nsslapd-db-page-rw-evict-rate':
                         'nsslapd-db-page-rw-evict-rate'}

            hline = ''  # header line
            vline = ''  # val line
            for attr, vals in dbdbent.iterAttrs():
                if attr in skips:
                    continue
                if attr.startswith('nsslapd-db-'):
                    short = attr.replace('nsslapd-db-', '')
                    val = vals[0]
                    width = max(len(short), len(val))
                    if len(hline) + width > 70:
                        ret += "\n" + hline + "\n" + vline
                        hline = vline = ''
                    hline += ' %*s' % (width, short)
                    vline += ' %*s' % (width, val)

            # per file db stats
            ret += "\n\nper file stats"
            # key is number
            # data is dict - key is attr name without the number -
            #                val is the attr val
            dbrec = {}
            dbattrs = ['dbfilename', 'dbfilecachehit',
                       'dbfilecachemiss', 'dbfilepagein', 'dbfilepageout']
            # cols maps dbattr name to column header and width
            cols = {'dbfilename': [len('dbfilename'), 'dbfilename'],
                    'dbfilecachehit': [9, 'cachehits'],
                    'dbfilecachemiss': [11, 'cachemisses'],
                    'dbfilepagein': [6, 'pagein'],
                    'dbfilepageout': [7, 'pageout']}
            for attr, vals in ent.iterAttrs():
                match = RE_DBMONATTR.match(attr)
                if match:
                    name = match.group(1)
                    num = match.group(2)
                    val = vals[0]
                    if name == 'dbfilename':
                        val = val.split('/')[-1]
                    dbrec.setdefault(num, {})[name] = val
                    vallen = len(val)
                    if vallen > cols[name][0]:
                        cols[name][0] = vallen
                match = RE_DBMONATTRSUN.match(attr)
                if match:
                    name = match.group(1)
                    if name == 'entrycache':
                        continue
                    num = match.group(2)
                    val = vals[0]
                    if name == 'dbfilename':
                        val = val.split('/')[-1]
                    dbrec.setdefault(num, {})[name] = val
                    vallen = len(val)
                    if vallen > cols[name][0]:
                        cols[name][0] = vallen
            # construct the format string based on the field widths
            fmtstr = ''
            ret += "\n"
            for attr in dbattrs:
                fmtstr += ' %%(%s)%ds' % (attr, cols[attr][0])
                ret += ' %*s' % tuple(cols[attr])
            for dbf in dbrec.values():
                ret += "\n" + (fmtstr % dbf)
            return ret
        except Exception as e:
            print ("caught exception", str(e))
        return ''

    def waitForEntry(self, dn, timeout=7200, attr='', quiet=True):
        scope = ldap.SCOPE_BASE
        filt = "(objectclass=*)"
        attrlist = []
        if attr:
            filt = "(%s=*)" % attr
            attrlist.append(attr)
        timeout += int(time.time())

        if isinstance(dn, Entry):
            dn = dn.dn

        # wait for entry and/or attr to show up
        if not quiet:
            sys.stdout.write("Waiting for %s %s:%s " % (self, dn, attr))
            sys.stdout.flush()
        entry = None
        while not entry and int(time.time()) < timeout:
            try:
                entry = self.getEntry(dn, scope, filt, attrlist)
            except NoSuchEntryError:
                pass  # found entry, but no attr
            except ldap.NO_SUCH_OBJECT:
                pass  # no entry yet
            except ldap.LDAPError as e:  # badness
                print("\nError reading entry", dn, e)
                break
            if not entry:
                if not quiet:
                    sys.stdout.write(".")
                    sys.stdout.flush()
                time.sleep(1)

        if not entry and int(time.time()) > timeout:
            print("\nwaitForEntry timeout for %s for %s" % (self, dn))
        elif entry:
            if not quiet:
                print("\nThe waited for entry is:", entry)
        else:
            print("\nError: could not read entry %s from %s" % (dn, self))

        return entry

    def setupChainingIntermediate(self):
        confdn = ','.join(("cn=config", DN_CHAIN))
        try:
            self.modify_s(confdn, [(ldap.MOD_ADD, 'nsTransmittedControl',
                                   ['2.16.840.1.113730.3.4.12',
                                    '1.3.6.1.4.1.1466.29539.12'])])
        except ldap.TYPE_OR_VALUE_EXISTS:
            self.log.error("chaining backend config already has the required ctrls")

    def setupChainingMux(self, suffix, isIntermediate, binddn, bindpw, urls):
        self.addSuffix(suffix, binddn, bindpw, urls)
        if isIntermediate:
            self.setupChainingIntermediate()

    def setupChainingFarm(self, suffix, binddn, bindpw):
        # step 1 - create the bind dn to use as the proxy
        self.setupBindDN(binddn, bindpw)
        self.addSuffix(suffix)  # step 2 - create the suffix
        # step 3 - add the proxy ACI to the suffix
        try:
            acival = ("(targetattr = \"*\")(version 3.0; acl \"Proxied " +
                      "authorization for database links\"; allow (proxy) " +
                      "userdn = \"ldap:///%s\";)" % binddn)
            self.modify_s(suffix, [(ldap.MOD_ADD, 'aci', [acival])])
        except ldap.TYPE_OR_VALUE_EXISTS:
            self.log.error("proxy aci already exists in suffix %s for %s",
                           suffix, binddn)

    def setupChaining(self, to, suffix, isIntermediate):
        """Setup chaining from self to to - self is the mux, to is the farm
        if isIntermediate is set, this server will chain requests from another
        server to to
        """
        bindcn = "chaining user"
        binddn = "cn=%s,cn=config" % bindcn
        bindpw = "chaining"

        to.setupChainingFarm(suffix, binddn, bindpw)
        self.setupChainingMux(
            suffix, isIntermediate, binddn, bindpw, to.toLDAPURL())

    def enableChainOnUpdate(self, suffix, bename):
        # first, get the mapping tree entry to modify
        mtent = self.mappingtree.list(suffix=suffix)
        dn = mtent.dn

        # next, get the path of the replication plugin
        e_plugin = self.getEntry(
            "cn=Multisupplier Replication Plugin,cn=plugins,cn=config",
            attrlist=['nsslapd-pluginPath'])
        path = e_plugin.getValue('nsslapd-pluginPath')

        mod = [(ldap.MOD_REPLACE, MT_PROPNAME_TO_ATTRNAME[MT_STATE],
                MT_STATE_VAL_BACKEND),
               (ldap.MOD_ADD, MT_PROPNAME_TO_ATTRNAME[MT_BACKEND], bename),
               (ldap.MOD_ADD, MT_PROPNAME_TO_ATTRNAME[MT_CHAIN_PATH], path),
               (ldap.MOD_ADD, MT_PROPNAME_TO_ATTRNAME[MT_CHAIN_FCT],
                MT_CHAIN_UPDATE_VAL_ON_UPDATE)]

        try:
            self.modify_s(dn, mod)
        except ldap.TYPE_OR_VALUE_EXISTS:
            print("chainOnUpdate already enabled for %s" % suffix)

    def setupConsumerChainOnUpdate(self, suffix, isIntermediate, binddn,
                                   bindpw, urls, beargs=None):
        beargs = beargs or {}
        # suffix should already exist
        # we need to create a chaining backend
        if 'nsCheckLocalACI' not in beargs:
            beargs['nsCheckLocalACI'] = 'on'  # enable local db aci eval.
        chainbe = self.setupBackend(suffix, binddn, bindpw, urls, beargs)
        # do the stuff for intermediate chains
        if isIntermediate:
            self.setupChainingIntermediate()
        # enable the chain on update
        return self.enableChainOnUpdate(suffix, chainbe)

    def setupBindDN(self, binddn, bindpw, attrs=None):
        """ Return - eventually creating - a person entry with the given dn
            and pwd.

            binddn can be a lib389.Entry
        """
        try:
            assert binddn
            if isinstance(binddn, Entry):
                assert binddn.dn
                binddn = binddn.dn
        except AssertionError:
            raise AssertionError("Error: entry dn should be set!" % binddn)

        ent = Entry(binddn)
        ent.setValues('objectclass', "top", "person")
        ent.setValues('userpassword', bindpw)
        ent.setValues('sn', "bind dn pseudo user")
        ent.setValues('cn', "bind dn pseudo user")

        # support for uid
        attribute, value = binddn.split(",")[0].split("=", 1)
        if attribute == 'uid':
            ent.setValues('objectclass', "top", "person", 'inetOrgPerson')
            ent.setValues('uid', value)

        if attrs:
            ent.update(attrs)

        try:
            self.add_s(ent)
        except ldap.ALREADY_EXISTS:
            self.log.warning("Entry %s already exists", binddn)

        try:
            entry = self._test_entry(binddn, ldap.SCOPE_BASE)
            return entry
        except MissingEntryError:
            self.log.exception("This entry should exist!")
            raise

    def setupWinSyncAgmt(self, args, entry):
        if 'winsync' not in args:
            return

        suffix = args['suffix']
        entry.setValues("objectclass", "nsDSWindowsReplicationAgreement")
        entry.setValues("nsds7WindowsReplicaSubtree",
                        args.get("win_subtree",
                                 "cn=users," + suffix))
        entry.setValues("nsds7DirectoryReplicaSubtree",
                        args.get("ds_subtree",
                                 "ou=People," + suffix))
        entry.setValues(
            "nsds7NewWinUserSyncEnabled", args.get('newwinusers', 'true'))
        entry.setValues(
            "nsds7NewWinGroupSyncEnabled", args.get('newwingroups', 'true'))
        windomain = ''
        if 'windomain' in args:
            windomain = args['windomain']
        else:
            windomain = '.'.join(ldap.explode_dn(suffix, 1))
        entry.setValues("nsds7WindowsDomain", windomain)
        if 'interval' in args:
            entry.setValues("winSyncInterval", args['interval'])
        if 'onewaysync' in args:
            if args['onewaysync'].lower() == 'fromwindows' or \
                    args['onewaysync'].lower() == 'towindows':
                entry.setValues("oneWaySync", args['onewaysync'])
            else:
                raise Exception("Error: invalid value %s for oneWaySync: " +
                                "must be fromWindows or toWindows"
                                % args['onewaysync'])

    # args - DirSrv consumer (repoth), suffix, binddn, bindpw, timeout
    # also need an auto_init argument
    def createAgreement(self, consumer, args, cn_format=r'meTo_%s:%s',
                        description_format=r'me to %s:%s'):
        """Create (and return) a replication agreement from self to consumer.
            - self is the supplier,
            - consumer is a DirSrv object (consumer can be a supplier)
            - cn_format - use this string to format the agreement name

        consumer:
            * a DirSrv object if chaining
            * an object with attributes: host, port, sslport, __str__

        args =  {
        'suffix': "dc=example,dc=com",
        'binddn': "cn=replication manager,cn=config",
        'bindpw': "replrepl",
        'bindmethod': 'simple',
        'log'   : True.
        'timeout': 120
        }

            self.suffixes is of the form {
                'o=suffix1': 'ldaps://consumer.example.com:636',
                'o=suffix2': 'ldap://consumer.example.net:3890'
            }
        """
        suffix = args['suffix']
        if not suffix:
            # This is a mandatory parameter of the command... it fails
            self.log.warning("createAgreement: suffix is missing")
            return None

        # get the RA binddn
        binddn = args.get('binddn')
        if not binddn:
            binddn = defaultProperties.get(REPLICATION_BIND_DN, None)
            if not binddn:
                # weird, internal error we do not retrieve the default
                # replication bind DN this replica agreement will fail
                # to update the consumer until the property will be set
                self.log.warning("createAgreement: binddn not provided and "
                                 "default value unavailable")
                pass

        # get the RA binddn password
        bindpw = args.get('bindpw')
        if not bindpw:
            bindpw = defaultProperties.get(REPLICATION_BIND_PW, None)
            if not bindpw:
                # weird, internal error we do not retrieve the default
                # replication bind DN password this replica agreement
                # will fail to update the consumer until the property will be
                # set
                self.log.warning("createAgreement: bindpw not provided and "
                                 "default value unavailable")
                pass

        # get the RA bind method
        bindmethod = args.get('bindmethod')
        if not bindmethod:
            bindmethod = defaultProperties.get(REPLICATION_BIND_METHOD, None)
            if not bindmethod:
                # weird, internal error we do not retrieve the default
                # replication bind method this replica agreement will
                # fail to update the consumer until the property will be set
                self.log.warning("createAgreement: bindmethod not provided and "
                                 "default value unavailable")
                pass

        nsuffix = normalizeDN(suffix)
        othhost, othport, othsslport = (
            consumer.host, consumer.port, consumer.sslport)
        othport = othsslport or othport

        # adding agreement to previously created replica
        # eventually setting self.suffixes dict.
        if nsuffix not in self.suffixes:
            replica_entries = self.replica.list(suffix)
            if not replica_entries:
                raise NoSuchEntryError(
                    "Error: no replica set up for suffix " + suffix)
            replent = replica_entries[0]
            self.suffixes[nsuffix] = {
                'dn': replent.dn,
                'type': int(replent.nsds5replicatype)
            }

        # define agreement entry
        cn = cn_format % (othhost, othport)
        dn_agreement = "cn=%s,%s" % (cn, self.suffixes[nsuffix]['dn'])
        try:
            entry = self.getEntry(dn_agreement, ldap.SCOPE_BASE)
        except ldap.NO_SUCH_OBJECT:
            entry = None
        if entry:
            self.log.warning("Agreement exists:", dn_agreement)
            self.suffixes.setdefault(nsuffix, {})[str(consumer)] = dn_agreement
            return dn_agreement
        if (nsuffix in self.agmt) and (consumer in self.agmt[nsuffix]):
            self.log.warning("Agreement exists:", dn_agreement)
            return dn_agreement

        # In a separate function in this scope?
        entry = Entry(dn_agreement)
        entry.update({
            'objectclass': ["top", "nsds5replicationagreement"],
            'cn': cn,
            'nsds5replicahost': othhost,
            'nsds5replicatimeout': str(args.get('timeout', 120)),
            'nsds5replicabinddn': binddn,
            'nsds5replicacredentials': bindpw,
            'nsds5replicabindmethod': bindmethod,
            'nsds5replicaroot': nsuffix,
            'nsds5replicaupdateschedule': '0000-2359 0123456',
            'description': description_format % (othhost, othport)
        })
        if 'starttls' in args:
            entry.setValues('nsds5replicatransportinfo', 'TLS')
            entry.setValues('nsds5replicaport', str(othport))
        elif othsslport:
            entry.setValues('nsds5replicatransportinfo', 'SSL')
            entry.setValues('nsds5replicaport', str(othsslport))
        else:
            entry.setValues('nsds5replicatransportinfo', 'LDAP')
            entry.setValues('nsds5replicaport', str(othport))
        if 'fractional' in args:
            entry.setValues('nsDS5ReplicatedAttributeList', args['fractional'])
        if 'auto_init' in args:
            entry.setValues('nsds5BeginReplicaRefresh', 'start')
        if 'fractional' in args:
            entry.setValues('nsDS5ReplicatedAttributeList', args['fractional'])
        if 'stripattrs' in args:
            entry.setValues('nsds5ReplicaStripAttrs', args['stripattrs'])

        if 'winsync' in args:  # state it clearly!
            self.setupWinSyncAgmt(args, entry)

        try:
            self.log.debug("Adding replica agreement: [%s]", entry)
            self.add_s(entry)
        except:
            #  TODO check please!
            raise
        entry = self.waitForEntry(dn_agreement)
        if entry:
            self.suffixes.setdefault(nsuffix, {})[str(consumer)] = dn_agreement
            # More verbose but shows what's going on
            if 'chain' in args:
                chain_args = {
                    'suffix': suffix,
                    'binddn': binddn,
                    'bindpw': bindpw
                }
                # Work on `self` aka producer
                if self.suffixes[nsuffix]['type'] == SUPPLIER_TYPE:
                    self.setupChainingFarm(**chain_args)
                # Work on `consumer`
                # TODO - is it really required?
                if consumer.suffixes[nsuffix]['type'] == LEAF_TYPE:
                    chain_args.update({
                        'isIntermediate': 0,
                        'urls': self.toLDAPURL(),
                        'args': args['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)
                elif consumer.suffixes[nsuffix]['type'] == HUB_TYPE:
                    chain_args.update({
                        'isIntermediate': 1,
                        'urls': self.toLDAPURL(),
                        'args': args['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)
        self.agmt.setdefault(nsuffix, {})[consumer] = dn_agreement
        return dn_agreement

    # moved to Replica
    def setupReplica(self, args):
        """Deprecated, use replica.add
        """
        return self.replica.add(**args)

    def startReplication_async(self, agmtdn):
        return self.replica.start_async(agmtdn)

    def checkReplInit(self, agmtdn):
        return self.replica.check_init(agmtdn)

    def waitForReplInit(self, agmtdn):
        return self.replica.wait_init(agmtdn)

    def startReplication(self, agmtdn):
        return self.replica.start_and_wait(agmtdn)

    def testReplication(self, suffix, *replicas):
        '''
            Make a "dummy" update on the the replicated suffix, and check
            all the provided replicas to see if they received the update.

            @param suffix - the replicated suffix we want to check
            @param *replicas - DirSrv instance, DirSrv instance, ...

            @return True of False if all servers are replicating correctly

            @raise None
        '''

        test_value = ensure_bytes('test replication from ' + self.serverid + ' to ' +
                      replicas[0].serverid + ': ' + str(int(time.time())))
        self.modify_s(suffix, [(ldap.MOD_REPLACE, 'description', test_value)])

        for replica in replicas:
            loop = 0
            replicated = False
            while loop <= 30:
                try:

                    entry = replica.getEntry(suffix, ldap.SCOPE_BASE,
                                             "(objectclass=*)")
                    if entry.hasValue('description', test_value):
                        replicated = True
                        break
                except ldap.LDAPError as e:
                    self.log.fatal('testReplication() failed to modify (%s), error (%s)', suffix, e)
                    return False
                loop += 1
                time.sleep(2)
            if not replicated:
                self.log.fatal('Replication is not in sync with replica server (%s)',
                               replica.serverid)
                return False

        return True

    def replicaSetupAll(self, repArgs):
        """setup everything needed to enable replication for a given suffix.
            1- eventually create the suffix
            2- enable replication logging
            3- create changelog
            4- create replica user
            repArgs is a dict with the following fields:
                {
                suffix - suffix to set up for replication (eventually create)
                            optional fields and their default values
                bename - name of backend corresponding to suffix, otherwise
                         it will use the *first* backend found (isn't that
                         dangerous?
                parent - parent suffix if suffix is a sub-suffix - default is
                         undef
                ro - put database in read only mode - default is read write
                type - replica type (SUPPLIER_TYPE, HUB_TYPE, LEAF_TYPE) -
                       default is supplier
                legacy - make this replica a legacy consumer - default is no

                binddn - bind DN of the replication manager user - default is
                         REPLBINDDN
                bindpw - bind password of the repl manager - default is
                         REPLBINDPW

                log - if true, replication logging is turned on - default false
                id - the replica ID - default is an auto incremented number
                }

            TODO: passing the repArgs as an object or as a **repArgs could be
                a better documentation choiche
                eg. replicaSetupAll(self, suffix, type=SUPPLIER_TYPE,
                    log=False, ...)
        """

        repArgs.setdefault('type', SUPPLIER_TYPE)
        user = repArgs.get('binddn'), repArgs.get('bindpw')

        # eventually create the suffix (Eg. o=userRoot)
        # TODO should I check the addSuffix output as it doesn't raise
        self.addSuffix(repArgs['suffix'])
        if 'bename' not in repArgs:
            entries_backend = self.backend.list(suffix=repArgs['suffix'])
            # just use first one
            repArgs['bename'] = entries_backend[0].cn
        if repArgs.get('log', False):
            self.enableReplLogging()

        # enable changelog for supplier and hub
        if repArgs['type'] != LEAF_TYPE:
            self.replica.changelog()
        # create replica user without timeout and expiration issues
        try:
            attrs = list(user)
            attrs.append({
                'nsIdleTimeout': '0',
                'passwordExpirationTime': '20381010000000Z'
                })
            self.setupBindDN(*attrs)
        except ldap.ALREADY_EXISTS:
            self.log.warning("User already exists: %r ", user)

        # setup replica
        # map old style args to new style replica args
        if repArgs['type'] == SUPPLIER_TYPE:
            repArgs['role'] = ReplicaRole.SUPPLIER
        elif repArgs['type'] == LEAF_TYPE:
            repArgs['role'] = ReplicaRole.CONSUMER
        else:
            repArgs['role'] = ReplicaRole.HUB
        repArgs['rid'] = repArgs['id']

        # remove invalid arguments from replica.add
        for invalid_arg in 'type id bename log'.split():
            if invalid_arg in repArgs:
                del repArgs[invalid_arg]

        ret = self.replica.add(**repArgs)
        if 'legacy' in repArgs:
            self.setupLegacyConsumer(*user)

        return ret

    def subtreePwdPolicy(self, basedn, pwdpolicy, **pwdargs):
        args = {'basedn': basedn, 'escdn': escapeDNValue(
            normalizeDN(basedn))}
        condn = "cn=nsPwPolicyContainer,%(basedn)s" % args
        poldn = ("cn=cn\\=nsPwPolicyEntry\\,%(escdn)s,cn=nsPwPolicyContainer" +
                 ",%(basedn)s" % args)
        temdn = ("cn=cn\\=nsPwTemplateEntry\\,%(escdn)s,cn=nsPwPolicyContain" +
                 "er,%(basedn)s" % args)
        cosdn = "cn=nsPwPolicy_cos,%(basedn)s" % args
        conent = Entry(condn)
        conent.setValues('objectclass', 'nsContainer')
        polent = Entry(poldn)
        polent.setValues('objectclass', ['ldapsubentry', 'passwordpolicy'])
        tement = Entry(temdn)
        tement.setValues('objectclass', ['extensibleObject',
                         'costemplate', 'ldapsubentry'])
        tement.setValues('cosPriority', '1')
        tement.setValues('pwdpolicysubentry', poldn)
        cosent = Entry(cosdn)
        cosent.setValues('objectclass', ['ldapsubentry',
                         'cosSuperDefinition', 'cosPointerDefinition'])
        cosent.setValues('cosTemplateDn', temdn)
        cosent.setValues(
            'cosAttribute', 'pwdpolicysubentry default operational-default')
        for ent in (conent, polent, tement, cosent):
            try:
                self.add_s(ent)
                self.log.debug("created subtree pwpolicy entry %s", ent.dn)
            except ldap.ALREADY_EXISTS:
                self.log.debug("subtree pwpolicy entry %s", ent.dn,
                              "already exists - skipping")
        self.setPwdPolicy({'nsslapd-pwpolicy-local': 'on'})
        self.setDNPwdPolicy(poldn, pwdpolicy, **pwdargs)

    def userPwdPolicy(self, user, pwdpolicy, **pwdargs):
        ary = ldap.explode_dn(user)
        par = ','.join(ary[1:])
        escuser = escapeDNValue(normalizeDN(user))
        args = {'par': par, 'udn': user, 'escudn': escuser}
        condn = "cn=nsPwPolicyContainer,%(par)s" % args
        poldn = ("cn=cn\\=nsPwPolicyEntry\\,%(escudn)s,cn=nsPwPolicyCont" +
                 "ainer,%(par)s" % args)
        conent = Entry(condn)
        conent.setValues('objectclass', 'nsContainer')
        polent = Entry(poldn)
        polent.setValues('objectclass', ['ldapsubentry', 'passwordpolicy'])
        for ent in (conent, polent):
            try:
                self.add_s(ent)
                self.log.debug("created user pwpolicy entry %s", ent.dn)
            except ldap.ALREADY_EXISTS:
                self.log.debug("user pwpolicy entry %s", ent.dn,
                      "already exists - skipping")
        mod = [(ldap.MOD_REPLACE, 'pwdpolicysubentry', poldn)]
        self.modify_s(user, mod)
        self.setPwdPolicy({'nsslapd-pwpolicy-local': 'on'})
        self.setDNPwdPolicy(poldn, pwdpolicy, **pwdargs)

    def setPwdPolicy(self, pwdpolicy, **pwdargs):
        self.setDNPwdPolicy(DN_CONFIG, pwdpolicy, **pwdargs)

    def setDNPwdPolicy(self, dn, pwdpolicy, **pwdargs):
        """input is dict of attr/vals"""
        mods = []
        for (attr, val) in pwdpolicy.items():
            mods.append((ldap.MOD_REPLACE, attr, ensure_bytes(val)))
        if pwdargs:
            for (attr, val) in pwdargs.items():
                mods.append((ldap.MOD_REPLACE, attr, ensure_bytes(val)))
        self.modify_s(dn, mods)

    # Moved to config
    # replaced by loglevel
    def enableReplLogging(self):
        """Enable logging of replication stuff (1<<13)"""
        val = LOG_REPLICA
        return self.config.loglevel([val])

    def disableReplLogging(self):
        return self.config.loglevel()

    def setLogLevel(self, *vals):
        """Set nsslapd-errorlog-level and return its value."""
        return self.config.loglevel(vals)

    def setAccessLogLevel(self, *vals):
        """Set nsslapd-accesslog-level and return its value."""
        return self.config.loglevel(vals, service='access')

    def setAccessLogBuffering(self, state):
        """Set nsslapd-accesslog-logbuffering - state is True or False"""
        return self.config.logbuffering(state)

    def configSSL(self, secport=636, secargs=None):
        """Configure SSL support into cn=encryption,cn=config.

            secargs is a dict like {
                'nsSSLPersonalitySSL': 'Server-Cert'
            }

            XXX moved to brooker.Config
        """
        return self.config.enable_ssl(secport, secargs)

    def getDir(self, filename, dirtype):
        """
        @param filename - the name of the test script calling this function
        @param dirtype - Either DATA_DIR and TMP_DIR are the allowed values
        @return - absolute path of the dirsrvtests data directory, or 'None'
                  on error

        Return the shared data/tmp directory relative to the ticket filename.
        The caller should always use "__file__" as the argument to this
        function.

        Get the script name from the filename that was provided:

            'ds/dirsrvtests/tickets/ticket_#####_test.py' -->
                'ticket_#####_test.py'

        Get the full path to the filename, and convert it to the data directory

            'ds/dirsrvtests/tickets/ticket_#####_test.py' -->
            'ds/dirsrvtests/data/'
            'ds/dirsrvtests/suites/dyanmic-plugins/dynamic-plugins_test.py' -->
            '/ds/dirsrvtests/data/'
        """
        dir_path = None
        if os.path.exists(filename):
            filename = os.path.abspath(filename)
            if '/suites/' in filename:
                idx = filename.find('/suites/')
            elif '/tickets/' in filename:
                idx = filename.find('/tickets/')
            elif '/stress/' in filename:
                idx = filename.find('/stress/')
            else:
                # Unknown script location
                return None

            if dirtype == TMP_DIR:
                dir_path = filename[:idx] + '/tmp/'
            elif dirtype == DATA_DIR:
                dir_path = filename[:idx] + '/data/'
            else:
                raise ValueError("Invalid directory type (%s), acceptable" +
                                 " values are DATA_DIR and TMP_DIR" % dirtype)

        return dir_path

    def clearTmpDir(self, filename):
        """
        @param filename - the name of the test script calling this function
        @return - nothing

        Clear the contents of the "tmp" dir, but leave the README file in
        place.
        """
        if os.path.exists(filename):
            filename = os.path.abspath(filename)
            if '/suites/' in filename:
                idx = filename.find('/suites/')
            elif '/tickets/' in filename:
                idx = filename.find('/tickets/')
            else:
                # Unknown script location
                return

            dir_path = filename[:idx] + '/tmp/'
            if dir_path:
                filelist = [tmpfile for tmpfile in os.listdir(dir_path)
                            if tmpfile != 'README']
                for tmpfile in filelist:
                    tmpfile = os.path.abspath(dir_path + tmpfile)
                    if os.path.isdir(tmpfile):
                        # Remove directory and all of its contents
                        shutil.rmtree(tmpfile)
                    else:
                        os.remove(tmpfile)

                return

        self.log.fatal('Failed to clear tmp directory (%s)', filename)

    def upgrade(self, upgradeMode):
        """
        @param upgradeMode - the upgrade is either "online" or "offline"
        """
        if upgradeMode == 'online':
            online = True
        else:
            online = False
        DirSrvTools.runUpgrade(self.ds_paths.prefix, online)

    #
    # The following are the functions to perform offline scripts(when the
    # server is stopped)
    #
    def ldif2db(self, bename, suffixes, excludeSuffixes, encrypt,
                import_file, import_cl=False):
        """
        @param bename - The backend name of the database to import
        @param suffixes - List/tuple of suffixes to import
        @param excludeSuffixes - List/tuple of suffixes to exclude from import
        @param encrypt - Perform attribute encryption
        @param input_file - File to import: file
        @return - True if import succeeded
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, 'ns-slapd')

        if self.status():
            self.log.error("ldif2db: Can not operate while directory server is running")
            return False

        if not bename and not suffixes:
            self.log.error("ldif2db: backend name or suffix missing")
            return False

        if not os.path.isfile(import_file):
            self.log.error("ldif2db: Can't find file: %s", import_file)
            return False

        cmd = [
            prog,
            'ldif2db',
            '-D', self.get_config_dir(),
            '-i', import_file,
        ]
        if bename:
            cmd.append('-n')
            cmd.append(bename)
        if suffixes:
            for suffix in suffixes:
                cmd.append('-s')
                cmd.append(suffix)
        if excludeSuffixes:
            for excludeSuffix in excludeSuffixes:
                cmd = cmd + ' -x ' + excludeSuffix
                cmd.append('-x')
                cmd.append(excludeSuffix)
        if encrypt:
            cmd.append('-E')
        if import_cl:
            cmd.append('-R')

        try:
            result = subprocess.check_output(cmd, encoding='utf-8')
        except subprocess.CalledProcessError as e:
            if e.returncode == TaskWarning.WARN_SKIPPED_IMPORT_ENTRY:
                self.log.debug("Command: %s skipped import entry warning %s",
                               format_cmd_list(cmd), e.returncode)
                return e.returncode
            else:
                self.log.debug("Command: %s failed with the return code %s and the error %s",
                               format_cmd_list(cmd), e.returncode, e.output)
                return False

        return True

    def db2ldif(self, bename, suffixes, excludeSuffixes, encrypt, repl_data,
                outputfile, export_cl=False):
        """
        @param bename - The backend name of the database to export
        @param suffixes - List/tuple of suffixes to export
        @param excludeSuffixes - List/tuple of suffixes to exclude from export
        @param encrypt - Perform attribute encryption
        @param repl_data - Export the replication data
        @param outputfile - The filename for the exported LDIF
        @return - True if export succeeded
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, 'ns-slapd')

        if self.status():
            self.log.error("db2ldif: Can not operate while directory server is running")
            return False

        if not bename and not suffixes:
            self.log.error("db2ldif: backend name or suffix missing")
            return False

        cmd = [
            prog,
            'db2ldif',
            '-D', self.get_config_dir()
        ]
        if bename:
            cmd.append('-n')
            cmd.append(bename)
        if suffixes:
            for suffix in suffixes:
                cmd.append('-s')
                cmd.append(suffix)
        if excludeSuffixes:
            for excludeSuffix in excludeSuffixes:
                cmd = cmd + ' -x ' + excludeSuffix
                cmd.append('-x')
                cmd.append(excludeSuffix)
        if encrypt:
            cmd.append('-E')
        if repl_data and not export_cl:
            cmd.append('-r')
        if export_cl:
            cmd.append('-R')
        if outputfile is not None:
            cmd.append('-a')
            cmd.append(outputfile)
        else:
            # No output file specified.  Use the default ldif location/name
            cmd.append('-a')
            tnow = datetime.now().strftime("%Y_%m_%d_%H_%M_%S")
            if bename:
                ldifname = os.path.join(self.ds_paths.ldif_dir, "%s-%s-%s.ldif" % (self.serverid, bename, tnow))
            else:
                ldifname = os.path.join(self.ds_paths.ldif_dir, "%s-%s.ldif" % (self.serverid, tnow))
            cmd.append(ldifname)
        try:
            result = subprocess.check_output(cmd, encoding='utf-8')
        except subprocess.CalledProcessError as e:
            self.log.debug("Command: %s failed with the return code %s and the error %s",
                           format_cmd_list(cmd), e.returncode, e.output)
            return False

        self.log.debug("db2ldif output: BEGIN")
        for line in result.split("\n"):
            self.log.debug(line)
        self.log.debug("db2ldif output: END")

        return True

    def bak2db(self, archive_dir):
        """
        @param archive_dir - The directory containing the backup
        @param bename - The backend name to restore
        @return - True if the restore succeeded
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, 'ns-slapd')

        if self.status():
            self.log.error("bak2db: Can not operate while directory server is running")
            return False

        if not archive_dir:
            self.log.error("bak2db: backup directory missing")
            return False
        elif not archive_dir.startswith("/"):
            archive_dir = os.path.join(self.ds_paths.backup_dir, archive_dir)

        try:
            cmd = [prog,
                   'archive2db',
                   '-a', archive_dir,
                   '-D', self.get_config_dir()]
            result = subprocess.check_output(cmd, encoding='utf-8')
        except subprocess.CalledProcessError as e:
            self.log.debug("Command: %s failed with the return code %s and the error %s",
                           format_cmd_list(cmd), e.returncode, e.output)
            return False

        self.log.debug("bak2db output: BEGIN")
        for line in result.split("\n"):
            self.log.debug(line)
        self.log.debug("bak2db output: END")

        return True

    def db2bak(self, archive_dir):
        """
        @param archive_dir - The directory to write the backup to
        @return - True if the backup succeeded
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, 'ns-slapd')

        if self.status():
            self.log.error("db2bak: Can not operate while directory server is running")
            return False

        if archive_dir is None:
            # Use the instance name and date/time as the default backup name
            tnow = datetime.now().strftime("%Y_%m_%d_%H_%M_%S")
            archive_dir = os.path.join(self.ds_paths.backup_dir, "%s-%s" % (self.serverid, tnow))
        elif not archive_dir.startswith("/"):
            # Relative path, append it to the bak directory
            archive_dir = os.path.join(self.ds_paths.backup_dir, archive_dir)

        try:
            cmd = [prog,
                   'db2archive',
                   '-a', archive_dir,
                   '-D', self.get_config_dir()]
            result = subprocess.check_output(cmd, encoding='utf-8')
        except subprocess.CalledProcessError as e:
            self.log.debug("Command: %s failed with the return code %s and the error %s",
                           format_cmd_list(cmd), e.returncode, e.output)
            return False

        self.log.debug("db2bak output: BEGIN")
        for line in result.split("\n"):
            self.log.debug(line)
        self.log.debug("db2bak output: END")

        return True

    def db2index(self, bename=None, suffixes=None, attrs=None, vlvTag=None):
        """
        @param bename - The backend name to reindex
        @param suffixes - List/tuple of suffixes to reindex, currently unused
        @param attrs - List/tuple of the attributes to index
        @param vlvTag - The VLV index name to index, currently unused
        @return - True if reindexing succeeded
        """
        prog = os.path.join(self.ds_paths.sbin_dir, 'ns-slapd')

        if self.status():
            self.log.error("db2index: Can not operate while directory server is running")
            return False
        cmd = [prog, ]
        # No backend specified, do an upgrade on all backends
        # Backend and no attrs specified, reindex with all backend indexes
        # Backend and attr/s specified, reindex backend with attr/s
        if bename:
            cmd.append('db2index')
            cmd.append('-n')
            cmd.append(bename)
            if attrs:
                 for attr in attrs:
                        cmd.append('-t')
                        cmd.append(attr)
            else:
                dse_ldif = DSEldif(self)
                indexes = dse_ldif.get_indexes(bename)
                if indexes:
                    for idx in indexes:
                        cmd.append('-t')
                        cmd.append(idx)
        else:
            cmd.append('upgradedb')
            cmd.append('-a')
            now = datetime.now().isoformat()
            cmd.append(os.path.join(self.get_bak_dir(), 'reindex_%s' % now))
            cmd.append('-f')

        cmd.append('-D')
        cmd.append(self.get_config_dir())

        try:
            result = subprocess.check_output(cmd, encoding='utf-8')
        except subprocess.CalledProcessError as e:
            self.log.debug("Command: %s failed with the return code %s and the error %s",
                           format_cmd_list(cmd), e.returncode, e.output)
            return False

        self.log.debug("db2index output: BEGIN")
        for line in result.split("\n"):
            self.log.debug(line)
        self.log.debug("db2index output: END")

        return True

    def backups(self, use_json):
        # Return a list of backups from the bakdir
        bakdir = self.get_bak_dir()
        dirlist = [item for item in os.listdir(bakdir) if os.path.isdir(os.path.join(bakdir, item))]
        if use_json:
            json_result = {'type': 'list', 'items': []}
        for backup in dirlist:
            bak = bakdir + "/" + backup
            bak_date = os.path.getmtime(bak)
            bak_date = datetime.fromtimestamp(bak_date).strftime('%Y-%m-%d %H:%M:%S')
            bak_size = subprocess.check_output(['du', '-sh', bak]).split()[0].decode('utf-8')
            if use_json:
                json_item = [backup, bak_date, bak_size]
                json_result['items'].append(json_item)
            else:
                self.log.info('Backup: %s - %s (%s)', bak, bak_date, bak_size)

        if use_json:
            print(json.dumps(json_result, indent=4))

        return True

    def del_backup(self, bak_dir):
        # Delete backup directory
        bakdir = self.get_bak_dir()
        del_dir = bakdir + "/" + bak_dir
        self.log.debug("Deleting backup directory: ", del_dir)
        shutil.rmtree(del_dir)

    def getLDIFSuffix(self, filename):
        suffix = ""
        with open(filename, 'r') as ldif_file:
            for line in ldif_file:
                if line.startswith("dn: "):
                    parts = line.split(" ", 1)
                    suffix = parts[1].rstrip().lower()
                    break
        return suffix

    def ldifs(self, use_json=False):
        # Return a list of backups from the bakdir
        ldifdir = self.get_ldif_dir()
        dirlist = [item for item in os.listdir(ldifdir)]

        if use_json:
            json_result = {'type': 'list', 'items': []}
        for ldif_file in dirlist:
            fullpath = ldifdir + "/" + ldif_file
            ldif_date = os.path.getmtime(fullpath)
            ldif_date = datetime.fromtimestamp(ldif_date).strftime('%Y-%m-%d %H:%M:%S')
            ldif_size = subprocess.check_output(['du', '-sh', fullpath]).split()[0].decode('utf-8')
            ldif_suffix = self.getLDIFSuffix(fullpath)
            if ldif_suffix == "":
                # This is not a valid LDIF file
                ldif_suffix = "Invalid LDIF"
            if use_json:
                json_item = [ldif_file, ldif_date, ldif_size, ldif_suffix]
                json_result['items'].append(json_item)
            else:
                self.log.info('{} ({}), Created ({}), Size ({})'.format(ldif_file, ldif_suffix, ldif_date, ldif_size))

        if use_json:
            print(json.dumps(json_result, indent=4))

        return True

    def del_ldif(self, ldifname):
        # Delete backup directory
        ldifdir = self.get_ldif_dir()
        del_file = ldifdir + "/" + ldifname
        self.log.debug("Deleting LDIF file: " + del_file)
        os.remove(del_file)

    def dbscan(self, bename=None, index=None, key=None, width=None, isRaw=False):
        """Wrapper around dbscan tool that analyzes and extracts information
        from an import Directory Server database file

        :param bename: The backend name to scan
        :param index: Index name (e.g., cn or cn.db) to scan
        :param key: Index key to dump
        :param id: Entry id to dump
        :param width: Entry truncate size (bytes)
        :param isRaw: Dump as a raw data
        :returns: A dumped string
        """

        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.bin_dir, DBSCAN)

        if not bename:
            self.log.error("dbscan: missing required backend name")
            return False

        if not index:
            self.log.error("dbscan: missing required index name")
            return False
        elif '.db' in index:
            indexfile = os.path.join(self.dbdir, bename, index)
        else:
            indexfile = os.path.join(self.dbdir, bename, index + '.db')
        # (we should also accept a version number for .db suffix)
        for f in glob.glob(f'{indexfile}*'):
            indexfile = f

        cmd = [prog, '-f', indexfile]

        if 'id2entry' in index:
            if key and key.isdigit():
                cmd.extend(['-K', key])
        else:
            if key:
                cmd.extend(['-k', key])

        if width:
            cmd.extend(['-t', width])

        if isRaw:
            cmd.append('-R')

        self.stop(timeout=10)

        self.log.info('Running script: %s', cmd)
        output = subprocess.check_output(cmd)

        self.start(timeout=10)

        return output

    def dbverify(self, bename):
        """
        @param bename - the backend name to verify
        @return - True if the verify succeded
        """
        prog = os.path.join(self.ds_paths.sbin_dir, 'ns-slapd')

        if self.status():
            self.log.error("dbverify: Can not operate while directory server is running")
            return False

        cmd = [
            prog,
            'dbverify',
            '-D', self.get_config_dir(),
            '-n', bename
        ]

        try:
            subprocess.check_output(cmd, encoding='utf-8')
        except subprocess.CalledProcessError as e:
            self.log.debug("Command: %s failed with the return code %s and the error %s",
                           format_cmd_list(cmd), e.returncode, e.output)
            return False
        return True

    def searchAccessLog(self, pattern):
        """
        Search all the access logs
        """
        return DirSrvTools.searchFile(self.accesslog + "*", pattern)

    def searchAuditLog(self, pattern):
        """
        Search all the audit logs
        """
        time.sleep(1)
        return DirSrvTools.searchFile(self.auditlog + "*", pattern)

    def searchErrorsLog(self, pattern):
        """
        Search all the error logs
        """
        time.sleep(1)
        return DirSrvTools.searchFile(self.errlog + "*", pattern)

    def detectDisorderlyShutdown(self):
        """
        Search the current errors log for a disorderly shutdown message
        """
        time.sleep(1)
        return DirSrvTools.searchFile(self.errlog, DISORDERLY_SHUTDOWN)

    def deleteLog(self, logtype, restart=True):
        """
        Delete all the logs for this log type.
        """
        if restart:
            self.stop()
        for log in glob.glob(logtype + "*"):
            if os.path.isfile(log):
                try:
                    os.remove(log)
                except:
                    self.log.info("deleteLog: fail to remove %s", log)
                    pass
        if restart:
            self.start()

    def deleteAccessLogs(self, restart=True):
        """
        Delete all the access logs.
        """
        self.deleteLog(self.accesslog, restart)

    def deleteAuditLogs(self, restart=True):
        """
        Delete all the audit logs.
        """
        self.deleteLog(self.auditlog, restart)

    def deleteErrorLogs(self, restart=True):
        """
        Delete all the error logs.
        """
        self.deleteLog(self.errlog, restart)

    def deleteAllLogs(self, restart=True):
        """
        Delete all the logs.
        """
        self.stop()
        self.deleteAccessLogs(restart=False)
        self.deleteErrorLogs(restart=False)
        self.deleteAuditLogs(restart=False)
        self.start()

    def get_effective_rights(self, sourcedn, base=DEFAULT_SUFFIX,
                             scope=ldap.SCOPE_SUBTREE, *args, **kwargs):
        """
        Conduct a search on effective rights for some object (sourcedn)
        against a filter.
        For arguments to this function, please see LDAPObject.search_s.
        For example:

        @param sourcedn - DN of entry to check
        @param base - Base DN of the suffix to check
        @param scope - search scope
        @param args -
        @param kwargs -
        @return - ldap result

        LDAPObject.search_s(base, scope[, filterstr='(objectClass=*)'
                            [, attrlist=None[, attrsonly=0]]]) -> list|None

         The sourcedn is the object that is having it's rights checked against
         all objects matched by filterstr
         If sourcedn is '', anonymous is checked.
         If you set targetattrs to "*" you will see ALL possible attributes for
         all possible objectclasses on the object.
         If you set targetattrs to "+" you will see operation attributes only.
         If you set targetattrs to "*@objectclass" you will only see the
         attributes from that class.
        You will want to look at entryLevelRights and attributeLevelRights in
        the result.
         entryLevelRights:
          * a - add
          * d - delete
          * n - rename
          * v - view
         attributeLevelRights
          * r - read
          * s - search
          * w - write to the attribute (add / replace)
          * o - obliterate (Delete the attribute)
          * c - Compare the attributes directory side
          * W - self write the attribute
          * O - self obliterate
        """
        # Is there a better way to do this check?
        if not (MAJOR >= 3 or (MAJOR == 2 and MINOR >= 7)):
            raise Exception("UNSUPPORTED EXTENDED OPERATION ON THIS VERSION " +
                            "OF PYTHON")
        ldap_result = None
        # This may not be thread safe. Is there a better way to do this?
        try:
            gerc = GetEffectiveRightsControl(True, authzId='dn:' +
                                             sourcedn.encode('UTF-8'))
            sctrl = [gerc]
            self.set_option(ldap.OPT_SERVER_CONTROLS, sctrl)
            # ldap_result = self.search_s(base, scope, *args, **kwargs)
            res = self.search(base, scope, *args, **kwargs)
            restype, ldap_result = self.result(res)
        finally:
            self.set_option(ldap.OPT_SERVER_CONTROLS, [])
        return ldap_result

    # Is there a better name for this function?
    def dereference(self, deref, base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE,
                    *args, **kwargs):
        """
        Perform a search which dereferences values from attributes such as
        member or unique member.
        For arguments to this function, please see LDAPObject.search_s. For
        example:

        @param deref - Dereference query
        @param base - Base DN of the suffix to check
        @param scope - search scope
        @param args -
        @param kwargs -
        @return - ldap result

        LDAPObject.search_s(base, scope[, filterstr='(objectClass=*)'
                            [, attrlist=None[, attrsonly=0]]]) -> list|None

        A deref query is of the format:

        "<attribute to derference>:<deref attr1>,<deref attr2>..."

        "uniqueMember:dn,objectClass"

        This will return the dn's and objectClasses of the dereferenced members
        of the group.
        """
        if not (MAJOR >= 3 or (MAJOR == 2 and MINOR >= 7)):
            raise Exception("UNSUPPORTED EXTENDED OPERATION ON THIS VERSION " +
                            " OF PYTHON")

        # This may not be thread safe. Is there a better way to do this?
        try:
            drc = DereferenceControl(True, deref=deref.encode('UTF-8'))
            sctrl = [drc]
            self.set_option(ldap.OPT_SERVER_CONTROLS, sctrl)

            # ldap_result = self.search_s(base, scope, *args, **kwargs)
            res = self.search(base, scope, *args, **kwargs)
            resp_type, resp_data, resp_msgid, decoded_resp_ctrls, _, _ = \
                self.result4(res, add_ctrls=1,
                             resp_ctrl_classes={CONTROL_DEREF:
                                                DereferenceControl})
        finally:
            self.set_option(ldap.OPT_SERVER_CONTROLS, [])
        return resp_data, decoded_resp_ctrls

    def buildLDIF(self, num, ldif_file, suffix='dc=example,dc=com'):
        """Generate a simple ldif file using the dbgen.pl script, and set the
           ownership and permissions to match the user that the server runs as.

           @param num - number of entries to create
           @param ldif_file - ldif file name(including the path)
           @suffix - DN of the parent entry in the ldif file
           @return - nothing
           @raise - OSError
        """
        raise Exception("Perl tools disabled on this system. Try dbgen py module.")


    def getConsumerMaxCSN(self, replica_entry, binddn=None, bindpw=None):
        """
        Attempt to get the consumer's maxcsn from its database
        """
        host = replica_entry.get_attr_val_utf8(AGMT_HOST)
        port = replica_entry.get_attr_val_utf8(AGMT_PORT)
        suffix = replica_entry.get_attr_val_utf8(REPL_ROOT)
        error_msg = "Unavailable"

        # If we are using LDAPI we need to provide the credentials, otherwise
        # use the existing credentials
        if binddn is None:
            binddn = self.binddn
        if bindpw is None:
            bindpw = self.bindpw

        # Open a connection to the consumer
        consumer = DirSrv(verbose=self.verbose)
        args_instance[SER_HOST] = host
        args_instance[SER_PORT] = int(port)
        args_instance[SER_ROOT_DN] = binddn
        args_instance[SER_ROOT_PW] = bindpw
        args_standalone = args_instance.copy()
        consumer.allocate(args_standalone)
        try:
            consumer.open()
        except ldap.LDAPError as e:
            self.log.debug('Connection to consumer (%s:%s) failed, error: %s',
                           host, port, e)
            return error_msg

        # Get the replica id from supplier to compare to the consumer's rid
        try:
            replica_entries = self.replica.list(suffix)
            if not replica_entries:
                # Error
                consumer.close()
                return None
            rid = ensure_str(replica_entries[0].getValue(REPL_ID))
        except:
            # Error
            consumer.close()
            return None

        # Search for the tombstone RUV entry
        try:
            entry = consumer.search_s(suffix, ldap.SCOPE_SUBTREE,
                                      REPLICA_RUV_FILTER, ['nsds50ruv'])
            consumer.close()
            if not entry:
                # Error out?
                self.log.error("Failed to retrieve database RUV entry from consumer")
                return error_msg
            elements = ensure_list_str(entry[0].getValues('nsds50ruv'))
            for ruv in elements:
                if ('replica %s ' % rid) in ruv:
                    ruv_parts = ruv.split()
                    if len(ruv_parts) == 5:
                        return ruv_parts[4]
                    else:
                        return error_msg
            return error_msg
        except:
            # Search failed, but return 0
            consumer.close()
            return error_msg

    def getReplAgmtStatus(self, agmt_entry, binddn=None, bindpw=None):
        '''
        Return the status message, if consumer is not in synch raise an
        exception
        '''
        agmt_maxcsn = None
        suffix = agmt_entry.get_attr_val_utf8(REPL_ROOT)
        agmt_name = agmt_entry.get_attr_val_utf8('cn')
        status = "Unknown"
        rc = -1

        try:
            entry = self.search_s(suffix, ldap.SCOPE_SUBTREE,
                                  REPLICA_RUV_FILTER, [AGMT_MAXCSN])
        except:
            return status

        '''
        There could be many agmts maxcsn attributes, find ours

        agmtMaxcsn: <suffix>;<agmt name>;<host>;<port>;<consumer rid>;<maxcsn>

            dc=example,dc=com;test_agmt;localhost;389:4;56536858000100010000

        or if the consumer is not reachable:

            dc=example,dc=com;test_agmt;localhost;389;unavailable

        '''

        maxcsns = ensure_list_str(entry[0].getValues(AGMT_MAXCSN))
        for csn in maxcsns:
            comps = csn.split(';')
            if agmt_name == comps[1]:
                # same replica, get maxcsn
                if len(comps) < 6:
                    return "Consumer unavailable"
                else:
                    agmt_maxcsn = comps[5]

        if agmt_maxcsn:
            con_maxcsn = self.getConsumerMaxCSN(agmt_entry, binddn=binddn, bindpw=bindpw)
            if con_maxcsn:
                if agmt_maxcsn == con_maxcsn:
                    status = "In Synchronization"
                    rc = 0
                else:
                    # Not in sync - attempt to discover the cause
                    repl_msg = "Unknown"
                    if agmt_entry.get_attr_val_utf8(AGMT_UPDATE_IN_PROGRESS) == 'TRUE':
                        # Replication is on going - this is normal
                        repl_msg = "Replication still in progress"
                    elif "Can't Contact LDAP" in \
                         agmt_entry.get_attr_val_utf8(AGMT_UPDATE_STATUS):
                        # Consumer is down
                        repl_msg = "Consumer can not be contacted"

                    status = ("Not in Synchronization: supplier " +
                              "(%s) consumer (%s)  Reason(%s)" %
                              (agmt_maxcsn, con_maxcsn, repl_msg))
        if rc != 0:
            raise ValueError(status)
        return status

    def delete_branch_s(self, basedn, scope, filterstr="(objectclass=*)", serverctrls=None, clientctrls=None):
        ents = self.search_s(basedn, scope, filterstr, escapehatch='i am sure')

        for ent in sorted(ents, key=lambda e: len(e.dn), reverse=True):
            self.log.debug("Delete entry children %s", ent.dn)
            self.delete_ext_s(ent.dn, serverctrls=serverctrls, clientctrls=clientctrls, escapehatch='i am sure')

    def backup_online(self, archive=None, db_type=None):
        """Creates a backup of the database"""

        if archive is None:
            # Use the instance name and date/time as the default backup name
            tnow = datetime.now().strftime("%Y_%m_%d_%H_%M_%S")
            if self.serverid is not None:
                backup_dir_name = "%s-%s" % (self.serverid, tnow)
            else:
                backup_dir_name = "backup-%s" % tnow
            archive = os.path.join(self.ds_paths.backup_dir, backup_dir_name)
        elif archive[0] != "/":
            # Relative path, append it to the bak directory
            archive = os.path.join(self.ds_paths.backup_dir, archive)

        task = BackupTask(self)
        task_properties = {'nsArchiveDir': archive}
        if db_type is not None:
            task_properties['nsDatabaseType'] = db_type
        task.create(properties=task_properties)

        return task

    def restore_online(self, archive, db_type=None):
        """Restores a database from a backup"""

        # Relative path, append it to the bak directory
        if archive[0] != "/":
            archive = os.path.join(self.ds_paths.backup_dir, archive)

        task = RestoreTask(self)
        task_properties = {'nsArchiveDir': archive}
        if db_type is not None:
            task_properties['nsDatabaseType'] = db_type

        task.create(properties=task_properties)

        return task
