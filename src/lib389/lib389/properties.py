'''
Created on Dec 5, 2013

@author: tbordaz
'''

####################################
#
# Properties supported by the server
#
####################################

#
# Those WITH related attribute name
#
SER_HOST       ='hostname'
SER_PORT       ='ldap-port'
SER_SECURE_PORT='ldap-secureport'
SER_ROOT_DN    ='root-dn'
SER_ROOT_PW    ='root-pw'
SER_USER_ID    ='user-id'

SER_PROPNAME_TO_ATTRNAME= {SER_HOST:'nsslapd-localhost',
                           SER_PORT:'nsslapd-port',
                           SER_SECURE_PORT:'nsslapd-securePort',
                           SER_ROOT_DN:'nsslapd-rootdn',
                           SER_ROOT_PW:'nsslapd-rootpw',
                           SER_USER_ID:'nsslapd-localuser'}
#
# Those WITHOUT related attribute name
#
SER_SERVERID_PROP   ='server-id'
SER_GROUP_ID        ='group-id'
SER_DEPLOYED_DIR    ='deployed-dir'
SER_BACKUP_INST_DIR ='inst-backupdir'
SER_CREATION_SUFFIX ='suffix'

####################################
#
# Properties supported by the Mapping Tree entries
#
####################################

# Properties name
MT_STATE        = 'state'
MT_BACKEND      = 'backend-name'
MT_SUFFIX       = 'suffix'
MT_REFERRAL     = 'referral'
MT_CHAIN_PATH   = 'chain-plugin-path'
MT_CHAIN_FCT    = 'chain-plugin-fct'
MT_CHAIN_UPDATE = 'chain-update-policy'
MT_PARENT_SUFFIX= 'parent-suffix'

# Properties values
MT_STATE_VAL_DISABLED  = 'disabled'
MT_STATE_VAL_BACKEND   = 'backend'
MT_STATE_VAL_REFERRAL  = 'referral'
MT_STATE_VAL_REFERRAL_ON_UPDATE='referral-on-update'
MT_STATE_VAL_CONTAINER ='container'
MT_STATE_TO_VALUES={MT_STATE_VAL_DISABLED:  0,
                    MT_STATE_VAL_BACKEND:   1,
                    MT_STATE_VAL_REFERRAL:  2,
                    MT_STATE_VAL_REFERRAL_ON_UPDATE: 3,
                    MT_STATE_VAL_CONTAINER: 4}

MT_CHAIN_UPDATE_VAL_ON_UPDATE   = 'repl_chain_on_update'
MT_OBJECTCLASS_VALUE   = 'nsMappingTree'

MT_PROPNAME_TO_VALUES = {MT_STATE:MT_STATE_TO_VALUES}
MT_PROPNAME_TO_ATTRNAME = {MT_STATE:        'nsslapd-state',
                           MT_BACKEND:      'nsslapd-backend',
                           MT_SUFFIX:       'cn',
                           MT_PARENT_SUFFIX:'nsslapd-parent-suffix',
                           MT_REFERRAL:     'nsslapd-referral',
                           MT_CHAIN_PATH:   'nsslapd-distribution-plugin',
                           MT_CHAIN_FCT :   'nsslapd-distribution-funct',
                           MT_CHAIN_UPDATE: 'nsslapd-distribution-root-update'}

####################################
#
# Properties supported by the backend
#
####################################
BACKEND_SUFFIX        = 'suffix'
BACKEND_NAME          = 'name'
BACKEND_READONLY      = 'read-only'
BACKEND_REQ_INDEX     = 'require-index'
BACKEND_CACHE_ENTRIES = 'entry-cache-number'
BACKEND_CACHE_SIZE    = 'entry-cache-size'
BACKEND_DNCACHE_SIZE  = 'dn-cache-size'
BACKEND_DIRECTORY     = 'directory'
BACKEND_DB_DEADLOCK   = 'db-deadlock'
BACKEND_CHAIN_BIND_DN = 'chain-bind-dn'
BACKEND_CHAIN_BIND_PW = 'chain-bind-pw'
BACKEND_CHAIN_URLS    = 'chain-urls'
BACKEND_STATS         = 'stats'

BACKEND_OBJECTCLASS_VALUE = 'nsBackendInstance'

BACKEND_PROPNAME_TO_ATTRNAME = {BACKEND_SUFFIX:        'nsslapd-suffix',
                                BACKEND_NAME:          'cn',
                                BACKEND_READONLY:      'nsslapd-readonly',
                                BACKEND_REQ_INDEX:     'nsslapd-require-index',
                                BACKEND_CACHE_ENTRIES: 'nsslapd-cachesize',
                                BACKEND_CACHE_SIZE:    'nsslapd-cachememsize',
                                BACKEND_DNCACHE_SIZE:  'nsslapd-dncachememsize',
                                BACKEND_DIRECTORY:     'nsslapd-directory',
                                BACKEND_DB_DEADLOCK:   'nsslapd-db-deadlock-policy',
                                BACKEND_CHAIN_BIND_DN: 'nsmultiplexorbinddn',
                                BACKEND_CHAIN_BIND_PW: 'nsmultiplexorcredentials',
                                BACKEND_CHAIN_URLS:    'nsfarmserverurl'}

####################################
#
# Properties of a replica
#
####################################

REPLICA_SUFFIX           = 'suffix'
REPLICA_ID               = 'rid'
REPLICA_TYPE             = 'type'
REPLICA_LEGACY_CONS      = 'legacy'
REPLICA_BINDDN           = 'binddn'
REPLICA_PURGE_INTERVAL   = 'purge-interval'
REPLICA_PURGE_DELAY      = 'purge-delay'
REPLICA_PRECISE_PURGING  = 'precise-purging'
REPLICA_REFERRAL         = 'referral'
REPLICA_FLAGS            = 'flags'

REPLICA_OBJECTCLASS_VALUE = 'nsds5Replica'

REPLICA_PROPNAME_TO_ATTRNAME = {
                                REPLICA_SUFFIX:           'nsds5replicaroot',
                                REPLICA_ID:               'nsds5replicaid',
                                REPLICA_TYPE:             'nsds5replicatype',
                                REPLICA_LEGACY_CONS:      'nsds5replicalegacyconsumer',
                                REPLICA_BINDDN:           'nsds5replicabinddn',
                                REPLICA_PURGE_INTERVAL:   'nsds5replicatombstonepurgeinterval',
                                REPLICA_PURGE_DELAY:      'nsds5ReplicaPurgeDelay',
                                REPLICA_PRECISE_PURGING:  'nsds5ReplicaPreciseTombstonePurging',
                                REPLICA_REFERRAL:         'nsds5ReplicaReferral',
                                REPLICA_FLAGS:            'nsds5flags'}

####################################
#
# Properties of a changelog
#
####################################
CHANGELOG_NAME          = 'cl-name'
CHANGELOG_DIR           = 'cl-dir'
CHANGELOG_MAXAGE        = 'cl-maxage'
CHANGELOG_MAXENTRIES    = 'cl-maxentries'
CHANGELOG_TRIM_INTERVAL = 'cl-trim-interval'
CHANGELOG_COMPACT_INTV  = 'cl-compact-interval'

CHANGELOG_PROPNAME_TO_ATTRNAME = {CHANGELOG_NAME:          'cn',
                                  CHANGELOG_DIR:           'nsslapd-changelogdir',
                                  CHANGELOG_MAXAGE:        'nsslapd-changelogmaxage',
                                  CHANGELOG_MAXENTRIES:    'nsslapd-changelogmaxentries',
                                  CHANGELOG_TRIM_INTERVAL: 'nsslapd-changelogtrim-interval',
                                  CHANGELOG_COMPACT_INTV:  'nsslapd-changelogcompactdb-interval',}

####################################
#
# Properties of an entry
#
####################################
ENTRY_OBJECTCLASS     = 'objectclass'
ENTRY_SN              = 'sn'
ENTRY_CN              = 'cn'
ENTRY_TYPE_PERSON     = 'person'
ENTRY_TYPE_INETPERSON = 'inetperson'
ENTRY_TYPE_GROUP      = 'group'
ENTRY_USERPASSWORD    = 'userpassword'
ENTRY_UID             = 'uid'

ENTRY_TYPE_TO_OBJECTCLASS = {ENTRY_TYPE_PERSON:     ["top", "person"],
                             ENTRY_TYPE_INETPERSON: ["top", "person", "inetOrgPerson"]}

####################################
#
# Properties supported by the replica agreement
#
####################################

RA_NAME                 = 'name'
RA_SUFFIX               = 'suffix'
RA_BINDDN               = 'binddn'
RA_BINDPW               = 'bindpw'
RA_METHOD               = 'method'
RA_DESCRIPTION          = 'description'
RA_SCHEDULE             = 'schedule'
RA_TRANSPORT_PROT       = 'transport-prot'
RA_FRAC_EXCLUDE         = 'fractional-exclude-attrs-inc'
RA_FRAC_EXCLUDE_TOTAL_UPDATE ='fractional-exclude-attrs-total'
RA_FRAC_STRIP           = 'fractional-strip-attrs'
RA_CONSUMER_PORT        = 'consumer-port'
RA_CONSUMER_HOST        = 'consumer-host'
RA_CONSUMER_TOTAL_INIT  = 'consumer-total-init'
RA_TIMEOUT              = 'timeout'
RA_CHANGES              = 'changes'

RA_OBJECTCLASS_VALUE = "nsds5replicationagreement"
RA_WINDOWS_OBJECTCLASS_VALUE = "nsDSWindowsReplicationAgreement"

RA_PROPNAME_TO_ATTRNAME = {RA_NAME:                 'cn',
                           RA_SUFFIX:               'nsds5replicaroot',
                           RA_BINDDN:               'nsds5replicabinddn',
                           RA_BINDPW:               'nsds5replicacredentials',
                           RA_METHOD:               'nsds5replicabindmethod',
                           RA_DESCRIPTION:          'description',
                           RA_SCHEDULE:             'nsds5replicaupdateschedule',
                           RA_TRANSPORT_PROT:       'nsds5replicatransportinfo',
                           RA_FRAC_EXCLUDE:         'nsDS5ReplicatedAttributeList',
                           RA_FRAC_EXCLUDE_TOTAL_UPDATE:'nsDS5ReplicatedAttributeListTotal',
                           RA_FRAC_STRIP:           'nsds5ReplicaStripAttrs',
                           RA_CONSUMER_PORT:        'nsds5replicaport',
                           RA_CONSUMER_HOST:        'nsds5ReplicaHost',
                           RA_CONSUMER_TOTAL_INIT:  'nsds5BeginReplicaRefresh',
                           RA_TIMEOUT:              'nsds5replicatimeout',
                           RA_CHANGES:              'nsds5replicaChangesSentSinceStartup'}

####################################
#
# Properties supported by the plugins
#
####################################

PLUGIN_NAME     = "name"
PLUGIN_PATH     = "path"
PLUGIN_ENABLE   = 'enable'

PLUGINS_OBJECTCLASS_VALUE = "nsSlapdPlugin"
PLUGINS_ENABLE_ON_VALUE   = "on"
PLUGINS_ENABLE_OFF_VALUE  = "off"

PLUGIN_PROPNAME_TO_ATTRNAME = {PLUGIN_NAME:     'cn',
                               PLUGIN_PATH:     'nsslapd-pluginPath',
                               PLUGIN_ENABLE:   'nsslapd-pluginEnabled'}

####################################
#
# Properties supported by the index
#
####################################
INDEX_TYPE     = "type"
INDEX_SYSTEM   = "system"
INDEX_MATCHING_RULE = "matching-rule"

INDEX_PROPNAME_TO_ATTRNAME = {INDEX_TYPE: 'nsIndexType',
                              INDEX_SYSTEM: 'nsSystemIndex',
                              INDEX_MATCHING_RULE: 'nsMatchingRule'}

####################################
#
# Properties supported by the tasks
#
####################################

TASK_WAIT        = "wait"
TASK_TOMB_STRIP  = "strip-csn"
EXPORT_REPL_INFO = "repl-info"


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
        Properties in 'properties' does NOT contain a heading '+'/'-', but 'prop'
        may contain heading '+'/'-'
        @param prop - string of a property name
        @param properties - dictionary of properties.

        @return True/False

        @raise None
    '''
    if rawProperty(prop) in properties:
        return True
    else:
        return False