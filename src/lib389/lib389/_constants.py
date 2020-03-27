# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import sys
import os
from enum import Enum, IntEnum
from lib389.properties import *

# current module object
mod = sys.modules[__name__]

(
    MASTER_TYPE,
    HUB_TYPE,
    LEAF_TYPE
) = list(range(3))

INSTALL_LATEST_CONFIG = '999999999'

REPLICA_FLAGS_CON = 0

TTL_DEFAULT_VAL = '86400'

# The structure is convenient for replica promote/demote methods
ReplicaRole = Enum("Replica role", "CONSUMER HUB MASTER STANDALONE")

CONSUMER_REPLICAID = 65535

REPLICA_RDONLY_TYPE = 2  # CONSUMER and HUB
REPLICA_WRONLY_TYPE = 1  # SINGLE and MULTI MASTER
REPLICA_RDWR_TYPE = REPLICA_RDONLY_TYPE | REPLICA_WRONLY_TYPE

REPLICA_FLAGS_RDONLY = 0
REPLICA_FLAGS_WRITE = 1

REPLICA_RUV_UUID = "ffffffff-ffffffff-ffffffff-ffffffff"
REPLICA_RUV_FILTER = ('(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectclass=nstombstone))')
REPLICA_OC_TOMBSTONE = "nsTombstone"
REPLICATION_BIND_DN = RA_BINDDN
REPLICATION_BIND_PW = RA_BINDPW
REPLICATION_BIND_METHOD = RA_METHOD
REPLICATION_TRANSPORT = RA_TRANSPORT_PROT
REPLICATION_TIMEOUT = RA_TIMEOUT

# Attributes that should be masked from logging output
SENSITIVE_ATTRS = ['userpassword',
                   'nsslapd-rootpw',
                   'nsds5replicacredentials',
                   'nsmultiplexorcredentials']

TRANS_STARTTLS = "starttls"
TRANS_SECURE = "secure"
TRANS_NORMAL = "normal"
REPL_TRANS_VALUE = {
            TRANS_STARTTLS: 'TLS',
            TRANS_SECURE: 'SSL',
            TRANS_NORMAL: 'LDAP'
        }

defaultProperties = {
            REPLICATION_BIND_DN: "cn=replication manager,cn=config",
            REPLICATION_BIND_PW: "password",
            REPLICATION_BIND_METHOD: "simple",
            REPLICATION_TRANSPORT: REPL_TRANS_VALUE[TRANS_NORMAL],
            REPLICATION_TIMEOUT: str(120)
        }


CFGSUFFIX = "o=NetscapeRoot"

# Some DN constants
DN_DM = "cn=Directory Manager"
PW_DM = "password"
DN_CONFIG = "cn=config"
DN_LDBM = "cn=ldbm database,cn=plugins,cn=config"
DN_CONFIG_LDBM = "cn=config,cn=ldbm database,cn=plugins,cn=config"
DN_USERROOT_LDBM = "cn=userRoot,cn=ldbm database,cn=plugins,cn=config"
DN_SCHEMA = "cn=schema"
DN_MONITOR = "cn=monitor"
DN_MONITOR_SNMP = "cn=snmp,cn=monitor"
DN_MONITOR_LDBM = "cn=monitor,cn=ldbm database,cn=plugins,cn=config"


CMD_PATH_SETUP_DS = "setup-ds.pl"
CMD_PATH_REMOVE_DS = "remove-ds.pl"
CMD_PATH_SETUP_DS_ADMIN = "setup-ds-admin.pl"
CMD_PATH_REMOVE_DS_ADMIN = "remove-ds-admin.pl"

# State of an DirSrv object
DIRSRV_STATE_INIT = 1
DIRSRV_STATE_ALLOCATED = 2
DIRSRV_STATE_OFFLINE = 3
DIRSRV_STATE_RUNNING = 4
DIRSRV_STATE_ONLINE = 5

# So uh  .... localhost.localdomain doesn't always exist. Stop. Using. It.
# LOCALHOST = "localhost.localdomain"
LOCALHOST_IP = "127.0.0.1"
LOCALHOST = "localhost"
LOCALHOST_SHORT = "localhost"
DEFAULT_PORT = 389
DEFAULT_SECURE_PORT = 636
DEFAULT_SUFFIX = 'dc=example,dc=com'
DEFAULT_SUFFIX_ESCAPED = 'dc\3Dexample\2Cdc\3Dcom'
DEFAULT_DOMAIN = 'example.com'
DEFAULT_BENAME = 'userRoot'  # warning it is case sensitive
DEFAULT_BACKUPDIR = '/tmp'
DEFAULT_INST_HEAD = 'slapd-'
DEFAULT_ENV_HEAD = 'dirsrv-'
DEFAULT_CHANGELOG_NAME = "changelog5"
DEFAULT_CHANGELOG_DB = 'changelogdb'

# CONF_DIR = 'etc/dirsrv'
# ENV_SYSCONFIG_DIR = '/etc/sysconfig'
ENV_LOCAL_DIR = '.dirsrv'

# CONFIG file (<prefix>/etc/sysconfig/dirsrv-* or
# $HOME/.dirsrv/dirsrv-*) keywords
CONF_SERVER_ID = 'SERVER_ID'
CONF_SERVER_DIR = 'SERVER_DIR'
CONF_SERVERBIN_DIR = 'SERVERBIN_DIR'
CONF_CONFIG_DIR = 'CONFIG_DIR'
CONF_INST_DIR = 'INST_DIR'
CONF_RUN_DIR = 'RUN_DIR'
CONF_DS_ROOT = 'DS_ROOT'
CONF_PRODUCT_NAME = 'PRODUCT_NAME'
CONF_LDAPI_ENABLED = 'LDAPI_ENABLED'
CONF_LDAPI_SOCKET = 'LDAPI_SOCKET'
CONF_LDAPI_AUTOBIND = 'LDAPI_AUTOBIND'
CONF_LDAPI_ROOTUSER = 'LDAPI_ROOTUSER'

DN_CONFIG = "cn=config"
DN_PLUGIN = "cn=plugins,%s" % DN_CONFIG
DN_MAPPING_TREE = "cn=mapping tree,%s" % DN_CONFIG
DN_CHANGELOG = "cn=changelog5,%s" % DN_CONFIG
DN_LDBM = "cn=ldbm database,%s" % DN_PLUGIN
DN_CHAIN = "cn=chaining database,%s" % DN_PLUGIN

DN_TASKS = "cn=tasks,%s" % DN_CONFIG
DN_INDEX_TASK = "cn=index,%s" % DN_TASKS
DN_EXPORT_TASK = "cn=export,%s" % DN_TASKS
DN_IMPORT_TASK = "cn=import,%s" % DN_TASKS
DN_BACKUP_TASK = "cn=backup,%s" % DN_TASKS
DN_RESTORE_TASK = "cn=restore,%s" % DN_TASKS
DN_MBO_TASK = "cn=memberOf task,%s" % DN_TASKS
DN_TOMB_FIXUP_TASK = "cn=fixup tombstones,%s" % DN_TASKS
DN_FIXUP_LINKED_ATTIBUTES = "cn=fixup linked attributes,%s" % DN_TASKS
DN_AUTOMEMBER_REBUILD_TASK = "cn=automember rebuild membership,%s" % DN_TASKS

# Script Constants
LDIF2DB = 'ldif2db'
DB2LDIF = 'db2ldif'
BAK2DB = 'bak2db'
DB2BAK = 'db2bak'
DB2INDEX = 'db2index'
DBSCAN = 'dbscan'

RDN_REPLICA = "cn=replica"

RETROCL_SUFFIX = "cn=changelog"

##################################
#
# Request Control OIDS
#
##################################
CONTROL_DEREF = '1.3.6.1.4.1.4203.666.5.16'

##################################
#
# Plugins
#
##################################

PLUGIN_7_BIT_CHECK = '7-bit check'
PLUGIN_ACCT_POLICY = 'Account Policy Plugin'
PLUGIN_ACCT_USABILITY = 'Account Usability Plugin'
PLUGIN_ACL = 'ACL Plugin'
PLUGIN_ACL_PREOP = 'ACL preoperation'
PLUGIN_ATTR_UNIQUENESS = 'attribute uniqueness'
PLUGIN_AUTOMEMBER = 'Auto Membership Plugin'
PLUGIN_CHAININGDB = 'chaining database'
PLUGIN_COLLATION = 'Internationalization Plugin'
PLUGIN_COS = 'Class of Service'
PLUGIN_DEREF = 'deref'
PLUGIN_DNA = 'Distributed Numeric Assignment Plugin'
PLUGIN_HTTP = 'HTTP Client'
PLUGIN_LINKED_ATTRS = 'Linked Attributes'
PLUGIN_MANAGED_ENTRY = 'Managed Entries'
PLUGIN_MEMBER_OF = 'MemberOf Plugin'
PLUGIN_PAM_PASSTHRU = 'PAM Pass Through Auth'
PLUGIN_PASSTHRU = 'Pass Through Authentication'
PLUGIN_POSIX_WINSYNC = 'Posix Winsync API'
PLUGIN_REFER_INTEGRITY = 'referential integrity postoperation'
PLUGIN_REPL_SYNC = 'Content Synchronization'
PLUGIN_REPLICATION_LEGACY = 'Legacy Replication Plugin'
PLUGIN_REPLICATION = 'Multimaster Replication Plugin'
PLUGIN_RETRO_CHANGELOG = 'Retro Changelog Plugin'
PLUGIN_ROLES = 'Roles Plugin'
PLUGIN_ROOTDN_ACCESS = 'RootDN Access Control'
PLUGIN_SCHEMA_RELOAD = 'Schema Reload'
PLUGIN_STATECHANGE = 'State Change Plugin'
PLUGIN_USN = 'USN'
PLUGIN_VIEWS = 'Views'
PLUGIN_WHOAMI = 'whoami'
PLUGIN_ADDN = 'addn'


#
# Constants
#
DEFAULT_USER = "dirsrv"
DEFAULT_USERHOME = "/tmp/lib389_home"
DEFAULT_USER_COMMENT = "lib389 DS user"
DATA_DIR = "data"
TMP_DIR = "tmp"
VALGRIND_WRAPPER = "ns-slapd.valgrind"
VALGRIND_LEAK_STR = " blocks are definitely lost in loss record "
VALGRIND_INVALID_STR = " Invalid (free|read|write)"
DISORDERLY_SHUTDOWN = ('Detected Disorderly Shutdown last time Directory '
                       'Server was running, recovering database')

#
# LOG: see https://access.redhat.com/documentation/en-US/Red_Hat_Directory
# _Server/10/html/Administration_Guide/Configuring_Logs.html
# The default log level is 16384
#
# It is legacy constants. Please, use IntEnum version (ErrorLog and AccessLog)
(LOG_TRACE,
 LOG_TRACE_PACKETS,
 LOG_TRACE_HEAVY,
 LOG_CONNECT,
 LOG_PACKET,
 LOG_SEARCH_FILTER,
 LOG_CONFIG_PARSER,
 LOG_ACL,
 LOG_ENTRY_PARSER,
 LOG_HOUSEKEEPING,
 LOG_REPLICA,
 LOG_DEFAULT,
 LOG_CACHE,
 LOG_PLUGIN,
 LOG_MICROSECONDS,
 LOG_ACL_SUMMARY) = [1 << x for x in (list(range(8)) + list(range(11, 19)))]


class ErrorLog(IntEnum):
    (TRACE,
     TRACE_PACKETS,
     TRACE_HEAVY,
     CONNECT,
     PACKET,
     SEARCH_FILTER,
     CONFIG_PARSER,
     ACL,
     ENTRY_PARSER,
     HOUSEKEEPING,
     REPLICA,
     DEFAULT,
     CACHE,
     PLUGIN,
     MICROSECONDS,
     ACL_SUMMARY) = [1 << x for x in (list(range(8)) + list(range(11, 19)))]


class AccessLog(IntEnum):
    NONE = 0
    INTERNAL = 4
    DEFAULT = 256  # Default log level
    ENTRY = 512
    MICROSECONDS = 131072

#
# Constants for individual tests
#
SUFFIX = 'dc=example,dc=com'
PASSWORD = 'password'

# Generate instance parameters on the fly
N=0
port_start = 38901
number_of_instances = 99

# Standalone topology
for i in range(port_start, port_start + number_of_instances):
    N+=1
    setattr(mod, "HOST_STANDALONE{0}".format(N), "LOCALHOST")
    setattr(mod, "PORT_STANDALONE{0}".format(N), i)
    setattr(mod, "SECUREPORT_STANDALONE{0}".format(N), i + 24700)
    setattr(mod, "SERVERID_STANDALONE{0}".format(N), "\"standalone{0}\"".format(N))
    setattr(mod, "REPLICAID_STANDALONE_{0}".format(N), 65535)

# For compatibility
HOST_STANDALONE = HOST_STANDALONE1
PORT_STANDALONE = PORT_STANDALONE1
SECUREPORT_STANDALONE = SECUREPORT_STANDALONE1
SERVERID_STANDALONE = SERVERID_STANDALONE1

# Replication topology - masters
N=0
port_start+=100
for i in range(port_start, port_start + number_of_instances):
    N+=1
    setattr(mod, "HOST_MASTER_{0}".format(N), "LOCALHOST")
    setattr(mod, "PORT_MASTER_{0}".format(N), i)
    setattr(mod, "SECUREPORT_MASTER_{0}".format(N), i + 24700)
    setattr(mod, "SERVERID_MASTER_{0}".format(N), "\"master{0}\"".format(N))
    setattr(mod, "REPLICAID_MASTER_{0}".format(N), N)

# Replication topology - hubs
N=0
port_start+=100
for i in range(port_start, port_start + number_of_instances):
    N+=1
    setattr(mod, "HOST_HUB_{0}".format(N), "LOCALHOST")
    setattr(mod, "PORT_HUB_{0}".format(N), i)
    setattr(mod, "SECUREPORT_HUB_{0}".format(N), i + 24700)
    setattr(mod, "SERVERID_HUB_{0}".format(N), "\"hub{0}\"".format(N))
    setattr(mod, "REPLICAID_HUB_{0}".format(N), 65535)

# Replication topology - consumers
N=0
port_start+=100
for i in range(port_start, port_start + number_of_instances):
    N+=1
    setattr(mod, "HOST_CONSUMER_{0}".format(N), "LOCALHOST")
    setattr(mod, "PORT_CONSUMER_{0}".format(N), i)
    setattr(mod, "SECUREPORT_CONSUMER_{0}".format(N), i + 24700)
    setattr(mod, "SERVERID_CONSUMER_{0}".format(N), "\"consumer{0}\"".format(N))

# Cleanup, we don't need to export that
del N, port_start, number_of_instances

# This is a template
args_instance = {SER_DEPLOYED_DIR: os.environ.get('PREFIX', None),
                 SER_BACKUP_INST_DIR: os.environ.get('BACKUPDIR',
                                                     DEFAULT_BACKUPDIR),
                 SER_ROOT_DN: DN_DM,
                 SER_ROOT_PW: PW_DM,
                 SER_HOST: LOCALHOST,
                 SER_PORT: DEFAULT_PORT,
                 SER_SERVERID_PROP: "template",
                 SER_CREATION_SUFFIX: DEFAULT_SUFFIX}

# Helper for linking dse.ldif values to the parse_config function
args_dse_keys = SER_PROPNAME_TO_ATTRNAME

DSRC_HOME = '~/.dsrc'
DSRC_CONTAINER = '/data/config/container.inf'
