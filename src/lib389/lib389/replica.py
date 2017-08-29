# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import os
import decimal
import time
from lib389._constants import *
from lib389.properties import *
from lib389.utils import normalizeDN, escapeDNValue, ensure_bytes
from lib389._replication import RUV
from lib389.repltools import ReplTools
from lib389 import DirSrv, Entry, NoSuchEntryError, InvalidArgumentError
from lib389._mapped_object import DSLdapObjects, DSLdapObject


class ReplicaLegacy(object):
    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in ReplicaLegacy.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def _get_mt_entry(self, suffix):
        """Return the replica dn of the given suffix."""
        mtent = self.conn.mappingtree.list(suffix=suffix)[0]
        return ','.join(("cn=replica", mtent.dn))

    @staticmethod
    def _valid_role(role):
        if role != ReplicaRole.MASTER and \
           role != ReplicaRole.HUB and \
           role != ReplicaRole.CONSUMER:
            return False
        else:
            return True

    @staticmethod
    def _valid_rid(role, rid=None):
        if role == ReplicaRole.MASTER:
            if not decimal.Decimal(rid) or \
               (rid <= 0) or \
               (rid >= CONSUMER_REPLICAID):
                return False
        else:
            if rid and (rid != CONSUMER_REPLICAID):
                return False
        return True

    @staticmethod
    def _set_or_default(attribute, properties, default):
        '''
            If 'attribute' or '+attribute' or '-attribute' exist in
            'properties' it does nothing. Else it set (ldap.MOD_REPLACE)
            'attribute' in 'properties'
            to the 'default' value
        '''
        add_attribute = "+%s" % attribute
        del_attribute = "-%s" % attribute
        if attribute in properties or \
           add_attribute in properties or \
           del_attribute in properties:
            return
        properties[attribute] = default

    def create_repl_manager(self, repl_manager_dn=None, repl_manager_pw=None):
        '''
            Create an entry that will be used to bind as replica manager.

            @param repl_manager_dn - DN of the bind entry. If not provided use
                                     the default one
            @param repl_manager_pw - Password of the entry. If not provide use
                                     the default one

            @return None

            @raise - KeyError if can not find valid values of Bind DN and Pwd
        '''

        # check the DN and PW
        try:
            repl_manager_dn = repl_manager_dn or \
                defaultProperties[REPLICATION_BIND_DN]
            repl_manager_pw = repl_manager_pw or \
                defaultProperties[REPLICATION_BIND_PW]
            if not repl_manager_dn or not repl_manager_pw:
                raise KeyError
        except KeyError:
            if not repl_manager_pw:
                self.log.warning("replica_createReplMgr: bind DN password " +
                                 "not specified")
            if not repl_manager_dn:
                self.log.warning("replica_createReplMgr: bind DN not " +
                                 "specified")
            raise

        # if the replication manager entry already exists, ust return
        try:
            entries = self.search_s(repl_manager_dn, ldap.SCOPE_BASE,
                                    "objectclass=*")
            if entries:
                # it already exist, fine
                return
        except ldap.NO_SUCH_OBJECT:
            pass

        # ok it does not exist, create it
        try:
            attrs = {'nsIdleTimeout': '0',
                     'passwordExpirationTime': '20381010000000Z'}
            self.conn.setupBindDN(repl_manager_dn, repl_manager_pw, attrs)
        except ldap.ALREADY_EXISTS:
            self.log.warn("User already exists (weird we just checked: %s " %
                          repl_manager_dn)

    def list(self, suffix=None, replica_dn=None):
        """
            Return a list of replica entries.
            If 'replica_dn' is specified it returns the related entry.
            If 'suffix' is specified it returns the replica that is configured
            for that 'suffix'.
            If both 'replica_dn' and 'suffix' are specified it returns the
            'replica_dn' entry.
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
            filtr = ("(&(objectclass=%s)(%s=%s))" %
                     (REPLICA_OBJECTCLASS_VALUE,
                      REPLICA_PROPNAME_TO_ATTRNAME[REPLICA_SUFFIX], suffix))
        else:
            base = DN_MAPPING_TREE
            filtr = "(objectclass=%s)" % REPLICA_OBJECTCLASS_VALUE

        # now do the effective search
        ents = self.conn.search_s(base, ldap.SCOPE_SUBTREE, filtr)
        return ents

    def setProperties(self, suffix=None, replica_dn=None, replica_entry=None,
                      properties=None):
        '''
            Set the properties of the replica. If an 'replica_entry' (Entry) is
            provided, it updates the entry, else it updates the entry on the
            server. If the 'replica_dn' is provided it retrieves the entry
            using it, else it retrieve the replica using the 'suffix'.

            @param suffix : suffix stored in that replica (online update)
            @param replica_dn: DN of the replica (online update)
            @param replica_entry: Entry of a replica (offline update)
            @param properties: dictionary of properties
            Supported properties are:
                REPLICA_SUFFIX
                REPLICA_ID
                REPLICA_TYPE
                REPLICA_BINDDN
                REPLICA_PURGE_DELAY
                REPLICA_PRECISE_PURGING
                REPLICA_REFERRAL
                REPLICA_FLAGS

            @return None

            @raise ValueError: if unknown properties
                    ValueError: if invalid replica_entry
                    ValueError: if replica_dn or suffix are not associated to
                                a replica

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
                self.log.debug("setProperties: %s:%s" %
                               (prop, properties[prop]))

        # At least we need to have suffix/replica_dn/replica_entry
        if not suffix and not replica_dn and not replica_entry:
            raise InvalidArgumentError("suffix and replica_dn and replica_" +
                                       "entry are missing")

        # the caller provides a set of properties to set into a replica entry
        if replica_entry:
            if not isinstance(replica_entry, Entry):
                raise ValueError("invalid instance of the replica_entry")

            # that is fine, now set the values
            for prop in properties:
                val = rawProperty(prop)

                # for Entry update it is a replace
                replica_entry.update({REPLICA_PROPNAME_TO_ATTRNAME[val]:
                                      properties[prop]})

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

            mods.append((op, REPLICA_PROPNAME_TO_ATTRNAME[val],
                         properties[prop]))

        # that is fine now to apply the MOD
        self.conn.modify_s(ents[0].dn, mods)

    def getProperties(self, suffix=None, replica_dn=None, replica_entry=None,
                      properties=None):
        raise NotImplementedError

    def get_role(self, suffix):
        """Return the replica role

        @return: ReplicaRole.MASTER, ReplicaRole.HUB, ReplicaRole.CONSUMER
        """

        filter_str = ('(&(objectclass=nsDS5Replica)(nsDS5ReplicaRoot=%s))'.format(suffix))

        try:
            replica_entry = self.conn.search_s(DN_CONFIG, ldap.SCOPE_SUBTREE,
                                               filter_str)
            if replica_entry:
                repltype = replica_entry[0].getValue(REPL_TYPE)
                replflags = replica_entry[0].getValue(REPL_FLAGS)

                if repltype == REPLICA_RDWR_TYPE and replflags == REPLICA_FLAGS_WRITE:
                    replicarole = ReplicaRole.MASTER
                elif repltype == REPLICA_RDONLY_TYPE and replflags == REPLICA_FLAGS_WRITE:
                    replicarole = ReplicaRole.HUB
                elif repltype == REPLICA_RDONLY_TYPE and replflags == REPLICA_FLAGS_RDONLY:
                    replicarole = ReplicaRole.CONSUMER
                else:
                    raise ValueError("Failed to determine a replica role")

                return replicarole
        except ldap.LDAPError as e:
            raise ValueError('Failed to get replica entry: %s' % str(e))

    def create(self, suffix=None, role=None, rid=None, args=None):
        """
            Create a replica entry on an existing suffix.

            @param suffix - dn of suffix
            @param role   - ReplicaRole.MASTER, ReplicaRole.HUB or
                            ReplicaRole.CONSUMER
            @param rid    - number that identify the supplier replica
                            (role=ReplicaRole.MASTER) in the topology.  For
                            hub/consumer (role=ReplicaRole.HUB or
                            ReplicaRole.CONSUMER), rid value is not used.
                            This parameter is mandatory for supplier.

            @param args   - dictionary of initial replica's properties
                Supported properties are:
                    REPLICA_SUFFIX
                    REPLICA_ID
                    REPLICA_TYPE
                    REPLICA_BINDDN [defaultProperties[REPLICATION_BIND_DN]]
                    REPLICA_PURGE_DELAY
                    REPLICA_PRECISE_PURGING
                    REPLICA_REFERRAL
                    REPLICA_FLAGS

            @return replica DN

            @raise InvalidArgumentError - if missing mandatory arguments
                   ValueError - argument with invalid value
                   LDAPError - failed to add replica entry

        """
        # Check validity of role
        if not role:
            self.log.fatal("Replica.create: replica role is not specified (ReplicaRole.*)")
            raise InvalidArgumentError("role missing")

        if not ReplicaLegacy._valid_role(role):
            self.log.fatal("enableReplication: replica role invalid (%s) " % role)
            raise ValueError("invalid role: %s" % role)

        # check the validity of 'rid'
        if not ReplicaLegacy._valid_rid(role, rid=rid):
            self.log.fatal("Replica.create: replica role is master but 'rid'" +
                           " is missing or invalid value")
            raise InvalidArgumentError("rid missing or invalid value")

        # check the validity of the suffix
        if not suffix:
            self.log.fatal("Replica.create: suffix is missing")
            raise InvalidArgumentError("suffix missing")
        else:
            nsuffix = normalizeDN(suffix)

        # role is fine, set the replica type
        if role == ReplicaRole.MASTER:
            rtype = REPLICA_RDWR_TYPE
        else:
            rtype = REPLICA_RDONLY_TYPE

        # Set the properties provided as mandatory parameter
        # The attribute name is not prefixed '+'/'-' => ldap.MOD_REPLACE
        properties = {REPLICA_SUFFIX: nsuffix,
                      REPLICA_ID: str(rid),
                      REPLICA_TYPE: str(rtype)}

        # If the properties in args are valid
        # add them to the 'properties' dictionary
        # The attribute name may be prefixed '+'/'-' => keep MOD type as
        # provided in args
        if args:
            for prop in args:
                if not inProperties(prop, REPLICA_PROPNAME_TO_ATTRNAME):
                    raise ValueError("unknown property: %s" % prop)
                properties[prop] = args[prop]

        # Now set default values of unset properties
        ReplicaLegacy._set_or_default(REPLICA_BINDDN, properties,
                                [defaultProperties[REPLICATION_BIND_DN]])

        # Set flags explicitly, so it will be more readable
        if role == ReplicaRole.CONSUMER:
            properties[REPLICA_FLAGS] = str(REPLICA_FLAGS_RDONLY)
        else:
            properties[REPLICA_FLAGS] = str(REPLICA_FLAGS_WRITE)

        #
        # Check if replica entry is already in the mapping-tree
        #
        mtents = self.conn.mappingtree.list(suffix=nsuffix)
        mtent = mtents[0]
        dn_replica = ','.join((RDN_REPLICA, mtent.dn))
        try:
            entry = self.conn.getEntry(dn_replica, ldap.SCOPE_BASE)
            self.log.warn("Already setup replica for suffix %r" % nsuffix)
            self.conn.suffixes.setdefault(nsuffix, {})
            self.conn.replica.setProperties(replica_dn=dn_replica,
                                            properties=properties)
            return dn_replica
        except ldap.NO_SUCH_OBJECT:
            entry = None

        #
        # Now create the replica entry
        #
        entry = Entry(dn_replica)
        entry.setValues("objectclass", "top", "nsDS5Replica",
                        "extensibleobject")
        self.conn.replica.setProperties(replica_entry=entry,
                                        properties=properties)
        self.conn.add_s(entry)
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
                except ldap.LDAPError as e:
                    self.log.fatal('Failed to delete replica agreement (%s),' +
                                   ' error: %s' %
                                   (agmt.dn, str(e)))
                    raise
        except ldap.LDAPError as e:
            self.log.fatal('Failed to search for replication agreements ' +
                           'under (%s), error: %s' % (dn_replica, str(e)))
            raise

    def disableReplication(self, suffix=None):
        '''
            Delete a replica related to the provided suffix.
            If this replica role was ReplicaRole.HUB or ReplicaRole.MASTER, it
            also deletes the changelog associated to that replica.  If it
            exists some replication agreement below that replica, they are
            deleted.

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
        except ldap.LDAPError as e:
            self.log.fatal('Failed to delete replica agreements!')
            raise

        # Delete the replica
        try:
            self.conn.delete_s(dn_replica)
        except ldap.LDAPError as e:
            self.log.fatal('Failed to delete replica configuration ' +
                           '(%s), error: %s' % (dn_replica, str(e)))
            raise

    def enableReplication(self, suffix=None, role=None,
                          replicaId=CONSUMER_REPLICAID,
                          properties=None):
        if not suffix:
            self.log.fatal("enableReplication: suffix not specified")
            raise ValueError("suffix missing")

        if not role:
            self.log.fatal("enableReplication: replica role not specify " +
                           "(ReplicaRole.*)")
            raise ValueError("role missing")

        #
        # Check the validity of the parameters
        #

        # First role and replicaID
        if role != ReplicaRole.MASTER and \
           role != ReplicaRole.HUB and \
           role != ReplicaRole.CONSUMER:
            self.log.fatal("enableReplication: replica role invalid (%s) " %
                           role)
            raise ValueError("invalid role: %s" % role)

        if role == ReplicaRole.MASTER:
            # check the replicaId [1..CONSUMER_REPLICAID[
            if not decimal.Decimal(replicaId) or \
               (replicaId <= 0) or \
               (replicaId >= CONSUMER_REPLICAID):
                self.log.fatal("enableReplication: invalid replicaId (%s) "
                               "for a RW replica" % replicaId)
                raise ValueError("invalid replicaId %d (expected [1.."
                                 "CONSUMER_REPLICAID]" % replicaId)
        elif replicaId != CONSUMER_REPLICAID:
            # check the replicaId is CONSUMER_REPLICAID
            self.log.fatal("enableReplication: invalid replicaId (%s) for a "
                           "Read replica (expected %d)" %
                           (replicaId, CONSUMER_REPLICAID))
            raise ValueError("invalid replicaId: %d for HUB/CONSUMER "
                             "replicaId is CONSUMER_REPLICAID" % replicaId)

        # Now check we have a suffix
        entries_backend = self.conn.backend.list(suffix=suffix)
        if not entries_backend:
            self.log.fatal("enableReplication: unable to retrieve the " +
                           "backend for %s" % suffix)
            raise ValueError("no backend for suffix %s" % suffix)

        ent = entries_backend[0]
        if normalizeDN(suffix) != normalizeDN(ent.getValue('nsslapd-suffix')):
            self.log.warning("enableReplication: suffix (%s) and backend "
                             "suffix (%s) differs" %
                             (suffix, entries_backend[0].nsslapd - suffix))
            pass

        # Now prepare the bindDN property
        if properties is None:
            properties = {REPLICA_BINDDN:
                          defaultProperties.get(REPLICATION_BIND_DN, None)}
        elif REPLICA_BINDDN not in properties:
            properties[REPLICA_BINDDN] = \
                defaultProperties.get(REPLICATION_BIND_DN, None)
            if not properties[REPLICA_BINDDN]:
                # weird, internal error we do not retrieve the default
                # replication bind DN this replica will not be updatable
                # through replication until the binddn property will be set
                self.log.warning("enableReplication: binddn not provided and" +
                                 " default value unavailable")
                pass

        # First add the changelog if master/hub
        if (role == ReplicaRole.MASTER) or (role == ReplicaRole.HUB):
            self.conn.changelog.create()

        # Second create the default replica manager entry if it does not exist
        # it should not be called from here but for the moment I am unsure when
        # to create it elsewhere
        self.conn.replica.create_repl_manager()
        # then enable replication
        ret = self.conn.replica.create(suffix=suffix, role=role, rid=replicaId,
                                       args=properties)

        return ret

    def check_init(self, agmtdn):
        """Check that a total update has completed
        @returns tuple - first element is done/not done, 2nd is no error/has
                        error
        @param agmtdn - the agreement dn
        """
        done, hasError = False, 0
        attrlist = ['cn',
                    'nsds5BeginReplicaRefresh',
                    'nsds5replicaUpdateInProgress',
                    'nsds5ReplicaLastInitStatus',
                    'nsds5ReplicaLastInitStart',
                    'nsds5ReplicaLastInitEnd']
        try:
            entry = self.conn.getEntry(
                agmtdn, ldap.SCOPE_BASE, "(objectclass=*)", attrlist)
        except NoSuchEntryError:
            self.log.exception("Error reading status from agreement %r" %
                               agmtdn)
            hasError = 1
        else:
            refresh = entry.nsds5BeginReplicaRefresh
            inprogress = entry.nsds5replicaUpdateInProgress
            status = entry.nsds5ReplicaLastInitStatus
            if not refresh:  # done - check status
                if not status:
                    print("No status yet")
                elif status.find(ensure_bytes("replica busy")) > -1:
                    print("Update failed - replica busy - status", status)
                    done = True
                    hasError = 2
                elif status.find(ensure_bytes("Total update succeeded")) > -1:
                    print("Update succeeded: status ", status)
                    done = True
                elif inprogress.lower() == ensure_bytes('true'):
                    print("Update in progress yet not in progress: status ",
                          status)
                else:
                    print("Update failed: status", status)
                    hasError = 1
                    done = True
            elif self.verbose:
                print("Update in progress: status", status)

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
        raise NotImplementedError("Check nsds5replicaupdateschedule before " +
                                  "writing!")

    def ruv(self, suffix, tryrepl=False):
        """return a replica update vector for the given suffix.

            @param suffix - eg. 'o=netscapeRoot'

            @raises NoSuchEntryError if missing
        """
        filt = "(&(nsUniqueID=%s)(objectclass=%s))" % (REPLICA_RUV_UUID,
                                                       REPLICA_OC_TOMBSTONE)
        attrs = ['nsds50ruv', 'nsruvReplicaLastModified']
        ents = self.conn.search_s(suffix, ldap.SCOPE_SUBTREE, filt, attrs)
        ent = None
        if ents and (len(ents) > 0):
            ent = ents[0]
        elif tryrepl:
            self.log.warn("Could not get RUV from %r entry -" +
                          " trying cn=replica" % suffix)
            ensuffix = escapeDNValue(normalizeDN(suffix))
            dn = ','.join(("cn=replica", "cn=%s" % ensuffix, DN_MAPPING_TREE))
            ents = self.conn.search_s(dn, ldap.SCOPE_BASE, "objectclass=*",
                                      attrs)

        if ents and (len(ents) > 0):
            ent = ents[0]
            self.log.debug("RUV entry is %r" % ent)
            return RUV(ent)

        raise NoSuchEntryError("RUV not found: suffix: %r" % suffix)

    def promote(self, suffix, newrole, rid=None, binddn=None):
        """
        Promote the replica

        @raise ValueError
        """

        if newrole != ReplicaRole.MASTER and newrole != ReplicaRole.HUB:
            raise ValueError('Can only prompt replica to "master" or "hub"')

        if not binddn:
            raise ValueError('"binddn" required for promotion')

        if newrole == ReplicaRole.MASTER:
            if not rid:
                raise ValueError('"rid" required for promotion')
        else:
            # Must be a hub - set the rid
            rid = CONSUMER_REPLICAID

        # Get replica entry
        filter_str = ('(&(objectclass=nsDS5Replica)(nsDS5ReplicaRoot=%s))' %
                      suffix)
        try:
            replica_entry = self.conn.search_s(DN_CONFIG, ldap.SCOPE_SUBTREE,
                                               filter_str)
        except ldap.LDAPError as e:
            raise ValueError('Failed to get replica entry: %s' % str(e))

        # Check the role type
        replicarole = self.get_role(suffix)

        if newrole.value < replicarole.value:
            raise ValueError('Can not promote replica to lower role: {} -> {}'.format(replicarole.name, newrole.name))

        #
        # Create the changelog
        #
        try:
            self.conn.changelog.create()
        except ldap.LDAPError as e:
            raise ValueError('Failed to create changelog: %s' % str(e))

        #
        # Check that a RID was provided, and its a valid number
        #
        if newrole == ReplicaRole.MASTER:
            try:
                rid = int(rid)
            except:
                # Not a number
                raise ValueError('"rid" value (%s) is not a number' % str(rid))

            if rid < 1 and rid > 65534:
                raise ValueError('"rid" value (%d) is not in range ' +
                                 ' 1 - 65534' % rid)

        #
        # Set bind dn
        #
        try:
            self.conn.modify_s(replica_entry[0].dn, [(ldap.MOD_REPLACE,
                               REPL_BINDDN, binddn)])
        except ldap.LDAPError as e:
            raise ValueError('Failed to update replica: ' + str(e))

        #
        # Set the replica type and flags
        #
        if newrole == ReplicaRole.HUB:
            try:
                self.conn.modify_s(replica_entry[0].dn,
                                   [(ldap.MOD_REPLACE, REPL_TYPE, '2'),
                                    (ldap.MOD_REPLACE, REPL_FLAGS, '1')])
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        else:  # master
            try:
                self.conn.modify_s(replica_entry[0].dn,
                                   [(ldap.MOD_REPLACE, REPL_TYPE, '3'),
                                    (ldap.MOD_REPLACE, REPL_FLAGS, '1'),
                                    (ldap.MOD_REPLACE, REPL_ID, str(rid))])
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))

    def demote(self, suffix, newrole):
        """
        Demote a replica to a hub or consumer

        @raise ValueError
        """
        if newrole != ReplicaRole.CONSUMER and newrole != ReplicaRole.HUB:
            raise ValueError('Can only demote replica to "hub" or "consumer"')

        # Get replica entry
        filter_str = ('(&(objectclass=nsDS5Replica)(nsDS5ReplicaRoot=%s))' %
                      suffix)
        try:
            replica_entry = self.conn.search_s(DN_CONFIG, ldap.SCOPE_SUBTREE,
                                               filter_str)
        except ldap.LDAPError as e:
            raise ValueError('Failed to get replica entry: %s' % str(e))

        # Check the role type
        replicarole = self.get_role(suffix)

        if newrole.value > replicarole.value:
            raise ValueError('Can not demote replica to higher role: {} -> {}'.format(replicarole.name, newrole.name))

        #
        # Demote it - set the replica type and flags
        #
        if newrole == ReplicaRole.HUB:
            flag = '1'
        else:
            flag = '0'
        try:
            self.conn.modify_s(replica_entry[0].dn,
                               [(ldap.MOD_REPLACE, REPL_TYPE, '2'),
                                (ldap.MOD_REPLACE, REPL_FLAGS, flag),
                                (ldap.MOD_REPLACE, REPL_ID,
                                 str(CONSUMER_REPLICAID))])
        except ldap.LDAPError as e:
            raise ValueError('Failed to update replica: ' + str(e))


class Replica(DSLdapObject):
    """Replica DSLdapObject with:
    - must attributes = ['cn', 'nsDS5ReplicaType', 'nsDS5ReplicaRoot',
                         'nsDS5ReplicaBindDN', 'nsDS5ReplicaId']
    - RDN attribute is 'cn'
    - There is one "replica" per backend

    :param instance: A instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    :param batch: Not implemented
    :type batch: bool
    """

    def __init__(self, instance, dn=None, batch=False):
        super(Replica, self).__init__(instance, dn, batch)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn', REPL_TYPE,
                                 REPL_ROOT, REPL_BINDDN, REPL_ID]

        self._create_objectclasses = ['top', 'extensibleObject',
                                      REPLICA_OBJECTCLASS_VALUE]
        self._protected = False
        self._suffix = None

    @staticmethod
    def _valid_role(role):
        """Return True if role is valid

        :param role: MASTER, HUB and CONSUMER
        :type role: ReplicaRole
        :returns: True if the role is a valid role object, otherwise return False
        """

        if role != ReplicaRole.MASTER and \
           role != ReplicaRole.HUB and \
           role != ReplicaRole.CONSUMER:
            return False
        else:
            return True

    @staticmethod
    def _valid_rid(role, rid=None):
        """Return True if rid is valid for the replica role

        :param role: MASTER, HUB and CONSUMER
        :type role: ReplicaRole
        :param rid: Only needed if the role is a MASTER
        :type rid: int
        :returns: True is rid is valid, otherwise return False
        """

        if rid is None:
            return False
        if role == ReplicaRole.MASTER:
            if not decimal.Decimal(rid) or \
               (rid <= 0) or \
               (rid >= CONSUMER_REPLICAID):
                return False
        else:
            if rid and (rid != CONSUMER_REPLICAID):
                return False
        return True

    def delete(self):
        """Delete a replica related to the provided suffix.

        If this replica role was ReplicaRole.HUB or ReplicaRole.MASTER, it
        also deletes the changelog associated to that replica. If it
        exists some replication agreement below that replica, they are
        deleted.

        :returns: None
        :raises: - InvalidArgumentError - if suffix is missing
                 - ldap.LDAPError - for all other update failures
        """

        # Get the suffix
        suffix = self.get_attr_val(REPL_ROOT)
        if not suffix:
            self.log.fatal("disableReplication: suffix is not defined")
            raise InvalidArgumentError("suffix missing")

        # Delete the agreements
        try:
            self.deleteAgreements()
        except ldap.LDAPError as e:
            self.log.fatal('Failed to delete replica agreements!')
            raise e

        # Delete the replica
        try:
            super(Replica, self).delete()
        except ldap.LDAPError as e:
            self.log.fatal('Failed to delete replica configuration ' +
                           '(%s), error: %s' % (self._dn, str(e)))
            raise e

    def deleteAgreements(self):
        """Delete all the agreements for the suffix

        :raises: LDAPError - If failing to delete or search for agreeme        :type binddn: strnts
        """

        # Delete the agreements
        try:
            suffix = self.get_attr_val(REPL_ROOT)
            agmts = self._instance.agreement.list(suffix=suffix)
            for agmt in agmts:
                try:
                    self._instance.delete_s(agmt.dn)
                except ldap.LDAPError as e:
                    self.log.fatal('Failed to delete replica agreement (%s),' +
                                   ' error: %s' % (agmt.dn, str(e)))
                    raise e
        except ldap.LDAPError as e:
            self.log.fatal('Failed to search for replication agreements ' +
                           'under (%s), error: %s' % (self._dn, str(e)))
            raise e

    def promote(self, newrole, binddn=None, rid=None):
        """Promote the replica to hub or master

        :param newrole: The new replication role for the replica: MASTER and HUB
        :type newrole: ReplicaRole
        :param binddn: The replication bind dn - only applied to master
        :type binddn: str
        :param rid: The replication ID, applies only to promotions to "master"
        :type rid: int

        :returns: None
        :raises: ValueError - If replica is not promoted
        """

        if not binddn:
            binddn = defaultProperties[REPLICATION_BIND_DN]

        # Check the role type
        replicarole = self.get_role()
        if newrole.value <= replicarole.value:
            raise ValueError('Can not promote replica to lower or the same role: {} -> {}'.format(replicarole.name, newrole.name))

        if newrole == ReplicaRole.MASTER:
            if not rid:
                raise ValueError('"rid" required for promotion')
        else:
            # Must be a hub - set the rid
            rid = CONSUMER_REPLICAID

        # Create the changelog
        try:
            self._instance.changelog.create()
        except ldap.LDAPError as e:
            raise ValueError('Failed to create changelog: %s' % str(e))

        # Check that a RID was provided, and its a valid number
        if newrole == ReplicaRole.MASTER:
            try:
                rid = int(rid)
            except:
                # Not a number
                raise ValueError('"rid" value (%s) is not a number' % str(rid))

            if rid < 1 and rid > 65534:
                raise ValueError('"rid" value (%d) is not in range ' +
                                 ' 1 - 65534' % rid)

        # Set bind dn
        try:
            self.set(REPL_BINDDN, binddn)
        except ldap.LDAPError as e:
            raise ValueError('Failed to update replica: ' + str(e))

        # Promote it - set the replica type, flags and rid
        if replicarole == ReplicaRole.CONSUMER and newrole == ReplicaRole.HUB:
            try:
                self.set(REPL_FLAGS, str(REPLICA_FLAGS_WRITE))
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        elif replicarole == ReplicaRole.CONSUMER and newrole == ReplicaRole.MASTER:
            try:
                self.apply_mods([(REPL_TYPE, str(REPLICA_RDWR_TYPE)),
                                 (REPL_FLAGS, str(REPLICA_FLAGS_WRITE)),
                                 (REPL_ID, str(rid))])
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        elif replicarole == ReplicaRole.HUB and newrole == ReplicaRole.MASTER:
            try:
                self.apply_mods([(REPL_TYPE, str(REPLICA_RDWR_TYPE)),
                                 (REPL_ID, str(rid))])
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))

    def demote(self, newrole):
        """Demote a replica to a hub or consumer

        :param newrole: The new replication role for the replica: CONSUMER and HUB
        :type newrole: ReplicaRole

        :returns: None
        :raises: ValueError - If replica is not demoted
        """

        # Check the role type
        replicarole = self.get_role()
        if newrole.value >= replicarole.value:
            raise ValueError('Can not demote replica to higher or the same role: {} -> {}'.format(replicarole.name, newrole.name))

        # Demote it - set the replica type, flags and rid
        if replicarole == ReplicaRole.MASTER and newrole == ReplicaRole.HUB:
            try:
                self.apply_mods([(REPL_TYPE, str(REPLICA_RDONLY_TYPE)),
                                 (REPL_ID, str(CONSUMER_REPLICAID))])
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        elif replicarole == ReplicaRole.MASTER and newrole == ReplicaRole.CONSUMER:
            try:
                self.apply_mods([(REPL_TYPE, str(REPLICA_RDONLY_TYPE)),
                                 (REPL_FLAGS, str(REPLICA_FLAGS_RDONLY)),
                                 (REPL_ID, str(CONSUMER_REPLICAID))])
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        elif replicarole == ReplicaRole.HUB and newrole == ReplicaRole.CONSUMER:
            try:
                self.set(REPL_FLAGS, str(REPLICA_FLAGS_RDONLY))
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))

    def get_role(self):
        """Return the replica role

        :returns: ReplicaRole.MASTER, ReplicaRole.HUB, ReplicaRole.CONSUMER
        """

        repltype = self.get_attr_val_int(REPL_TYPE)
        replflags = self.get_attr_val_int(REPL_FLAGS)

        if repltype == REPLICA_RDWR_TYPE and replflags == REPLICA_FLAGS_WRITE:
            replicarole = ReplicaRole.MASTER
        elif repltype == REPLICA_RDONLY_TYPE and replflags == REPLICA_FLAGS_WRITE:
            replicarole = ReplicaRole.HUB
        elif repltype == REPLICA_RDONLY_TYPE and replflags == REPLICA_FLAGS_RDONLY:
            replicarole = ReplicaRole.CONSUMER
        else:
            raise ValueError("Failed to determine a replica role")

        return replicarole

    def check_init(self, agmtdn):
        """Check that a total update has completed

        :param agmtdn: The agreement DN
        :type agmtdn: str

        :returns: A tuple - first element is done/not done, 2nd is no error/has error

        THIS SHOULD BE IN THE NEW AGREEMENT CLASS
        """

        done, hasError = False, 0
        attrlist = ['cn',
                    'nsds5BeginReplicaRefresh',
                    'nsds5replicaUpdateInProgress',
                    'nsds5ReplicaLastInitStatus',
                    'nsds5ReplicaLastInitStart',
                    'nsds5ReplicaLastInitEnd']
        try:
            entry = self._instance.getEntry(
                agmtdn, ldap.SCOPE_BASE, "(objectclass=*)", attrlist)
        except NoSuchEntryError:
            self._log.exception("Error reading status from agreement {}".format(agmtdn))
            hasError = 1
        else:
            refresh = entry.nsds5BeginReplicaRefresh
            inprogress = entry.nsds5replicaUpdateInProgress
            status = entry.nsds5ReplicaLastInitStatus
            if not refresh:  # done - check status
                if not status:
                    print("No status yet")
                elif status.find("replica busy") > -1:
                    print("Update failed - replica busy - status", status)
                    done = True
                    hasError = 2
                elif status.find("Total update succeeded") > -1:
                    print("Update succeeded: status ", status)
                    done = True
                elif inprogress.lower() == 'true':
                    print("Update in progress yet not in progress: status ",
                          status)
                else:
                    print("Update failed: status", status)
                    hasError = 1
                    done = True
            elif self.verbose:
                print("Update in progress: status", status)

        return done, hasError

    def wait_init(self, agmtdn):
        """Initialize replication and wait for completion.

        :param agmtdn: The agreement DN
        :type agmtdn: str

        :returns: 0 if the initialization is complete

        THIS SHOULD BE IN THE NEW AGREEMENT CLASS
        """

        done = False
        haserror = 0
        while not done and not haserror:
            time.sleep(1)  # give it a few seconds to get going
            done, haserror = self.check_init(agmtdn)
        return haserror

    def start_and_wait(self, agmtdn):
        """Initialize an agreement and wait for it to complete

        @param agmtdn - agreement dn
        @return - 0 if successful
        THIS SHOULD BE IN THE NEW AGREEMENT CLASS
        """

        rc = self.start_async(agmtdn)
        if not rc:
            rc = self.wait_init(agmtdn)
            if rc == 2:  # replica busy - retry
                rc = self.start_and_wait(agmtdn)
        return rc

    def start_async(self, agmtdn):
        """Initialize replication without waiting.

        :param agmtdn: The agreement DN
        :type agmtdn: str

        :returns: None

        THIS SHOULD BE IN THE NEW AGREEMENT CLASS
        """

        self._log.info("Starting async replication %s" % agmtdn)
        mod = [(ldap.MOD_ADD, 'nsds5BeginReplicaRefresh', 'start')]
        self._instance.modify_s(agmtdn, mod)

    def get_ruv_entry(self):
        """Return the database RUV entry

        :returns: The database RUV entry
        :raises: ValeuError - If suffix is not setup for replication
                 LDAPError - If there is a problem trying to search for the RUV
        """

        try:
            entry = self._instance.search_s(self._suffix,
                                            ldap.SCOPE_SUBTREE,
                                            REPLICA_RUV_FILTER)
            if entry:
                return entry[0]
            else:
                raise ValueError('Suffix (%s) is not setup for replication' % self._suffix)
        except ldap.LDAPError as e:
            raise e

    def test(self, *replica_dirsrvs):
        """Make a "dummy" update on the the replicated suffix, and check
        all the provided replicas to see if they received the update.

        :param *replica_dirsrvs: DirSrv instance, DirSrv instance, ...
        :type *replica_dirsrvs: list of DirSrv

        :returns: True - if all servers have recevioed the update by this
                  replica, otherwise return False
        :raises: LDAPError - when failing to update/search database
        """

        # Generate a unique test value
        test_value = ('test replication from ' + self._instance.serverid +
                      ' to ' + replica_dirsrvs[0].serverid + ': ' +
                      str(int(time.time())))
        self._instance.modify_s(self._suffix,
            [(ldap.MOD_REPLACE, 'description', test_value)])

        for replica in replica_dirsrvs:
            loop = 0
            replicated = False
            while loop <= 30:
                # Wait 60 seconds before giving up
                try:

                    entry = replica.getEntry(self._suffix,
                                             ldap.SCOPE_BASE,
                                             '(objectclass=*)')
                    if entry.hasValue('description', test_value):
                        replicated = True
                        break
                except ldap.LDAPError as e:
                    raise e
                loop += 1
                time.sleep(2)  # Check the replica every 2 seconds
            if not replicated:
                self._log.error('Replication is not in sync with replica ' +
                                'server (%s)' % replica.serverid)
                return False

        # All is good, remove the test mod from the suffix entry
        self._instance.modify_s(self._suffix,
            [(ldap.MOD_DELETE, 'description', test_value)])

        return True


class Replicas(DSLdapObjects):
    """Replica DSLdapObjects for all replicas

    :param instance: A instance
    :type instance: lib389.DirSrv
    :param batch: Not implemented
    :type batch: bool
    """

    def __init__(self, instance, batch=False):
        super(Replicas, self).__init__(instance=instance, batch=False)
        self._objectclasses = [REPLICA_OBJECTCLASS_VALUE]
        self._filterattrs = [REPL_ROOT]
        self._childobject = Replica
        self._basedn = DN_MAPPING_TREE

    def get(self, selector=[], dn=None):
        """Get a child entry (DSLdapObject, Replica, etc.) with dn or selector
        using a base DN and objectClasses of our object (DSLdapObjects, Replicas, etc.)
        After getting the replica, update the replica._suffix parameter.

        :param dn: DN of wanted entry
        :type dn: str
        :param selector: An additional filter to objectClasses, i.e. 'backend_name'
        :type dn: str

        :returns: A child entry
        """

        replica = super(Replicas, self).get(selector, dn)
        if replica:
            # Get and set the replica's suffix
            replica._suffix = replica.get_attr_val(REPL_ROOT)
        return replica

    def enable(self, suffix, role, replicaID=None, args=None):
        """Enable replication for this suffix

        :param suffix: The suffix to enable replication for
        :type suffix: str
        :param role: MASTER, HUB and CONSUMER
        :type role: ReplicaRole
        :param replicaID: number that identify the supplier replica
                          (role=ReplicaRole.MASTER) in the topology.
                          For hub/consumer (role=ReplicaRole.HUB or
                          ReplicaRole.CONSUMER), rid value is not used.
                          This parameter is mandatory for supplier.
        :type replicaID: int
        :param args: A dictionary of additional replica properties
        :type args: dict

        :returns: Replica DSLdapObject
        :raises: - InvalidArgumentError - if missing mandatory arguments
                 - ValueError - argument with invalid value
                 - LDAPError - failed to add replica entry
        """

        # Normalize the suffix
        suffix = normalizeDN(suffix)

        # Check validity of role
        if not role:
            self._log.fatal("Replica.create: replica role is not specified (ReplicaRole.*)")
            raise InvalidArgumentError("role missing")

        if not Replica._valid_role(role):
            self._log.fatal("enableReplication: replica role invalid (%s) " % role)
            raise ValueError("invalid role: %s" % role)

        # role is fine, set the replica type
        if role == ReplicaRole.MASTER:
            rtype = REPLICA_RDWR_TYPE
            # check the validity of 'rid'
            if not Replica._valid_rid(role, rid=replicaID):
                self._log.fatal("Replica.create: replica role is master but " +
                               "'rid' is missing or invalid value")
                raise InvalidArgumentError("rid missing or invalid value")
        else:
            rtype = REPLICA_RDONLY_TYPE

        # Set the properties provided as mandatory parameter
        properties = {'cn': 'replica',
                      REPL_ROOT: suffix,
                      REPL_ID: str(replicaID),
                      REPL_TYPE: str(rtype)}

        # If the properties in args are valid add them to 'properties'
        if args:
            for prop in args:
                if not inProperties(prop, REPLICA_PROPNAME_TO_ATTRNAME):
                    raise ValueError("unknown property: %s" % prop)
                properties[prop] = args[prop]

        # Set flags explicitly, so it will be more readable
        if role == ReplicaRole.CONSUMER:
            properties[REPL_FLAGS] = str(REPLICA_FLAGS_RDONLY)
        else:
            properties[REPL_FLAGS] = str(REPLICA_FLAGS_WRITE)

        # Check if replica entry is already in the mapping-tree
        try:
            replica = self.get(suffix)
            # Should we return an error, or just return the existing relica?
            self._log.warn("Already setup replica for suffix %s" % suffix)
            return replica
        except ldap.NO_SUCH_OBJECT:
            pass

        # Create changelog
        if (role == ReplicaRole.MASTER) or (role == ReplicaRole.HUB):
            self._instance.changelog.create()

        # Create the default replica manager entry if it does not exist
        if REPL_BINDDN not in properties:
            properties[REPL_BINDDN] = defaultProperties[REPLICATION_BIND_DN]
        if REPLICATION_BIND_PW not in properties:
            repl_pw = defaultProperties[REPLICATION_BIND_PW]
        else:
            repl_pw = properties[REPLICATION_BIND_PW]
            # Remove this property so we don't add it to the replica entry
            del properties[REPLICATION_BIND_PW]

        ReplTools.createReplManager(self._instance,
                                    repl_manager_dn=properties[REPL_BINDDN],
                                    repl_manager_pw=repl_pw)

        # Now create the replica entry
        mtents = self._instance.mappingtree.list(suffix=suffix)
        self._basedn = mtents[0].dn
        replica = self.create(RDN_REPLICA, properties)
        replica._suffix = suffix

        return replica

    def disable(self, suffix):
        """Disable replication on the suffix specified

        :param suffix: Replicated suffix to disable
        :type suffix: str

        :returns: None
        :raises: ValueError is suffix is not being replicated
        """

        try:
            replica = self.get(suffix)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('Suffix (%s) is not setup for replication' % suffix)

        role = replica.get_role()
        if role in (ReplicaRole.MASTER, ReplicaRole.HUB):
            self._instance.changelog.delete()

        try:
            replica.delete()
        except ldap.LDAPError as e:
            raise ValueError('Failed to disable replication for suffix ' +
                             '(%s) LDAP error (%s)' % (suffix, str(e)))

    def promote(self, suffix, newrole, binddn=None, rid=None):
        """Promote the replica to hub or master

        :param newrole: The new replication role for the replica: MASTER and HUB
        :type newrole: ReplicaRole
        :param binddn: The replication bind dn - only applied to master
        :type binddn: str
        :param rid: The replication ID, applies only to promotions to "master"
        :type rid: int

        :returns: None
        :raises: ValueError - If replica is not promoted
        """

        replica = self.get(suffix)
        try:
            replica = self.get(suffix)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('Suffix (%s) is not setup for replication' % suffix)
        replica.promote(newrole, binddn, rid)

    def demote(self, suffix, newrole):
        """Demote a replica to a hub or consumer

        :param newrole: The new replication role for the replica: CONSUMER and HUB
        :type newrole: ReplicaRole

        :returns: None
        :raises: ValueError - If replica is not demoted
        """

        replica = self.get(suffix)
        try:
            replica = self.get(suffix)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('Suffix (%s) is not setup for replication' % suffix)
        replica.demote(newrole)

    def get_dn(self, suffix):
        """Return the DN of the replica from cn=config, this is also
        known as the mapping tree entry

        :param suffix: The replication suffix to get the mapping tree DN
        :type suffix: str

        :returns: The DN of the replication entry from cn=config
        """

        try:
            replica = self.get(suffix)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('Suffix (%s) is not setup for replication' % suffix)
        return replica._dn

    def get_ruv_entry(self, suffix):
        """Return the database RUV entry for the provided suffix

        :returns: The database RUV entry
        :raises: - ValeuError - If suffix is not setup for replication
                 - LDAPError - If there is a problem trying to search for the RUV
        """

        try:
            replica = self.get(suffix)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('Suffix (%s) is not setup for replication' % suffix)
        return replica.get_ruv_entry()

    def test(self, suffix, *replica_dirsrvs):
        """Make a "dummy" update on the the replicated suffix, and check
        all the provided replicas to see if they received the update.

        :param suffix: The replicated suffix we want to check
        :type suffix: str
        :param *replica_dirsrvs: DirSrv instance, DirSrv instance, ...
        :type *replica_dirsrvs: list of DirSrv

        :returns: True - if all servers have received the update by this
                  replica, otherwise return False
        :raises: LDAPError - when failing to update/search database
        """

        try:
            replica = self.get(suffix)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('Suffix (%s) is not setup for replication' % suffix)
        return replica.test(*replica_dirsrvs)
