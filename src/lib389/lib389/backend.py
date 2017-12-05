# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389._constants import *
from lib389.properties import *
from lib389.utils import normalizeDN, ensure_str, ensure_bytes
from lib389 import Entry

# Need to fix this ....

from lib389._mapped_object import DSLdapObjects, DSLdapObject
from lib389.mappingTree import MappingTrees, MappingTree
from lib389.exceptions import NoSuchEntryError, InvalidArgumentError

# We need to be a factor to the backend monitor
from lib389.monitor import MonitorBackend
from lib389.index import Indexes

# This is for sample entry creation.
from lib389.configurations import get_sample_entries

from lib389.lint import DSBLE0001

class BackendLegacy(object):
    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Backend.proxied_methods:
            from lib389 import DirSrv
            return DirSrv.__getattr__(self.conn, name)

    def list(self, suffix=None, backend_dn=None, bename=None):
        """
            Returns a search result of the backend(s) entries with all their
            attributes

            If 'suffix'/'backend_dn'/'benamebase' are specified. It uses
            'backend_dn' first, then 'suffix', then 'benamebase'.

            If neither 'suffix', 'backend_dn' and 'benamebase' are specified,
            it returns all the backend entries

            Get backends by name or suffix

            @param suffix - suffix of the backend
            @param backend_dn - DN of the backend entry
            @param bename - 'commonname'/'cn' of the backend (e.g. 'userRoot')

            @return backend entries

            @raise None
        """

        filt = "(objectclass=%s)" % BACKEND_OBJECTCLASS_VALUE
        if backend_dn:
            self.log.info("List backend %s" % backend_dn)
            base = backend_dn
            scope = ldap.SCOPE_BASE
        elif suffix:
            self.log.info("List backend with suffix=%s" % suffix)
            base = DN_PLUGIN
            scope = ldap.SCOPE_SUBTREE
            filt = ("(&%s(|(%s=%s)(%s=%s)))" %
                    (filt,
                     BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX], suffix,
                     BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX],
                     normalizeDN(suffix))
                    )
        elif bename:
            self.log.info("List backend 'cn=%s'" % bename)
            base = "%s=%s,%s" % (BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME],
                                 bename, DN_LDBM)
            scope = ldap.SCOPE_BASE
        else:
            self.log.info("List all the backends")
            base = DN_PLUGIN
            scope = ldap.SCOPE_SUBTREE

        try:
            ents = self.conn.search_s(base, scope, filt)
        except ldap.NO_SUCH_OBJECT:
            return None

        return ents

    def _readonly(self, bename=None, readonly='on', suffix=None):
        """Put a database in readonly mode
            @param  bename  -   the backend name (eg. addressbook1)
            @param  readonly-   'on' or 'off'

            NOTE: I can ldif2db to a read-only database. After the
                  import, the database will still be in readonly.

            NOTE: When a db is read-only, it seems you need to restart
                  the directory server before creating further
                  agreements or initialize consumers
        """
        if bename and suffix:
            raise ValueError("Specify either bename or suffix")

        if suffix:
            raise NotImplementedError()

        self.conn.modify_s(','.join(('cn=' + bename, DN_LDBM)), [
            (ldap.MOD_REPLACE, BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_READONLY],
             readonly)
        ])

    def delete(self, suffix=None, backend_dn=None, bename=None):
        """
        Deletes the backend entry with the following steps:

        Delete the indexes entries under this backend
        Delete the encrypted attributes entries under this backend
        Delete the encrypted attributes keys entries under this backend

        If a mapping tree entry uses this backend (nsslapd-backend),
        it raise ldap.UNWILLING_TO_PERFORM

        If 'suffix'/'backend_dn'/'benamebase' are specified.
        It uses 'backend_dn' first, then 'suffix', then 'benamebase'.

        @param suffix - suffix of the backend
        @param backend_dn - DN of the backend entry
        @param bename - 'commonname'/'cn' of the backend (e.g. 'userRoot')

        @return None

        @raise ldap.UNWILLING_TO_PERFORM - if several backends match the
                                     argument provided suffix does not
                                     match backend suffix.  It exists a
                                     mapping tree that use that backend


        """

        # First check the backend exists and retrieved its suffix
        be_ents = self.conn.backend.list(suffix=suffix,
                                         backend_dn=backend_dn,
                                         bename=bename)
        if len(be_ents) == 0:
            raise ldap.UNWILLING_TO_PERFORM(
                "Unable to retrieve the backend (%r, %r, %r)" %
                (suffix, backend_dn, bename))
        elif len(be_ents) > 1:
            for ent in be_ents:
                self.log.fatal("Multiple backend match the definition: %s" %
                               ent.dn)
            if (not suffix) and (not backend_dn) and (not bename):
                raise ldap.UNWILLING_TO_PERFORM(
                    "suffix and backend DN and backend name are missing")
            raise ldap.UNWILLING_TO_PERFORM(
                "Not able to identify the backend to delete")
        else:
            be_ent = be_ents[0]
            be_suffix = be_ent.getValue(
                BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX])

        # Verify the provided suffix is the one stored in the found backend
        if suffix:
            if normalizeDN(suffix) != normalizeDN(be_suffix):
                raise ldap.UNWILLING_TO_PERFORM(
                    "provided suffix (%s) differs from backend suffix (%s)"
                    % (suffix, be_suffix))

        # now check there is no mapping tree for that suffix
        mt_ents = self.conn.mappingtree.list(suffix=be_suffix)
        if len(mt_ents) > 0:
            raise ldap.UNWILLING_TO_PERFORM(
                "It still exists a mapping tree (%s) for that backend (%s)" %
                (mt_ents[0].dn, be_ent.dn))

        # Now delete the indexes
        found_bename = ensure_str(be_ent.getValue(BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME]))
        if not bename:
            bename = found_bename
        elif bename.lower() != found_bename.lower():
            raise ldap.UNWILLING_TO_PERFORM("Backend name specified (%s) differs from the retrieved one (%s)" % (bename, found_bename))

        self.conn.index.delete_all(bename)

        # finally delete the backend children and the backend itself
        ents = self.conn.search_s(be_ent.dn, ldap.SCOPE_ONELEVEL)
        for ent in ents:
            self.log.debug("Delete entry children %s" % (ent.dn))
            self.conn.delete_s(ent.dn)

        self.log.debug("Delete backend entry %s" % (be_ent.dn))
        self.conn.delete_s(be_ent.dn)

        return

    def create(self, suffix=None, properties=None):
        """
            Creates backend entry and returns its dn.

            If the properties 'chain-bind-pwd' and 'chain-bind-dn' and
            'chain-urls' are specified the backend is a chained backend.  A
            chaining backend is created under
                'cn=chaining database,cn=plugins,cn=config'.

            A local backend is created under
                'cn=ldbm database,cn=plugins,cn=config'

            @param suffix - suffix stored in the backend
            @param properties - dictionary with properties values
            supported properties are
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
                BACKEND_SUFFIX        = 'suffix'
                BACKEND_SAMPLE_ENTRIES = 'sample_entries'

            @return backend DN of the created backend

            @raise LDAPError

        """
        def _getBackendName(parent):
            '''
                Use to build a backend name that is not already used
            '''
            index = 1
            while True:
                bename = "local%ddb" % index
                base = ("%s=%s,%s" %
                        (BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME],
                         bename, parent))
                filt = "(objectclass=%s)" % BACKEND_OBJECTCLASS_VALUE
                self.log.debug("_getBackendName: baser=%s : fileter=%s" %
                               (base, filt))
                try:
                    self.conn.getEntry(base, ldap.SCOPE_BASE, filt)
                except (NoSuchEntryError, ldap.NO_SUCH_OBJECT):
                    self.log.info("backend name will be %s" % bename)
                    return bename
                index += 1

        # suffix is mandatory. If may be in the properties
        if isinstance(properties, dict) and properties.get(BACKEND_SUFFIX, None) is not None:
            suffix = properties.get(BACKEND_SUFFIX)
        if not suffix:
            raise ldap.UNWILLING_TO_PERFORM('Missing Suffix')
        else:
            nsuffix = normalizeDN(suffix)

        # Check it does not already exist a backend for that suffix
        if self.conn.verbose:
            self.log.info("Checking suffix %s for existence" % suffix)
        ents = self.conn.backend.list(suffix=suffix)
        if len(ents) != 0:
            raise ldap.ALREADY_EXISTS
        # Check if we are creating a local/chained backend
        chained_suffix = (properties and
                          (BACKEND_CHAIN_BIND_DN in properties) and
                          (BACKEND_CHAIN_BIND_PW in properties) and
                          (BACKEND_CHAIN_URLS in properties))
        if chained_suffix:
            self.log.info("Creating a chaining backend")
            dnbase = DN_CHAIN
        else:
            self.log.info("Creating a local backend")
            dnbase = DN_LDBM

        # Get the future backend name
        if properties and BACKEND_NAME in properties:
            cn = properties[BACKEND_NAME]
        else:
            cn = _getBackendName(dnbase)

        # Check the future backend name does not already exists
        # we can imagine having no backends for 'suffix' but having a backend
        # with the same name
        dn = "%s=%s,%s" % (BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME], cn,
                           dnbase)
        ents = self.conn.backend.list(backend_dn=dn)
        if ents:
            raise ldap.ALREADY_EXISTS("Backend already exists with that DN: %s"
                                      % ents[0].dn)

        # All checks are done, Time to create the backend
        try:
            entry = Entry(dn)
            entry.update({
                'objectclass': ['top', 'extensibleObject',
                                BACKEND_OBJECTCLASS_VALUE],
                BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME]: cn,
                BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX]: nsuffix,
            })

            if chained_suffix:
                entry.update(
                    {BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_CHAIN_URLS]:
                     properties[BACKEND_CHAIN_URLS],
                     BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_CHAIN_BIND_DN]:
                     properties[BACKEND_CHAIN_BIND_DN],
                     BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_CHAIN_BIND_PW]:
                     properties[BACKEND_CHAIN_BIND_PW]
                     })

            self.log.debug("adding entry: %r" % entry)
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS as e:
            self.log.error("Entry already exists: %r" % dn)
            raise ldap.ALREADY_EXISTS("%s : %r" % (e, dn))
        except ldap.LDAPError as e:
            self.log.error("Could not add backend entry: %r" % dn)
            raise e

        backend_entry = self.conn._test_entry(dn, ldap.SCOPE_BASE)

        return backend_entry

    def getProperties(self, suffix=None, backend_dn=None, bename=None,
                      properties=None):
        raise NotImplemented

    def setProperties(self, suffix=None, backend_dn=None, bename=None,
                      properties=None):
        raise NotImplemented

    def toSuffix(self, entry=None, name=None):
        '''
            Return, for a given backend entry, the suffix values.
            Suffix values are identical from a LDAP point of views.
            Suffix values may be surrounded by ", or containing '\'
            escape characters.

            @param entry - LDAP entry of the backend
            @param name  - backend DN

            @result list of values of suffix attribute (aka 'cn')

            @raise ldap.NO_SUCH_OBJECT - in name is invalid DN
                   ValueError - entry does not contains the suffix attribute
                   InvalidArgumentError - if both entry/name are missing
        '''
        attr_suffix = BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX]
        if entry:
            if not entry.hasValue(attr_suffix):
                raise ValueError("Entry has no %s attribute %r" %
                                 (attr_suffix, entry))
            return entry.getValues(attr_suffix)
        elif name:
            filt = "(objectclass=%s)" % BACKEND_OBJECTCLASS_VALUE

            attrs = [attr_suffix]
            ent = self.conn.getEntry(name, ldap.SCOPE_BASE, filt, attrs)
            self.log.debug("toSuffix: %s found by its DN" % ent.dn)

            if not ent.hasValue(attr_suffix):
                raise ValueError("Entry has no %s attribute %r" %
                                 (attr_suffix, ent))
            return ent.getValues(attr_suffix)
        else:
            raise InvalidArgumentError("entry or name are mandatory")

    def requireIndex(self, suffix):
        '''
        Should be moved in setProperties
        '''
        entries_backend = self.backend.list(suffix=suffix)
        # assume 1 local backend
        dn = entries_backend[0].dn
        replace = [(ldap.MOD_REPLACE, 'nsslapd-require-index', 'on')]
        self.modify_s(dn, replace)


class Backend(DSLdapObject):
    """Backend DSLdapObject with:
    - must attributes = ['cn', 'nsslapd-suffix']
    - RDN attribute is 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    _must_attributes = ['nsslapd-suffix', 'cn']

    def __init__(self, instance, dn=None):
        super(Backend, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['nsslapd-suffix', 'cn']
        self._create_objectclasses = ['top', 'extensibleObject', BACKEND_OBJECTCLASS_VALUE]
        self._protected = False
        self._lint_functions = [self._lint_mappingtree]
        # Check if a mapping tree for this suffix exists.
        self._mts = MappingTrees(self._instance)

    def create_sample_entries(self, version):
        """Creates sample entries under nsslapd-suffix value

        :param version: Sample entries version, i.e. 001003006
        :type version: str
        """

        self._log.debug('Creating sample entries at version %s....' % version)
        # Grab the correct sample entry config
        centries = get_sample_entries(version)
        # apply it.
        basedn = self.get_attr_val('nsslapd-suffix')
        cent = centries(self._instance, basedn)
        cent.apply()

    def _validate(self, rdn, properties, basedn):
        # We always need to call the super validate first. This way we can
        # guarantee that properties is a dictionary.
        # However, backend can take different properties. One is
        # based on the actual key, value of the object
        # one is the "python magic" types.
        # So we actually have to do the super validation later.
        if properties is None:
            raise ldap.UNWILLING_TO_PERFORM('Invalid request to create. Properties cannot be None')
        if type(properties) != dict:
            raise ldap.UNWILLING_TO_PERFORM("properties must be a dictionary")

        # This is converting the BACKEND_ types to the DS nsslapd- attribute values
        nprops = {}
        for key, value in properties.items():
            try:
                nprops[BACKEND_PROPNAME_TO_ATTRNAME[key]] = [value, ]
            except KeyError:
                # This means, it's not a mapped value, so continue
                nprops[key] = value

        (dn, valid_props) = super(Backend, self)._validate(rdn, nprops, basedn)

        try:
            self._mts.get(ensure_str(valid_props['nsslapd-suffix'][0]))
            raise ldap.UNWILLING_TO_PERFORM("Mapping tree for this suffix exists!")
        except ldap.NO_SUCH_OBJECT:
            pass
        try:
            self._mts.get(ensure_str(valid_props['cn'][0]))
            raise ldap.UNWILLING_TO_PERFORM("Mapping tree for this database exists!")
        except ldap.NO_SUCH_OBJECT:
            pass
        # We have to stash our valid props so that mapping tree can use them ...
        self._nprops_stash = valid_props

        return (dn, valid_props)

    def create(self, dn=None, properties=None, basedn=None):
        """Add a new backend entry, create mapping tree,
         and, if requested, sample entries

        :param rdn: RDN of the new entry
        :type rdn: str
        :param properties: Attributes and parameters for the new entry
        :type properties: dict
        :param basedn: Base DN of the new entry
        :type rdn: str

        :returns: DSLdapObject of the created entry
        """

        sample_entries = properties.pop(BACKEND_SAMPLE_ENTRIES, False)
        # Okay, now try to make the backend.
        super(Backend, self).create(dn, properties, basedn)
        # We check if the mapping tree exists in create, so do this *after*
        self._mts.create(properties = {
            'cn': self._nprops_stash['nsslapd-suffix'],
            'nsslapd-state' : 'backend',
            'nsslapd-backend' : self._nprops_stash['cn'] ,
        })
        if sample_entries is not False:
            self.create_sample_entries(sample_entries)
        return self

    def delete(self):
        """Deletes the backend, it's mapping tree and all related indices.
        This can be changed with the self._protected flag!

        :raises: - UnwillingToPerform - if backend is protected
                 - UnwillingToPerform - if nsslapd-state is not 'backend'
        """

        if self._protected:
            raise ldap.UNWILLING_TO_PERFORM("This is a protected backend!")
        # First check if the mapping tree has our suffix still.
        # suffix = self.get_attr_val('nsslapd-suffix')
        bename = self.get_attr_val_utf8('cn')
        try:
            mt = self._mts.get(selector=bename)
            # Assert the type is "backend"
            # Are these the right types....?
            if mt.get_attr_val('nsslapd-state') != ensure_bytes('backend'):
                raise ldap.UNWILLING_TO_PERFORM('Can not delete the mapping tree, not for a backend! You may need to delete this backend via cn=config .... ;_; ')
            # Delete our mapping tree if it exists.
            mt.delete()
        except ldap.NO_SUCH_OBJECT:
            # Righto, it's already gone! Do nothing ...
            pass
        # Delete all our related indices
        self._instance.index.delete_all(bename)

        # Now remove our children, this is all ldbm config
        self._instance.delete_branch_s(self._dn, ldap.SCOPE_ONELEVEL)
        # The super will actually delete ourselves.
        super(Backend, self).delete()

    def _lint_mappingtree(self):
        """Backend lint

        This should check for:
        * missing mapping tree entries for the backend
        * missing indices if we are local and have log access?
        """

        # Check for the missing mapping tree.
        suffix = self.get_attr_val_utf8('nsslapd-suffix')
        bename = self.get_attr_val_bytes('cn')
        try:
            mt = self._mts.get(suffix)
            if mt.get_attr_val_bytes('nsslapd-backend') != bename and mt.get_attr_val('nsslapd-state') != ensure_bytes('backend') :
                raise ldap.NO_SUCH_OBJECT("We have a matching suffix, but not a backend or correct database name.")
        except ldap.NO_SUCH_OBJECT:
            result = DSBLE0001
            result['items'] = [bename, ]
            return result
        return None

    def get_mapping_tree(self):
        suffix = self.get_attr_val_utf8('nsslapd-suffix')
        return self._mts.get(suffix)

    def get_monitor(self):
        """Get a MonitorBackend(DSLdapObject) for the backend"""

        monitor = MonitorBackend(instance=self._instance, dn= "cn=monitor,%s" % self._dn)
        return monitor

    def get_indexes(self):
        """Get an Indexes(DSLdapObject) for the backend"""

        indexes = Indexes(self._instance, basedn="cn=index,%s" % self._dn)
        return indexes
    # Future: add reindex task for this be.


class Backends(DSLdapObjects):
    """DSLdapObjects that represents DN_LDBM base DN
    This only does ldbm backends. Chaining backends are a special case
    of this, so they can be subclassed off.

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance, basedn=None):
        # Basedn has to be here, despite not being used to satisfy
        # cli_base _generic_create.
        super(Backends, self).__init__(instance=instance)
        self._objectclasses = [BACKEND_OBJECTCLASS_VALUE]
        self._filterattrs = ['cn', 'nsslapd-suffix', 'nsslapd-directory']
        self._childobject = Backend
        self._basedn = DN_LDBM
