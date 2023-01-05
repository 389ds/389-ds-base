# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import base64
import ldap
import decimal
import time
import datetime
import logging
import uuid
import json
import copy
from operator import itemgetter
from itertools import permutations
from lib389._constants import CONSUMER_REPLICAID, REPLICA_RDWR_TYPE, REPLICA_FLAGS_WRITE, REPLICA_RDONLY_TYPE, \
                              REPLICA_FLAGS_RDONLY, REPLICA_ID, REPLICA_TYPE, REPLICA_SUFFIX, REPLICA_BINDDN, \
                              RDN_REPLICA, REPLICA_FLAGS, REPLICA_RUV_UUID, REPLICA_OC_TOMBSTONE, DN_MAPPING_TREE, \
                              DN_CONFIG, DN_PLUGIN, REPLICATION_BIND_DN, REPLICATION_BIND_PW, ReplicaRole, \
                              defaultProperties
from lib389.properties import REPLICA_OBJECTCLASS_VALUE, REPLICA_OBJECTCLASS_VALUE, REPLICA_SUFFIX, \
                              REPLICA_PROPNAME_TO_ATTRNAME, REPL_BINDDN, REPL_TYPE, REPL_ID, REPL_FLAGS, \
                              REPL_BIND_GROUP, SER_HOST, SER_PORT, SER_SECURE_PORT, SER_ROOT_DN, SER_ROOT_PW, \
                              REPL_ROOT, inProperties, rawProperty

from lib389.utils import (normalizeDN, escapeDNValue, ensure_bytes, ensure_str,
                          ensure_list_str, ds_is_older, copy_with_permissions,
                          ds_supports_new_changelog)
from lib389 import DirSrv, Entry, NoSuchEntryError, InvalidArgumentError
from lib389._mapped_object import DSLdapObjects, DSLdapObject
from lib389.passwd import password_generate
from lib389.mappingTree import MappingTrees
from lib389.agreement import Agreements
from lib389.dirsrv_log import DirsrvErrorLog
from lib389.tombstone import Tombstones
from lib389.tasks import CleanAllRUVTask
from lib389.idm.domain import Domain
from lib389.idm.group import Groups
from lib389.idm.services import ServiceAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.conflicts import ConflictEntries
from lib389.lint import (DSREPLLE0001, DSREPLLE0002, DSREPLLE0003, DSREPLLE0004,
                         DSREPLLE0005, DSCLLE0001)


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
        if role != ReplicaRole.SUPPLIER and \
           role != ReplicaRole.HUB and \
           role != ReplicaRole.CONSUMER:
            return False
        else:
            return True

    @staticmethod
    def _valid_rid(role, rid=None):
        if role == ReplicaRole.SUPPLIER:
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
                self.log.warning("replica_createReplMgr: bind DN password "
                                 "not specified")
            if not repl_manager_dn:
                self.log.warning("replica_createReplMgr: bind DN not "
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
            self.log.warn("User already exists (weird we just checked: %s ",
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
                self.log.debug("setProperties: %s:%s",
                               prop, properties[prop])

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

        @return: ReplicaRole.SUPPLIER, ReplicaRole.HUB, ReplicaRole.CONSUMER
        """

        filter_str = ('(&(objectclass=nsDS5Replica)(nsDS5ReplicaRoot=%s))'.format(suffix))

        try:
            replica_entry = self.conn.search_s(DN_CONFIG, ldap.SCOPE_SUBTREE,
                                               filter_str)
            if replica_entry:
                repltype = replica_entry[0].getValue(REPL_TYPE)
                replflags = replica_entry[0].getValue(REPL_FLAGS)

                if repltype == REPLICA_RDWR_TYPE and replflags == REPLICA_FLAGS_WRITE:
                    replicarole = ReplicaRole.SUPPLIER
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
            @param role   - ReplicaRole.SUPPLIER, ReplicaRole.HUB or
                            ReplicaRole.CONSUMER
            @param rid    - number that identify the supplier replica
                            (role=ReplicaRole.SUPPLIER) in the topology.  For
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
            self.log.fatal("enableReplication: replica role invalid (%s) ", role)
            raise ValueError("invalid role: %s" % role)

        # check the validity of 'rid'
        if not ReplicaLegacy._valid_rid(role, rid=rid):
            self.log.fatal("Replica.create: replica role is supplier but 'rid'"
                           " is missing or invalid value")
            raise InvalidArgumentError("rid missing or invalid value")

        # check the validity of the suffix
        if not suffix:
            self.log.fatal("Replica.create: suffix is missing")
            raise InvalidArgumentError("suffix missing")
        else:
            nsuffix = normalizeDN(suffix)

        # role is fine, set the replica type
        if role == ReplicaRole.SUPPLIER:
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
            self.log.warn("Already setup replica for suffix %r", nsuffix)
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
                    self.log.fatal('Failed to delete replica agreement (%s),'
                                   ' error: %s',
                                   agmt.dn, e)
                    raise
        except ldap.LDAPError as e:
            self.log.fatal('Failed to search for replication agreements '
                           'under (%s), error: %s', dn_replica, e)
            raise

    def disableReplication(self, suffix=None):
        '''
            Delete a replica related to the provided suffix.
            If this replica role was ReplicaRole.HUB or ReplicaRole.SUPPLIER, it
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
            self.log.fatal('Failed to delete replica agreements!  ' + str(e))
            raise

        # Delete the replica
        try:
            self.conn.delete_s(dn_replica)
        except ldap.LDAPError as e:
            self.log.fatal('Failed to delete replica configuration '
                           '(%s), error: %s', dn_replica, e)
            raise

    def enableReplication(self, suffix=None, role=None,
                          replicaId=CONSUMER_REPLICAID,
                          properties=None):
        if not suffix:
            self.log.fatal("enableReplication: suffix not specified")
            raise ValueError("suffix missing")

        if not role:
            self.log.fatal("enableReplication: replica role not specify "
                           "(ReplicaRole.*)")
            raise ValueError("role missing")

        #
        # Check the validity of the parameters
        #

        # First role and replicaID
        if (
            role != ReplicaRole.SUPPLIER and
            role != ReplicaRole.HUB and
            role != ReplicaRole.CONSUMER
        ):
            self.log.fatal("enableReplication: replica role invalid (%s) ",
                           role)
            raise ValueError("invalid role: %s" % role)

        if role == ReplicaRole.SUPPLIER:
            # check the replicaId [1..CONSUMER_REPLICAID[
            if not decimal.Decimal(replicaId) or \
               (replicaId <= 0) or \
               (replicaId >= CONSUMER_REPLICAID):
                self.log.fatal("enableReplication: invalid replicaId (%s) "
                               "for a RW replica", replicaId)
                raise ValueError("invalid replicaId %d (expected [1.."
                                 "CONSUMER_REPLICAID]" % replicaId)
        elif replicaId != CONSUMER_REPLICAID:
            # check the replicaId is CONSUMER_REPLICAID
            self.log.fatal("enableReplication: invalid replicaId (%s) for a "
                           "Read replica (expected %d)",
                           replicaId, CONSUMER_REPLICAID)
            raise ValueError("invalid replicaId: %d for HUB/CONSUMER "
                             "replicaId is CONSUMER_REPLICAID" % replicaId)

        # Now check we have a suffix
        entries_backend = self.conn.backend.list(suffix=suffix)
        if not entries_backend:
            self.log.fatal("enableReplication: unable to retrieve the "
                           "backend for %s", suffix)
            raise ValueError("no backend for suffix %s" % suffix)

        ent = entries_backend[0]
        if normalizeDN(suffix) != normalizeDN(ent.getValue('nsslapd-suffix')):
            self.log.warning("enableReplication: suffix (%s) and backend "
                             "suffix (%s) differs",
                             suffix, entries_backend[0].nsslapd - suffix)
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
                self.log.warning("enableReplication: binddn not provided and"
                                 " default value unavailable")
                pass

        # First add the changelog if supplier/hub
        if (role == ReplicaRole.SUPPLIER) or (role == ReplicaRole.HUB):
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
            self.log.exception("Error reading status from agreement %r",
                               agmtdn)
            hasError = 1
        else:
            refresh = entry.nsds5BeginReplicaRefresh
            inprogress = entry.nsds5replicaUpdateInProgress
            status = entry.nsds5ReplicaLastInitStatus
            if not refresh:  # done - check status
                if not status:
                    self.log.info("No status yet")
                elif status.find(ensure_bytes("replica busy")) > -1:
                    self.log.info("Update failed - replica busy - status", status)
                    done = True
                    hasError = 2
                elif status.find(ensure_bytes("Total update succeeded")) > -1:
                    self.log.info("Update succeeded: status ", status)
                    done = True
                elif inprogress.lower() == ensure_bytes('true'):
                    self.log.info("Update in progress yet not in progress: status ",
                          status)
                else:
                    self.log.info("Update failed: status", status)
                    hasError = 1
                    done = True
            else:
                self.log.debug("Update in progress: status", status)

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
        self.log.info("Starting async replication %s", agmtdn)
        mod = [(ldap.MOD_ADD, 'nsds5BeginReplicaRefresh', b'start')]
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
            self.log.warn("Could not get RUV from %r entry -"
                          " trying cn=replica", suffix)
            ensuffix = escapeDNValue(normalizeDN(suffix))
            dn = ','.join(("cn=replica", "cn=%s" % ensuffix, DN_MAPPING_TREE))
            ents = self.conn.search_s(dn, ldap.SCOPE_BASE, "objectclass=*",
                                      attrs)

        if ents and (len(ents) > 0):
            ent = ents[0]
            self.log.debug("RUV entry is %r", ent)
            return RUV(ent)

        raise NoSuchEntryError("RUV not found: suffix: %r" % suffix)

    def promote(self, suffix, newrole, rid=None, binddn=None):
        """
        Promote the replica

        @raise ValueError
        """

        if newrole != ReplicaRole.SUPPLIER and newrole != ReplicaRole.HUB:
            raise ValueError('Can only prompt replica to "supplier" or "hub"')

        if not binddn:
            raise ValueError('"binddn" required for promotion')

        if newrole == ReplicaRole.SUPPLIER:
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
        if not ds_supports_new_changelog():
            try:
                self.conn.changelog.create()
            except ldap.LDAPError as e:
                raise ValueError('Failed to create changelog: %s' % str(e))

        #
        # Check that a RID was provided, and its a valid number
        #
        if newrole == ReplicaRole.SUPPLIER:
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
        else:  # supplier
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


class RUV(object):
    """Represents the server in memory RUV object. The RUV contains each
    update vector the server knows of, along with knowledge of CSN state of the
    replica we have sent data to.

    :param ruvs: A list of nsds50ruv values.
    :type ruvs: list[str]
    :param logger: A logging interface.
    :type logger: logging object
    """

    def __init__(self, ruvs=[], logger=None):
        if logger is not None:
            self._log = logger
        else:
            self._log = logging.getLogger(__name__)
        self._rids = []
        self._rid_url = {}
        self._rid_rawruv = {}
        self._rid_csn = {}
        self._rid_maxcsn = {}
        self._rid_modts = {}
        self._data_generation = None
        self._data_generation_csn = None
        # Process the array of data
        for r in ruvs:
            pr = r.replace('{', '').replace('}', '').split(' ')
            if pr[0] == 'replicageneration':
                # replicageneration 5a2ffd0f000000010000
                self._data_generation = pr[1]
            elif pr[0] == 'replica':
                # replica 1 ldap://ldapkdc.example.com:39001 5a2ffd0f000100010000 5a2ffd0f000200010000
                # Ignore the ruv if there are no rid or no url
                if len(pr) < 3:
                    continue
                # Don't add rids if they have no csn (no writes) yet.
                rid = pr[1]
                self._rids.append(rid)
                try:
                    self._rid_url[rid] = pr[2]
                except IndexError:
                    self._rids.remove(rid)
                    continue
                self._rid_rawruv[rid] = r
                try:
                    self._rid_csn[rid] = pr[3]
                except IndexError:
                    self._rid_csn[rid] = '00000000000000000000'
                try:
                    self._rid_maxcsn[rid] = pr[4]
                except IndexError:
                    self._rid_maxcsn[rid] = '00000000000000000000'
                try:
                    self._rid_modts[rid] = pr[5]
                except IndexError:
                    self._rid_modts[rid] = '00000000'

    @staticmethod
    def parse_csn(csn):
        """Parse CSN into human readable format '1970-01-31 00:00:00'

        :param csn: the CSN to format
        :type csn: str
        :returns: str
        """
        if len(csn) != 20 or len(csn) != 8 and not isinstance(csn, str):
            ValueError("Wrong CSN value was supplied")

        timestamp = int(csn[:8], 16)
        time_str = datetime.datetime.utcfromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')
        # We are parsing shorter CSN which contains only timestamp
        if len(csn) == 8:
            return time_str
        else:
            seq = int(csn[8:12], 16)
            subseq = int(csn[16:20], 16)
            if seq != 0 or subseq != 0:
                return f"{time_str} {str(seq)} {str(subseq)}"
            else:
                return f"{time_str}"

    def format_ruv(self):
        """Parse RUV into human readable format

        :returns: dict
        """
        result = {}
        if self._data_generation:
            result["data_generation"] = {"name": self._data_generation,
                                         "value": self._data_generation_csn}
        else:
            result["data_generation"] = None

        ruvs = []
        for rid in self._rids:
            ruvs.append({"raw_ruv": self._rid_rawruv.get(rid),
                         "rid": rid,
                         "url": self._rid_url.get(rid),
                         "csn": RUV().parse_csn(self._rid_csn.get(rid, '00000000000000000000')),
                         "raw_csn": self._rid_csn.get(rid, '00000000000000000000'),
                         "maxcsn": RUV().parse_csn(self._rid_maxcsn.get(rid, '00000000000000000000')),
                         "raw_maxcsn": self._rid_maxcsn.get(rid, '00000000000000000000'),
                         "modts": RUV().parse_csn(self._rid_modts.get(rid, '00000000'))})
        result["ruvs"] = ruvs
        return result

    def alloc_rid(self):
        """Based on the RUV, determine an available RID for the replication
        topology that is unique.

        :returns: str
        """
        self._log.debug("Allocated rids: %s" % self._rids)
        for i in range(1, 65534):
            self._log.debug("Testing ... %s" % i)
            if str(i) not in self._rids:
                return str(i)
        raise Exception("Unable to alloc rid!")

    def is_synced(self, other_ruv):
        """Compare two server ruvs to determine if they are synced. This does not
        mean that replication is in sync (due to things like fractional repl), but
        in some cases can show that "at least some known point" has been achieved in
        the replication process.

        :param other_ruv: The other ruv object
        :type other_ruv: RUV object
        :returns: bool
        """
        self._log.debug("RUV: Comparing dg %s %s" % (self._data_generation, other_ruv._data_generation))
        if self._data_generation != other_ruv._data_generation:
            self._log.debug("RUV: Incorrect datageneration")
            return False
        if set(self._rids) != set(other_ruv._rids):
            self._log.debug("RUV: Incorrect rid lists, is sync working?")
            return False
        for rid in self._rids:
            my_csn = self._rid_csn.get(rid, '00000000000000000000')
            other_csn = other_ruv._rid_csn.get(rid, '00000000000000000000')
            self._log.debug("RUV: Comparing csn %s %s %s" % (rid, my_csn, other_csn))
            if my_csn < other_csn:
                return False
        return True


class ChangelogLDIF(object):
    def __init__(self, file_path, output_file):
        """A class for working with Changelog LDIF file

        :param file_path: LDIF file path
        :type file_path: str
        :param output_file: LDIF file path
-       :type output_file: str
        """
        self.file_path = file_path
        self.output_file = output_file

    def grep_csn(self):
        """Grep and interpret CSNs

        :param file: LDIF file path
        :type file: str
        """
        with open(self.output_file, 'w') as LDIF_OUT:
            LDIF_OUT.write(f"# LDIF File: {self.output_file}\n")
            with open(self.file_path) as LDIF_IN:
                for line in LDIF_IN.readlines():
                    if "ruv:" in line or "csn:" in line:
                        csn = ""
                        maxcsn = ""
                        modts = ""
                        line = line.split("\n")[0]
                        if "ruv:" in line:
                            ruv = RUV([line.split("ruv: ")[1]])
                            ruv_dict = ruv.format_ruv()
                            csn = ruv_dict["csn"]
                            maxcsn = ruv_dict["maxcsn"]
                            modts = ruv_dict["modts"]
                        elif "csn:" in line:
                            csn = RUV().parse_csn(line.split("csn: ")[1])
                        if maxcsn or modts:
                            LDIF_OUT.write(f'{line} ({csn}\n')
                            if maxcsn:
                                LDIF_OUT.write(f"; {maxcsn}\n")
                            if modts:
                                LDIF_OUT.write(f"; {modts}\n")
                            LDIF_OUT.write(")\n")
                        else:
                            LDIF_OUT.write(f"{line} ({csn})\n")

    def decode(self):
        """Decode the changelog

        :param file: LDIF file path
        :type file: str
        """
        with open(self.output_file, 'w') as LDIF_OUT:
            LDIF_OUT.write(f"# LDIF File: {self.output_file}\n")
            with open(self.file_path) as LDIF_IN:
                encoded_str = ""
                for line in LDIF_IN.readlines():
                    if line.startswith("change::") or line.startswith("changes::"):
                        LDIF_OUT.write("change::\n")
                        try:
                            encoded_str = line.split("change:: ")[1]
                        except IndexError:
                            encoded_str = line.split("changes:: ")[1]
                        continue
                    if not encoded_str:
                        LDIF_OUT.write(line.split('\n')[0] + "\n")
                        continue
                    if line == "\n":
                        decoded_str = ensure_str(base64.b64decode(encoded_str))
                        LDIF_OUT.write(decoded_str + "\n")
                        encoded_str = ""
                        continue
                    encoded_str += line

    def process(self):
        # Process the file as is, just log it into the new custom file
        with open(self.output_file, 'w') as LDIF_OUT:
            LDIF_OUT.write(f"# LDIF File: {self.output_file}")
            with open(self.file_path) as LDIF_IN:
                for line in LDIF_IN.readlines():
                    LDIF_OUT.write(line)


class Changelog(DSLdapObject):
    """Represents the Directory Server changelog of a specific backend. This is used for
    replication.

    :param instance: An instance
    :type instance: lib389.DirSrv
    """
    def __init__(self, instance, suffix=None, dn=None):
        from lib389.backend import Backends
        super(Changelog, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._create_objectclasses = [
            'top',
            'extensibleobject',
        ]
        if not ds_supports_new_changelog():
            raise ValueError('changelog (integrated to main database) is not supported in that version of the server')
        if not suffix:
            raise ValueError('A changelog is specific to a suffix and the suffix value is missing')

        # retrieve the backend associated to the provided suffix
        be_insts = Backends(instance).list()
        found_suffix = False
        for be in be_insts:
            be_suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
            if suffix.lower() == be_suffix:
                found_suffix = True
                break
        if not found_suffix:
            raise ValueError(f'No backend associated with nsslapd-suffix "{suffix}"')

        # changelog is a child of the backend
        self._dn = 'cn=changelog,' + be.dn

    def set_max_entries(self, value):
        """Configure the max entries the changelog can hold.

        :param value: the number of entries.
        :type value: str
        """
        self.replace('nsslapd-changelogmaxentries', value)

    def set_trim_interval(self, value):
        """The time between changelog trims in seconds.

        :param value: The time in seconds
        :type value: str
        """
        self.replace('nsslapd-changelogtrim-interval', value)

    def set_max_age(self, value):
        """The maximum age of entries in the changelog.

        :param value: The age with a time modifier of s, m, h, d, w.
        :type value: str
        """
        self.replace('nsslapd-changelogmaxage', value)

    def set_encrypt(self):
        """Set the changelog encryption

        :param value:
        :type value: str
        """
        self.replace('nsslapd-encryptionalgorithm', 'AES')

    def unset_encrypt(self):
        """stop encrypting the changelog

        :param value:
        :type value: str
        """
        self.remove_all('nsslapd-encryptionalgorithm')


class Changelog5(DSLdapObject):
    """Represents the Directory Server changelog. This is used for
    replication. Only one changelog is needed for every server.

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance, dn='cn=changelog5,cn=config'):
        super(Changelog5, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn', 'nsslapd-changelogdir']
        self._create_objectclasses = [
            'top',
            'nsChangelogConfig',
        ]
        if ds_is_older('1.4.0'):
            self._create_objectclasses = [
                'top',
                'extensibleobject',
            ]
        self._protected = False

    @classmethod
    def lint_uid(cls):
        return 'changelog'

    def _lint_cl_trimming(self):
        """Check that cl trimming is at least defined to prevent unbounded growth"""
        try:
            if self.get_attr_val_utf8('nsslapd-changelogmaxentries') is None and \
                self.get_attr_val_utf8('nsslapd-changelogmaxage') is None:
                report = copy.deepcopy(DSCLLE0001)
                report['fix'] = report['fix'].replace('YOUR_INSTANCE', self._instance.serverid)
                report['check'] = f'changelog:cl_trimming'
                yield report
        except:
            # No changelog
            pass

    def set_max_entries(self, value):
        """Configure the max entries the changelog can hold.

        :param value: the number of entries.
        :type value: str
        """
        self.replace('nsslapd-changelogmaxentries', value)

    def set_trim_interval(self, value):
        """The time between changelog trims in seconds.

        :param value: The time in seconds
        :type value: str
        """
        self.replace('nsslapd-changelogtrim-interval', value)

    def set_max_age(self, value):
        """The maximum age of entries in the changelog.

        :param value: The age with a time modifier of s, m, h, d, w.
        :type value: str
        """

        self.replace('nsslapd-changelogmaxage', value)


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
    """

    def __init__(self, instance, dn=None):
        super(Replica, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = [
            'cn',
            'nsDS5ReplicaType',
            'nsDS5ReplicaRoot',
            'nsDS5ReplicaId',
        ]

        self._create_objectclasses = [
            'top',
            'nsds5Replica'
        ]
        if ds_is_older('1.4.0'):
            self._create_objectclasses.append('extensibleobject')
        self._protected = False
        self._suffix = None

    @classmethod
    def lint_uid(cls):
        return 'replication'

    def _lint_agmts_status(self):
        replicas = Replicas(self._instance).list()
        for replica in replicas:
            agmts = replica.get_agreements().list()
            suffix = replica.get_suffix()
            for agmt in agmts:
                agmt_name = agmt.get_name()
                try:
                    status = json.loads(agmt.get_agmt_status(return_json=True))
                    if "Not in Synchronization" in status['msg'] and not "Replication still in progress" in status['reason']:
                        if status['state'] == 'red':
                            # Serious error
                            if "Consumer can not be contacted" in status['reason']:
                                report = copy.deepcopy(DSREPLLE0005)
                                report['detail'] = report['detail'].replace('SUFFIX', suffix)
                                report['detail'] = report['detail'].replace('AGMT', agmt_name)
                                report['check'] = f'replication:agmts_status'
                                yield report
                            else:
                                report = copy.deepcopy(DSREPLLE0001)
                                report['detail'] = report['detail'].replace('SUFFIX', suffix)
                                report['detail'] = report['detail'].replace('AGMT', agmt_name)
                                report['detail'] = report['detail'].replace('MSG', status['reason'])
                                report['fix'] = report['fix'].replace('SUFFIX', suffix)
                                report['fix'] = report['fix'].replace('AGMT', agmt_name)
                                report['fix'] = report['fix'].replace('YOUR_INSTANCE', self._instance.serverid)
                                report['check'] = f'replication:agmts_status'
                                yield report
                        elif status['state'] == 'amber':
                            # Warning
                            report = copy.deepcopy(DSREPLLE0003)
                            report['detail'] = report['detail'].replace('SUFFIX', suffix)
                            report['detail'] = report['detail'].replace('AGMT', agmt_name)
                            report['detail'] = report['detail'].replace('MSG', status['reason'])
                            report['check'] = f'replication:agmts_status'
                            yield report
                except ldap.LDAPError as e:
                    report = copy.deepcopy(DSREPLLE0004)
                    report['detail'] = report['detail'].replace('SUFFIX', suffix)
                    report['detail'] = report['detail'].replace('AGMT', agmt_name)
                    report['detail'] = report['detail'].replace('ERROR', str(e))
                    report['check'] = f'replication:agmts_status'
                    yield report

    def _lint_conflicts(self):
        replicas = Replicas(self._instance).list()
        for replica in replicas:
            conflicts = ConflictEntries(self._instance, replica.get_suffix()).list()
            suffix = replica.get_suffix()
            if len(conflicts) > 0:
                report = copy.deepcopy(DSREPLLE0002)
                report['detail'] = report['detail'].replace('SUFFIX', suffix)
                report['detail'] = report['detail'].replace('COUNT', str(len(conflicts)))
                report['fix'] = report['fix'].replace('YOUR_INSTANCE', self._instance.serverid)
                report['check'] = f'replication:conflicts'
                yield report

    def _validate(self, rdn, properties, basedn):
        (tdn, str_props) = super(Replica, self)._validate(rdn, properties, basedn)
        # We override the tdn here. We use the MT for the suffix.
        mts = MappingTrees(self._instance)
        s_suffix = ensure_str(str_props['nsDS5ReplicaRoot'][0])
        mt = mts.get(s_suffix)
        tdn = 'cn=replica,%s' % mt.dn
        return (tdn, str_props)

    def _populate_suffix(self):
        """Some internal tasks need this populated.
        """
        if self._suffix is None:
            self._suffix = self.get_attr_val_utf8('nsDS5ReplicaRoot')

    def _valid_role(role):
        """Return True if role is valid

        :param role: SUPPLIER, HUB and CONSUMER
        :type role: ReplicaRole
        :returns: True if the role is a valid role object, otherwise return False
        """

        if role != ReplicaRole.SUPPLIER and \
           role != ReplicaRole.HUB and \
           role != ReplicaRole.CONSUMER:
            return False
        else:
            return True

    def _valid_rid(role, rid=None):
        """Return True if rid is valid for the replica role

        :param role: SUPPLIER, HUB and CONSUMER
        :type role: ReplicaRole
        :param rid: Only needed if the role is a SUPPLIER
        :type rid: int
        :returns: True is rid is valid, otherwise return False
        """

        if rid is None:
            return False
        if role == ReplicaRole.SUPPLIER:
            if not decimal.Decimal(rid) or \
               (rid <= 0) or \
               (rid >= CONSUMER_REPLICAID):
                return False
        else:
            if rid and (rid != CONSUMER_REPLICAID):
                return False
        return True

    def cleanRUV(self, rid):
        """Run a cleanallruv task, only on a supplier, after deleting or demoting
        it.  It is okay if it fails.
        """
        if rid != '65535':
            properties = {'replica-base-dn': self.get_attr_val_utf8('nsDS5ReplicaRoot'),
                          'replica-id': rid,
                          'replica-force-cleaning': 'yes'}
            try:
                clean_task = CleanAllRUVTask(self._instance)
                clean_task.create(properties=properties)
            except ldap.LDAPError as e:
                self._log.debug("Failed to run cleanAllRUV task: " + str(e))

    def delete(self):
        """Delete a replica related to the provided suffix.

        If this replica role was ReplicaRole.HUB or ReplicaRole.SUPPLIER, it
        also deletes the changelog associated to that replica. If it
        exists some replication agreement below that replica, they are
        deleted.  If this is a supplier we also clean the database ruv.

        :returns: None
        :raises: - InvalidArgumentError - if suffix is missing
                 - ldap.LDAPError - for all other update failures
        """
        # Delete the agreements
        self._delete_agreements()

        # Delete the replica
        return super(Replica, self).delete()

    def _delete_agreements(self):
        """Delete all the agreements for the suffix

        :raises: LDAPError - If failing to delete or search for agreements
        """
        # Get the suffix
        self._populate_suffix()

        # Delete standard agmts
        agmts = self.get_agreements()
        for agmt in agmts.list():
            agmt.delete()

        # Delete winysnc agmts
        agmts = self.get_agreements(winsync=True)
        for agmt in agmts.list():
            agmt.delete()

    def promote(self, newrole, binddn=None, binddn_group=None, rid=None):
        """Promote the replica to hub or supplier

        :param newrole: The new replication role for the replica: SUPPLIER and HUB
        :type newrole: ReplicaRole
        :param binddn: The replication bind dn - only applied to supplier
        :type binddn: str
        :param binddn_group: The replication bind dn group - only applied to supplier
        :type binddn: str
        :param rid: The replication ID, applies only to promotions to "supplier"
        :type rid: int
        :returns: None
        :raises: ValueError - If replica is not promoted
        """

        # Set the bind dn, use the existing one if it exists
        if binddn is None and binddn_group is None:
            curr_dn = self.get_attr_val(REPL_BINDDN)
            if curr_dn is None:
                binddn = defaultProperties[REPLICATION_BIND_DN]
            else:
                binddn = curr_dn

        # Check the role type
        replicarole = self.get_role()
        if newrole.value <= replicarole.value:
            raise ValueError('Can not promote replica to lower or the same role: {} -> {}'.format(replicarole.name, newrole.name))

        if newrole == ReplicaRole.SUPPLIER:
            if not rid:
                raise ValueError('"rid" required for promotion')
        else:
            # Must be a hub - set the rid
            rid = CONSUMER_REPLICAID

        # Check that a RID was provided, and its a valid number
        if newrole == ReplicaRole.SUPPLIER:
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
            if binddn:
                self.set(REPL_BINDDN, binddn)
            else:
                self.set(REPL_BIND_GROUP, binddn_group)
        except ldap.LDAPError as e:
            raise ValueError('Failed to update replica: ' + str(e))

        # Promote it - set the replica type, flags and rid
        if replicarole == ReplicaRole.CONSUMER and newrole == ReplicaRole.HUB:
            try:
                self.set(REPL_FLAGS, str(REPLICA_FLAGS_WRITE))
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        elif replicarole == ReplicaRole.CONSUMER and newrole == ReplicaRole.SUPPLIER:
            try:
                self.replace_many((REPL_TYPE, str(REPLICA_RDWR_TYPE)),
                                  (REPL_FLAGS, str(REPLICA_FLAGS_WRITE)),
                                  (REPL_ID, str(rid)))
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        elif replicarole == ReplicaRole.HUB and newrole == ReplicaRole.SUPPLIER:
            try:
                self.replace_many((REPL_TYPE, str(REPLICA_RDWR_TYPE)),
                                  (REPL_ID, str(rid)))
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
        rid = self.get_attr_val_utf8(REPL_ID)
        if newrole.value >= replicarole.value:
            raise ValueError('Can not demote replica to higher or the same role: {} -> {}'.format(replicarole.name, newrole.name))

        # Demote it - set the replica type, flags and rid
        if replicarole == ReplicaRole.SUPPLIER and newrole == ReplicaRole.HUB:
            try:
                self.replace_many((REPL_TYPE, str(REPLICA_RDONLY_TYPE)),
                                  (REPL_ID, str(CONSUMER_REPLICAID)))
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        elif replicarole == ReplicaRole.SUPPLIER and newrole == ReplicaRole.CONSUMER:
            try:
                self.replace_many((REPL_TYPE, str(REPLICA_RDONLY_TYPE)),
                                  (REPL_FLAGS, str(REPLICA_FLAGS_RDONLY)),
                                  (REPL_ID, str(CONSUMER_REPLICAID)))
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        elif replicarole == ReplicaRole.HUB and newrole == ReplicaRole.CONSUMER:
            try:
                self.set(REPL_FLAGS, str(REPLICA_FLAGS_RDONLY))
            except ldap.LDAPError as e:
                raise ValueError('Failed to update replica: ' + str(e))
        if replicarole == ReplicaRole.SUPPLIER:
            # We are no longer a supplier, clean up the old RID
            self.cleanRUV(rid)

    def get_role(self):
        """Return the replica role

        :returns: ReplicaRole.SUPPLIER, ReplicaRole.HUB, ReplicaRole.CONSUMER
        """

        repltype = self.get_attr_val_int(REPL_TYPE)
        replflags = self.get_attr_val_int(REPL_FLAGS)

        if repltype == REPLICA_RDWR_TYPE and replflags == REPLICA_FLAGS_WRITE:
            replicarole = ReplicaRole.SUPPLIER
        elif repltype == REPLICA_RDONLY_TYPE and replflags == REPLICA_FLAGS_WRITE:
            replicarole = ReplicaRole.HUB
        elif repltype == REPLICA_RDONLY_TYPE and replflags == REPLICA_FLAGS_RDONLY:
            replicarole = ReplicaRole.CONSUMER
        else:
            raise ValueError("Failed to determine a replica role")

        return replicarole

    def test_replication(self, replica_dirsrvs):
        """Make a "dummy" update on the the replicated suffix, and check
        all the provided replicas to see if they received the update.

        :param *replica_dirsrvs: DirSrv instance, DirSrv instance, ...
        :type *replica_dirsrvs: list of DirSrv

        :returns: True - if all servers have received the update by this
                  replica, otherwise return False
        :raises: LDAPError - when failing to update/search database
        """

        # Generate a unique test value
        test_value = ensure_bytes('test replication from ' + self._instance.serverid +
                      ' to ' + replica_dirsrvs[0].serverid + ': ' +
                      str(int(time.time())))

        my_domain = Domain(self._instance, self._suffix)
        my_domain.replace('description', test_value)

        for replica in replica_dirsrvs:
            r_domain = Domain(replica, self._suffix)
            loop = 0
            replicated = False
            while loop <= 30:
                # Wait 60 seconds before giving up
                try:
                    r_test_values = r_domain.get_attr_vals_bytes('description')

                    if test_value in r_test_values:
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
        my_domain.remove('description', None)

        return True

    def get_agreements(self, winsync=False):
        """Return the set of agreements related to this suffix replica
        :param: winsync: If True then return winsync replication agreements,
                         otherwise return teh standard replication agreements.
        :returns: A list Replicas objects
        """
        return Agreements(self._instance, self.dn, winsync=winsync)

    def get_consumer_replicas(self, get_credentials):
        """Return the set of consumer replicas related to this suffix replica through its agreements

        :param get_credentials: A user-defined callback function which returns the binding credentials
                                using given host and port data - {"binddn": "cn=Directory Manager",
                                "bindpw": "password"}
        :returns: Replicas object
        """

        agmts = self.get_agreements()
        result_replicas = []
        connections = []

        try:
            for agmt in agmts:
                host = agmt.get_attr_val_utf8("nsDS5ReplicaHost")
                port = agmt.get_attr_val_utf8("nsDS5ReplicaPort")
                protocol = agmt.get_attr_val_utf8_l("nsDS5ReplicaTransportInfo")

                # The function should be defined outside and
                # it should have all the logic for figuring out the credentials
                credentials = get_credentials(host, port)
                if not credentials["binddn"]:
                    self._log.debug("Bind DN was not specified")
                    continue

                # Open a connection to the consumer
                consumer = DirSrv(verbose=self._instance.verbose)
                args_instance[SER_HOST] = host
                if protocol == "ssl" or protocol == "ldaps":
                    args_instance[SER_SECURE_PORT] = int(port)
                else:
                    args_instance[SER_PORT] = int(port)
                args_instance[SER_ROOT_DN] = credentials["binddn"]
                args_instance[SER_ROOT_PW] = credentials["bindpw"]
                args_standalone = args_instance.copy()
                consumer.allocate(args_standalone)
                try:
                    consumer.open()
                except ldap.LDAPError as e:
                    self._log.debug(f"Connection to consumer ({host}:{port}) failed, error: {e}")
                    raise
                connections.append(consumer)
                result_replicas.append(Replicas(consumer))
        except:
            for conn in connections:
                conn.close()
            raise

        return result_replicas

    def get_rid(self):
        """Return the current replicas RID for this suffix

        :returns: str
        """
        return self.get_attr_val_utf8('nsDS5ReplicaId')

    def get_ruv(self):
        """Return the in memory ruv of this replica suffix.

        :returns: RUV object
        :raises: LDAPError
        """
        self._populate_suffix()
        data = []
        try:
            ent = self._instance.search_ext_s(
                base=self._suffix,
                scope=ldap.SCOPE_SUBTREE,
                filterstr='(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectclass=nstombstone))',
                attrlist=['nsds50ruv'],
                serverctrls=self._server_controls, clientctrls=self._client_controls,
                escapehatch='i am sure')[0]
            data = ensure_list_str(ent.getValues('nsds50ruv'))
        except IndexError:
            # There is no ruv entry, it's okay
            pass

        return RUV(data)

    def get_maxcsn(self, replica_id = None):
        """Return the current replica's maxcsn for this suffix

        :returns: str
        """
        if replica_id is None:
            replica_id = self.get_rid()
        replica_ruvs = self.get_ruv()
        return replica_ruvs._rid_maxcsn.get(replica_id, '00000000000000000000')

    def get_ruv_agmt_maxcsns(self):
        """Return the in memory ruv of this replica suffix.

        :returns: RUV object
        :raises: LDAPError
        """
        self._populate_suffix()

        try:
            ent = self._instance.search_ext_s(
                base=self._suffix,
                scope=ldap.SCOPE_SUBTREE,
                filterstr='(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectclass=nstombstone))',
                attrlist=['nsds5agmtmaxcsn'],
                serverctrls=self._server_controls, clientctrls=self._client_controls,
                escapehatch='i am sure')[0]
        except IndexError:
            # there is no ruv entry, it's okay
            return []

        return ensure_list_str(ent.getValues('nsds5agmtmaxcsn'))

    def begin_task_cl2ldif(self):
        """Begin the changelog to ldif task
        """
        self.replace('nsds5task', 'cl2ldif')

    def begin_task_ldif2cl(self):
        """Begin ldif to changelog task
        """
        self.replace('nsds5task', 'ldif2cl')

    def task_finished(self):
        """Wait for a replica task to complete: CL2LDIF / LDIF2CL
        """
        loop_limit = 30
        while loop_limit > 0:
            time.sleep(1)
            task_running = self.get_attr_val('nsds5task')
            if task_running is None:
                return True
            loop_limit -= 1

        # Task is still running?!
        return False


    def get_suffix(self):
        """Return the suffix
        """
        if self._suffix is None:
            self._populate_suffix()

        return self._suffix

    def status(self, binddn=None, bindpw=None, winsync=False):
        """Get a list of the status for every agreement
        """
        agmtList = []
        agmts = Agreements(self._instance, self.dn, winsync=winsync).list()
        for agmt in agmts:
            raw_status = agmt.status(binddn=binddn, bindpw=bindpw, use_json=True, winsync=winsync)
            agmtList.append(json.loads(raw_status))

        # sort the list of agreements by the lag time
        sortedList = sorted(agmtList, key=itemgetter('replication-lag-time'))
        return(sortedList)

    def get_tombstone_count(self):
        """Get the number of tombstones
        """
        tombstones = Tombstones(self._instance, self._suffix).list()
        return len(tombstones)


class Replicas(DSLdapObjects):
    """Replica DSLdapObjects for all replicas

    :param instance: A instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance):
        super(Replicas, self).__init__(instance=instance)
        self._objectclasses = [REPLICA_OBJECTCLASS_VALUE]
        self._filterattrs = [REPL_ROOT]
        self._childobject = Replica
        self._basedn = DN_MAPPING_TREE

    def create(self, rdn=None, properties=None):
        replica = super(Replicas, self).create(rdn, properties)

        # Set up changelog trimming by default
        if properties is not None:
            for attr, val in properties.items():
                if attr.lower() == 'nsds5replicaroot':
                    cl = Changelog(self._instance, val[0])
                    cl.set_max_age("7d")
                    break

        return replica

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
            replica._populate_suffix()
        return replica

    def process_and_dump_changelog(self, replica_root, output_file, csn_only=False, preserve_ldif_done=False, decode=False):
        """Dump and decode Directory Server replication changelog

        :param replica_root: Replica suffix that needs to be processed
        :type replica_root: DN
        :param output_file: The file name for the exported changelog LDIF file
        :type replica_root: str
        :param csn_only: Grep only the CSNs from the file
        :type csn_only: bool
        :param preserve_ldif_done: Preserve the result LDIF and rename it to [old_name].done
        :type preserve_ldif_done: bool
        :param decode: Decode any base64 values from the changelog
        :type log: bool
        """

        # Dump the changelog for the replica
        try:
            replica = self.get(replica_root)
            replica_name = replica.get_attr_val_utf8_l("nsDS5ReplicaName")
            ldif_dir = self._instance.get_ldif_dir()
            file_path = os.path.join(ldif_dir, f'{replica_name}_cl.ldif')
        except:
            raise ValueError(f'The suffix "{replica_root}" is not enabled for replication')

        replica.begin_task_cl2ldif()
        if not replica.task_finished():
            raise ValueError("The changelog to LDIF task (CL2LDIF) did not complete in time")

        # Decode the dumped changelog if we are using a non default location
        cl_ldif = ChangelogLDIF(file_path, output_file=output_file)
        if csn_only:
            cl_ldif.grep_csn()
        elif decode:
            cl_ldif.decode()
        else:
            cl_ldif.process()

        if preserve_ldif_done:
            os.rename(file_path, f'{file_path}.done')
        else:
            os.remove(file_path)

    def restore_changelog(self, replica_root, log=None):
        """Restore Directory Server replication changelog from '.ldif' or '.ldif.done' file

        :param replica_root: Replica suffixes that need to be processed (and optional LDIF file path)
        :type replica_root: list of str
        :param log: The logger object
        :type log: logger
        """

        # Dump the changelog for the replica
        try:
            replica = self.get(replica_root)
        except:
            raise ValueError(f'The specified root "{replica_root}" is not enbaled for replication')

        replica_name = replica.get_attr_val_utf8_l("nsDS5ReplicaName")
        ldif_dir = self._instance.get_ldif_dir()
        cl_dir_content = os.listdir(ldif_dir)
        changelog_ldif = [i.lower() for i in cl_dir_content if i.lower() == f"{replica_name}_cl.ldif"]
        changelog_ldif_done = [i.lower() for i in cl_dir_content if i.lower() == f"{replica_name}_cl.ldif.done"]

        if changelog_ldif:
            replica.begin_task_ldif2cl()
            if not replica.task_finished():
                raise ValueError("The changelog import task (LDIF2CL) did not complete in time")
        elif changelog_ldif_done:
            ldif_done_file = os.path.join(ldif_dir, changelog_ldif_done[0])
            ldif_file = os.path.join(ldif_dir, f"{replica_name}_cl.ldif")
            ldif_file_exists = os.path.exists(ldif_file)
            if ldif_file_exists:
                copy_with_permissions(ldif_file, f'{ldif_file}.backup')
            copy_with_permissions(ldif_done_file, ldif_file)
            replica.begin_task_ldif2cl()
            if not replica.task_finished():
                raise ValueError("The changelog import task (LDIF2CL) did not complete in time")
            os.remove(ldif_file)
            if ldif_file_exists:
                os.rename(f'{ldif_file}.backup', ldif_file)
        else:
            log.error(f"Changelog LDIF for '{replica_root}' was not found")


class BootstrapReplicationManager(DSLdapObject):
    """A Replication Manager credential for bootstrapping the repl process.
    This is used by the replication manager object to coordinate the initial
    init so that server creds are available.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: The dn to create
    :type dn: str
    :param rdn_attr: The attribute to use for the RDN
    :type rdn_attr: str
    """
    def __init__(self, instance, dn='cn=replication manager,cn=config', rdn_attr='cn'):
        super(BootstrapReplicationManager, self).__init__(instance, dn)
        self._rdn_attribute = rdn_attr
        self._must_attributes = ['cn', 'userPassword']
        self._create_objectclasses = [
            'top',
            'inetUser',  # for uid
            'netscapeServer',  # for cn
            'nsAccount',  # for authentication attributes
            ]
        if ds_is_older('1.4.0'):
            self._create_objectclasses.remove('nsAccount')
        self._protected = False
        self.common_name = 'replication manager'


class ReplicationManager(object):
    """The lib389 replication manager. This is used to coordinate
    replicas and agreements between servers.

    Unlike the raw replicas / agreement types that manipulate the
    servers configuration, this is a "high level" coordination type.
    It's capable of taking multiple instances and joining them. It
    consumes many lib389 types like Replicas, Agreements and more.

    It is capable of creating the first supplier in a topoolgy, joining
    suppliers and consumers to that topology, populating per-server
    replication credentials, dynamic rid allocation, and more.

    Unlike hand management of agreements, this is able to take simpler
    steps to agreement creation. For example:

    repl = ReplicationManager(<suffix>)
    repl.create_first_supplier(supplier1)
    repl.join_supplier(supplier1, supplier2)

    Contrast to previous implementations of replication which required
    much more knowledge and parameters, this is able to securely add
    suppliers.

    :param suffix: The suffix to replicate.
    :type suffix: str
    :param logger: A logging interface
    :type logger: python logging

    """
    def __init__(self, suffix, logger=None):
        self._suffix = suffix
        if logger is not None:
            self._log = logger
        else:
            self._log = logging.getLogger(__name__)
        self._alloc_rids = []
        self._repl_creds = {}

    def _inst_to_agreement_name(self, to_instance):
        """From an instance, determine the agreement name that we
        would use for it. Internal only.
        """
        return str(to_instance.port)[-3:]

    def create_first_supplier(self, instance):
        """In a topology, this creates the "first" supplier that has the
        database and content. A number of bootstrap tasks are performed
        on this supplier, as well as creating it's replica type.

        Once the first supplier is created, all other suppliers can be joined to
        it via "join_supplier".

        :param instance: An instance
        :type instance: lib389.DirSrv
        """
        # This is a special wrapper to create. We know it's a supplier,
        # and this is the "first" of the topology.
        # So this can wrap it and make it easy.
        self._log.debug("Creating first supplier on %s" % instance.ldapuri)

        # With changelog now integrated with the main database
        # The config cn=changelog5,cn=config entry is no longer needed

        rgroup_dn = self._create_service_account(instance, instance)

        # Allocate the first rid, 1.
        replicas = Replicas(instance)
        replicas.create(properties={
            'cn': 'replica',
            'nsDS5ReplicaRoot': self._suffix,
            'nsDS5ReplicaId': '1',
            'nsDS5Flags': '1',
            'nsDS5ReplicaType': '3',
            'nsDS5ReplicaBindDNGroup': rgroup_dn,
            'nsds5replicabinddngroupcheckinterval': '0'
        })
        self._log.debug("SUCCESS: Created first supplier on %s" % instance.ldapuri)

    def _create_service_group(self, from_instance):
        """Internally create the service group that contains replication managers.
        This may become part of the default objects in the future. Internal only.

        When we join a consumer to hub the function check that the group is in place
        """

        groups = Groups(from_instance, basedn=self._suffix, rdn=None)
        from_replicas = Replicas(from_instance)
        try:
            from_r = from_replicas.get(self._suffix)
            repl_type = from_r.get_attr_val_int('nsDS5ReplicaType')
        except ldap.NO_SUCH_OBJECT:
            repl_type = None

        if repl_type == 3 or repl_type is None:
            repl_group = groups.ensure_state(properties={
                'cn': 'replication_managers',
            })
            return repl_group
        else:
            try:
                repl_group = groups.get('replication_managers')
                return repl_group
            except ldap.NO_SUCH_OBJECT:
                self._log.warning("{} doesn't have cn=replication_managers,{} entry \
                          and the instance is not read-write".format(from_instance.serverid, self._suffix))
                raise

    def _create_service_account(self, from_instance, to_instance):
        """Create the server replication service account, and
        make it a member of the service group. Internal Only.
        """
        repl_group = self._create_service_group(from_instance)
        # Create our service account.
        ous = OrganizationalUnits(from_instance, self._suffix)
        ous.ensure_state(properties={
            'ou': 'Services'
        })

        # Do we have TLS?
        port = to_instance.sslport

        services = ServiceAccounts(from_instance, self._suffix)
        # Generate the password and save the credentials
        # for putting them into agreements in the future
        service_name = '{}:{}'.format(to_instance.host, port)
        creds = password_generate()
        repl_service = services.ensure_state(properties={
            'cn': service_name,
            'userPassword': creds
        })
        self._repl_creds[service_name] = creds

        repl_group.ensure_member(repl_service.dn)

        return repl_group.dn

    def _bootstrap_replica(self, from_replica, to_replica, to_instance):
        """In the supplier join process a chicken-egg issues arises
        that we require the service account on the target supplier for
        our agreement to be valid, but be can't send it that data without
        our service account.

        Resolve that issue by "bootstrapping" the database. This creates a
        bootstrap replication manager and conducts a one-way total init.
        Once complete the bootstrap agreement is removed, and the service
        accounts now exist on both ends allowing the join process to continue.

        Internal Only.
        """
        repl_manager_password = password_generate()
        # Create a repl manager on the replica
        brm = BootstrapReplicationManager(to_instance)
        brm.create(properties={
            'cn': brm.common_name,
            'userPassword': repl_manager_password
        })

        to_replica.set('nsDS5ReplicaBindDN', brm.dn)

        agmt_name = self._inst_to_agreement_name(to_instance)

        # add a temp agreement from A -> B
        from_agreements = from_replica.get_agreements()
        temp_agmt = from_agreements.create(properties={
            'cn': "temp_%s" % agmt_name,
            'nsDS5ReplicaRoot': self._suffix,
            'nsDS5ReplicaBindDN': brm.dn,
            'nsDS5ReplicaBindMethod': 'simple' ,
            'nsDS5ReplicaTransportInfo': 'LDAP',
            'nsds5replicaTimeout': '5',
            'description': "temp_%s" % agmt_name,
            'nsDS5ReplicaHost': to_instance.host,
            'nsDS5ReplicaPort': str(to_instance.port),
            'nsDS5ReplicaCredentials': repl_manager_password,
            'nsds5ReplicaFlowControlWindow': '100',
        })
        # Do a replica refresh.
        temp_agmt.begin_reinit()
        (done, error) = temp_agmt.wait_reinit()
        assert done is True
        assert error is False
        # Now remove the temp agmt between A -> B
        temp_agmt.delete()
        # Rm the binddn.
        to_replica.remove_all('nsDS5ReplicaBindDN')
        # Remove the repl manager.
        brm.delete()
        self._log.info("SUCCESS: bootstrap to %s completed" % to_instance.ldapuri)

    def join_supplier(self, from_instance, to_instance):
        """Join a new supplier in MMR to this instance. This will complete
        a total init of the data "from instance" to "to instance".

        This can be conducted from any supplier in the topology as "from" supplier.

        :param from_instance: An instance already in the topology.
        :type from_instance: lib389.DirSrv
        :param to_instance: An instance to join to the topology.
        :type to_instance: lib389.DirSrv
        """
        # Is the to_instance already a replica of the suffix?
        to_replicas = Replicas(to_instance)
        try:
            to_r = to_replicas.get(self._suffix)
            self._log.warning("{} is already a replica for this suffix".format(to_instance.serverid))
            return
        except ldap.NO_SUCH_OBJECT:
            pass

        # Make sure we replicate this suffix too ...
        from_replicas = Replicas(from_instance)
        from_r = from_replicas.get(self._suffix)

        # Create our credentials
        repl_dn = self._create_service_account(from_instance, to_instance)

        # Find the ruv on from_instance
        ruv = from_r.get_ruv()

        # Get a free rid
        rid = ruv.alloc_rid()
        assert rid not in self._alloc_rids
        self._alloc_rids.append(rid)

        self._log.debug("Allocating rid %s" % rid)
        # Create replica on to_instance, with bootstrap details.
        to_r = to_replicas.create(properties={
            'cn': 'replica',
            'nsDS5ReplicaRoot': self._suffix,
            'nsDS5ReplicaId': rid,
            'nsDS5Flags': '1',
            'nsDS5ReplicaType': '3',
            'nsds5replicabinddngroupcheckinterval': '0'
        })

        # WARNING: You need to create passwords and agmts BEFORE you tot_init!

        # perform the _bootstrap. This creates a temporary repl manager
        # to allow the tot_init to occur.
        self._bootstrap_replica(from_r, to_r, to_instance)

        # Now put in an agreement from to -> from
        # both ends.
        self.ensure_agreement(from_instance, to_instance)
        self.ensure_agreement(to_instance, from_instance, init=True)

        # Now fix our replica credentials from -> to
        to_r.set('nsDS5ReplicaBindDNGroup', repl_dn)

        # Now finally test it ...
        self.test_replication(from_instance, to_instance)
        self.test_replication(to_instance, from_instance)
        # Done!
        self._log.info("SUCCESS: joined supplier from %s to %s" % (from_instance.ldapuri, to_instance.ldapuri))

    def join_hub(self, from_instance, to_instance):
        """Join a new hub to this instance. This will complete
        a total init of the data "from instance" to "to instance".

        This can be conducted from any supplier or hub in the topology as "from" supplier.

        Not implement yet.

        :param from_instance: An instance already in the topology.
        :type from_instance: lib389.DirSrv
        :param to_instance: An instance to join to the topology.
        :type to_instance: lib389.DirSrv
        """

        to_replicas = Replicas(to_instance)
        try:
            to_r = to_replicas.get(self._suffix)
            self._log.warning("{} is already a replica for this suffix".format(to_instance.serverid))
            return
        except ldap.NO_SUCH_OBJECT:
            pass

        # Make sure we replicate this suffix too ...
        from_replicas = Replicas(from_instance)
        from_r = from_replicas.get(self._suffix)

        # Create replica on to_instance, with bootstrap details.
        to_r = to_replicas.create(properties={
            'cn': 'replica',
            'nsDS5ReplicaRoot': self._suffix,
            'nsDS5ReplicaId': '65535',
            'nsDS5Flags': '1',
            'nsDS5ReplicaType': '2',
            'nsds5replicabinddngroupcheckinterval': '0'
        })

        # WARNING: You need to create passwords and agmts BEFORE you tot_init!
        repl_dn = self._create_service_account(from_instance, to_instance)

        # perform the _bootstrap. This creates a temporary repl manager
        # to allow the tot_init to occur.
        self._bootstrap_replica(from_r, to_r, to_instance)

        # Now put in an agreement from to -> from
        # both ends.
        self.ensure_agreement(from_instance, to_instance)

        # Now fix our replica credentials from -> to
        to_r.set('nsDS5ReplicaBindDNGroup', repl_dn)

        # Now finally test it ...
        self.test_replication(from_instance, to_instance)
        # Done!
        self._log.info("SUCCESS: joined consumer from %s to %s" % (from_instance.ldapuri, to_instance.ldapuri))

    def join_consumer(self, from_instance, to_instance):
        """Join a new consumer to this instance. This will complete
        a total init of the data "from instance" to "to instance".

        This can be conducted from any supplier or hub in the topology as "from" supplier.


        :param from_instance: An instance already in the topology.
        :type from_instance: lib389.DirSrv
        :param to_instance: An instance to join to the topology.
        :type to_instance: lib389.DirSrv
        """
        to_replicas = Replicas(to_instance)
        try:
            to_r = to_replicas.get(self._suffix)
            self._log.warning("{} is already a replica for this suffix".format(to_instance.serverid))
            return
        except ldap.NO_SUCH_OBJECT:
            pass

        # Make sure we replicate this suffix too ...
        from_replicas = Replicas(from_instance)
        from_r = from_replicas.get(self._suffix)

        # Create replica on to_instance, with bootstrap details.
        to_r = to_replicas.create(properties={
            'cn': 'replica',
            'nsDS5ReplicaRoot': self._suffix,
            'nsDS5ReplicaId': '65535',
            'nsDS5Flags': '0',
            'nsDS5ReplicaType': '2',
            'nsds5replicabinddngroupcheckinterval': '0'
        })

        # WARNING: You need to create passwords and agmts BEFORE you tot_init!
        # If from_instance replica isn't read-write (hub, probably), we just check it is there
        repl_group = self._create_service_group(from_instance)

        # perform the _bootstrap. This creates a temporary repl manager
        # to allow the tot_init to occur.
        self._bootstrap_replica(from_r, to_r, to_instance)

        # Now put in an agreement from to -> from
        # both ends.
        self.ensure_agreement(from_instance, to_instance)

        # Now fix our replica credentials from -> to
        to_r.set('nsDS5ReplicaBindDNGroup', repl_group.dn)

        # Now finally test it ...
        # If from_instance replica isn't read-write (hub, probably), we will test it later
        if from_r.get_attr_val_int('nsDS5ReplicaType') == 3:
            self.test_replication(from_instance, to_instance)

        # Done!
        self._log.info("SUCCESS: joined consumer from %s to %s" % (from_instance.ldapuri, to_instance.ldapuri))

    def _get_replica_creds(self, from_instance, write_instance):
        """For the supplier "from_instance" create or derive the credentials
        needed for it's replication service account. In some cases the
        credentials are created, write them to "write instance" as a new
        service account userPassword.

        This function signature exists for bootstrapping: We need to
        link supplier A and B, but they have not yet replicated. So we generate
        credentials for B, and write them to A's instance, where they will
        then be replicated back to B. If this wasn't the case, we would generate
        the credentials on B, write them to B, but B has no way to authenticate
        to A because the service account doesn't have credentials there yet.

        Internal Only.
        """

        rdn = '{}:{}'.format(from_instance.host, from_instance.sslport)
        try:
            creds = self._repl_creds[rdn]
        except KeyError:
            # okay, re-use the creds
            fr_replicas = Replicas(from_instance)
            fr_r = fr_replicas.get(self._suffix)
            from_agmts = fr_r.get_agreements()
            agmts = from_agmts.list()

            assert len(agmts) > 0, "from_instance agreement is not found and credentials are not present \
                                    in ReplicationManager. You should call create_first_supplier first."
            agmt = agmts[0]
            creds = agmt.get_attr_val_utf8('nsDS5ReplicaCredentials')

        services = ServiceAccounts(write_instance, self._suffix)
        sa_dn = services.get(rdn).dn

        return sa_dn, creds

    def ensure_agreement(self, from_instance, to_instance, init=False):
        """Guarantee that a replication agreement exists 'from_instance' send
        data 'to_instance'. This can be for *any* instance, supplier, hub, or
        consumer.

        Both instances must have been added to the topology with
        create first supplier, join_supplier, join_consumer or join_hub.

        :param from_instance: An instance already in the topology.
        :type from_instance: lib389.DirSrv
        :param to_instance: An instance to replicate to.
        :type to_instance: lib389.DirSrv
        """
        # Make sure that an agreement from -> to exists.
        # At the moment we assert this by checking host and port
        # details.

        # init = True means to create credentials on the "to" supplier, because
        # we are initialising in reverse.

        # init = False (default) means creds *might* exist, and we create them
        # on the "from" supplier.

        from_replicas = Replicas(from_instance)
        from_r = from_replicas.get(self._suffix)

        from_agmts = from_r.get_agreements()

        agmt_name = self._inst_to_agreement_name(to_instance)

        try:
            agmt = from_agmts.get(agmt_name)
            self._log.info("SUCCESS: Agreement from %s to %s already exists" % (from_instance.ldapuri, to_instance.ldapuri))
            return agmt
        except ldap.NO_SUCH_OBJECT:
            # Okay, it doesn't exist, lets go ahead!
            pass

        if init is True:
            (dn, creds) = self._get_replica_creds(from_instance, to_instance)
        else:
            (dn, creds) = self._get_replica_creds(from_instance, from_instance)

        assert dn is not None
        assert creds is not None

        agmt = from_agmts.create(properties={
            'cn': agmt_name,
            'nsDS5ReplicaRoot': self._suffix,
            'nsDS5ReplicaBindDN': dn,
            'nsDS5ReplicaBindMethod': 'simple' ,
            'nsDS5ReplicaTransportInfo': 'LDAP',
            'nsds5replicaTimeout': '5',
            'description': agmt_name,
            'nsDS5ReplicaHost': to_instance.host,
            'nsDS5ReplicaPort': str(to_instance.port),
            'nsDS5ReplicaCredentials': creds,
        })
        # Done!
        self._log.info("SUCCESS: Agreement from %s to %s is was created" % (from_instance.ldapuri, to_instance.ldapuri))
        return agmt

    def remove_supplier(self, instance, remaining_instances=[], purge_sa=True):
        """Remove an instance from the replication topology.

        If purge service accounts is true, remove the instances service account.

        The purge_sa *must* be conducted on a remaining supplier to guarantee
        the result.

        We recommend remaining instances contains *all* suppliers that have an
        agreement to instance, to ensure no dangling agreements exist. Suppliers
        with no agreement are skipped.

        :param instance: An instance to remove from the topology.
        :type from_instance: lib389.DirSrv
        :param remaining_instances: The remaining suppliers of the topology.
        :type remaining_instances: list[lib389.DirSrv]
        :param purge_sa: Purge the service account for instance
        :type purge_sa: bool
        """
        if purge_sa and len(remaining_instances) > 0:
            services = ServiceAccounts(remaining_instances[0], self._suffix)
            try:
                sa = services.get('%s:%s' % (instance.host, instance.sslport))
                sa.delete()
            except ldap.NO_SUCH_OBJECT:
                # It's already gone ...
                pass

        agmt_name = self._inst_to_agreement_name(instance)
        for r_inst in remaining_instances:
            agmts = Agreements(r_inst)
            try:
                agmt = agmts.get(agmt_name)
                agmt.delete()
            except ldap.NO_SUCH_OBJECT:
                # No agreement, that's good!
                pass

        from_replicas = Replicas(instance)
        from_r = from_replicas.get(self._suffix)
        # This should delete the agreements ....
        from_r.delete()

    def disable_to_supplier(self, to_instance, from_instances=[]):
        """For all suppliers "from" disable all agreements "to" instance.

        :param to_instance: The instance to stop recieving data.
        :type to_instance: lib389.DirSrv
        :param from_instances: The instances to stop sending data.
        :type from_instances: list[lib389.DirSrv]
        """
        agmt_name = self._inst_to_agreement_name(to_instance)
        for r_inst in from_instances:
            agmts = Agreements(r_inst)
            agmt = agmts.get(agmt_name)
            agmt.pause()

    def enable_to_supplier(self, to_instance, from_instances=[]):
        """For all suppliers "from" enable all agreements "to" instance.

        :param to_instance: The instance to start recieving data.
        :type to_instance: lib389.DirSrv
        :param from_instances: The instances to start sending data.
        :type from_instances: list[lib389.DirSrv]
        """
        agmt_name = self._inst_to_agreement_name(to_instance)
        for r_inst in from_instances:
            agmts = Agreements(r_inst)
            agmt = agmts.get(agmt_name)
            agmt.resume()

    def wait_for_ruv(self, from_instance, to_instance, timeout=20):
        """Wait for the in-memory ruv 'from_instance' to be advanced past on
        'to_instance'. Note this does not mean the ruvs are "exact matches"
        only that some set of CSN states has been advanced past. Topics like
        fractional replication may or may not interfer in this process.

        In essence this is a rough check that to_instance is at least
        at the replication state of from_instance. You should consider using
        wait_for_replication instead for a guarantee.

        :param from_instance: The instance whos state we we want to check from
        :type from_instance: lib389.DirSrv
        :param to_instance: The instance whos state we want to check matches from.
        :type to_instance: lib389.DirSrv

        """
        from_replicas = Replicas(from_instance)
        from_r = from_replicas.get(self._suffix)

        to_replicas = Replicas(to_instance)
        to_r = to_replicas.get(self._suffix)

        from_ruv = from_r.get_ruv()

        for i in range(0, timeout):
            to_ruv = to_r.get_ruv()
            if to_ruv.is_synced(from_ruv):
                self._log.info("SUCCESS: RUV from %s to %s is in sync" % (from_instance.ldapuri, to_instance.ldapuri))
                return True
            time.sleep(1)
        raise Exception("RUV did not sync in time!")

    def wait_while_replication_is_progressing(self, from_instance, to_instance, timeout=5):
        """ Wait while replication is progressing
            used by wait_for_replication to avoid timeout because of
              slow replication (typically when traces have been added)
            Returns true is repliaction is stalled.

        :param from_instance: The instance whos state we we want to check from
        :type from_instance: lib389.DirSrv
        :param to_instance: The instance whos state we want to check matches from.
        :type to_instance: lib389.DirSrv
        :param timeout: Fail after timeout seconds.
        :type timeout: int

        """
        from_replicas = Replicas(from_instance)
        from_r = from_replicas.get(self._suffix)

        to_replicas = Replicas(to_instance)
        to_r = to_replicas.get(self._suffix)

        target_csn = from_r.get_maxcsn()
        last_csn = '00000000000000000000'
        try:
            csn = to_r.get_maxcsn(from_r.get_rid())
        except Exception:
            csn = '00000000000000000000'
        while (csn < target_csn):
            last_csn = csn
            for i in range(0, timeout):
                time.sleep(1)
                try:
                    csn = to_r.get_maxcsn(from_r.get_rid())
                except Exception:
                    csn = '00000000000000000000'
                if csn > last_csn:
                    break
            if csn <= last_csn:
                return False
        return True


    def wait_for_replication(self, from_instance, to_instance, timeout=60):
        """Wait for a replication event to occur from instance to instance. This
        shows some point of synchronisation has occured.

        :param from_instance: The instance whos state we we want to check from
        :type from_instance: lib389.DirSrv
        :param to_instance: The instance whos state we want to check matches from.
        :type to_instance: lib389.DirSrv
        :param timeout: Fail after timeout seconds.
        :type timeout: int

        """
        # Touch something then wait_for_replication.
        from_groups = Groups(from_instance, basedn=self._suffix, rdn=None)
        to_groups = Groups(to_instance, basedn=self._suffix, rdn=None)
        from_group = from_groups.get('replication_managers')
        to_group = to_groups.get('replication_managers')

        change = str(uuid.uuid4())

        from_group.replace('description', change)
        self.wait_while_replication_is_progressing(from_instance, to_instance)

        for i in range(0, timeout):
            desc = to_group.get_attr_val_utf8('description')
            from_desc = from_group.get_attr_val_utf8('description')

            if change == desc and change == from_desc:
                self._log.info("SUCCESS: Replication from %s to %s is working" % (from_instance.ldapuri, to_instance.ldapuri))
                return True
            if desc == from_desc:
                self._log.info("Retry: Replication from %s to %s is in sync but not having expected value (expect %s / got description=%s)" % (from_instance.ldapuri, to_instance.ldapuri, change, desc))
                from_group.replace('description', change)
            else:
                self._log.info("Retry: Replication from %s to %s is NOT in sync (description=%s / description=%s)" % (from_instance.ldapuri, to_instance.ldapuri, from_desc, desc))
            time.sleep(1)
        self._log.info("FAIL: Replication from %s to %s is NOT working. (too many retries)" % (from_instance.ldapuri, to_instance.ldapuri))
        # Replication is broken ==> Lets get the replication error logs
        for inst in (from_instance, to_instance):
            self._log.info(f"*** {inst.serverid} Error log: ***")
            lines = DirsrvErrorLog(inst).match('.*NSMMReplicationPlugin.*')
            # Keep only last lines (enough to be sure to log last replication session)
            n = 30
            if len(lines) > n:
                lines = lines[-n:]
            for line in lines:
                self._log.info(line.strip())
        raise Exception("Replication did not sync in time!")


    def test_replication(self, from_instance, to_instance, timeout=20):
        """Wait for a replication event to occur from instance to instance. This
        shows some point of synchronisation has occured.

        :param from_instance: The instance whos state we we want to check from
        :type from_instance: lib389.DirSrv
        :param to_instance: The instance whos state we want to check matches from.
        :type to_instance: lib389.DirSrv
        :param timeout: Fail after timeout seconds.
        :type timeout: int

        """
        # It's the same ....
        self.wait_for_replication(from_instance, to_instance, timeout)

    def test_replication_topology(self, instances, timeout=20):
        """Confirm replication works between all permutations of suppliers
        in the topology.

        :param instances: The suppliers.
        :type instances: list[lib389.DirSrv]
        :param timeout: Fail after timeout seconds.
        :type timeout: int

        """
        for p in permutations(instances, 2):
            a, b = p
            self.test_replication(a, b, timeout)

    def get_rid(self, instance):
        """For a given supplier, retrieve it's RID for this suffix.

        :param instance: The instance
        :type instance: lib389.DirSrv
        :returns: str
        """
        replicas = Replicas(instance)
        replica = replicas.get(self._suffix)
        return replica.get_rid()


class ReplicationMonitor(object):
    """The lib389 replication monitor. This is used to check the status
    of many instances at once.
    It also allows to monitor independent topologies and get them into
    the one combined report.

    :param instance: A supplier or hub for replication topology monitoring
    :type instance: list of DirSrv objects
    :param logger: A logging interface
    :type logger: python logging
    """

    def __init__(self, instance, logger=None):
        self._instance = instance
        if logger is not None:
            self._log = logger
        else:
            self._log = logging.getLogger(__name__)

    def _get_replica_status(self, instance, report_data, use_json, get_credentials=None):
        """Load all of the status data to report
        and add new hostname:port pairs for future processing
        :type get_credentials: function
        """

        replicas_status = []
        replicas = Replicas(instance)
        for replica in replicas.list():
            replica_id = replica.get_rid()
            replica_root = replica.get_suffix()
            replica_maxcsn = replica.get_maxcsn()
            agmts_status = []
            agmts = replica.get_agreements()
            for agmt in agmts.list():
                host = agmt.get_attr_val_utf8_l("nsds5replicahost")
                port = agmt.get_attr_val_utf8_l("nsds5replicaport")
                if get_credentials is not None:
                    credentials = get_credentials(host, port)
                    binddn = credentials["binddn"]
                    bindpw = credentials["bindpw"]
                else:
                    binddn = instance.binddn
                    bindpw = instance.bindpw
                protocol = agmt.get_attr_val_utf8_l('nsds5replicatransportinfo')
                # Supply protocol here because we need it only for connection
                # and agreement status is already preformatted for the user output
                consumer = f"{host}:{port}"
                if consumer not in report_data:
                    report_data[f"{consumer}:{protocol}"] = None
                if use_json:
                    agmts_status.append(json.loads(agmt.status(use_json=True, binddn=binddn, bindpw=bindpw)))
                else:
                    agmts_status.append(agmt.status(binddn=binddn, bindpw=bindpw))
            replicas_status.append({"replica_id": replica_id,
                                    "replica_root": replica_root,
                                    "replica_status": "Online",
                                    "maxcsn": replica_maxcsn,
                                    "agmts_status": agmts_status})
        return replicas_status

    def generate_report(self, get_credentials, use_json=False):
        """Generate a replication report for each supplier or hub and the instances
        that are connected with it by agreements.

        :param get_credentials: A user-defined callback function with parameters (host, port) which returns
                                a dictionary with binddn and bindpw keys -
                                example values "cn=Directory Manager" and "password"
        :type get_credentials: function
        :returns: dict
        """
        report_data = {}
        initial_inst_key = f"{self._instance.config.get_attr_val_utf8_l('nsslapd-localhost')}:{self._instance.config.get_attr_val_utf8_l('nsslapd-port')}"
        # Do this on an initial instance to get the agreements to other instances
        try:
            report_data[initial_inst_key] = self._get_replica_status(self._instance, report_data, use_json, get_credentials)
        except ldap.LDAPError as e:
            self._log.debug(f"Connection to consumer ({supplier_hostname}:{supplier_port}) failed, error: {e}")
            report_data[initial_inst_key] = [{"replica_status": f"Unreachable - {e.args[0]['desc']}"}]

        # Check if at least some replica report on other instances was generated
        repl_exists = False

        # While we have unprocessed instances - continue
        while True:
            try:
                supplier = [host_port for host_port, processed_data in report_data.items() if processed_data is None][0]
            except IndexError:
                break

            del report_data[supplier]
            s_splitted = supplier.split(":")
            supplier_hostname = s_splitted[0]
            supplier_port = s_splitted[1]
            supplier_protocol = s_splitted[2]
            supplier_hostport_only = ":".join(s_splitted[:2])

            # The function should be defined outside and
            # it should have all the logic for figuring out the credentials.
            # It is done for flexibility purpuses between CLI, WebUI and lib389 API applications
            credentials = get_credentials(supplier_hostname, supplier_port)
            if not credentials["binddn"]:
                report_data[supplier_hostport_only] = [{"replica_status": "Unavailable - Bind DN was not specified"}]
                continue

            # Open a connection to the consumer
            supplier_inst = DirSrv(verbose=self._instance.verbose)
            args_instance = {SER_HOST: supplier_hostname}
            if supplier_protocol == "ssl" or supplier_protocol == "ldaps":
                args_instance[SER_SECURE_PORT] = int(supplier_port)
            else:
                args_instance[SER_PORT] = int(supplier_port)
            args_instance[SER_ROOT_DN] = credentials["binddn"]
            args_instance[SER_ROOT_PW] = credentials["bindpw"]
            args_standalone = args_instance.copy()
            supplier_inst.allocate(args_standalone)
            try:
                supplier_inst.open()
            except ldap.LDAPError as e:
                self._log.debug(f"Connection to consumer ({supplier_hostname}:{supplier_port}) failed, error: {e}")
                report_data[supplier_hostport_only] = [{"replica_status": f"Unreachable - {e.args[0]['desc']}"}]
                continue

            report_data[supplier_hostport_only] = self._get_replica_status(supplier_inst, report_data, use_json)
            repl_exists = True

        # Get rid of the repeated items
        report_data_parsed = {}
        for key, value in report_data.items():
            current_inst_rids = [val["replica_id"] for val in report_data[key] if "replica_id" in val.keys()]
            report_data_parsed[key] = sorted(current_inst_rids)
        report_data_filtered = {}
        for key, value in report_data_parsed.items():
            if value not in report_data_filtered.values():
                report_data_filtered[key] = value

        # Now remove the protocol from the name
        report_data_final = {}
        for key, value in report_data.items():
            # We take the initial instance only if it is the only existing part of the report
            if key in report_data_filtered.keys() or not repl_exists:
                if not value:
                    value = [{"replica_status": "Unavailable - No replicas were found"}]

                report_data_final[key] = value

        return report_data_final
