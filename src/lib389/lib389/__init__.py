# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""The lib389 module.


    IMPORTANT: Ternary operator syntax is unsupported on RHEL5
        x if cond else y #don't!

    The lib389 functionalities are split in various classes
        defined in brookers.py

    TODO: reorganize method parameters according to SimpleLDAPObject
        naming: filterstr, attrlist
"""
try:
    from subprocess import Popen, PIPE, STDOUT
    HASPOPEN = True
except ImportError:
    import popen2
    HASPOPEN = False

import io
import sys
import os
import stat
import pwd
import grp
import os.path
import base64
import socket
import ldif
import re
import ldap
import ldapurl
import time
import operator
import shutil
import datetime
import logging
import decimal
import glob
import tarfile
import subprocess
import collections
import signal
import errno
from shutil import copy2
try:
    # There are too many issues with this on EL7
    # Out of the box, it's just outright broken ...
    import six.moves.urllib.request
    import six.moves.urllib.parse
    import six.moves.urllib.error
    import six
except ImportError:
    pass
from ldap.ldapobject import SimpleLDAPObject
from ldap.cidict import cidict
from ldap import LDAPError
# file in this package

from lib389._constants import *
from lib389.properties import *
from lib389._entry import Entry
from lib389._replication import CSN, RUV
from lib389._ldifconn import LDIFConn
from lib389.tools import DirSrvTools
from lib389.mit_krb5 import MitKrb5
from lib389.utils import (
    isLocalHost,
    is_a_dn,
    normalizeDN,
    suffixfilt,
    escapeDNValue,
    update_newhost_with_fqdn,
    formatInfData,
    ensure_bytes,
    ensure_str)
from lib389.paths import Paths

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

# My logger
log = logging.getLogger(__name__)

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
                    copystat(srcname, dstname, follow_symlinks=not symlinks)
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

    def rebind(self):
        """Reconnect to the DS

            @raise ldap.CONFIDENTIALITY_REQUIRED - missing TLS:
        """
        if hasattr(ldap, 'PYLDAP_VERSION') and MAJOR >= 3:
            super(DirSrv, self).__init__(uri, bytes_mode=False, trace_level=TRACE_LEVEL)
        else:
            super(DirSrv, self).__init__(uri, trace_level=TRACE_LEVEL)
        # self.start_tls_s()
        self.simple_bind_s(ensure_str(self.binddn), self.bindpw)

    def __add_brookers__(self):
        from lib389.config import Config
        from lib389.aci import Aci
        from lib389.nss_ssl import NssSsl
        from lib389.config import RSA
        from lib389.config import Encryption
        from lib389.dirsrv_log import DirsrvAccessLog, DirsrvErrorLog
        from lib389.ldclt import Ldclt
        from lib389.mappingTree import MappingTrees
        from lib389.mappingTree import MappingTreeLegacy as MappingTree
        from lib389.backend import Backends
        from lib389.backend import BackendLegacy as Backend
        from lib389.suffix import Suffix
        from lib389.replica import ReplicaLegacy as Replica
        from lib389.replica import Replicas
        from lib389.changelog import Changelog
        from lib389.agreement import Agreement
        from lib389.schema import Schema
        from lib389.plugins import Plugins
        from lib389.tasks import Tasks
        from lib389.index import Index
        from lib389.monitor import Monitor, MonitorLDBM
        from lib389.rootdse import RootDSE

        # Need updating
        self.agreement = Agreement(self)
        self.replica = Replica(self)
        self.changelog = Changelog(self)
        self.backend = Backend(self)
        self.config = Config(self)
        self.index = Index(self)
        self.mappingtree = MappingTree(self)
        self.suffix = Suffix(self)
        self.schema = Schema(self)
        self.plugins = Plugins(self)
        self.tasks = Tasks(self)
        # Do we have a certdb path?
        # if MAJOR < 3:
        self.monitor = Monitor(self)
        self.monitorldbm = MonitorLDBM(self)
        self.rootdse = RootDSE(self)
        self.backends = Backends(self)
        self.mappingtrees = MappingTrees(self)
        self.replicas = Replicas(self)
        self.aci = Aci(self)
        self.nss_ssl = NssSsl(self)
        self.rsa = RSA(self)
        self.encryption = Encryption(self)
        self.ds_access_log = DirsrvAccessLog(self)
        self.ds_error_log = DirsrvErrorLog(self)
        self.ldclt = Ldclt(self)

    def __init__(self, verbose=False, timeout=10):
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
        self.verbose = verbose
        if self.verbose:
            log.setLevel(logging.DEBUG)
        else:
            log.setLevel(logging.INFO)
        self.log = log

        self.timeout = timeout
        self.confdir = None

        self.ds_paths = Paths(instance=self)

        # Reset the args (py.test reuses the args_instance for each test case)
        args_instance[SER_DEPLOYED_DIR] = os.environ.get('PREFIX', self.ds_paths.prefix)
        args_instance[SER_BACKUP_INST_DIR] = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)
        args_instance[SER_ROOT_DN] = DN_DM
        args_instance[SER_ROOT_PW] = PW_DM
        args_instance[SER_HOST] = LOCALHOST
        args_instance[SER_PORT] = DEFAULT_PORT
        args_instance[SER_SECURE_PORT] = None
        args_instance[SER_SERVERID_PROP] = None  # "template"
        args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
        args_instance[SER_USER_ID] = None
        args_instance[SER_GROUP_ID] = None
        args_instance[SER_REALM] = None
        args_instance[SER_INST_SCRIPTS_ENABLED] = None

        # We allocate a "default" prefix here which allows an un-allocate or
        # un-instantiated DirSrv
        # instance to be able to do an an instance discovery. For example:
        #  ds = lib389.DirSrv()
        #  ds.list(all=True)
        self.prefix = args_instance[SER_DEPLOYED_DIR]
        self.containerised = False

        self.__wrapmethods()
        self.__add_brookers__()

    def __str__(self):
        """XXX and in SSL case?"""
        return self.host + ":" + str(self.port)

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

        if SER_SERVERID_PROP not in args:
            self.log.debug('SER_SERVERID_PROP not provided, assuming non-local instance')
            # The lack of this value basically rules it out in most cases
            self.isLocal = False
            self.ds_paths = Paths(instance=self)
        else:
            self.ds_paths = Paths(args[SER_SERVERID_PROP], instance=self)


        # Do we have ldapi settings?
        # Do we really need .strip() on this?
        self.ldapi_enabled = args.get(SER_LDAPI_ENABLED, 'off')
        self.ldapi_socket = args.get(SER_LDAPI_SOCKET, None)
        self.host = None
        self.ldapuri = None
        self.sslport = None
        self.port = None
        self.inst_scripts = args.get(SER_INST_SCRIPTS_ENABLED, None)
        # Or do we have tcp / ip settings?
        if self.ldapi_enabled == 'on' and self.ldapi_socket is not None:
            self.ldapi_autobind = args.get(SER_LDAPI_AUTOBIND, 'off')
            self.isLocal = True
            if self.verbose:
                self.log.info("Allocate %s with %s" % (self.__class__, self.ldapi_socket))
        elif args.get(SER_LDAP_URL, None) is not None:
            self.ldapuri = args.get(SER_LDAP_URL)
            if self.verbose:
                self.log.info("Allocate %s with %s" % (self.__class__, self.ldapuri))
        else:
            # Settings from args of server attributes
            self.strict_hostname = args.get(SER_STRICT_HOSTNAME_CHECKING, False)
            if self.strict_hostname is True:
                self.host = args.get(SER_HOST, LOCALHOST)
                if self.host == LOCALHOST:
                    DirSrvTools.testLocalhost()
                else:
                    # Make sure our name is in hosts
                    DirSrvTools.searchHostsFile(self.host, None)
            else:
                self.host = args.get(SER_HOST, LOCALHOST_SHORT)
            self.port = args.get(SER_PORT, DEFAULT_PORT)
            self.sslport = args.get(SER_SECURE_PORT)
            self.isLocal = isLocalHost(self.host)
            if self.verbose:
                self.log.info("Allocate %s with %s:%s" % (self.__class__, self.host, (self.sslport or self.port)))

        self.binddn = args.get(SER_ROOT_DN, DN_DM)
        self.bindpw = args.get(SER_ROOT_PW, PW_DM)
        self.creation_suffix = args.get(SER_CREATION_SUFFIX, DEFAULT_SUFFIX)
        # These settings are only needed on a local connection.
        if self.isLocal:
            self.userid = args.get(SER_USER_ID)
            if not self.userid:
                if os.getuid() == 0:
                    # as root run as default user
                    self.userid = DEFAULT_USER
                else:
                    self.userid = pwd.getpwuid(os.getuid())[0]

            # Settings from args of server attributes
            self.serverid = args.get(SER_SERVERID_PROP, None)
            self.groupid = args.get(SER_GROUP_ID, self.userid)
            self.backupdir = args.get(SER_BACKUP_INST_DIR, DEFAULT_BACKUPDIR)
            # Allocate from the args, or use our env, or use /
            if args.get(SER_DEPLOYED_DIR, self.prefix) is not None:
                self.prefix = args.get(SER_DEPLOYED_DIR, self.prefix)
        self.realm = args.get(SER_REALM, None)
        if self.realm is not None:
            self.krb5_realm = MitKrb5(realm=self.realm, debug=self.verbose)

        # Those variables needs to be revisited (sroot for 64 bits)
        # self.sroot     = os.path.join(self.prefix, "lib/dirsrv")
        # self.errlog    = os.path.join(self.prefix,
        #                     "var/log/dirsrv/slapd-%s/errors" % self.serverid)

        # additional settings
        self.suffixes = {}
        self.agmt = {}

        self.state = DIRSRV_STATE_ALLOCATED
        if self.verbose:
            self.log.info("Allocate %s with %s:%s" % (self.__class__,
                                                      self.host,
                                                      (self.sslport or
                                                       self.port)))

    def openConnection(self, *args, **kwargs):
        """
        Open a new connection to our LDAP server
        *IMPORTANT*
        This is different to re-opening on the same dirsrv, as bugs in pyldap
        mean that ldap.set_option doesn't take effect! You need to use this
        to allow some of the start TLS options to work!
        """
        server = DirSrv(verbose=self.verbose)
        args_instance[SER_HOST] = self.host
        args_instance[SER_PORT] = self.port
        if self.sslport is not None:
            args_instance[SER_SECURE_PORT] = self.sslport
        args_instance[SER_SERVERID_PROP] = self.serverid
        args_standalone = args_instance.copy()
        server.allocate(args_standalone)
        server.open(*args, **kwargs)

        return server

    def list(self, all=False, serverid=None):
        """
            Returns a list dictionary. For a created instance that is on the
            local file system (e.g. <prefix>/etc/dirsrv/slapd-*), it exists
            a file describing its properties
            (environment): <prefix>/etc/sysconfig/dirsrv-<serverid> or
                           $HOME/.dirsrv/dirsv-<serverid>
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
        def test_and_set(prop, propname, variable, value):
            '''
                If variable is  'propname' it adds to
                'prop' dictionary the propname:value
            '''
            if variable == propname:
                prop[propname] = value
                return 1
            return 0

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
            prop[SER_DEPLOYED_DIR] = self.prefix
            myfile = open(filename, 'r')
            for line in myfile:
                # retrieve the value in line::
                #    <PROPNAME>=<string> [';' export <PROPNAME>]

                # skip comment lines
                if line.startswith('#'):
                    continue

                # skip lines without assignment
                if '=' not in line:
                    continue
                value = line.split(';', 1)[0]

                # skip lines without assignment
                if '=' not in value:
                    continue

                variable = value.split('=', 1)[0]
                value = value.split('=', 1)[1]
                value = value.strip(' \t\n')
                for property in (CONF_SERVER_DIR,
                                 CONF_SERVERBIN_DIR,
                                 CONF_CONFIG_DIR,
                                 CONF_INST_DIR,
                                 CONF_RUN_DIR,
                                 CONF_DS_ROOT,
                                 CONF_PRODUCT_NAME):
                    if test_and_set(prop, property, variable, value):
                        break

            # Now, we have passed the sysconfig environment file.
            #  read in and parse the dse.ldif to determine our SER_* values.
            # probably should use path join?
            dsefile = '%s/dse.ldif' % prop[CONF_CONFIG_DIR]
            if os.path.exists(dsefile):
                ldifconn = LDIFConn(dsefile)
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
                # SER_GROUP_ID        (groupid) ???
                # SER_DEPLOYED_DIR    (prefix) Already provided to for
                #                              discovery
                # SER_BACKUP_INST_DIR (backupdir) nsslapd-bakdir <<-- maybe?
                # We need to convert these two to int
                #  because other routines get confused if we don't
                for intkey in [SER_PORT, SER_SECURE_PORT]:
                    if prop[intkey] is not None:
                        prop[intkey] = int(prop[intkey])
            return prop

        def search_dir(instances, pattern, stop_value=None):
            '''
                It search all the files matching pattern.
                It there is not stop_value, it adds the properties found in
                each file to the 'instances'
                Else it searches the specific stop_value (instance's serverid)
                to add only its properties in the 'instances'

                @param instances - list of dictionary containing the instances
                                   properties
                @param pattern - pattern to find the files containing the
                                 properties
                @param stop_value - serverid value if we are looking only for
                                    one specific instance

                @return True or False - If stop_value is None it returns False.
                                        If stop_value is specified, it returns
                                        True if it added the property
                                        dictionary in instances. Or False if it
                                        did not find it.
            '''
            added = False
            for instance in glob.glob(pattern):
                serverid = os.path.basename(instance)[len(DEFAULT_ENV_HEAD):]

                # skip removed instance and admin server entry
                if '.removed' in serverid or 'dirsrv-admin' in instance:
                    continue

                # it is found, store its properties in the list
                if stop_value:
                    if stop_value == serverid:
                        instances.append(_parse_configfile(instance, serverid))
                        added = True
                        break
                    else:
                        # this is not the searched value, continue
                        continue
                else:
                    # we are not looking for a specific value, just add it
                    instances.append(_parse_configfile(instance, serverid))

            return added

        # Retrieves all instances under '/etc/sysconfig' and '/etc/dirsrv'

        # Instances/Environment are
        #
        #    file: /etc/sysconfig/dirsrv-<serverid>  (env)
        #    inst: /etc/dirsrv/slapd-<serverid>      (conf)
        #
        #    or
        #
        #    file: $HOME/.dirsrv/dirsrv-<serverid>       (env)
        #    inst: <prefix>/etc/dirsrv/slapd-<serverid>  (conf)
        #

        # Don't need a default value now since it's set in init.
        prefix = self.prefix
        if serverid is None and hasattr(self, 'serverid'):
            serverid = self.serverid

        # first identify the directories we will scan
        sysconfig_head = self.ds_paths.initconfig_dir
        privconfig_head = os.path.join(os.getenv('HOME'), ENV_LOCAL_DIR)
        if not os.path.isdir(sysconfig_head):
            privconfig_head = None
        if self.verbose:
            self.log.info("dir (sys) : %s" % sysconfig_head)
        if privconfig_head and self.verbose:
            self.log.info("dir (priv): %s" % privconfig_head)

        # list of the found instances
        instances = []

        # now prepare the list of instances properties
        if not all:
            # easy case we just look for the current instance

            # we have two location to retrieve the self.serverid
            # privconfig_head and sysconfig_head

            # first check the private repository
            if privconfig_head:
                pattern = "%s*" % os.path.join(privconfig_head,
                                               DEFAULT_ENV_HEAD)
                found = search_dir(instances, pattern, serverid)
                if self.verbose and len(instances) > 0:
                    self.log.info("List from %s" % privconfig_head)
                    for instance in instances:
                        self.log.info("list instance %r\n" % instance)
                if found:
                    assert len(instances) == 1
                else:
                    assert len(instances) == 0
            else:
                found = False

            # second, if not already found, search the system repository
            if not found:
                pattern = "%s*" % os.path.join(sysconfig_head,
                                               DEFAULT_ENV_HEAD)
                search_dir(instances, pattern, serverid)
                if self.verbose and len(instances) > 0:
                    self.log.info("List from %s" % privconfig_head)
                    for instance in instances:
                        self.log.info("list instance %r\n" % instance)

        else:
            # all instances must be retrieved
            if privconfig_head:
                pattern = "%s*" % os.path.join(privconfig_head,
                                               DEFAULT_ENV_HEAD)
                search_dir(instances, pattern)
                if self.verbose and len(instances) > 0:
                    self.log.info("List from %s" % privconfig_head)
                    for instance in instances:
                        self.log.info("list instance %r\n" % instance)

            pattern = "%s*" % os.path.join(sysconfig_head, DEFAULT_ENV_HEAD)
            search_dir(instances, pattern)
            if self.verbose and len(instances) > 0:
                self.log.info("List from %s" % privconfig_head)
                for instance in instances:
                    self.log.info("list instance %r\n" % instance)

        return instances

    def _createDirsrv(self):
        """Create a new instance of directory server

        @param self - containing the set properties

            SER_HOST            (host)
            SER_PORT            (port)
            SER_SECURE_PORT     (sslport)
            SER_ROOT_DN         (binddn)
            SER_ROOT_PW         (bindpw)
            SER_CREATION_SUFFIX (creation_suffix)
            SER_USER_ID         (userid)
            SER_SERVERID_PROP   (serverid)
            SER_GROUP_ID        (groupid)
            SER_DEPLOYED_DIR    (prefix)
            SER_BACKUP_INST_DIR (backupdir)
            SER_REALM           (krb5_realm)

        @return None

        @raise None

        }
        """

        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, CMD_PATH_SETUP_DS)

        if not os.path.isfile(prog):
            log.error("Can't find file: %r, removing extension" % prog)
            prog = prog[:-3]

        # Create and extract a service keytab
        args = {SER_HOST: self.host,
                SER_PORT: self.port,
                SER_SECURE_PORT: self.sslport,
                SER_ROOT_DN: self.binddn,
                SER_ROOT_PW: self.bindpw,
                SER_CREATION_SUFFIX: self.creation_suffix,
                SER_USER_ID: self.userid,
                SER_SERVERID_PROP: self.serverid,
                SER_GROUP_ID: self.groupid,
                SER_DEPLOYED_DIR: self.prefix,
                SER_BACKUP_INST_DIR: self.backupdir,
                SER_STRICT_HOSTNAME_CHECKING: self.strict_hostname}

        if self.inst_scripts is not None:
            args[SER_INST_SCRIPTS_ENABLED] = self.inst_scripts

        content = formatInfData(args)
        result = DirSrvTools.runInfProg(prog, content, self.verbose,
                                        prefix=self.prefix)
        if result != 0:
            raise Exception('Failed to run setup-ds.pl')
        if self.realm is not None:
            # This may conflict in some tests, we may need to use /etc/host
            # aliases or we may need to use server id
            self.krb5_realm.create_principal(principal='ldap/%s' % self.host)
            ktab = '%s/ldap.keytab' % (self.ds_paths.config_dir)
            self.krb5_realm.create_keytab(principal='ldap/%s' % self.host, keytab=ktab)
            with open('%s/dirsrv-%s' % (self.ds_paths.initconfig_dir, self.serverid), 'a') as sfile:
                sfile.write("\nKRB5_KTNAME=%s/etc/dirsrv/slapd-%s/"
                            "ldap.keytab\nexport KRB5_KTNAME\n" %
                            (self.prefix, self.serverid))
            self.restart()

            # Restart the instance

    def _createPythonDirsrv(self, version):
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
        slapd_options.set('defaults', version)

        slapd_options.verify()
        slapd = slapd_options.collect()

        # In order to work by "default" for tests, we need to create a backend.
        userroot = {
            'cn': 'userRoot',
            'nsslapd-suffix': self.creation_suffix,
            BACKEND_SAMPLE_ENTRIES: version,
        }
        backends = [userroot,]

        # Go!
        sds.create_from_args(general, slapd, backends, None)
        if self.realm is not None:
            # This may conflict in some tests, we may need to use /etc/host
            # aliases or we may need to use server id
            self.krb5_realm.create_principal(principal='ldap/%s' % self.host)
            ktab = '%s/ldap.keytab' % (self.ds_paths.config_dir)
            self.krb5_realm.create_keytab(principal='ldap/%s' % self.host, keytab=ktab)
            with open('%s/dirsrv-%s' % (self.ds_paths.initconfig_dir, self.serverid), 'a') as sfile:
                sfile.write("\nKRB5_KTNAME=%s/etc/dirsrv/slapd-%s/"
                            "ldap.keytab\nexport KRB5_KTNAME\n" %
                            (self.prefix, self.serverid))
            self.restart()

    def create(self, pyinstall=False, version=INSTALL_LATEST_CONFIG):
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
        if self.state != DIRSRV_STATE_ALLOCATED:
            raise ValueError("invalid state for calling create: %s" %
                             self.state)

        if self.exists():
            raise ValueError("Error it already exists the instance (%s)" %
                             self.list()[0][CONF_INST_DIR])

        if not self.serverid:
            raise ValueError("SER_SERVERID_PROP is missing, " +
                             "it is required to create an instance")

        # Check how we want to be installed.
        env_pyinstall = False
        if os.getenv('PYINSTALL', False) is not False:
            env_pyinstall = True
        # Time to create the instance and retrieve the effective sroot

        if (env_pyinstall or pyinstall):
            self._createPythonDirsrv(version)
        else:
            self._createDirsrv()

        # Retrieve sroot from the sys/priv config file
        assert(self.exists())
        self.sroot = self.list()[0][CONF_SERVER_DIR]

        # Now the instance is created but DirSrv is not yet connected to it
        self.state = DIRSRV_STATE_OFFLINE

    def delete(self):
        '''
            Deletes the instance with the parameters sets in dirsrv
            The state changes  -> DIRSRV_STATE_ALLOCATED

            @param self

            @return None

            @raise None
        '''

        # Grab all the instances now, before we potentially remove the last one
        insts = self.list(all=True)

        if self.state == DIRSRV_STATE_ONLINE:
            self.close()

        if not self.exists():
            raise ValueError("Error can not find instance %s[%s:%d]" %
                             (self.serverid, self.host, self.port))

        # Now time to remove the instance
        prog = os.path.join(self.ds_paths.sbin_dir, CMD_PATH_REMOVE_DS)
        if (not self.prefix or self.prefix == '/') and os.geteuid() != 0:
            raise ValueError("Error: without prefix deployment it is required to be root user")
        cmd = "%s -i %s%s" % (prog, DEFAULT_INST_HEAD, self.serverid)
        self.log.debug("running: %s " % cmd)
        try:
            os.system(cmd)
        except:
            log.exception("error executing %r" % cmd)

        # If this was the last instance being deleted, remove the DEFAULT_USER
        # if lib389 created the default user
        if os.getuid() == 0:
            # Only the root user could of added the entry
            if len(insts) == 1:
                # No more instances (this was the last one)
                if pwd.getpwnam(DEFAULT_USER).pw_gecos == DEFAULT_USER_COMMENT:
                    # We created this user, so we will delete it
                    cmd = ['/usr/sbin/userdel', DEFAULT_USER]
                    try:
                        subprocess.call(cmd)
                    except subprocess.CalledProcessError as e:
                        log.exception('Failed to delete default user ' +
                                      '(%s): error %s' % (DEFAULT_USER,
                                                          e.output))

        self.state = DIRSRV_STATE_ALLOCATED

    def open(self, saslmethod=None, sasltoken=None, certdir=None, starttls=False, connOnly=False, reqcert=ldap.OPT_X_TLS_HARD,
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

        ##################
        # WARNING: While you have a python ldap connection open some settings like
        # ldap.set_option MAY NOT WORK AS YOU EXPECT.
        # There are cases (especially CACERT/USERCERTS) where when one connection
        # is open set_option SILENTLY fails!!!!
        #
        # You MAY need to set post_open=False in your DirSrv start/restart instance!
        ##################

        uri = self.toLDAPURL()

        if certdir is None and self.isLocal:
            certdir = self.get_cert_dir()
            log.debug("Using dirsrv ca certificate %s" % certdir)

        if userkey is not None:
            # Note this sets LDAP.OPT not SELF. Because once self has opened
            # it can NOT change opts AT ALL.
            ldap.set_option(ldap.OPT_X_TLS_KEYFILE, userkey)
            log.debug("Using user private key %s" % userkey)
        if usercert is not None:
            # Note this sets LDAP.OPT not SELF. Because once self has opened
            # it can NOT change opts AT ALL.
            ldap.set_option(ldap.OPT_X_TLS_CERTFILE, usercert)
            log.debug("Using user certificate %s" % usercert)

        if certdir is not None:
            """
            We have a certificate directory, so lets start up TLS negotiations
            """
            # Note this sets LDAP.OPT not SELF. Because once self has opened
            # it can NOT change opts AT ALL.
            ldap.set_option(ldap.OPT_X_TLS_CACERTDIR, certdir)
            log.debug("Using external ca certificate %s" % certdir)

        if certdir or starttls:
            try:
                # Note this sets LDAP.OPT not SELF. Because once self has opened
                # it can NOT change opts on reused (ie restart)
                ldap.set_option(ldap.OPT_X_TLS_REQUIRE_CERT, reqcert)
                log.debug("Using certificate policy %s" % reqcert)
                log.debug("ldap.OPT_X_TLS_REQUIRE_CERT = %s" % reqcert)
            except ldap.LDAPError as e:
                log.fatal('TLS negotiation failed: %s' % str(e))
                raise e

        ## NOW INIT THIS. This MUST be after all the ldap.OPT set above,
        # so that we inherit the settings correctly!!!!
        if self.verbose:
            self.log.info('open(): Connecting to uri %s' % uri)
        if hasattr(ldap, 'PYLDAP_VERSION') and MAJOR >= 3:
            super(DirSrv, self).__init__(uri, bytes_mode=False, trace_level=TRACE_LEVEL)
        else:
            super(DirSrv, self).__init__(uri, trace_level=TRACE_LEVEL)

        if starttls and not uri.startswith('ldaps'):
            self.start_tls_s()

        if saslmethod and saslmethod.lower() == 'gssapi':
            """
            Perform kerberos/gssapi authentication
            """
            try:
                sasl_auth = ldap.sasl.gssapi("")
                self.sasl_interactive_bind_s("", sasl_auth)
            except ldap.LOCAL_ERROR as e:
                # No Ticket - ultimately invalid credentials
                log.debug("Error: No Ticket (%s)" % str(e))
                raise ldap.INVALID_CREDENTIALS
            except ldap.LDAPError as e:
                log.debug("SASL/GSSAPI Bind Failed: %s" % str(e))
                raise e

        elif saslmethod == 'EXTERNAL':
            # Do nothing.
            sasl_auth = ldap.sasl.external()
            self.sasl_interactive_bind_s("", sasl_auth)
        elif saslmethod and sasltoken is not None:
            # Just pass the sasltoken in!
            self.sasl_interactive_bind_s("", sasltoken)
        elif saslmethod:
            # Unknown or unsupported method
            log.debug('Unsupported SASL method: %s' % saslmethod)
            raise ldap.UNWILLING_TO_PERFORM

        elif self.can_autobind():
            # Connect via ldapi, and autobind.
            # do nothing: the bind is complete.
            if self.verbose:
                log.info("open(): Using root autobind ...")
            sasl_auth = ldap.sasl.external()
            self.sasl_interactive_bind_s("", sasl_auth)

        else:
            """
            Do a simple bind
            """
            try:
                self.simple_bind_s(ensure_str(self.binddn), self.bindpw)
            except ldap.SERVER_DOWN as e:
                # TODO add server info in exception
                log.debug("Cannot connect to %r" % uri)
                raise e
            except ldap.LDAPError as e:
                log.debug("Error: Failed to authenticate: %s", str(e))
                raise e

        """
        Authenticated, now finish the initialization
        """
        if self.verbose:
            log.info("open(): bound as %s" % self.binddn)
        if not connOnly:
            self.__initPart2()
        self.state = DIRSRV_STATE_ONLINE

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
            self.unbind_s()

        self.state = DIRSRV_STATE_OFFLINE

    def start(self, timeout=120, post_open=True):
        '''
            It starts an instance and rebind it. Its final state after rebind
            (open) is DIRSRV_STATE_ONLINE

            @param self
            @param timeout (in sec) to wait for successful start

            @return None

            @raise None
        '''

        if self.status() is True:
            return

        if self.with_systemd() and not self.containerised:
            # Do systemd things here ...
            subprocess.check_call(["/usr/bin/systemctl",
                                    "start",
                                    "dirsrv@%s" % self.serverid])
        else:
            # Start the process.
            # Wait for it to terminate
            # This means the server is probably ready to go ....
            env = {}
            if self.has_asan():
                log.error("NOTICE: Starting instance with ASAN options")
                log.error("This is probably not what you want. Please contact support.")
                log.error("ASAN options will be copied from your environment")
                env['ASAN_SYMBOLIZER_PATH'] = "/usr/bin/llvm-symbolizer"
                env['ASAN_OPTIONS'] = "symbolize=1 detect_deadlocks=1 log_path=%s/ns-slapd-%s.asan" % (self.ds_paths.run_dir, self.serverid)
                env.update(os.environ)
            subprocess.check_call(["%s/ns-slapd" % self.get_sbin_dir(),
                                    "-D",
                                    self.ds_paths.config_dir,
                                    "-i",
                                    self.ds_paths.pid_file], env=env)
            count = timeout
            pid = pid_from_file(self.ds_paths.pid_file)
            while (pid is None) and count > 0:
                count -= 1
                time.sleep(1)
                pid = pid_from_file(self.ds_paths.pid_file)
            if pid == 0 or pid is None:
                raise ValueError
            # Wait
            while not pid_exists(pid) and count > 0:
                # It looks like DS changes the value in here at some point ...
                # It's probably a DS bug, but if we "keep checking" the file, eventually
                # we get the main server pid, and it's ready to go.
                pid = pid_from_file(self.ds_paths.pid_file)
                time.sleep(1)
                count -= 1
            if not pid_exists(pid):
                raise Exception("Failed to start DS")
        if post_open:
            self.open()

    def stop(self, timeout=120):
        '''
            It stops an instance.
            It changes the state  -> DIRSRV_STATE_OFFLINE

            @param self
            @param timeout (in sec) to wait for successful stop

            @return None

            @raise None
        '''
        if self.status() is False:
            return

        if self.with_systemd() and not self.containerised:
            # Do systemd things here ...
            subprocess.check_call(["/usr/bin/systemctl",
                                    "stop",
                                    "dirsrv@%s" % self.serverid])
        else:
            # TODO: Make the pid path in the files things
            # TODO: use the status call instead!!!!
            count = timeout
            pid = pid_from_file(self.ds_paths.pid_file)
            if pid == 0 or pid is None:
                raise ValueError
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
        if self.with_systemd() and not self.containerised:
            # Do systemd things here ...
            rc = subprocess.call(["/usr/bin/systemctl",
                                    "is-active", "--quiet",
                                    "dirsrv@%s" % self.serverid])
            if rc == 0:
                return True
                # This .... probably will mess something up
                # self.state = DIRSRV_STATE_RUNNING
            self.state = DIRSRV_STATE_OFFLINE
            return False
        else:
            # TODO: Make the pid path in the files things
            # TODO: use the status call instead!!!!
            pid = pid_from_file(self.ds_paths.pid_file)
            if pid is None:
                # No pidfile yet ...
                self.state = DIRSRV_STATE_OFFLINE
                return False
            if pid == 0:
                self.state = DIRSRV_STATE_OFFLINE
                raise ValueError
            # Wait
            if not pid_exists(pid):
                self.state = DIRSRV_STATE_OFFLINE
                return False
            return True

    def restart(self, timeout=120, post_open=True):
        '''
            It restarts an instance and rebind it. Its final state after rebind
            (open) is DIRSRV_STATE_ONLINE.

            @param self
            @param timeout (in sec) to wait for successful stop

            @return None

            @raise None
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
                    log.info("clearBackupFS: fail to remove %s" % backup_file)
                    pass
        else:
            backup_dir, backup_pattern = self._infoBackupFS()
            list_backup_files = glob.glob(backup_pattern)
            for f in list_backup_files:
                try:
                    os.remove(f)
                except:
                    log.info("clearBackupFS: fail to remove %s" % backup_file)
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
                               (e.g. /var/lib/dirsrv/slapd-master/changelogdb)

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
        if self.prefix:
            os.chdir("%s/" % self.prefix)
            prefix_pattern = "%s/" % self.prefix
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
                    log.debug("backupFS b_dir = %s (%s) [name=%s]" %
                              (b_dir, self.prefix, name))
                    if prefix_pattern:
                        name = re.sub(prefix_pattern, '', name)

                    if os.path.isdir(name):
                        listFilesToBackup.append(name)
                        log.debug("backupFS add = %s (%s)" %
                                  (name, self.prefix))

                for file in files:
                    name = os.path.join(root, file)
                    if prefix_pattern:
                        name = re.sub(prefix_pattern, '', name)

                    if os.path.isfile(name):
                        listFilesToBackup.append(name)
                        log.debug("backupFS add = %s (%s)" %
                                  (name, self.prefix))

        # create the archive
        name = "backup_%s_%s.tar.gz" % (self.serverid, time.strftime("%m%d%Y_%H%M%S"))
        backup_file = os.path.join(backup_dir, name)
        tar = tarfile.open(backup_file, "w:gz")

        for name in listFilesToBackup:
            tar.add(name)
        tar.close()
        log.info("backupFS: archive done : %s" % backup_file)

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
            log.warning("Unable to restore the instance (missing backup)")
            raise ValueError("Unable to restore the instance (missing backup)")
        if not os.path.isfile(backup_file):
            log.warning("Unable to restore the instance (%s is not a file)" %
                        backup_file)
            raise ValueError("Unable to restore the instance " +
                             "(%s is not a file)" % backup_file)

        #
        # Second do some clean up
        #

        # previous db (it may exists new db files not in the backup)
        log.debug("restoreFS: remove subtree %s/*" % os.path.dirname(self.dbdir))
        for root, dirs, files in os.walk(os.path.dirname(self.dbdir)):
            for d in dirs:
                if d not in ("bak", "ldif"):
                    log.debug("restoreFS: before restore remove directory" +
                              " %s/%s" % (root, d))
                    shutil.rmtree("%s/%s" % (root, d))

        # previous error/access logs
        log.debug("restoreFS: remove error logs %s" % self.errlog)
        for f in glob.glob("%s*" % self.errlog):
                log.debug("restoreFS: before restore remove file %s" % (f))
                os.remove(f)
        log.debug("restoreFS: remove access logs %s" % self.accesslog)
        for f in glob.glob("%s*" % self.accesslog):
                log.debug("restoreFS: before restore remove file %s" % (f))
                os.remove(f)
        log.debug("restoreFS: remove audit logs %s" % self.accesslog)
        for f in glob.glob("%s*" % self.auditlog):
                log.debug("restoreFS: before restore remove file %s" % (f))
                os.remove(f)

        # Then restore from the directory where DS was deployed
        here = os.getcwd()
        if self.prefix:
            prefix_pattern = "%s/" % self.prefix
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
                    log.debug("restoreFS: restored %s" % member.name)
                    tar.extract(member.name)
                else:
                    log.debug("restoreFS: not restored %s (no write access)" %
                              member.name)
            else:
                log.debug("restoreFS: restored %s" % member.name)
                tar.extract(member.name)

        tar.close()

        #
        # Now be safe, triggers a recovery at restart
        #
        guardian_file = os.path.join(self.dbdir, "guardian")
        if os.path.isfile(guardian_file):
            try:
                log.debug("restoreFS: remove %s" % guardian_file)
                os.remove(guardian_file)
            except:
                log.warning("restoreFS: fail to remove %s" % guardian_file)
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
        host = self.host
        if self.ldapi_enabled == 'on' and self.ldapi_socket is not None:
            return "ldapi://%s" % (ldapurl.ldapUrlEscape(ensure_str(ldapi_socket)))
        elif self.ldapuri:
            return self.ldapuri
        elif self.sslport:
            return "ldaps://%s:%d/" % (ensure_str(host), self.sslport)
        else:
            return "ldap://%s:%d/" % (ensure_str(host), self.port)

    def can_autobind(self):
        """Check if autobind/LDAPI is enabled."""
        return self.ldapi_enabled == 'on' and self.ldapi_socket is not None and self.ldapi_autobind == 'on'

    def getServerId(self):
        """Return the server identifier."""
        return self.serverid

    def get_ldif_dir(self):
        """Return the server instance ldif directory."""
        return self.ds_paths.ldif_dir

    def get_bak_dir(self):
        """Return the server instance ldif directory."""
        return self.ds_paths.backup_dir

    def get_local_state_dir(self):
        return self.ds_paths.local_state_dir

    def get_config_dir(self):
        return self.ds_paths.config_dir

    def get_cert_dir(self):
        return self.ds_paths.cert_dir

    def get_sysconf_dir(self):
        return self.ds_paths.sysconf_dir

    def get_initconfig_dir(self):
        return self.ds_paths.initconfig_dir

    def get_sbin_dir(self):
        return self.ds_paths.sbin_dir

    def get_bin_dir(self):
        return self.ds_paths.bin_dir

    def get_plugin_dir(self):
        return self.ds_paths.plugin_dir

    def get_tmp_dir(self):
        return self.ds_paths.tmp_dir

    def has_asan(self):
        return self.ds_paths.asan_enabled

    def with_systemd(self):
        return self.ds_paths.with_systemd

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
        if self.verbose:
            log.debug("Retrieving entry with %r" % [args])
        if len(args) == 1 and 'scope' not in kwargs:
            args += (ldap.SCOPE_BASE, )

        res = self.search(*args, **kwargs)
        restype, obj = self.result(res)
        # TODO: why not test restype?
        if not obj:
            raise NoSuchEntryError("no such entry for %r" % [args])

        if self.verbose:
            log.info("Retrieved entry %s" % obj)
        if isinstance(obj, Entry):
            return obj
        else:  # assume list/tuple
            if obj[0] is None:
                raise NoSuchEntryError("Entry is None")
            return obj[0]

    def _test_entry(self, dn, scope=ldap.SCOPE_BASE):
        try:
            entry = self.getEntry(dn, scope)
            log.info("Found entry %s" % entry)
            return entry
        except NoSuchEntryError:
            log.exception("Entry %s was added successfully, but I cannot " +
                          "search it" % dn)
            raise MissingEntryError("Entry %s was added successfully, but " +
                                    "I cannot search it" % dn)

    def __wrapmethods(self):
        """This wraps all methods of SimpleLDAPObject, so that we can intercept
        the methods that deal with entries.  Instead of using a raw list of
        tuples of lists of hashes of arrays as the entry object, we want to
        wrap entries in an Entry class that provides some useful methods"""
        for name in dir(self.__class__.__bases__[0]):
            attr = getattr(self, name)
            if isinstance(attr, collections.Callable):
                setattr(self, name, wrapper(attr, name))

    def addLDIF(self, input_file, cont=False):
        class LDIFAdder(ldif.LDIFParser):
            def __init__(self, input_file, conn, cont=False,
                         ignored_attr_types=None, max_entries=0,
                         process_url_schemes=None
                         ):
                myfile = input_file
                if isinstance(input_file, six.string_types):
                    myfile = open(input_file, "r")
                self.conn = conn
                self.cont = cont
                ldif.LDIFParser.__init__(self, myfile, ignored_attr_types,
                                         max_entries, process_url_schemes)
                self.parse()
                if isinstance(input_file, six.string_types):
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
                    log.exception("Error: could not add entry %s" % dn)

        adder = LDIFAdder(input_file, self, cont)

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
            for dbf in six.itervalues(dbrec):
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
            log.error("chaining backend config already has the required ctrls")

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
            log.error("proxy aci already exists in suffix %s for %s" % (
                suffix, binddn))

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
            "cn=Multimaster Replication Plugin,cn=plugins,cn=config",
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
            log.warn("Entry %s already exists" % binddn)

        try:
            entry = self._test_entry(binddn, ldap.SCOPE_BASE)
            return entry
        except MissingEntryError:
            log.exception("This entry should exist!")
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
            - consumer is a DirSrv object (consumer can be a master)
            - cn_format - use this string to format the agreement name

        consumer:
            * a DirSrv object if chaining
            * an object with attributes: host, port, sslport, __str__

        args =  {
        'suffix': "dc=example,dc=com",
        'binddn': "cn=replrepl,cn=config",
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
            log.warning("createAgreement: suffix is missing")
            return None

        # get the RA binddn
        binddn = args.get('binddn')
        if not binddn:
            binddn = defaultProperties.get(REPLICATION_BIND_DN, None)
            if not binddn:
                # weird, internal error we do not retrieve the default
                # replication bind DN this replica agreement will fail
                # to update the consumer until the property will be set
                log.warning("createAgreement: binddn not provided and " +
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
                log.warning("createAgreement: bindpw not provided and " +
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
                log.warning("createAgreement: bindmethod not provided and " +
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
            log.warn("Agreement exists:", dn_agreement)
            self.suffixes.setdefault(nsuffix, {})[str(consumer)] = dn_agreement
            return dn_agreement
        if (nsuffix in self.agmt) and (consumer in self.agmt[nsuffix]):
            log.warn("Agreement exists:", dn_agreement)
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
            log.debug("Adding replica agreement: [%s]" % entry)
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
                if self.suffixes[nsuffix]['type'] == MASTER_TYPE:
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

        test_value = ('test replication from ' + self.serverid + ' to ' +
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
                    log.fatal('testReplication() failed to modify (%s), error (%s)' % (suffix, str(e)))
                    return False
                loop += 1
                time.sleep(2)
            if not replicated:
                log.fatal('Replication is not in sync with replica server (%s)'
                          % replica.serverid)
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
                type - replica type (MASTER_TYPE, HUB_TYPE, LEAF_TYPE) -
                       default is master
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
                eg. replicaSetupAll(self, suffix, type=MASTER_TYPE,
                    log=False, ...)
        """

        repArgs.setdefault('type', MASTER_TYPE)
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

        # enable changelog for master and hub
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
            log.warn("User already exists: %r " % user)

        # setup replica
        # map old style args to new style replica args
        if repArgs['type'] == MASTER_TYPE:
            repArgs['role'] = REPLICAROLE_MASTER
        elif repArgs['type'] == LEAF_TYPE:
            repArgs['role'] = REPLICAROLE_CONSUMER
        else:
            repArgs['role'] = REPLICAROLE_HUB
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
                if self.verbose:
                    print("created subtree pwpolicy entry", ent.dn)
            except ldap.ALREADY_EXISTS:
                print("subtree pwpolicy entry", ent.dn,
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
                if self.verbose:
                    print("created user pwpolicy entry", ent.dn)
            except ldap.ALREADY_EXISTS:
                print("user pwpolicy entry", ent.dn,
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
        for (attr, val) in six.iteritems(pwdpolicy):
            mods.append((ldap.MOD_REPLACE, attr, str(val)))
        if pwdargs:
            for (attr, val) in six.iteritems(pwdargs):
                mods.append((ldap.MOD_REPLACE, attr, str(val)))
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

        log.fatal('Failed to clear tmp directory (%s)' % filename)

    def upgrade(self, upgradeMode):
        """
        @param upgradeMode - the upgrade is either "online" or "offline"
        """
        if upgradeMode == 'online':
            online = True
        else:
            online = False
        DirSrvTools.runUpgrade(self.prefix, online)

    #
    # The following are the functions to perform offline scripts(when the
    # server is stopped)
    #
    def ldif2db(self, bename, suffixes, excludeSuffixes, encrypt,
                *import_files):
        """
        @param bename - The backend name of the database to import
        @param suffixes - List/tuple of suffixes to import
        @param excludeSuffixes - List/tuple of suffixes to exclude from import
        @param encrypt - Perform attribute encryption
        @param input_files - Files to import: file, file, file
        @return - True if import succeeded
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, 'ns-slapd')

        if not bename and not suffixes:
            log.error("ldif2db: backend name or suffix missing")
            return False

        for ldif in import_files:
            if not os.path.isfile(ldif):
                log.error("ldif2db: Can't find file: %s" % ldif)
                return False

        cmd = '%s ldif2db -D %s' % (prog, self.get_config_dir())
        if bename:
            cmd = cmd + ' -n ' + bename
        if suffixes:
            for suffix in suffixes:
                cmd = cmd + ' -s ' + suffix
        if excludeSuffixes:
            for excludeSuffix in excludeSuffixes:
                cmd = cmd + ' -x ' + excludeSuffix
        if encrypt:
            cmd = cmd + ' -E'
        for ldif in import_files:
            cmd = cmd + ' -i ' + ldif

        self.stop(timeout=10)
        log.info('Running script: %s' % cmd)
        result = True
        try:
            os.system(cmd)
        except:
            log.error("ldif2db: error executing %s" % cmd)
            result = False
        self.start(timeout=10)

        return result

    def db2ldif(self, bename, suffixes, excludeSuffixes, encrypt, repl_data,
                outputfile):
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

        if not bename and not suffixes:
            log.error("db2ldif: backend name or suffix missing")
            return False

        # The shell wrapper is not always reliable, so bypass it. We want to
        # kill it off anyway!
        cmd = '%s db2ldif -D %s' % (prog, self.get_config_dir())
        if bename:
            cmd = cmd + ' -n ' + bename
        if suffixes:
            for suffix in suffixes:
                cmd = cmd + ' -s ' + suffix
        if excludeSuffixes:
            for excludeSuffix in excludeSuffixes:
                cmd = cmd + ' -x ' + excludeSuffix
        if encrypt:
            cmd = cmd + ' -E'
        if repl_data:
            cmd = cmd + ' -r'
        if outputfile:
            cmd = cmd + ' -a ' + outputfile

        self.stop(timeout=10)
        log.info('Running script: %s' % cmd)
        result = True
        try:
            os.system(cmd)
        except:
            log.error("db2ldif: error executing %s" % cmd)
            result = False
        self.start(timeout=10)

        return result

    def bak2db(self, archive_dir, bename=None):
        """
        @param archive_dir - The directory containing the backup
        @param bename - The backend name to restore
        @return - True if the restore succeeded
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, BAK2DB)

        if not archive_dir:
            log.error("bak2db: backup directory missing")
            return False

        cmd = '%s %s -Z %s' % (prog, archive_dir, self.serverid)
        if bename:
            cmd = cmd + ' -n ' + bename

        self.stop(timeout=10)
        log.info('Running script: %s' % cmd)
        result = True
        try:
            os.system(cmd)
        except:
            log.error("bak2db: error executing %s" % cmd)
            result = False
        self.start(timeout=10)

        return result

    def db2bak(self, archive_dir):
        """
        @param archive_dir - The directory to write the backup to
        @return - True if the backup succeeded
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, DB2BAK)

        if not archive_dir:
            log.error("db2bak: backup directory missing")
            return False

        cmd = '%s %s -Z %s' % (prog, archive_dir, self.serverid)

        self.stop(timeout=10)
        log.info('Running script: %s' % cmd)
        result = True
        try:
            os.system(cmd)
        except:
            log.error("db2bak: error executing %s" % cmd)
            result = False
        self.start(timeout=10)

        return result

    def db2index(self, bename=None, suffixes=None, attrs=None, vlvTag=None):
        """
        @param bename - The backend name to reindex
        @param suffixes - List/tuple of suffixes to reindex
        @param attrs - List/tuple of the attributes to index
        @param vlvTag - The VLV index name to index
        @return - True if reindexing succeeded
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.sbin_dir, DB2INDEX)

        if not bename and not suffixes:
            log.error("db2index: missing required backend name or suffix")
            return False

        cmd = '%s -Z %s' % (prog, self.serverid)
        if bename:
            cmd = cmd + ' -n %s' % bename
        if suffixes:
            for suffix in suffixes:
                cmd = cmd + ' -s %s' % suffix
        if attrs:
            for attr in attrs:
                cmd = cmd + ' -t %s' % attr
        if vlvTag:
            cmd = cmd + ' -T %s' % vlvTag

        self.stop(timeout=10)
        log.info('Running script: %s' % cmd)
        result = True
        try:
            os.system(cmd)
        except:
            log.error("db2index: error executing %s" % cmd)
            result = False
        self.start(timeout=10)

        return result

    def dbscan(self, bename=None, index=None, key=None, width=None, isRaw=False):
        """
        @param bename - The backend name to scan
        @param index - index name (e.g., cn or cn.db) to scan
        @param key - index key to dump
        @param id - entry id to dump
        @param width - entry truncate size (bytes)
        @param isRaw - dump as raw data
        @return - dumped string
        """
        DirSrvTools.lib389User(user=DEFAULT_USER)
        prog = os.path.join(self.ds_paths.bin_dir, DBSCAN)

        if not bename:
            log.error("dbscan: missing required backend name")
            return False

        if not index:
            log.error("dbscan: missing required index name")
            return False
        elif '.db' in index:
            indexfile = os.path.join(self.dbdir, bename, index)
        else:
            indexfile = os.path.join(self.dbdir, bename, index + '.db')

        option = ''
        if 'id2entry' in index:
            if key and key.isdigit():
                option = ' -K %s' % key
        else:
            if key:
                option = ' -k %s' % key

        if width:
            option = option + ' -t %d' % width

        if isRaw:
            option = option + ' -R'

        cmd = '%s -f %s' % (prog, indexfile)

        if len(option) > 0:
            cmd = cmd + option

        self.stop(timeout=10)
        log.info('Running script: %s' % cmd)
        proc = Popen(cmd.split(), stdout=PIPE)
        outs = ''
        try:
            outs = proc.communicate()
        except OSError as e:
            log.exception('dbscan: error executing (%s): error %d - %s' %
                          (cmd, e.errno, e.strerror))
            raise e
        self.start(timeout=10)

        log.info('Output from ' + cmd)
        log.info(outs)
        return outs

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
                os.remove(log)
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
        try:
            os.system('%s -s %s -n %d -o %s' % (os.path.join(self.ds_paths.bin_dir, 'dbgen.pl'), suffix, num, ldif_file))
            os.chmod(ldif_file, 0o644)
            if os.getuid() == 0:
                # root user - chown the ldif to the server user
                uid = pwd.getpwnam(self.userid).pw_uid
                gid = grp.getgrnam(self.userid).gr_gid
                os.chown(ldif_file, uid, gid)
        except OSError as e:
            log.exception('Failed to create ldif file (%s): error %d - %s' %
                          (ldif_file, e.errno, e.strerror))
            raise e

    def getConsumerMaxCSN(self, replica_entry):
        """
        Attempt to get the consumer's maxcsn from its database
        """
        host = replica_entry.getValue(AGMT_HOST)
        port = replica_entry.getValue(AGMT_PORT)
        suffix = replica_entry.getValue(REPL_ROOT)
        error_msg = "Unavailable"

        # Open a connection to the consumer
        consumer = DirSrv(verbose=self.verbose)
        args_instance[SER_HOST] = host
        args_instance[SER_PORT] = int(port)
        args_instance[SER_ROOT_DN] = self.binddn
        args_instance[SER_ROOT_PW] = self.bindpw
        args_standalone = args_instance.copy()
        consumer.allocate(args_standalone)
        try:
            consumer.open()
        except ldap.LDAPError as e:
            self.log.debug('Connection to consumer (%s:%s) failed, error: %s' %
                           (host, port, str(e)))
            return error_msg

        # Get the replica id from supplier to compare to the consumer's rid
        try:
            replica_entries = self.replica.list(suffix)
            if not replica_entries:
                # Error
                consumer.close()
                return None
            rid = replica_entries[0].getValue(REPL_ID)
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
                return error_msg
            elements = entry[0].getValues('nsds50ruv')
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

    def getReplAgmtStatus(self, agmt_entry):
        '''
        Return the status message, if consumer is not in synch raise an
        exception
        '''
        agmt_maxcsn = None
        suffix = agmt_entry.getValue(REPL_ROOT)
        agmt_name = agmt_entry.getValue('cn')
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
        maxcsns = entry[0].getValues(AGMT_MAXCSN)
        for csn in maxcsns:
            comps = csn.split(';')
            if agmt_name == comps[1]:
                # same replica, get maxcsn
                if len(comps) < 6:
                    return "Consumer unavailable"
                else:
                    agmt_maxcsn = comps[5]

        if agmt_maxcsn:
            con_maxcsn = self.getConsumerMaxCSN(agmt_entry)
            if con_maxcsn:
                if agmt_maxcsn == con_maxcsn:
                    status = "In Synchronization"
                    rc = 0
                else:
                    # Not in sync - attmpt to discover the cause
                    repl_msg = "Unknown"
                    if agmt_entry.getValue(AGMT_UPDATE_IN_PROGRESS) == 'TRUE':
                        # Replication is on going - this is normal
                        repl_msg = "Replication still in progress"
                    elif "Can't Contact LDAP" in \
                         agmt_entry.getValue(AGMT_UPDATE_STATUS):
                        # Consumer is down
                        repl_msg = "Consumer can not be contacted"

                    status = ("Not in Synchronization: supplier " +
                              "(%s) consumer (%s)  Reason(%s)" %
                              (agmt_maxcsn, con_maxcsn, repl_msg))
        if rc != 0:
            raise ValueError(status)
        return status

    # This could be made to delete by filter ....
    def delete_branch_s(self, basedn, scope):
        ents = self.search_s(basedn, scope)
        for ent in ents:
            self.log.debug("Delete entry children %s" % (ent.dn))
            self.delete_s(ent.dn)
