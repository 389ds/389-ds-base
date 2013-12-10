'''
Created on Dec 5, 2013

@author: tbordaz
'''

#
# Properties supported by the server WITH related attribute name
#
SER_HOST='hostname'
SER_PORT='ldap-port'
SER_SECURE_PORT='ldap-secureport'
SER_ROOT_DN='root-dn'
SER_ROOT_PW='root-pw'
SER_USER_ID='user-id'

SER_PROPNAME_TO_ATTRNAME= {SER_HOST:'nsslapd-localhost',
                           SER_PORT:'nsslapd-port',
                           SER_SECURE_PORT:'nsslapd-securePort',
                           SER_ROOT_DN:'nsslapd-rootdn',
                           SER_ROOT_PW:'nsslapd-rootpw',
                           SER_USER_ID:'nsslapd-localuser'}
#
# Properties supported by the server WITHOUT related attribute name
#
SER_SERVERID_PROP='server-id'
SER_GROUP_ID='group-id'
SER_DEPLOYED_DIR='deployed-dir'
SER_BACKUP_INST_DIR='inst-backupdir'
SER_CREATION_SUFFIX='suffix'

#
# Properties supported by the replica agreement
#
RA_SCHEDULE_PROP='schedule'
RA_TRANSPORT_PROT_PROP='transport-prot'
RA_FRAC_EXCLUDE_PROP='fractional-exclude-attrs-inc'
RA_FRAC_EXCLUDE_TOTAL_UPDATE_PROP='fractional-exclude-attrs-total'
RA_FRAC_STRIP_PROP='fractional-strip-attrs'
RA_CONSUMER_PORT='consumer-port'
RA_CONSUMER_HOST='consumer-host'
RA_CONSUMER_TOTAL_INIT='consumer-total-init'
RA_TIMEOUT='timeout'

RA_PROPNAME_TO_ATTRNAME = {RA_SCHEDULE_PROP:'nsds5replicaupdateschedule',
                           RA_TRANSPORT_PROT_PROP:'nsds5replicatransportinfo',
                           RA_FRAC_EXCLUDE_PROP:'nsDS5ReplicatedAttributeList',
                           RA_FRAC_EXCLUDE_TOTAL_UPDATE_PROP:'nsDS5ReplicatedAttributeListTotal',
                           RA_FRAC_STRIP_PROP:'nsds5ReplicaStripAttrs',
                           RA_CONSUMER_PORT:'nsds5replicaport',
                           RA_CONSUMER_HOST:'nsds5ReplicaHost',
                           RA_CONSUMER_TOTAL_INIT:'nsds5BeginReplicaRefresh',
                           RA_TIMEOUT:'nsds5replicatimeout'}


            