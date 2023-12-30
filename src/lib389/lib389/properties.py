# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

####################################
#
# Properties supported by the server
#
####################################

#
# Those WITH related attribute name
#
SER_HOST = 'hostname'
SER_PORT = 'ldap-port'
SER_LDAP_URL = 'ldapurl'
SER_SECURE_PORT = 'ldap-secureport'
SER_ROOT_DN = 'root-dn'
SER_ROOT_PW = 'root-pw'
SER_USER_ID = 'user-id'
SER_CREATION_SUFFIX = 'suffix'
SER_LDAPI_ENABLED = 'ldapi_enabled'
SER_LDAPI_SOCKET = 'ldapi_socket'
SER_LDAPI_AUTOBIND = 'ldapi_autobind'
SER_INST_SCRIPTS_ENABLED = 'InstScriptsEnabled'
SER_DB_LIB = 'db_lib'

SER_PROPNAME_TO_ATTRNAME = {SER_HOST: 'nsslapd-localhost',
                            SER_PORT: 'nsslapd-port',
                            SER_SECURE_PORT: 'nsslapd-securePort',
                            SER_ROOT_DN: 'nsslapd-rootdn',
                            # SER_ROOT_PW: 'nsslapd-rootpw', # We can't use this value ...
                            SER_USER_ID: 'nsslapd-localuser',
                            SER_CREATION_SUFFIX: 'nsslapd-defaultnamingcontext',
                            SER_LDAPI_ENABLED: 'nsslapd-ldapilisten',
                            SER_LDAPI_SOCKET: 'nsslapd-ldapifilepath',
                            SER_LDAPI_AUTOBIND: 'nsslapd-ldapiautobind',
                            SER_DB_LIB: 'nsslapd-backend-implement',
                            }
#
# Those WITHOUT related attribute name
#
SER_SERVERID_PROP = 'server-id'
SER_GROUP_ID = 'group-id'
SER_DEPLOYED_DIR = 'deployed-dir'
SER_BACKUP_INST_DIR = 'inst-backupdir'
SER_STRICT_HOSTNAME_CHECKING = 'strict_hostname_checking'

####################################
#
# Properties supported by the Mapping Tree entries
#
####################################

# Properties name
MT_STATE = 'state'
MT_BACKEND = 'backend-name'
MT_SUFFIX = 'suffix'
MT_REFERRAL = 'referral'
MT_CHAIN_PATH = 'chain-plugin-path'
MT_CHAIN_FCT = 'chain-plugin-fct'
MT_CHAIN_UPDATE = 'chain-update-policy'
MT_PARENT_SUFFIX = 'parent-suffix'

# Properties values
MT_STATE_VAL_DISABLED = 'disabled'
MT_STATE_VAL_BACKEND = 'backend'
MT_STATE_VAL_REFERRAL = 'referral'
MT_STATE_VAL_REFERRAL_ON_UPDATE = 'referral-on-update'
MT_STATE_VAL_CONTAINER = 'container'
MT_STATE_TO_VALUES = {MT_STATE_VAL_DISABLED: 0,
                      MT_STATE_VAL_BACKEND: 1,
                      MT_STATE_VAL_REFERRAL: 2,
                      MT_STATE_VAL_REFERRAL_ON_UPDATE: 3,
                      MT_STATE_VAL_CONTAINER: 4}

MT_CHAIN_UPDATE_VAL_ON_UPDATE = 'repl_chain_on_update'
MT_OBJECTCLASS_VALUE = 'nsMappingTree'

MT_PROPNAME_TO_VALUES = {MT_STATE: MT_STATE_TO_VALUES}
MT_PROPNAME_TO_ATTRNAME = {MT_STATE: 'nsslapd-state',
                           MT_BACKEND: 'nsslapd-backend',
                           MT_SUFFIX: 'cn',
                           MT_PARENT_SUFFIX: 'nsslapd-parent-suffix',
                           MT_REFERRAL: 'nsslapd-referral',
                           MT_CHAIN_PATH: 'nsslapd-distribution-plugin',
                           MT_CHAIN_FCT: 'nsslapd-distribution-funct',
                           MT_CHAIN_UPDATE: 'nsslapd-distribution-root-update'}

####################################
#
# Properties supported by the backend
#
####################################
BACKEND_SUFFIX = 'suffix'
BACKEND_NAME = 'name'
BACKEND_READONLY = 'read-only'
BACKEND_REQ_INDEX = 'require-index'
BACKEND_CACHE_ENTRIES = 'entry-cache-number'
BACKEND_CACHE_SIZE = 'entry-cache-size'
BACKEND_DNCACHE_SIZE = 'dn-cache-size'
BACKEND_DIRECTORY = 'directory'
BACKEND_DB_DEADLOCK = 'db-deadlock'
BACKEND_CHAIN_BIND_DN = 'chain-bind-dn'
BACKEND_CHAIN_BIND_PW = 'chain-bind-pw'
BACKEND_CHAIN_URLS = 'chain-urls'
BACKEND_STATS = 'stats'
BACKEND_SAMPLE_ENTRIES = 'sample_entries'
BACKEND_OBJECTCLASS_VALUE = 'nsBackendInstance'
BACKEND_REPL_ENABLED = 'enable_replication'
BACKEND_REPL_ROLE = 'replica_role'
BACKEND_REPL_ID = 'replica_id'
BACKEND_REPL_BINDDN = 'replica_binddn'
BACKEND_REPL_BINDPW = 'replica_bindpw'
BACKEND_REPL_BINDGROUP = 'replica_bindgroup'
BACKEND_REPL_CL_MAX_ENTRIES = "changelog_max_entries"
BACKEND_REPL_CL_MAX_AGE = "changelog_max_age"

# THIS NEEDS TO BE REMOVED. HACKS!!!!
BACKEND_PROPNAME_TO_ATTRNAME = {BACKEND_SUFFIX: 'nsslapd-suffix',
                                BACKEND_NAME: 'cn',
                                BACKEND_READONLY: 'nsslapd-readonly',
                                BACKEND_REQ_INDEX: 'nsslapd-require-index',
                                BACKEND_CACHE_ENTRIES: 'nsslapd-cachesize',
                                BACKEND_CACHE_SIZE: 'nsslapd-cachememsize',
                                BACKEND_DNCACHE_SIZE: 'nsslapd-dncachememsize',
                                BACKEND_DIRECTORY: 'nsslapd-directory',
                                BACKEND_DB_DEADLOCK:
                                    'nsslapd-db-deadlock-policy',
                                BACKEND_CHAIN_BIND_DN: 'nsmultiplexorbinddn',
                                BACKEND_CHAIN_BIND_PW:
                                    'nsmultiplexorcredentials',
                                BACKEND_CHAIN_URLS: 'nsfarmserverurl'}

####################################
#
# Properties of a replica
#
####################################


#
# General attributes (for both replica and agmt)
#
REPL_RUV = 'nsds50ruv'
REPL_ROOT = 'nsDS5ReplicaRoot'
REPL_PROTOCOL_TIMEOUT = 'nsds5ReplicaProtocolTimeout'
REPL_BINDDN = 'nsDS5ReplicaBindDN'
REPL_REAP_ACTIVE = 'nsds5ReplicaReapActive'

#
# Replica Entry Attributes
#
REPL_PURGE_DELAY = 'nsds5ReplicaPurgeDelay'
REPL_RETRY_MIN = 'nsds5ReplicaBackoffMin'
REPL_RETRY_MAX = 'nsds5ReplicaBackoffMax'
REPL_TYPE = 'nsDS5ReplicaType'
REPL_FLAGS = 'nsDS5Flags'
REPL_ID = 'nsDS5ReplicaId'
REPL_STATE = 'nsState'
REPL_NAME = 'nsDS5ReplicaName'
REPL_BIND_GROUP = 'nsds5replicabinddngroup'
REPL_BIND_GROUP_INTERVAL = 'nsds5replicabinddngroupcheckinterval'
REPL_REF = 'nsds5ReplicaReferral'
REPL_TOMBSTONE_PURGE_INTERVAL = 'nsds5ReplicaTombstonePurgeInterval'
REPL_CLEAN_RUV = 'nsds5ReplicaCleanRUV'
REPL_ABORT_RUV = 'nsds5ReplicaAbortCleanRUV'
REPL_COUNT_COUNT = 'nsds5ReplicaChangeCount'
REPL_PRECISE_PURGE = 'nsds5ReplicaPreciseTombstonePurging'
REPL_RELEASE_TIMEOUT = 'nsds5replicaReleaseTimeout'

# The values are from the REST API
REPLICA_SUFFIX = 'suffix'
REPLICA_PURGE_DELAY = 'ReplicaPurgeDelay'
REPLICA_ROOT = 'ReplicaRoot'
REPLICA_PROTOCOL_TIMEOUT = 'ReplicaProtocolTimeout'
REPLICA_BINDDN = 'ReplicaBindDN'
REPLICA_REAP_ACTIVE = 'ReplicaReapActive'
REPLICA_RETRY_MIN = 'ReplicaBackoffMin'
REPLICA_RETRY_MAX = 'ReplicaBackoffMax'
REPLICA_TYPE = 'ReplicaType'
REPLICA_FLAGS = 'ReplicaFlags'
REPLICA_ID = 'Replicaid'
REPLICA_STATE = 'ReplicaState'
REPLICA_NAME = 'ReplicaName'
REPLICA_BIND_GROUP = 'ReplicaBindDNGroup'
REPLICA_BIND_GROUP_INTERVAL = 'ReplicaBindDNGroupCheckInterval'
REPLICA_REFERRAL = 'ReplicaReferral'
REPLICA_TOMBSTONE_PURGE_INTERVAL = 'ReplicaTombstonePurgeInterval'
REPLICA_PURGE_INTERVAL = REPLICA_TOMBSTONE_PURGE_INTERVAL
REPLICA_CLEAN_RUV = 'ReplicaCleanRUV'
REPLICA_ABORT_RUV = 'ReplicaAbortCleanRUV'
REPLICA_COUNT_COUNT = 'ReplicaChangeCount'
REPLICA_PRECISE_PURGING = 'ReplicaPreciseTombstonePurging'
REPLICA_RELEASE_TIMEOUT = 'ReplicaReleaseTimeout'

REPLICA_PROPNAME_TO_ATTRNAME = {REPLICA_SUFFIX: REPL_ROOT,
                                REPLICA_ROOT: REPL_ROOT,
                                REPLICA_ID: REPL_ID,
                                REPLICA_TYPE: REPL_TYPE,
                                REPLICA_PURGE_INTERVAL:
                                    REPL_TOMBSTONE_PURGE_INTERVAL,
                                REPLICA_PURGE_DELAY: REPL_PURGE_DELAY,
                                REPLICA_REFERRAL: REPL_REF,
                                REPLICA_PROTOCOL_TIMEOUT:
                                    REPL_PROTOCOL_TIMEOUT,
                                REPLICA_BINDDN: REPL_BINDDN,
                                REPLICA_REAP_ACTIVE: REPL_REAP_ACTIVE,
                                REPLICA_RETRY_MIN: REPL_RETRY_MIN,
                                REPLICA_RETRY_MAX: REPL_RETRY_MAX,
                                REPLICA_FLAGS: REPL_FLAGS,
                                REPLICA_STATE: REPL_STATE,
                                REPLICA_NAME: REPL_NAME,
                                REPLICA_BIND_GROUP: REPL_BIND_GROUP,
                                REPLICA_BIND_GROUP_INTERVAL:
                                    REPL_BIND_GROUP_INTERVAL,
                                REPLICA_TOMBSTONE_PURGE_INTERVAL:
                                    REPL_TOMBSTONE_PURGE_INTERVAL,
                                REPLICA_CLEAN_RUV: REPL_CLEAN_RUV,
                                REPLICA_ABORT_RUV: REPL_ABORT_RUV,
                                REPLICA_COUNT_COUNT: REPL_COUNT_COUNT,
                                REPLICA_PRECISE_PURGING: REPL_PRECISE_PURGE,
                                REPLICA_RELEASE_TIMEOUT: REPL_RELEASE_TIMEOUT}

####################################
#
# Properties of a changelog
#
####################################


#
# Changelog Attributes
#
CL_DIR = 'nsslapd-changelogdir'
CL_MAX_ENTRIES = 'nsslapd-changelogmaxentries'
CL_MAXAGE = 'nsslapd-changelogmaxage'
CL_COMPACT_INTERVAL = 'nsslapd-changelogcompactdb-interval'
CL_TRIM_INTERVAL = 'nsslapd-changelogtrim-interval'
CL_CONCURRENT_WRITES = 'nsslapd-changelogmaxconcurrentwrites'
CL_ENCRYPT_ALG = 'nsslapd-encryptionalgorithm'
CL_SYM_KEY = 'nsSymmetricKey'

# REST names
CHANGELOG_NAME = 'cl-name'
CHANGELOG_DIR = 'ChangelogDir'
CHANGELOG_MAXAGE = 'ChangelogMaxAge'
CHANGELOG_MAXENTRIES = 'ChangelogMaxEntries'
CHANGELOG_TRIM_INTERVAL = 'ChangelogTrimInterval'
CHANGELOG_COMPACT_INTV = 'ChangelogCompactDBInterval'
CHANGELOG_CONCURRENT_WRITES = 'ChangelogMaxConcurrentWrites'
CHANGELOG_ENCRYPT_ALG = 'ChangelogEncryptionAlgorithm'
CHANGELOG_SYM_KEY = 'ChangelogSymmetricKey'

CL_DIR = 'nsslapd-changelogdir'
CL_MAX_ENTRIES = 'nsslapd-changelogmaxentries'
CL_MAXAGE = 'nsslapd-changelogmaxage'
CL_COMPACT_INTERVAL = 'nsslapd-changelogcompactdb-interval'
CL_TRIM_INTERVAL = 'nsslapd-changelogtrim-interval'
CL_CONCURRENT_WRITES = 'nsslapd-changelogmaxconcurrentwrites'
CL_ENCRYPT_ALG = 'nsslapd-encryptionalgorithm'
CL_SYM_KEY = 'nsSymmetricKey'

CHANGELOG_PROPNAME_TO_ATTRNAME = {CHANGELOG_NAME: 'cn',
                                  CHANGELOG_DIR: CL_DIR,
                                  CHANGELOG_MAXAGE: CL_MAXAGE,
                                  CHANGELOG_MAXENTRIES: CL_MAX_ENTRIES,
                                  CHANGELOG_TRIM_INTERVAL: CL_TRIM_INTERVAL,
                                  CHANGELOG_COMPACT_INTV: CL_COMPACT_INTERVAL,
                                  CHANGELOG_CONCURRENT_WRITES:
                                      CL_CONCURRENT_WRITES,
                                  CHANGELOG_ENCRYPT_ALG: CL_ENCRYPT_ALG,
                                  CHANGELOG_SYM_KEY: CL_SYM_KEY}

####################################
#
# Properties of an entry
#
####################################
ENTRY_OBJECTCLASS = 'objectclass'
ENTRY_SN = 'sn'
ENTRY_CN = 'cn'
ENTRY_TYPE_PERSON = 'person'
ENTRY_TYPE_INETPERSON = 'inetperson'
ENTRY_TYPE_GROUP = 'group'
ENTRY_USERPASSWORD = 'userpassword'
ENTRY_UID = 'uid'

ENTRY_TYPE_TO_OBJECTCLASS = {ENTRY_TYPE_PERSON: ["top",
                                                 "person"],
                             ENTRY_TYPE_INETPERSON: ["top",
                                                     "person",
                                                     "inetOrgPerson"]}

####################################
#
# Properties supported by the replica agreement
#
####################################

#
# Repl Agreement Attributes
#
AGMT_BIND_METHOD = 'nsDS5ReplicaBindMethod'
AGMT_HOST = 'nsDS5ReplicaHost'
AGMT_PORT = 'nsDS5ReplicaPort'
AGMT_TRANSPORT_INFO = 'nsDS5ReplicaTransportInfo'
AGMT_TRANSPORT_URI = 'nsDS5ReplicaTransportUri'
AGMT_TRANSPORT_CA_URI = 'nsDS5ReplicaTransportCAUri'
AGMT_ATTR_LIST = 'nsDS5ReplicatedAttributeList'
AGMT_ATTR_LIST_TOTAL = 'nsDS5ReplicatedAttributeListTotal'
AGMT_TIMEOUT = 'nsds5replicaTimeout'
AGMT_CRED = 'nsDS5ReplicaCredentials'
AGMT_REFRESH = 'nsds5BeginReplicaRefresh'
AGMT_SCHEDULE = 'nsds5ReplicaUpdateSchedule'
AGMT_BUSY_WAIT_TIME = 'nsds5ReplicaBusyWaitTime'
AGMT_SESSION_PAUSE_TIME = 'nsds5ReplicaSessionPauseTime'
AGMT_ENABLED = 'nsds5ReplicaEnabled'
AGMT_STRIP_ATTRS = 'nsds5ReplicaStripAttrs'
AGMT_FLOW_WINDOW = 'nsds5ReplicaFlowControlWindow'
AGMT_FLOW_PAUSE = 'nsds5ReplicaFlowControlPause'
AGMT_MAXCSN = 'nsds5AgmtMaxCSN'
AGMT_UPDATE_START = 'nsds5replicaLastUpdateStart'
AGMT_UPDATE_END = 'nsds5replicaLastUpdateEnd'
AGMT_CHANGES_SINCE_STARTUP = 'nsds5replicaChangesSentSinceStartup'  # base64
AGMT_UPDATE_STATUS = 'nsds5replicaLastUpdateStatus'
AGMT_UPDATE_STATUS_JSON = 'nsds5replicaLastUpdateStatusJSON'
AGMT_UPDATE_IN_PROGRESS = 'nsds5replicaUpdateInProgress'
AGMT_INIT_START = 'nsds5replicaLastInitStart'
AGMT_INIT_END = 'nsds5replicaLastInitEnd'
AGMT_INIT_STATUS = 'nsds5replicaLastInitStatus'

#
# WinSync Agreement Attributes
#
AGMT_WINSYNC_SUBTREE = 'nsds7WindowsReplicaSubtree'
AGMT_WINSYNC_REPLICA_SUBTREE = 'nsds7DirectoryReplicaSubtree'
AGMT_WINSYNC_NEW_USERSYNC = 'nsds7NewWinUserSyncEnabled'
AGMT_WINSYNC_NEW_GROUP_SYNC = 'nsds7NewWinGroupSyncEnabled'
AGMT_WINSYNC_DOMAIN = 'nsds7WindowsDomain'
AGMT_WINSYNC_COOKIE = 'nsds7DirsyncCookie'
AGMT_WINSYNC_INTERVAL = 'winSyncInterval'
AGMT_WINSYNC_ONE_WAY_SYNC = 'oneWaySync'
AGMT_WINSYNC_MOVE_ACTION = 'winSyncMoveAction'
AGMT_WINSYNC_WIN_FILTER = 'winSyncWindowsFilter'
AGMT_WINSYNC_DIR_FILTER = 'winSyncDirectoryFilter'
AGMT_WINSYNC_SUBTREE_PAIR = 'winSyncSubtreePair'

RA_NAME = 'ReplicaName'
RA_SUFFIX = 'ReplicaRoot'
RA_BINDDN = 'ReplicaBindDN'
RA_BINDPW = 'ReplicaBindCredentials'
RA_METHOD = 'ReplicaBindMethod'
RA_DESCRIPTION = 'description'
RA_SCHEDULE = 'ReplicaSchedule'
RA_TRANSPORT_PROT = 'ReplicaTransportInfo'
RA_TRANSPORT_URI = 'ReplicaTransportUri'
RA_TRANSPORT_CA_URI = 'ReplicaTransportCAUri'
RA_FRAC_EXCLUDE = 'ReplicatedAttributeList'
RA_FRAC_EXCLUDE_TOTAL_UPDATE = 'ReplicatedAttributeListTotal'
RA_FRAC_STRIP = 'ReplicaStripAttrs'
RA_CONSUMER_PORT = 'ReplicaPort'
RA_CONSUMER_HOST = 'ReplicaHost'
RA_CONSUMER_TOTAL_INIT = 'ReplicaBeginRefresh'
RA_TIMEOUT = 'ReplicaConnTimeout'
RA_CHANGES = 'ReplicaChangesSentSinceStartup'
RA_BUSY_WAIT = 'ReplicaBusyWaitTime'
RA_PAUSE_TIME = 'ReplicaSessionPauseTime'
RA_ENABLED = 'ReplicaEnabled'
RA_FLOW_WINDOW = 'ReplicaFlowControlWindow'
RA_FLOW_PAUSE = 'ReplicaFlowControlPause'
RA_RUV = 'ReplicaRUV'
RA_MAXCSN = 'ReplicaMaxCSN'
RA_LAST_UPDATE_START = 'ReplicaLastUpdateStart'
RA_LAST_UPDATE_END = 'ReplicaLastUpdateEnd'
RA_LAST_UPDATE_STATUS = 'ReplicaLastUpdateStatus'
RA_UPDATE_IN_PROGRESS = 'ReplicaUpdateInProgress'
RA_LAST_INIT_START = 'ReplicaLastInitStart'
RA_LAST_INIT_END = 'ReplicaLastInitEnd'
RA_LAST_INIT_STATUS = 'ReplicaLastInitStatus'

REPLICA_OBJECTCLASS_VALUE = 'nsds5Replica'
RA_OBJECTCLASS_VALUE = "nsds5replicationagreement"
RA_WINDOWS_OBJECTCLASS_VALUE = "nsDSWindowsReplicationAgreement"

RA_PROPNAME_TO_ATTRNAME = {RA_NAME: 'cn',
                           RA_SUFFIX: REPL_ROOT,
                           RA_BINDDN: REPL_BINDDN,
                           RA_BINDPW: AGMT_CRED,
                           RA_METHOD: AGMT_BIND_METHOD,
                           RA_DESCRIPTION: 'description',
                           RA_SCHEDULE: AGMT_SCHEDULE,
                           RA_TRANSPORT_PROT: AGMT_TRANSPORT_INFO,
                           RA_FRAC_EXCLUDE: AGMT_ATTR_LIST,
                           RA_FRAC_EXCLUDE_TOTAL_UPDATE: AGMT_ATTR_LIST_TOTAL,
                           RA_FRAC_STRIP: AGMT_STRIP_ATTRS,
                           RA_CONSUMER_PORT: AGMT_PORT,
                           RA_CONSUMER_HOST: AGMT_HOST,
                           RA_CONSUMER_TOTAL_INIT: AGMT_REFRESH,
                           RA_TIMEOUT: AGMT_TIMEOUT,
                           RA_CHANGES: AGMT_CHANGES_SINCE_STARTUP,
                           RA_BUSY_WAIT: AGMT_BUSY_WAIT_TIME,
                           RA_PAUSE_TIME: AGMT_SESSION_PAUSE_TIME,
                           RA_ENABLED: AGMT_ENABLED,
                           RA_FLOW_WINDOW: AGMT_FLOW_WINDOW,
                           RA_FLOW_PAUSE: AGMT_FLOW_PAUSE,
                           RA_RUV: REPL_RUV,
                           RA_MAXCSN: AGMT_MAXCSN,
                           RA_LAST_UPDATE_START: AGMT_UPDATE_START,
                           RA_LAST_UPDATE_END: AGMT_UPDATE_END,
                           RA_LAST_UPDATE_STATUS: AGMT_UPDATE_STATUS,
                           RA_UPDATE_IN_PROGRESS: AGMT_UPDATE_IN_PROGRESS,
                           RA_LAST_INIT_START: AGMT_INIT_START,
                           RA_LAST_INIT_END: AGMT_INIT_END,
                           RA_LAST_INIT_STATUS: AGMT_INIT_STATUS,
                           REPLICA_REAP_ACTIVE: REPL_REAP_ACTIVE}

####################################
#
# Properties supported by the plugins
#
####################################

PLUGIN_NAME = "name"
PLUGIN_PATH = "path"
PLUGIN_ENABLE = 'enable'

PLUGINS_OBJECTCLASS_VALUE = "nsSlapdPlugin"
PLUGINS_ENABLE_ON_VALUE = "on"
PLUGINS_ENABLE_OFF_VALUE = "off"

PLUGIN_PROPNAME_TO_ATTRNAME = {PLUGIN_NAME: 'cn',
                               PLUGIN_PATH: 'nsslapd-pluginPath',
                               PLUGIN_ENABLE: 'nsslapd-pluginEnabled'}

####################################
#
# Properties supported by the index
#
####################################
INDEX_TYPE = "type"
INDEX_SYSTEM = "system"
INDEX_MATCHING_RULE = "matching-rule"

INDEX_PROPNAME_TO_ATTRNAME = {INDEX_TYPE: 'nsIndexType',
                              INDEX_SYSTEM: 'nsSystemIndex',
                              INDEX_MATCHING_RULE: 'nsMatchingRule'}

####################################
#
# Properties supported by the tasks
#
####################################

TASK_WAIT = "wait"
TASK_TOMB_STRIP = "strip-csn"
EXPORT_REPL_INFO = "repl-info"


####################################
#
# Properties related to logging.
#
####################################

LOG_ACCESS_ENABLED = "nsslapd-accesslog-logging-enabled"
LOG_ACCESS_LEVEL = "nsslapd-accesslog-level"
LOG_ACCESS_PATH = "nsslapd-accesslog"

LOG_ERROR_ENABLED = "nsslapd-errorlog-logging-enabled"
LOG_ERROR_LEVEL = "nsslapd-errorlog-level"
LOG_ERROR_PATH = "nsslapd-errorlog"


def rawProperty(prop):
    '''
        Return the string 'prop' without the heading '+'/'-'
        @param prop - string of a property name

        @return property name without heading '+'/'-'

        @raise None
    '''
    if str(prop).startswith('+') or str(prop).startswith('-'):
        return prop[1::]
    else:
        return prop


def inProperties(prop, properties):
    '''
        Return True if 'prop' is in the 'properties' dictionary
        Properties in 'properties' does NOT contain a heading '+'/'-',
         but 'prop' may contain heading '+'/'-'
        @param prop - string of a property name
        @param properties - dictionary of properties.

        @return True/False

        @raise None
    '''
    if rawProperty(prop) in properties:
        return True
    else:
        return False
