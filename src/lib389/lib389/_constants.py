# replicatype @see https://access.redhat.com/knowledge/docs/en-US/Red_Hat_Directory_Server/8.1/html/Administration_Guide/Managing_Replication-Configuring-Replication-cmd.html
# 2 for consumers and hubs (read-only replicas)
# 3 for both single and multi-master suppliers (read-write replicas)
# TODO: let's find a way to be consistent - eg. using bitwise operator
(MASTER_TYPE,
 HUB_TYPE,
 LEAF_TYPE) = range(3)
 
REPLICAROLE_MASTER    = "master"
REPLICAROLE_HUB       = "hub"
REPLICAROLE_CONSUMER  = "consumer"

CONSUMER_REPLICAID = 65535

REPLICA_RDONLY_TYPE = 2  # CONSUMER and HUB
REPLICA_WRONLY_TYPE = 1  # SINGLE and MULTI MASTER
REPLICA_RDWR_TYPE = REPLICA_RDONLY_TYPE | REPLICA_WRONLY_TYPE

REPLICATION_BIND_DN = 'replication_bind_dn'
REPLICATION_BIND_PW = 'replication_bind_pw'
REPLICATION_BIND_METHOD = 'replication_bind_method'

defaultProperties = {
    REPLICATION_BIND_DN: "cn=replrepl,cn=config",
    REPLICATION_BIND_PW: "password",
    REPLICATION_BIND_METHOD: "simple"
}


CFGSUFFIX = "o=NetscapeRoot"
DEFAULT_USER = "nobody"

# Some DN constants
DN_DM = "cn=Directory Manager"
DN_CONFIG = "cn=config"
DN_LDBM = "cn=ldbm database,cn=plugins,cn=config"
DN_MAPPING_TREE = "cn=mapping tree,cn=config"
DN_CHAIN = "cn=chaining database,cn=plugins,cn=config"
DN_CHANGELOG = "cn=changelog5,cn=config"

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
