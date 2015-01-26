'''
Created on Dec 5, 2013

@author: tbordaz
'''

import ldap
import re
import time

from lib389._constants import *
from lib389._entry import FormatDict
from lib389.utils import normalizeDN
from lib389 import Entry, DirSrv, NoSuchEntryError, InvalidArgumentError
from lib389.properties import *


class Agreement(object):
    ALWAYS = '0000-2359 0123456'
    NEVER = '2358-2359 0'

    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Agreement.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def status(self, agreement_dn):
        """Return a formatted string with the replica status. Looking like:
            Status for meTo_localhost.localdomain:50389 agmt localhost.localdomain:50389
            Update in progress: TRUE
            Last Update Start: 20131121132756Z
            Last Update End: 0
            Num. Changes Sent: 1:10/0
            Num. changes Skipped: None
            Last update Status: 0 Replica acquired successfully: Incremental update started
            Init in progress: None
            Last Init Start: 0
            Last Init End: 0
            Last Init Status: None
            Reap Active: 0
            @param agreement_dn - DN of the replication agreement

            @returns string containing the status of the replica agreement

            @raise NoSuchEntryError - if agreement_dn is an unknown entry
        """

        attrlist = ['cn', 'nsds5BeginReplicaRefresh', 'nsds5replicaUpdateInProgress',
                    'nsds5ReplicaLastInitStatus', 'nsds5ReplicaLastInitStart',
                    'nsds5ReplicaLastInitEnd', 'nsds5replicaReapActive',
                    'nsds5replicaLastUpdateStart', 'nsds5replicaLastUpdateEnd',
                    'nsds5replicaChangesSentSinceStartup', 'nsds5replicaLastUpdateStatus',
                    'nsds5replicaChangesSkippedSinceStartup', 'nsds5ReplicaHost',
                    'nsds5ReplicaPort']
        try:
            ent = self.conn.getEntry(
                agreement_dn, ldap.SCOPE_BASE, "(objectclass=*)", attrlist)
        except NoSuchEntryError:
            raise NoSuchEntryError(
                "Error reading status from agreement", agreement_dn)
        else:
            retstr = (
                "Status for %(cn)s agmt %(nsDS5ReplicaHost)s:%(nsDS5ReplicaPort)s" "\n"
                "Update in progress: %(nsds5replicaUpdateInProgress)s" "\n"
                "Last Update Start: %(nsds5replicaLastUpdateStart)s" "\n"
                "Last Update End: %(nsds5replicaLastUpdateEnd)s" "\n"
                "Num. Changes Sent: %(nsds5replicaChangesSentSinceStartup)s" "\n"
                "Num. changes Skipped: %(nsds5replicaChangesSkippedSinceStartup)s" "\n"
                "Last update Status: %(nsds5replicaLastUpdateStatus)s" "\n"
                "Init in progress: %(nsds5BeginReplicaRefresh)s" "\n"
                "Last Init Start: %(nsds5ReplicaLastInitStart)s" "\n"
                "Last Init End: %(nsds5ReplicaLastInitEnd)s" "\n"
                "Last Init Status: %(nsds5ReplicaLastInitStatus)s" "\n"
                "Reap Active: %(nsds5ReplicaReapActive)s" "\n"
            )
            # FormatDict manages missing fields in string formatting
            return retstr % FormatDict(ent.data)

    def _check_interval(self, interval):
        '''
            Check the interval for schedule replication is valid:
            HH [0..23]
            MM [0..59]
            DAYS [0-6]{1,7}

            @param interval - interval in the format 'HHMM-HHMM D+' (D is day number [0-6])

            @return None

            @raise ValueError - if the inteval is illegal
        '''
        c = re.compile(re.compile('^([0-9][0-9])([0-9][0-9])-([0-9][0-9])([0-9][0-9]) ([0-6]{1,7})$'))
        if not c.match(interval):
            raise ValueError("Bad schedule format %r" % interval)
        schedule = c.split(interval, c.groups)

        # check the hours
        hour = int(schedule[1])
        if ((hour < 0) or (hour > 23)):
            raise ValueError("Bad schedule format %r: illegal hour %d" % (interval, hour))
        hour = int(schedule[3])
        if ((hour < 0) or (hour > 23)):
            raise ValueError("Bad schedule format %r: illegal hour %d" % (interval, hour))
        if int(schedule[1]) > int(schedule[3]):
            raise ValueError("Bad schedule (start HOUR larger than end HOUR) %r: illegal hour %d" % (interval, int(schedule[1])))

        # check the minutes
        minute = int(schedule[2])
        if ((minute < 0) or (minute > 59)):
            raise ValueError("Bad schedule format %r: illegal minute %d" % (interval, minute))
        minute = int(schedule[4])
        if ((minute < 0) or (minute > 59)):
            raise ValueError("Bad schedule format %r: illegal minute %d" % (interval, minute))

    def schedule(self, agmtdn=None, interval=ALWAYS):
        """Schedule the replication agreement
            @param agmtdn - DN of the replica agreement
            @param interval - in the form
                    - Agreement.ALWAYS
                    - Agreement.NEVER
                    - or 'HHMM-HHMM D+' With D=[0123456]+

            @return - None

            @raise ValueError - if interval is not valid
                   ldap.NO_SUCH_OBJECT - if agmtdn does not exist
        """
        if not agmtdn:
            raise InvalidArgumentError("agreement DN is missing")

        # check the validity of the interval
        if interval != Agreement.ALWAYS and interval != Agreement.NEVER:
            self._check_interval(interval)

        # Check if the replica agreement exists
        try:
            self.conn.getEntry(agmtdn, ldap.SCOPE_BASE)
        except ldap.NO_SUCH_OBJECT:
            raise

        # update it
        self.log.info("Schedule replication agreement %s" % agmtdn)
        mod = [(
            ldap.MOD_REPLACE, 'nsds5replicaupdateschedule', [interval])]
        self.conn.modify_s(agmtdn, mod)

    def getProperties(self, agmnt_dn=None, properties=None):
        '''
            returns a dictionary of the requested properties. If properties is missing,
            it returns all the properties.
            @param agmtdn - is the replica agreement DN
            @param properties - is the list of properties name
            Supported properties are
                RA_NAME
                RA_SUFFIX
                RA_BINDDN
                RA_BINDPW
                RA_METHOD
                RA_DESCRIPTION
                RA_SCHEDULE
                RA_TRANSPORT_PROT
                RA_FRAC_EXCLUDE
                RA_FRAC_EXCLUDE_TOTAL_UPDATE
                RA_FRAC_STRIP
                RA_CONSUMER_PORT
                RA_CONSUMER_HOST
                RA_CONSUMER_TOTAL_INIT
                RA_TIMEOUT
                RA_CHANGES

            @return - returns a dictionary of the properties

            @raise ValueError - if invalid property name
                   ldap.NO_SUCH_OBJECT - if agmtdn does not exist
                   InvalidArgumentError - missing mandatory argument


        '''

        if not agmnt_dn:
            raise InvalidArgumentError("agmtdn is a mandatory argument")

        #
        # prepare the attribute list to retrieve from the RA
        # if properties is None, all RA attributes are retrieved
        #
        attrs = []
        if properties:
            for prop_name in properties:
                prop_attr = RA_PROPNAME_TO_ATTRNAME[prop_name]
                if not prop_attr:
                    raise ValueError("Improper property name: %s ", prop_name)
                attrs.append(prop_attr)

        filt = "(objectclass=*)"
        result = {}
        try:
            entry = self.conn.getEntry(agmnt_dn, ldap.SCOPE_BASE, filt, attrs)

            # Build the result from the returned attributes
            for attr in entry.getAttrs():
                # given an attribute name retrieve the property name
                props = [k for k, v in RA_PROPNAME_TO_ATTRNAME.iteritems() if v.lower() == attr.lower()]

                # If this attribute is present in the RA properties, adds it to result
                if len(props) > 0:
                    result[props[0]] = entry.getValues(attr)

        except ldap.NO_SUCH_OBJECT:
            raise

        return result

    def setProperties(self, suffix=None, agmnt_dn=None, agmnt_entry=None, properties=None):
        '''
            Set the properties of the agreement. If an 'agmnt_entry' (Entry) is provided, it updates the entry, else
            it updates the entry on the server. If the 'agmnt_dn' is provided it retrieves the entry using it, else
            it retrieve the agreement using the 'suffix'.

            @param suffix : suffix stored in that agreement (online update)
            @param agmnt_dn: DN of the agreement (online update)
            @param agmnt_entry: Entry of a agreement (offline update)
            @param properties: dictionary of properties
            Supported properties are
                RA_NAME
                RA_SUFFIX
                RA_BINDDN
                RA_BINDPW
                RA_METHOD
                RA_DESCRIPTION
                RA_SCHEDULE
                RA_TRANSPORT_PROT
                RA_FRAC_EXCLUDE
                RA_FRAC_EXCLUDE_TOTAL_UPDATE
                RA_FRAC_STRIP
                RA_CONSUMER_PORT
                RA_CONSUMER_HOST
                RA_CONSUMER_TOTAL_INIT
                RA_TIMEOUT
                RA_CHANGES


            @return None

            @raise ValueError: if unknown properties
                    ValueError: if invalid agreement_entry
                    ValueError: if agmnt_dn or suffix are not associated to a replica
                    InvalidArgumentError: If missing mandatory parameter


        '''
        # No properties provided
        if len(properties) == 0:
            return

        # check that the given properties are valid
        for prop in properties:
            # skip the prefix to add/del value
            if not inProperties(prop, RA_PROPNAME_TO_ATTRNAME):
                raise ValueError("unknown property: %s" % prop)
            else:
                self.log.debug("setProperties: %s:%s" % (prop, properties[prop]))

        # At least we need to have suffix/agmnt_dn/agmnt_entry
        if not suffix and not agmnt_dn and not agmnt_entry:
            raise InvalidArgumentError("suffix and agmnt_dn and agmnt_entry are missing")

        # TODO
        if suffix:
            raise NotImplemented

                # the caller provides a set of properties to set into a replica entry
        if agmnt_entry:
            if not isinstance(agmnt_entry, Entry):
                raise ValueError("invalid instance of the agmnt_entry")

            # that is fine, now set the values
            for prop in properties:
                val = rawProperty(prop)

                # for Entry update it is a replace
                agmnt_entry.update({RA_PROPNAME_TO_ATTRNAME[val]: properties[prop]})

            return

        # for each provided property build the mod
        mod = []
        for prop in properties:

            # retrieve/check the property name
            # and if the value needs to be added/deleted/replaced
            if prop.startswith('+'):
                mod_type = ldap.MOD_ADD
                prop_name = prop[1:]
            elif prop.startswith('-'):
                mod_type = ldap.MOD_DELETE
                prop_name = prop[1:]
            else:
                mod_type = ldap.MOD_REPLACE
                prop_name = prop

            attr = RA_PROPNAME_TO_ATTRNAME[prop_name]
            if not attr:
                raise ValueError("invalid property name %s" % prop_name)

            # Now do the value checking we can do
            if prop_name == RA_SCHEDULE:
                self._check_interval(properties[prop])

            mod.append((mod_type, attr, properties[prop]))

        # Now time to run the effective modify
        self.conn.modify_s(agmnt_dn, mod)

    def list(self, suffix=None, consumer_host=None, consumer_port=None, agmtdn=None):
        '''
            Returns the search result of the replica agreement(s) under the replica (replicaRoot is 'suffix').

            Either 'suffix' or 'agmtdn' need to be specfied.
            'consumer_host' and 'consumer_port' are either not specified or specified both.

            If 'agmtdn' is specified, it returns the search result entry of that replication agreement.
            else if consumer host/port are specified it returns the replica agreements toward
            that consumer host:port.
            Finally if neither 'agmtdn' nor 'consumser host/port' are specifies it returns
            all the replica agreements under the replica (replicaRoot is 'suffix').

            @param - suffix is the suffix targeted by the total update
            @param - consumer_host hostname of the consumer
            @param - consumer_port port of the consumer
            @param - agmtdn DN of the replica agreement

            @return - search result of the replica agreements

            @raise - InvalidArgument: if missing mandatory argument (agmtdn or suffix, then host and port)
                   - ValueError - if some properties are not valid
                   - NoSuchEntryError - If no replica defined for the suffix
        '''
        if not suffix and not agmtdn:
            raise InvalidArgumentError("suffix or agmtdn are required")

        if (consumer_host and not consumer_port) or (not consumer_host and consumer_port):
            raise InvalidArgumentError("consumer host/port are required together")

        if agmtdn:
            # easy case, just return the RA
            filt = "objectclass=*"
            return self.conn.search_s(agmtdn, ldap.SCOPE_BASE, filt)
        else:
            # Retrieve the replica
            replica_entries = self.conn.replica.list(suffix)
            if not replica_entries:
                raise NoSuchEntryError("Error: no replica set up for suffix " + suffix)
            replica_entry = replica_entries[0]

            # Now returns the replica agreement for that suffix that replicates to
            # consumer host/port
            if consumer_host and consumer_port:
                filt = "(&(objectclass=%s)(%s=%s)(%s=%d))" % (RA_OBJECTCLASS_VALUE,
                                                              RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_HOST], consumer_host,
                                                              RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_PORT], consumer_port)
            else:
                filt = "(objectclass=%s)" % RA_OBJECTCLASS_VALUE
            return self.conn.search_s(replica_entry.dn, ldap.SCOPE_ONELEVEL, filt)

    def create(self, suffix=None, host=None, port=None, properties=None):
        """Create (and return) a replication agreement from self to consumer.
            - self is the supplier,

            @param consumer: one of the following (consumer can be a master)
                    * a DirSrv object if chaining
                    * an object with attributes: host, port, sslport, __str__
            @param suffix    - eg. 'dc=babel,dc=it'
            @param properties      - further properties dict.
            Support properties
                RA_NAME
                RA_SUFFIX
                RA_BINDDN
                RA_BINDPW
                RA_METHOD
                RA_DESCRIPTION
                RA_SCHEDULE
                RA_TRANSPORT_PROT
                RA_FRAC_EXCLUDE
                RA_FRAC_EXCLUDE_TOTAL_UPDATE
                RA_FRAC_STRIP
                RA_CONSUMER_PORT
                RA_CONSUMER_HOST
                RA_CONSUMER_TOTAL_INIT
                RA_TIMEOUT
                RA_CHANGES


            @return dn_agreement - DN of the created agreement

            @raise InvalidArgumentError - If the suffix is missing
            @raise NosuchEntryError     - if a replica doesn't exist for that suffix
            @raise UNWILLING_TO_PERFORM if the database was previously
                    in read-only state. To create new agreements you
                    need to *restart* the directory server

        """
        import string

        # Check we have a suffix [ mandatory ]
        if not suffix:
            self.log.warning("create: suffix is missing")
            raise InvalidArgumentError('suffix is mandatory')

        if properties:
            binddn      = properties.get(RA_BINDDN)         or defaultProperties[REPLICATION_BIND_DN]
            bindpw      = properties.get(RA_BINDPW)         or defaultProperties[REPLICATION_BIND_PW]
            bindmethod  = properties.get(RA_METHOD)         or defaultProperties[REPLICATION_BIND_METHOD]
            format      = properties.get(RA_NAME)           or r'meTo_$host:$port'
            description = properties.get(RA_DESCRIPTION)    or format
            transport   = properties.get(RA_TRANSPORT_PROT) or defaultProperties[REPLICATION_TRANSPORT]
            timeout     = properties.get(RA_TIMEOUT)        or defaultProperties[REPLICATION_TIMEOUT]
        else:
            binddn      = defaultProperties[REPLICATION_BIND_DN]
            bindpw      = defaultProperties[REPLICATION_BIND_PW]
            bindmethod  = defaultProperties[REPLICATION_BIND_METHOD]
            format      = r'meTo_$host:$port'
            description = format
            transport   = defaultProperties[REPLICATION_TRANSPORT]
            timeout     = defaultProperties[REPLICATION_TIMEOUT]

        # Compute the normalized suffix to be set in RA entry
        nsuffix = normalizeDN(suffix)

        # adding agreement under the replica entry
        replica_entries = self.conn.replica.list(suffix)
        if not replica_entries:
            raise NoSuchEntryError(
                "Error: no replica set up for suffix " + suffix)
        replica = replica_entries[0]

        # define agreement entry
        cn = string.Template(format).substitute({'host': host, 'port': port})
        dn_agreement = ','.join(["cn=%s" % cn, replica.dn])

        # This is probably unnecessary because
        # we can just raise ALREADY_EXISTS
        try:

            entry = self.conn.getEntry(dn_agreement, ldap.SCOPE_BASE)
            self.log.warn("Agreement already exists: %r" % dn_agreement)
            return dn_agreement
        except ldap.NO_SUCH_OBJECT:
            entry = None

        # In a separate function in this scope?
        entry = Entry(dn_agreement)
        entry.update({
            'objectclass': ["top", RA_OBJECTCLASS_VALUE],
            RA_PROPNAME_TO_ATTRNAME[RA_NAME]:           cn,
            RA_PROPNAME_TO_ATTRNAME[RA_SUFFIX]:         nsuffix,
            RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_HOST]:  host,
            RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_PORT]:  str(port),
            RA_PROPNAME_TO_ATTRNAME[RA_TRANSPORT_PROT]: transport,
            RA_PROPNAME_TO_ATTRNAME[RA_TIMEOUT]:        str(timeout),
            RA_PROPNAME_TO_ATTRNAME[RA_BINDDN]:         binddn,
            RA_PROPNAME_TO_ATTRNAME[RA_BINDPW]:         bindpw,
            RA_PROPNAME_TO_ATTRNAME[RA_METHOD]:         bindmethod,
            RA_PROPNAME_TO_ATTRNAME[RA_DESCRIPTION]:    string.Template(description).substitute({'host': host, 'port': port})
        })

        # we make a copy here because we cannot change
        # the passed in properties dict
        propertiescopy = {}
        if properties:
            import copy
            propertiescopy = copy.deepcopy(properties)

        # further arguments
        if 'winsync' in propertiescopy:  # state it clearly!
            self.conn.setupWinSyncAgmt(propertiescopy, entry)

        try:
            self.log.debug("Adding replica agreement: [%r]" % entry)
            self.conn.add_s(entry)
        except:
            #  FIXME check please!
            raise

        entry = self.conn.waitForEntry(dn_agreement)
        if entry:
            # More verbose but shows what's going on
            if 'chain' in propertiescopy:
                raise NotImplementedError
                chain_args = {
                    'suffix': suffix,
                    'binddn': binddn,
                    'bindpw': bindpw
                }
                # Work on `self` aka producer
                if replica.nsds5replicatype == MASTER_TYPE:
                    self.conn.setupChainingFarm(**chain_args)
                # Work on `consumer`
                # TODO - is it really required?
                if replica.nsds5replicatype == LEAF_TYPE:
                    chain_args.update({
                        'isIntermediate': 0,
                        'urls': self.conn.toLDAPURL(),
                        'args': propertiescopy['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)
                elif replica.nsds5replicatype == HUB_TYPE:
                    chain_args.update({
                        'isIntermediate': 1,
                        'urls': self.conn.toLDAPURL(),
                        'args': propertiescopy['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)

        return dn_agreement

    def init(self, suffix=None, consumer_host=None, consumer_port=None):
        """Trigger a total update of the consumer replica
            - self is the supplier,
            - consumer is a DirSrv object (consumer can be a master)
            - cn_format - use this string to format the agreement name
            @param - suffix is the suffix targeted by the total update [mandatory]
            @param - consumer_host hostname of the consumer [mandatory]
            @param - consumer_port port of the consumer [mandatory]

            @raise InvalidArgument: if missing mandatory argurment (suffix/host/port)
        """
        #
        # check the required parameters are set
        #
        if not suffix:
            self.log.fatal("initAgreement: suffix is missing")
            raise InvalidArgumentError('suffix is mandatory argument')

        nsuffix = normalizeDN(suffix)

        if not consumer_host:
            self.log.fatal("initAgreement: host is missing")
            raise InvalidArgumentError('host is mandatory argument')

        if not consumer_port:
            self.log.fatal("initAgreement: port is missing")
            raise InvalidArgumentError('port is mandatory argument')
                #
        # check the replica agreement already exist
        #
        replica_entries = self.conn.replica.list(suffix)
        if not replica_entries:
            raise NoSuchEntryError(
                    "Error: no replica set up for suffix " + suffix)
        replica_entry = replica_entries[0]
        self.log.debug("initAgreement: looking for replica agreements under %s" % replica_entry.dn)
        try:
            filt = "(&(objectclass=nsds5replicationagreement)(nsds5replicahost=%s)(nsds5replicaport=%d)(nsds5replicaroot=%s))" % (consumer_host, consumer_port, nsuffix)
            entry = self.conn.getEntry(replica_entry.dn, ldap.SCOPE_ONELEVEL, filt)
        except ldap.NO_SUCH_OBJECT:
            self.log.fatal("initAgreement: No replica agreement to %s:%d for suffix %s" % (consumer_host, consumer_port, nsuffix))
            raise

        #
        # trigger the total init
        #
        self.log.info("Starting total init %s" % entry.dn)
        mod = [(ldap.MOD_ADD, 'nsds5BeginReplicaRefresh', 'start')]
        self.conn.modify_s(entry.dn, mod)

    def pause(self, agmtdn, interval=NEVER):
        """Pause this replication agreement.  This replication agreement
          will send no more changes.  Use the resume() method to "unpause".
          It tries to disable the replica agreement. If it fails (not implemented in all version),
          it uses the schedule() with interval '2358-2359 0'
            @param agmtdn - agreement dn
            @param interval - (default NEVER) replication schedule to use

            @return None

            @raise ValueError - if interval is not valid
        """
        self.log.info("Pausing replication %s" % agmtdn)
        mod = [(
            ldap.MOD_REPLACE, 'nsds5ReplicaEnabled', ['off'])]
        try:
            self.conn.modify_s(agmtdn, mod)
        except ldap.LDAPError:
            # before 1.2.11, no support for nsds5ReplicaEnabled
            # use schedule hack
            self.schedule(interval)

        # Allow a little time for the change to take effect
        time.sleep(2)

    def resume(self, agmtdn, interval=ALWAYS):
        """Resume a paused replication agreement, paused with the "pause" method.
           It tries to enabled the replica agreement. If it fails (not implemented in all version),
           it uses the schedule() with interval '0000-2359 0123456'
            @param agmtdn  - agreement dn
            @param interval - (default ALWAYS) replication schedule to use

            @return None

            @raise ValueError - if interval is not valid
                   ldap.NO_SUCH_OBJECT - if agmtdn does not exist
        """
        self.log.info("Resuming replication %s" % agmtdn)
        mod = [(
            ldap.MOD_REPLACE, 'nsds5ReplicaEnabled', ['on'])]
        try:
            self.conn.modify_s(agmtdn, mod)
        except ldap.LDAPError:
            # before 1.2.11, no support for nsds5ReplicaEnabled
            # use schedule hack
            self.schedule(interval)

        # Allow a little time for the change to take effect
        time.sleep(2)

    def changes(self, agmnt_dn):
        """Return a list of changes sent by this agreement."""
        retval = 0
        try:
            ent = self.conn.getEntry(
                agmnt_dn, ldap.SCOPE_BASE, "(objectclass=*)", [RA_PROPNAME_TO_ATTRNAME[RA_CHANGES]])
        except:
            raise NoSuchEntryError(
                "Error reading status from agreement", agmnt_dn)

        if ent.nsds5replicaChangesSentSinceStartup:
            val = ent.nsds5replicaChangesSentSinceStartup
            items = val.split(' ')
            if len(items) == 1:
                retval = int(items[0])
            else:
                for item in items:
                    ary = item.split(":")
                    if ary and len(ary) > 1:
                        retval = retval + int(ary[1].split("/")[0])
        return retval
