'''
Created on Dec 19, 2013

@author: tbordaz
'''

import ldap
import os
import decimal
import time
from lib389 import DirSrv, Entry, NoSuchEntryError, InvalidArgumentError
from lib389._constants import *
from lib389._replication import RUV
from lib389.properties import *
from lib389.utils import normalizeDN, escapeDNValue


class Replica(object):
    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Replica.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def _get_mt_entry(self, suffix):
        """Return the replica dn of the given suffix."""
        mtent = self.conn.mappingtree.list(suffix=suffix)
        return ','.join(("cn=replica", mtent.dn))

    @staticmethod
    def _valid_role(role):
        if role != REPLICAROLE_MASTER and role != REPLICAROLE_HUB and role != REPLICAROLE_CONSUMER:
            return False
        else:
            return True

    @staticmethod
    def _valid_rid(role, rid=None):
        if role == REPLICAROLE_MASTER:
            if not decimal.Decimal(rid) or (rid <= 0) or (rid >= CONSUMER_REPLICAID):
                return False
        else:
            if rid and (rid != CONSUMER_REPLICAID):
                return False
        return True

    @staticmethod
    def _set_or_default(attribute, properties, default):
        '''
            If 'attribute' or '+attribute' or '-attribute' exist in 'properties'
            it does nothing. Else it set (ldap.MOD_REPLACE) 'attribute' in 'properties'
            to the 'default' value
        '''
        add_attribute = "+%s" % attribute
        del_attribute = "-%s" % attribute
        if attribute in properties or add_attribute in properties or del_attribute in properties:
            return
        properties[attribute] = default

    def create_repl_manager(self, repl_manager_dn=None, repl_manager_pw=None):
        '''
            Create an entry that will be used to bind as replica manager.

            @param repl_manager_dn - DN of the bind entry. If not provided use the default one
            @param repl_manager_pw - Password of the entry. If not provide use the default one

            @return None

            @raise - KeyError if can not find valid values of Bind DN and Pwd
        '''

        # check the DN and PW
        try:
            repl_manager_dn = repl_manager_dn or defaultProperties[REPLICATION_BIND_DN]
            repl_manager_pw = repl_manager_pw or defaultProperties[REPLICATION_BIND_PW]
            if not repl_manager_dn or not repl_manager_pw:
                raise KeyError
        except KeyError:
            if not repl_manager_pw:
                self.log.warning("replica_createReplMgr: bind DN password not specified")
            if not repl_manager_dn:
                self.log.warning("replica_createReplMgr: bind DN not specified")
            raise

        # if the replication manager entry already exists, ust return
        try:
            entries = self.search_s(repl_manager_dn, ldap.SCOPE_BASE, "objectclass=*")
            if entries:
                #it already exist, fine
                return
        except ldap.NO_SUCH_OBJECT:
            pass

        # ok it does not exist, create it
        try:
            attrs = {
                    'nsIdleTimeout': '0',
                    'passwordExpirationTime': '20381010000000Z'
                    }
            self.conn.setupBindDN(repl_manager_dn, repl_manager_pw, attrs)
        except ldap.ALREADY_EXISTS:
            self.log.warn("User already exists (weird we just checked: %s " % repl_manager_dn)

    def list(self, suffix=None, replica_dn=None):
        """
            Return a list of replica entries.
            If 'replica_dn' is specified it returns the related entry.
            If 'suffix' is specified it returns the replica that is configured for that 'suffix'.
            If both 'replica_dn' and 'suffix' are specified it returns the 'replica_dn' entry.
            If none of them are specified, it returns all the replicas

            @param suffix - suffix of a replica
            @param replica_dn - DN of a replica

            @return replica entries

            @raise search exceptions: ldap.NO_SUCH_OBJECT, ..
        """

        # set base/filtr according to the arguments
        if replica_dn:
            base = replica_dn
            filtr = "(objectclass=%s)" % REPLICA_OBJECTCLASS_VALUE
        elif suffix:
            base = DN_MAPPING_TREE
            filtr = "(&(objectclass=%s)(%s=%s))" % (REPLICA_OBJECTCLASS_VALUE,
                                                    REPLICA_PROPNAME_TO_ATTRNAME[REPLICA_SUFFIX], suffix)
        else:
            base = DN_MAPPING_TREE
            filtr = "(objectclass=%s)" % REPLICA_OBJECTCLASS_VALUE

        # now do the effective search
        ents = self.conn.search_s(base, ldap.SCOPE_SUBTREE, filtr)
        return ents

    def setProperties(self, suffix=None, replica_dn=None, replica_entry=None, properties=None):
        '''
            Set the properties of the replica. If an 'replica_entry' (Entry) is provided, it updates the entry, else
            it updates the entry on the server. If the 'replica_dn' is provided it retrieves the entry using it, else
            it retrieve the replica using the 'suffix'.

            @param suffix : suffix stored in that replica (online update)
            @param replica_dn: DN of the replica (online update)
            @param replica_entry: Entry of a replica (offline update)
            @param properties: dictionary of properties
            Supported properties are:
                REPLICA_SUFFIX
                REPLICA_ID
                REPLICA_TYPE
                REPLICA_LEGACY_CONS
                REPLICA_BINDDN
                REPLICA_PURGE_INTERVAL
                REPLICA_PURGE_DELAY
                REPLICA_PRECISE_PURGING
                REPLICA_REFERRAL
                REPLICA_FLAGS

            @return None

            @raise ValueError: if unknown properties
                    ValueError: if invalid replica_entry
                    ValueError: if replica_dn or suffix are not associated to a replica
                    InvalidArgumentError: If missing mandatory parameter

        '''

        # No properties provided
        if len(properties) == 0:
            return

        # check that the given properties are valid
        for prop in properties:
            # skip the prefix to add/del value
            if not inProperties(prop, REPLICA_PROPNAME_TO_ATTRNAME):
                raise ValueError("unknown property: %s" % prop)
            else:
                self.log.debug("setProperties: %s:%s" % (prop, properties[prop]))

        # At least we need to have suffix/replica_dn/replica_entry
        if not suffix and not replica_dn and not replica_entry:
            raise InvalidArgumentError("suffix and replica_dn and replica_entry are missing")

        # the caller provides a set of properties to set into a replica entry
        if replica_entry:
            if not isinstance(replica_entry, Entry):
                raise ValueError("invalid instance of the replica_entry")

            # that is fine, now set the values
            for prop in properties:
                val = rawProperty(prop)

                # for Entry update it is a replace
                replica_entry.update({REPLICA_PROPNAME_TO_ATTRNAME[val]: properties[prop]})

            return

        # If it provides the suffix or the replicaDN, replica.list will
        # return the appropriate entry
        ents = self.conn.replica.list(suffix=suffix, replica_dn=replica_dn)
        if len(ents) != 1:
            if replica_dn:
                raise ValueError("invalid replica DN: %s" % replica_dn)
            else:
                raise ValueError("invalid suffix: %s" % suffix)

        # build the MODS
        mods = []
        for prop in properties:
            # take the operation type from the property name
            val = rawProperty(prop)
            if str(prop).startswith('+'):
                op = ldap.MOD_ADD
            elif str(prop).startswith('-'):
                op = ldap.MOD_DELETE
            else:
                op = ldap.MOD_REPLACE

            mods.append((op, REPLICA_PROPNAME_TO_ATTRNAME[val], properties[prop]))

        # that is fine now to apply the MOD
        self.conn.modify_s(ents[0].dn, mods)

    def getProperties(self, suffix=None, replica_dn=None, replica_entry=None, properties=None):
        raise NotImplementedError

    def create(self, suffix=None, role=None, rid=None, args=None):
        """
            Create a replica entry on an existing suffix.

            @param suffix - dn of suffix
            @param role   - REPLICAROLE_MASTER, REPLICAROLE_HUB or REPLICAROLE_CONSUMER
            @param rid    - number that identify the supplier replica (role=REPLICAROLE_MASTER) in the topology.
                            For hub/consumer (role=REPLICAROLE_HUB or REPLICAROLE_CONSUMER), rid value is not used.
                            This parameter is mandatory for supplier.

            @param args   - dictionnary of initial replica's properties
                Supported properties are:
                    REPLICA_SUFFIX
                    REPLICA_ID
                    REPLICA_TYPE
                    REPLICA_LEGACY_CONS    ['off']
                    REPLICA_BINDDN         [defaultProperties[REPLICATION_BIND_DN]]
                    REPLICA_PURGE_INTERVAL
                    REPLICA_PURGE_DELAY
                    REPLICA_PRECISE_PURGING
                    REPLICA_REFERRAL
                    REPLICA_FLAGS

            @return replica DN

            @raise InvalidArgumentError - if missing mandatory arguments
                   ValueError - argument with invalid value

        """
        # Check validity of role
        if not role:
            self.log.fatal("Replica.create: replica role not specify (REPLICAROLE_*)")
            raise InvalidArgumentError("role missing")

        if not Replica._valid_role(role):
            self.log.fatal("enableReplication: replica role invalid (%s) " % role)
            raise ValueError("invalid role: %s" % role)

        # check the validity of 'rid'
        if not Replica._valid_rid(role, rid=rid):
            self.log.fatal("Replica.create: replica role is master but 'rid' is missing or invalid value")
            raise InvalidArgumentError("rid missing or invalid value")

        # check the validity of the suffix
        if not suffix:
            self.log.fatal("Replica.create: suffix is missing")
            raise InvalidArgumentError("suffix missing")
        else:
            nsuffix = normalizeDN(suffix)

        # role is fine, set the replica type
        if role == REPLICAROLE_MASTER:
            rtype = REPLICA_RDWR_TYPE
        else:
            rtype = REPLICA_RDONLY_TYPE

        # Set the properties provided as mandatory parameter
        # The attribute name is not prefixed '+'/'-' => ldap.MOD_REPLACE
        properties = {REPLICA_SUFFIX:  nsuffix,
                      REPLICA_ID:      str(rid),
                      REPLICA_TYPE:    str(rtype)}

        # If the properties in args are valid
        # add them to the 'properties' dictionary
        # The attribute name may be prefixed '+'/'-' => keep MOD type as provided in args
        if args:
            for prop in args:
                if not inProperties(prop, REPLICA_PROPNAME_TO_ATTRNAME):
                    raise ValueError("unknown property: %s" % prop)
                properties[prop] = args[prop]

        # Now set default values of unset properties
        Replica._set_or_default(REPLICA_LEGACY_CONS, properties, 'off')
        Replica._set_or_default(REPLICA_BINDDN, properties, [defaultProperties[REPLICATION_BIND_DN]])

        if role != REPLICAROLE_CONSUMER:
            properties[REPLICA_FLAGS] = "1"

        # create replica entry in mapping-tree
        mtents = self.conn.mappingtree.list(suffix=nsuffix)
        mtent = mtents[0]
        dn_replica = ','.join((RDN_REPLICA, mtent.dn))
        try:
            entry = self.conn.getEntry(dn_replica, ldap.SCOPE_BASE)
            self.log.warn("Already setup replica for suffix %r" % nsuffix)
            self.conn.suffixes.setdefault(nsuffix, {})
            self.conn.replica.setProperties(replica_dn=dn_replica, properties=properties)
            return dn_replica
        except ldap.NO_SUCH_OBJECT:
            entry = None

        #
        # Now create the replica entry
        #
        entry = Entry(dn_replica)
        entry.setValues("objectclass", "top", REPLICA_OBJECTCLASS_VALUE, "extensibleobject")
        self.conn.replica.setProperties(replica_entry=entry, properties=properties)
        self.conn.add_s(entry)

        # check if the entry exists TODO better to raise!
        self.conn._test_entry(dn_replica, ldap.SCOPE_BASE)

        self.conn.suffixes[nsuffix] = {'dn': dn_replica, 'type': rtype}
        return dn_replica

    def deleteAgreements(self, suffix=None):
        '''
        Delete all the agreements for the suffix
        '''
        # check the validity of the suffix
        if not suffix:
            self.log.fatal("disableReplication: suffix is missing")
            raise InvalidArgumentError("suffix missing")
        else:
            nsuffix = normalizeDN(suffix)

        # Build the replica config DN
        mtents = self.conn.mappingtree.list(suffix=nsuffix)
        mtent = mtents[0]
        dn_replica = ','.join((RDN_REPLICA, mtent.dn))

        # Delete the agreements
        try:
            agmts = self.conn.agreement.list(suffix=suffix)
            for agmt in agmts:
                try:
                    self.conn.delete_s(agmt.dn)
                except ldap.LDAPError, e:
                    self.log.fatal('Failed to delete replica agreement (%s), error: %s' %
                        (admt.dn, e.message('desc')))
                    raise ldap.LDAPError
        except ldap.LDAPError, e:
            self.log.fatal('Failed to search for replication agreements under (%s), error: %s' %
                (dn_replica, e.message('desc')))
            raise ldap.LDAPError

    def disableReplication(self, suffix=None):
        '''
            Delete a replica related to the provided suffix.
            If this replica role was REPLICAROLE_HUB or REPLICAROLE_MASTER, it also deletes the changelog
            associated to that replica.
            If it exists some replication agreement below that replica, they are deleted.

            @param suffix - dn of suffix
            @return None
            @raise InvalidArgumentError - if suffix is missing
                   ldap.LDAPError - for all other update failures

        '''

        # check the validity of the suffix
        if not suffix:
            self.log.fatal("disableReplication: suffix is missing")
            raise InvalidArgumentError("suffix missing")
        else:
            nsuffix = normalizeDN(suffix)

        # Build the replica config DN
        mtents = self.conn.mappingtree.list(suffix=nsuffix)
        mtent = mtents[0]
        dn_replica = ','.join((RDN_REPLICA, mtent.dn))

        # Delete the agreements
        try:
            self.deleteAgreements(nsuffix)
        except ldap.LDAPError, e:
            self.log.fatal('Failed to delete replica agreements!')
            raise ldap.LDAPError

        # Delete the replica
        try:
            self.conn.delete_s(dn_replica)
        except ldap.LDAPError, e:
            self.log.fatal('Failed to delete replica configuration (%s), error: %s' % (dn_replica, e.message('desc')))
            raise ldap.LDAPError

        # Delete the changelog
        try:
            self.conn.changelog.delete()
        except ldap.LDAPError, e:
            self.log.error('Failed to delete changelog: ' + e.message['desc'])
            raise ldap.LDAPError

    def enableReplication(self, suffix=None, role=None, replicaId=CONSUMER_REPLICAID, binddn=None):
        if not suffix:
            self.log.fatal("enableReplication: suffix not specified")
            raise ValueError("suffix missing")

        if not role:
            self.log.fatal("enableReplication: replica role not specify (REPLICAROLE_*)")
            raise ValueError("role missing")

        #
        # Check the validity of the parameters
        #

        # First role and replicaID
        if role != REPLICAROLE_MASTER and role != REPLICAROLE_HUB and role != REPLICAROLE_CONSUMER:
            self.log.fatal("enableReplication: replica role invalid (%s) " % role)
            raise ValueError("invalid role: %s" % role)

            # master
            replica_type = REPLICA_RDWR_TYPE
        else:
            # hub or consumer
            replica_type = REPLICA_RDONLY_TYPE

        if role == REPLICAROLE_MASTER:
            # check the replicaId [1..CONSUMER_REPLICAID[
            if not decimal.Decimal(replicaId) or (replicaId <= 0) or (replicaId >= CONSUMER_REPLICAID):
                self.log.fatal("enableReplication: invalid replicaId (%s) for a RW replica" % replicaId)
                raise ValueError("invalid replicaId %d (expected [1..CONSUMER_REPLICAID[" % replicaId)
        elif replicaId != CONSUMER_REPLICAID:
            # check the replicaId is CONSUMER_REPLICAID
            self.log.fatal("enableReplication: invalid replicaId (%s) for a Read replica (expected %d)" %
                (replicaId, CONSUMER_REPLICAID))
            raise ValueError("invalid replicaId: %d for HUB/CONSUMER replicaId is CONSUMER_REPLICAID" % replicaId)

        # Now check we have a suffix
        entries_backend = self.conn.backend.list(suffix=suffix)
        if not entries_backend:
            self.log.fatal("enableReplication: enable to retrieve the backend for %s" % suffix)
            raise ValueError("no backend for suffix %s" % suffix)

        ent = entries_backend[0]
        if normalizeDN(suffix) != normalizeDN(ent.getValue('nsslapd-suffix')):
            self.log.warning("enableReplication: suffix (%s) and backend suffix (%s) differs" %
                (suffix, entries_backend[0].nsslapd - suffix))
            pass

        # Now prepare the bindDN property
        if not binddn:
            binddn = defaultProperties.get(REPLICATION_BIND_DN, None)
            if not binddn:
                # weird, internal error we do not retrieve the default replication bind DN
                # this replica will not be updatable through replication until the binddn
                # property will be set
                self.log.warning("enableReplication: binddn not provided and default value unavailable")
                pass

        # Now do the effectif job
        # First add the changelog if master/hub
        if (role == REPLICAROLE_MASTER) or (role == REPLICAROLE_HUB):
            self.conn.changelog.create()

        # Second create the default replica manager entry if it does not exist
        # it should not be called from here but for the moment I am unsure when to create it elsewhere
        self.conn.replica.create_repl_manager()

        # then enable replication
        properties = {REPLICA_BINDDN: [binddn]}
        ret = self.conn.replica.create(suffix=suffix, role=role, rid=replicaId, args=properties)

        return ret

    def check_init(self, agmtdn):
        """returns tuple - first element is done/not done, 2nd is no error/has error
            @param agmtdn - the agreement dn
        """
        done, hasError = False, 0
        attrlist = ['cn', 'nsds5BeginReplicaRefresh', 'nsds5replicaUpdateInProgress',
                    'nsds5ReplicaLastInitStatus', 'nsds5ReplicaLastInitStart',
                    'nsds5ReplicaLastInitEnd']
        try:
            entry = self.conn.getEntry(
                agmtdn, ldap.SCOPE_BASE, "(objectclass=*)", attrlist)
        except NoSuchEntryError:
            self.log.exception("Error reading status from agreement %r" % agmtdn)
            hasError = 1
        else:
            refresh = entry.nsds5BeginReplicaRefresh
            inprogress = entry.nsds5replicaUpdateInProgress
            status = entry.nsds5ReplicaLastInitStatus
            if not refresh:  # done - check status
                if not status:
                    print "No status yet"
                elif status.find("replica busy") > -1:
                    print "Update failed - replica busy - status", status
                    done = True
                    hasError = 2
                elif status.find("Total update succeeded") > -1:
                    print "Update succeeded: status ", status
                    done = True
                elif inprogress.lower() == 'true':
                    print "Update in progress yet not in progress: status ", status
                else:
                    print "Update failed: status", status
                    hasError = 1
                    done = True
            elif self.verbose:
                print "Update in progress: status", status

        return done, hasError

    def start_and_wait(self, agmtdn):
        """@param agmtdn - agreement dn"""
        rc = self.start_async(agmtdn)
        if not rc:
            rc = self.wait_init(agmtdn)
            if rc == 2:  # replica busy - retry
                rc = self.start_and_wait(agmtdn)
        return rc

    def wait_init(self, agmtdn):
        """Initialize replication and wait for completion.
        @oaram agmtdn - agreement dn
        """
        done = False
        haserror = 0
        while not done and not haserror:
            time.sleep(1)  # give it a few seconds to get going
            done, haserror = self.check_init(agmtdn)
        return haserror

    def start_async(self, agmtdn):
        """Initialize replication without waiting.
            @param agmtdn - agreement dn
        """
        self.log.info("Starting async replication %s" % agmtdn)
        mod = [(ldap.MOD_ADD, 'nsds5BeginReplicaRefresh', 'start')]
        self.conn.modify_s(agmtdn, mod)

    def keep_in_sync(self, agmtdn):
        """
        @param agmtdn -
        """
        self.log.info("Setting agreement for continuous replication")
        raise NotImplementedError("Check nsds5replicaupdateschedule before writing!")

    def ruv(self, suffix, tryrepl=False):
        """return a replica update vector for the given suffix.

            @param suffix - eg. 'o=netscapeRoot'

            @raises NoSuchEntryError if missing
        """
        filt = "(&(nsUniqueID=%s)(objectclass=%s))" % (REPLICA_RUV_UUID, REPLICA_OC_TOMBSTONE)
        attrs = ['nsds50ruv', 'nsruvReplicaLastModified']
        ents = self.conn.search_s(suffix, ldap.SCOPE_SUBTREE, filt, attrs)
        ent = None
        if ents and (len(ents) > 0):
            ent = ents[0]
        elif tryrepl:
            self.log.warn("Could not get RUV from %r entry - trying cn=replica" % suffix)
            ensuffix = escapeDNValue(normalizeDN(suffix))
            dn = ','.join(("cn=replica", "cn=%s" % ensuffix, DN_MAPPING_TREE))
            ents = self.conn.search_s(dn, ldap.SCOPE_BASE, "objectclass=*", attrs)

        if ents and (len(ents) > 0):
            ent = ents[0]
            self.log.debug("RUV entry is %r" % ent)
            return RUV(ent)

        raise NoSuchEntryError("RUV not found: suffix: %r" % suffix)



