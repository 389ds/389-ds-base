# replicatype @see https://access.redhat.com/knowledge/docs/en-US/Red_Hat_Directory_Server/8.1/html/Administration_Guide/Managing_Replication-Configuring-Replication-cmd.html
# 2 for consumers and hubs (read-only replicas)
# 3 for both single and multi-master suppliers (read-write replicas)

(MASTER_TYPE,
 HUB_TYPE,
 LEAF_TYPE) = range(3)
 
REPLICAROLE_MASTER    = "master"
REPLICAROLE_HUB       = "hub"
REPLICAROLE_CONSUMER  = "consumer"

CONSUMER_REPLICAID = 65535

REPLICA_RDONLY_TYPE = 2  # CONSUMER and HUB
REPLICA_WRONLY_TYPE = 1  # SINGLE and MULTI MASTER
REPLICA_RDWR_TYPE   = REPLICA_RDONLY_TYPE | REPLICA_WRONLY_TYPE

REPLICA_RUV_UUID    = "ffffffff-ffffffff-ffffffff-ffffffff"
REPLICA_OC_TOMBSTONE= "nsTombstone"
REPLICATION_BIND_DN     = 'replication_bind_dn'
REPLICATION_BIND_PW     = 'replication_bind_pw'
REPLICATION_BIND_METHOD = 'replication_bind_method'
REPLICATION_TRANSPORT   = 'replication_transport'
REPLICATION_TIMEOUT     = 'replication_timeout'

TRANS_STARTTLS  = "starttls"
TRANS_SECURE    = "secure"
TRANS_NORMAL    = "normal"
REPL_TRANS_VALUE = {TRANS_STARTTLS: 'TLS',
                    TRANS_SECURE:   'SSL',
                    TRANS_NORMAL:   'LDAP'}

defaultProperties = {
    REPLICATION_BIND_DN:        "cn=replrepl,cn=config",
    REPLICATION_BIND_PW:        "password",
    REPLICATION_BIND_METHOD:    "simple",
    REPLICATION_TRANSPORT:      REPL_TRANS_VALUE[TRANS_NORMAL],
    REPLICATION_TIMEOUT:        str(120)
}


CFGSUFFIX = "o=NetscapeRoot"
DEFAULT_USER = "nobody"

# Some DN constants
DN_DM = "cn=Directory Manager"
DN_CONFIG = "cn=config"
DN_LDBM = "cn=ldbm database,cn=plugins,cn=config"
DN_SCHEMA = "cn=schema"

CMD_PATH_SETUP_DS = "/setup-ds.pl"
CMD_PATH_REMOVE_DS = "/remove-ds.pl"

# State of an DirSrv object
DIRSRV_STATE_INIT='initial'
DIRSRV_STATE_ALLOCATED='allocated'
DIRSRV_STATE_OFFLINE='offline'
DIRSRV_STATE_ONLINE='online'

LOCALHOST = "localhost.localdomain"
DEFAULT_PORT        = 389
DEFAULT_SECURE_PORT = 636
DEFAULT_SUFFIX      = 'dc=example,dc=com'
DEFAULT_BENAME      = 'userRoot'    # warning it is case sensitive
DEFAULT_BACKUPDIR   = '/tmp'
DEFAULT_INST_HEAD   = 'slapd-'
DEFAULT_ENV_HEAD    = 'dirsrv-'
DEFAULT_CHANGELOG_NAME = "changelog5"
DEFAULT_CHANGELOG_DB   = 'changelogdb'

PW_DM = "password"

CONF_DIR = 'etc/dirsrv'
ENV_SYSCONFIG_DIR = '/etc/sysconfig'
ENV_LOCAL_DIR = '.dirsrv'


# CONFIG file (<prefix>/etc/sysconfig/dirsrv-* or $HOME/.dirsrv/dirsrv-*) keywords
CONF_SERVER_DIR    = 'SERVER_DIR'
CONF_SERVERBIN_DIR = 'SERVERBIN_DIR'
CONF_CONFIG_DIR    = 'CONFIG_DIR'
CONF_INST_DIR      = 'INST_DIR'
CONF_RUN_DIR       = 'RUN_DIR'
CONF_DS_ROOT       = 'DS_ROOT'
CONF_PRODUCT_NAME  = 'PRODUCT_NAME'


DN_CONFIG       = "cn=config"
DN_PLUGIN       = "cn=plugins,%s"       % DN_CONFIG
DN_MAPPING_TREE = "cn=mapping tree,%s"  % DN_CONFIG
DN_CHANGELOG    = "cn=changelog5,%s"    % DN_CONFIG
DN_LDBM         = "cn=ldbm database,%s" % DN_PLUGIN
DN_CHAIN        = "cn=chaining database,%s" % DN_PLUGIN

DN_TASKS           = "cn=tasks,%s"            % DN_CONFIG
DN_INDEX_TASK      = "cn=index,%s"            % DN_TASKS
DN_EXPORT_TASK     = "cn=export,%s"           % DN_TASKS
DN_IMPORT_TASK     = "cn=import,%s"           % DN_TASKS
DN_BACKUP_TASK     = "cn=backup,%s"           % DN_TASKS
DN_RESTORE_TASK    = "cn=restore,%s"          % DN_TASKS
DN_MBO_TASK        = "cn=memberOf task,%s"    % DN_TASKS
DN_TOMB_FIXUP_TASK = "cn=fixup tombstones,%s" % DN_TASKS

RDN_REPLICA     = "cn=replica"

RETROCL_SUFFIX = "cn=changelog"


##################################
###
### Plugins
###
##################################

PLUGIN_ACCT_POLICY        = 'Account Policy Plugin'
PLUGIN_ACCT_USABILITY     = 'Account Usability Plugin'
PLUGIN_ACL                = 'ACL Plugin'
PLUGIN_ACL_PREOP          = 'ACL preoperation'
PLUGIN_ATTR_UNIQUENESS    = 'attribute uniqueness'
PLUGIN_AUTOMEMBER         = 'Auto Membership Plugin'
PLUGIN_CHAININGDB         = 'chaining database'
PLUGIN_COLLATION          = 'Internationalization Plugin'
PLUGIN_COS                = 'Class of Service'
PLUGIN_DEREF              = 'deref'
PLUGIN_DNA                = 'Distributed Numeric Assignment Plugin'
PLUGIN_HTTP               = 'HTTP Client'
PLUGIN_LINKED_ATTRS       = 'Linked Attributes'
PLUGIN_MANAGED_ENTRY      = 'Managed Entries'
PLUGIN_MEMBER_OF          = 'MemberOf Plugin'
PLUGIN_PAM_PASSTHRU       = 'PAM Pass Through Auth'
PLUGIN_PASSTHRU           = 'Pass Through Authentication'
PLUGIN_POSIX_WINSYNC      = 'Posix Winsync API'
PLUGIN_REFER_INTEGRITY    = 'referential integrity postoperation'
PLUGIN_REPL_SYNC          = 'Content Synchronization'
PLUGIN_REPLICATION_LEGACY = 'Legacy Replication Plugin'
PLUGIN_REPLICATION        = 'Multimaster Replication Plugin'
PLUGIN_RETRO_CHANGELOG    = 'Retro Changelog Plugin'
PLUGIN_ROLES              = 'Roles Plugin'
PLUGIN_ROOTDN_ACCESS      = 'RootDN Access Control'
PLUGIN_SCHEMA_RELOAD      = 'Schema Reload'
PLUGIN_STATECHANGE        = 'State Change Plugin'
PLUGIN_USN                = 'USN'
PLUGIN_VIEWS              = 'Views'
PLUGIN_WHOAMI             = 'whoami'


#
# constants
#
DEFAULT_USER = "nobody"

#
# LOG: see https://access.redhat.com/site/documentation/en-US/Red_Hat_Directory_Server/9.0/html/Administration_Guide/Configuring_Logs.html
# The default log level is 16384
#
( 
LOG_TRACE,
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
LOG_ACL_SUMMARY) = [ 1<<x for x in ( range(8) + range(11,19)) ]
