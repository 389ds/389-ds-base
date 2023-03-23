# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from datetime import datetime
import os
import copy
import ldap
from lib389._constants import DN_LDBM, DN_CHAIN, DN_PLUGIN, DEFAULT_BENAME
from lib389.properties import BACKEND_OBJECTCLASS_VALUE, BACKEND_PROPNAME_TO_ATTRNAME, BACKEND_CHAIN_BIND_DN, \
                              BACKEND_CHAIN_BIND_PW, BACKEND_CHAIN_URLS, BACKEND_PROPNAME_TO_ATTRNAME, BACKEND_NAME, \
                              BACKEND_SUFFIX, BACKEND_SAMPLE_ENTRIES, TASK_WAIT
from lib389.utils import normalizeDN, ensure_str, assert_c
from lib389 import Entry

# Need to fix this ....

from lib389._mapped_object import DSLdapObjects, DSLdapObject, CompositeDSLdapObject
from lib389.mappingTree import MappingTrees
from lib389.exceptions import NoSuchEntryError, InvalidArgumentError
from lib389.replica import Replicas, Changelog
from lib389.cos import (CosTemplates, CosIndirectDefinitions,
                        CosPointerDefinitions, CosClassicDefinitions)

from lib389.index import Index, Indexes, VLVSearches, VLVSearch
from lib389.tasks import ImportTask, ExportTask, Tasks
from lib389.encrypted_attributes import EncryptedAttr, EncryptedAttrs


# This is for sample entry creation.
from lib389.configurations import get_sample_entries

from lib389.lint import DSBLE0001, DSBLE0002, DSBLE0003, DSVIRTLE0001, DSCLLE0001


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
            self.log.info("List backend %s", backend_dn)
            base = backend_dn
            scope = ldap.SCOPE_BASE
        elif suffix:
            self.log.info("List backend with suffix=%s", suffix)
            base = DN_PLUGIN
            scope = ldap.SCOPE_SUBTREE
            filt = ("(&%s(|(%s=%s)(%s=%s)))" %
                    (filt,
                     BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX], suffix,
                     BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX],
                     normalizeDN(suffix))
                    )
        elif bename:
            self.log.info("List backend 'cn=%s'", bename)
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
                self.log.fatal("Multiple backend match the definition: %s",
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
            self.log.debug("Delete entry children %s", ent.dn)
            self.conn.delete_s(ent.dn)

        self.log.debug("Delete backend entry %s", be_ent.dn)
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
                self.log.debug("_getBackendName: baser=%s : fileter=%s",
                               base, filt)
                try:
                    self.conn.getEntry(base, ldap.SCOPE_BASE, filt)
                except (NoSuchEntryError, ldap.NO_SUCH_OBJECT):
                    self.log.info("backend name will be %s", bename)
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
            self.log.info("Checking suffix %s for existence", suffix)
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

            self.log.debug("adding entry: %r", entry)
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS as e:
            self.log.error("Entry already exists: %r", dn)
            raise ldap.ALREADY_EXISTS("%s : %r" % (e, dn))
        except ldap.LDAPError as e:
            self.log.error("Could not add backend entry: %r", dn)
            raise e

        backend_entry = self.conn._test_entry(dn, ldap.SCOPE_BASE)

        return backend_entry

    def getProperties(self, suffix=None, backend_dn=None, bename=None,
                      properties=None):
        raise NotImplementedError

    def setProperties(self, suffix=None, backend_dn=None, bename=None,
                      properties=None):
        raise NotImplementedError

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
            self.log.debug("toSuffix: %s found by its DN", ent.dn)

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

    @classmethod
    def lint_uid(cls):
        return 'backends'


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
        # Check if a mapping tree for this suffix exists.
        self._mts = MappingTrees(self._instance)

    def lint_uid(self):
        return self.get_attr_val_utf8_l('cn').lower()

    def _lint_virt_attrs(self):
        """Check if any virtual attribute are incorrectly indexed"""
        bename = self.lint_uid()
        indexes = self.get_indexes()
        suffix = self.get_attr_val_utf8('nsslapd-suffix')
        # First check nsrole
        try:
            indexes.get('nsrole')
            report = copy.deepcopy(DSVIRTLE0001)
            report['check'] = f'backends:{bename}:virt_attrs'
            report['detail'] = report['detail'].replace('ATTR', 'nsrole')
            report['fix'] = report['fix'].replace('ATTR', 'nsrole')
            report['fix'] = report['fix'].replace('SUFFIX', suffix)
            report['fix'] = report['fix'].replace('YOUR_INSTANCE', self._instance.serverid)
            report['items'].append(suffix)
            report['items'].append('nsrole')
            yield report
        except:
            pass

        # Check COS next
        for cosDefType in [CosIndirectDefinitions, CosPointerDefinitions, CosClassicDefinitions]:
            defs = cosDefType(self._instance, suffix).list()
            for cosDef in defs:
                attrs = cosDef.get_attr_val_utf8_l("cosAttribute").split()
                for attr in attrs:
                    if attr in ["default", "override", "operational", "operational-default", "merge-schemes"]:
                        # We are at the end, just break out
                        break
                    try:
                        indexes.get(attr)
                        # If we got here there is an index (bad)
                        report = copy.deepcopy(DSVIRTLE0001)
                        report['check'] = f'backends:{bename}:virt_attrs'
                        report['detail'] = report['detail'].replace('ATTR', attr)
                        report['fix'] = report['fix'].replace('ATTR', attr)
                        report['fix'] = report['fix'].replace('SUFFIX', suffix)
                        report['fix'] = report['fix'].replace('YOUR_INSTANCE', self._instance.serverid)
                        report['items'].append(suffix)
                        report['items'].append("Class Of Service (COS)")
                        report['items'].append("cosAttribute: " + attr)
                        yield report
                    except:
                        # this is what we hope for
                        pass

    def _lint_search(self):
        """Perform a search and make sure an entry is accessible
        """
        dn = self.get_attr_val_utf8('nsslapd-suffix')
        bename = self.lint_uid()
        suffix = DSLdapObject(self._instance, dn=dn)
        try:
            suffix.get_attr_val('objectclass')
        except ldap.NO_SUCH_OBJECT:
            # backend root entry not created yet
            DSBLE0003['items'] = [dn, ]
            DSBLE0003['check'] = f'backends:{bename}:search'
            yield DSBLE0003
        except ldap.LDAPError as e:
            # Some other error
            DSBLE0002['detail'] = DSBLE0002['detail'].replace('ERROR', str(e))
            DSBLE0002['check'] = f'backends:{bename}:search'
            DSBLE0002['items'] = [dn, ]
            yield DSBLE0002

    def _lint_mappingtree(self):
        """Backend lint

        This should check for:
        * missing mapping tree entries for the backend
        * missing indices if we are local and have log access?
        """
        # Check for the missing mapping tree.
        suffix = self.get_attr_val_utf8('nsslapd-suffix')
        bename = self.lint_uid()
        try:
            mt = self._mts.get(suffix)
            if mt.get_attr_val_utf8('nsslapd-backend') != bename and mt.get_attr_val_utf8('nsslapd-state') != 'backend':
                raise ldap.NO_SUCH_OBJECT("We have a matching suffix, but not a backend or correct database name.")
        except ldap.NO_SUCH_OBJECT:
            result = DSBLE0001
            result['check'] = f'backends:{bename}:mappingtree'
            result['items'] = [bename, ]
            yield result

    def _lint_cl_trimming(self):
        """Check that cl trimming is at least defined to prevent unbounded growth"""
        bename = self.lint_uid()
        suffix = self.get_attr_val_utf8('nsslapd-suffix')
        replicas = Replicas(self._instance)
        try:
            # Check if replication is enabled
            replicas.get(suffix)
            # Check the changelog
            cl = Changelog(self._instance, suffix=suffix)
            if cl.get_attr_val_utf8('nsslapd-changelogmaxentries') is None and \
               cl.get_attr_val_utf8('nsslapd-changelogmaxage') is None:
                report = copy.deepcopy(DSCLLE0001)
                report['fix'] = report['fix'].replace('YOUR_INSTANCE', self._instance.serverid)
                report['check'] = f'backends:{bename}::cl_trimming'
                yield report
        except:
            # Suffix is not replicated
            self._log.debug(f"_lint_cl_trimming - backend ({suffix}) is not replicated")
            pass

    def create_sample_entries(self, version):
        """Creates sample entries under nsslapd-suffix value

        :param version: Sample entries version, i.e. 001003006
        :type version: str
        """

        self._log.debug('Requested sample entries at version %s....' % version)
        # Grab the correct sample entry config - remember this is a function ptr.
        centries = get_sample_entries(version)
        # apply it.
        basedn = self.get_attr_val('nsslapd-suffix')
        cent = centries(self._instance, basedn)
        # Now it's built, we can get the version for logging.
        self._log.debug('Creating sample entries at version %s' % cent.version)
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

    def create(self, dn=None, properties=None, basedn=DN_LDBM, create_mapping_tree=True):
        """Add a new backend entry, create mapping tree,
         and, if requested, sample entries

        :param dn: DN of the new entry
        :type dn: str
        :param properties: Attributes and parameters for the new entry
        :type properties: dict
        :param basedn: Base DN of the new entry
        :type basedn: str
        :param create_mapping_tree: If a related mapping tree node should be created
        :type create_mapping_tree: bool

        :returns: DSLdapObject of the created entry
        """

        sample_entries = False
        parent_suffix = False

        # normalize suffix (remove spaces between comps)
        if dn is not None:
            dn_comps = ldap.dn.explode_dn(dn.lower())
            dn = ",".join(dn_comps)

        if properties is not None:
            dn_comps = ldap.dn.explode_dn(properties['nsslapd-suffix'])
            ndn = ",".join(dn_comps)
            properties['nsslapd-suffix'] = ndn
            sample_entries = properties.pop(BACKEND_SAMPLE_ENTRIES, False)
            parent_suffix = properties.pop('parent', False)

        # Okay, now try to make the backend.
        super(Backend, self).create(dn, properties, basedn)

        # We check if the mapping tree exists in create, so do this *after*
        if create_mapping_tree is True:
            properties = {
                'cn': self._nprops_stash['nsslapd-suffix'],
                'nsslapd-state': 'backend',
                'nsslapd-backend': self._nprops_stash['cn'],
            }
            if parent_suffix:
                # This is a subsuffix, set the parent suffix
                properties['nsslapd-parent-suffix'] = parent_suffix
            self._mts.create(properties=properties)

        # We can't create the sample entries unless a mapping tree was installed.
        if sample_entries is not False and create_mapping_tree is True:
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
            if mt.get_attr_val_utf8_l('nsslapd-state') != 'backend':
                raise ldap.UNWILLING_TO_PERFORM('Can not delete the mapping tree, not for a backend! You may need to delete this backend via cn=config .... ;_; ')

            # Delete replicas first
            try:
                Replicas(self._instance).get(mt.get_attr_val_utf8('cn')).delete()
            except ldap.NO_SUCH_OBJECT:
                # No replica, no problem
                pass

            # Delete our mapping tree if it exists.
            mt.delete()
        except ldap.NO_SUCH_OBJECT:
            # Righto, it's already gone! Do nothing ...
            pass

        # Now remove our children, this is all ldbm config
        self._instance.delete_branch_s(self._dn, ldap.SCOPE_SUBTREE)

    def get_suffix(self):
        return self.get_attr_val_utf8_l('nsslapd-suffix')

    def disable(self):
        # Disable backend (mapping tree)
        suffix = self.get_attr_val_utf8_l('nsslapd-suffix')
        mt = self._mts.get(suffix)
        mt.set('nsslapd-nsstate', 'Disabled')

    def enable(self):
        # Enable Backend (mapping tree)
        suffix = self.get_attr_val_utf8_l('nsslapd-suffix')
        mt = self._mts.get(suffix)
        mt.set('nsslapd-nsstate', 'Backend')

    def get_mapping_tree(self):
        suffix = self.get_attr_val_utf8('nsslapd-suffix')
        return self._mts.get(suffix)

    def get_monitor(self):
        """Get a MonitorBackend(DSLdapObject) for the backend"""
        # We need to be a factor to the backend monitor
        from lib389.monitor import MonitorBackend

        monitor = MonitorBackend(instance=self._instance, dn="cn=monitor,%s" % self._dn)
        return monitor

    def get_indexes(self):
        """Get an Indexes(DSLdapObject) for the backend"""

        indexes = Indexes(self._instance, basedn="cn=index,%s" % self._dn)
        return indexes

    def get_index(self, attr_name):
        for index in self.get_indexes().list():
            idx_name = index.get_attr_val_utf8_l('cn')
            if idx_name == attr_name.lower():
                return index
        return None

    def del_index(self, attr_name):
        for index in self.get_indexes().list():
            idx_name = index.get_attr_val_utf8_l('cn')
            if idx_name == attr_name.lower():
                index.delete()
                return
        raise ValueError("Can not delete index because it does not exist")

    def add_index(self, attr_name, types, matching_rules=None, reindex=False):
        """ Add an index.

        :param attr_name - name of the attribute to index
        :param types - a List of index types(eq, pres, sub, approx)
        :param matching_rules - a List of matching rules for the index
        :param reindex - If set to True then index the attribute after creating it.
        """

        # Reject adding an index for a virtual attribute
        virt_attr_list = ['nsrole']
        for cosDefType in [CosIndirectDefinitions, CosPointerDefinitions, CosClassicDefinitions]:
            defs = cosDefType(self._instance, self.get_suffix()).list()
            for cosDef in defs:
                attrs = cosDef.get_attr_val_utf8_l("cosAttribute").split()
                for attr in attrs:
                    if attr in ["default", "override", "operational", "operational-default", "merge-schemes"]:
                        # We are at the end, just break out
                        break
                    virt_attr_list.append(attr)
        if attr_name.lower() in virt_attr_list:
            raise ValueError(f"You should not index a virtual attribute ({attr_name})")

        new_index = Index(self._instance)
        props = {'cn': attr_name,
                 'nsSystemIndex': 'False',
                 'nsIndexType': types,
                 }
        if matching_rules is not None:
            mrs = []
            for mr in matching_rules:
                mrs.append(mr)
            # Only add if there are actually rules present in the list.
            if len(mrs) > 0:
                props['nsMatchingRule'] = mrs
        new_index.create(properties=props, basedn="cn=index," + self._dn)

        if reindex:
            self.reindex(attr_name)

    def reindex(self, attrs=None, wait=False):
        """Reindex the attributes for this backend
        :param attrs - an optional list of attributes to index
        :param wait - Set to true to wait for task to complete
        """
        args = None
        if wait:
            args = {TASK_WAIT: True}
        bename = self.get_attr_val_utf8('cn')
        reindex_task = Tasks(self._instance)
        reindex_task.reindex(benamebase=bename, attrname=attrs, args=args)

    def get_encrypted_attrs(self, just_names=False):
        """Get a list of the excrypted attributes
        :param just_names - If True only the encrypted attribute names are returned (instead of the full attribute entry)
        :returns - a list of attributes
        """
        attrs = EncryptedAttrs(self._instance, basedn=self._dn).list()
        if just_names:
            results = []
            for attr in attrs:
                results.append(attr.get_attr_val_utf8_l('cn'))
            return results
        else:
            return attrs

    def add_encrypted_attr(self, attr_name):
        """Add an encrypted attribute
        :param attr_name - name of the new encrypted attribute
        """
        new_attr = EncryptedAttr(self._instance)
        new_attr.create(basedn="cn=encrypted attributes," + self._dn, properties={'cn': attr_name,'nsEncryptionAlgorithm': 'AES'})

    def del_encrypted_attr(self, attr_name):
        """Delete encrypted attribute
        :param attr_name - Name of the encrypted attribute to delete
        """
        enc_attrs = EncryptedAttrs(self._instance, basedn="cn=encrypted attributes," + self._dn).list()
        for enc_attr in enc_attrs:
            attr = enc_attr.get_attr_val_utf8_l('cn').lower()
            if attr_name == attr.lower():
                enc_attr.delete()
                break

    def import_ldif(self, ldifs, chunk_size=None, encrypted=False, gen_uniq_id=None, only_core=False,
                    include_suffixes=None, exclude_suffixes=None):
        """Do an import of the suffix"""

        bs = Backends(self._instance)
        task = bs.import_ldif(self.rdn, ldifs, chunk_size, encrypted, gen_uniq_id, only_core,
                              include_suffixes, exclude_suffixes)
        return task

    def export_ldif(self, ldif=None, use_id2entry=False, encrypted=False, min_base64=False, no_uniq_id=False,
                    replication=False, not_folded=False, no_seq_num=False, include_suffixes=None, exclude_suffixes=None):
        """Do an export of the suffix"""

        bs = Backends(self._instance)
        task = bs.export_ldif(self.rdn, ldif, use_id2entry, encrypted, min_base64, no_uniq_id,
                              replication, not_folded, no_seq_num, include_suffixes, exclude_suffixes)
        return task

    def get_vlv_searches(self, vlv_name=None):
        """Return the VLV seaches for this backend, or return a specific search
        :param vlv_name - name of a VLV search entry to return
        :returns - A list of VLV searches or a single VLV sarch entry
        """
        vlv_searches = VLVSearches(self._instance, basedn=self._dn).list()
        if vlv_name is None:
            return vlv_searches

        # return specific search
        for vlv in vlv_searches:
            search_name = vlv.get_attr_val_utf8_l('cn')
            if search_name == vlv_name.lower():
                return vlv

        # No match
        raise ValueError("Failed to find VLV search entry")

    def add_vlv_search(self, vlvname, props, reindex=False):
        """Add a VLV search entry
        :param: vlvname - Name of the new VLV search entry
        :props - A dict of the attribute value pairs for the VLV search entry
        :param - reindex - Set to True to index the new attribute right away
        """
        basedn = self._dn
        vlv = VLVSearch(instance=self._instance)
        vlv.create(rdn="cn=" + vlvname, properties=props, basedn=basedn)

    def get_sub_suffixes(self):
        """Return a list of Backend's
        returns: a List of subsuffix entries
        """
        subsuffixes = []
        top_be_suffix = self.get_attr_val_utf8_l('nsslapd-suffix')
        mts = self._mts.list()
        for mt in mts:
            parent_suffix = mt.get_attr_val_utf8_l('nsslapd-parent-suffix')
            if parent_suffix is None:
                continue
            if parent_suffix == top_be_suffix:
                child_suffix = mt.get_attr_val_utf8_l('cn')
                be_insts = Backends(self._instance).list()
                for be in be_insts:
                    be_suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
                    if child_suffix == be_suffix:
                        subsuffixes.append(be)
                        break
        return subsuffixes

    def get_cos_indirect_defs(self):
        return CosIndirectDefinitions(self._instance, self._dn).list()

    def get_cos_pointer_defs(self):
        return CosPointerDefinitions(self._instance, self._dn).list()

    def get_cos_classic_defs(self):
        return CosClassicDefinitions(self._instance, self._dn).list()

    def get_cos_templates(self):
        return CosTemplates(self._instance, self._dn).list()

    def get_state(self):
        suffix = self.get_attr_val_utf8('nsslapd-suffix')
        try:
            mt = self._mts.get(suffix)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError("Backend missing mapping tree entry, unable to get state")
        return mt.get_attr_val_utf8('nsslapd-state')

    def set_state(self, new_state):
        new_state = new_state.lower()
        suffix = self.get_attr_val_utf8('nsslapd-suffix')
        try:
            mt = self._mts.get(suffix)
        except ldap.NO_SUCH_OBJECT:
            raise ValueError("Backend missing mapping tree entry, unable to set configuration")

        if new_state not in ['backend', 'disabled',  'referral',  'referral on update']:
            raise ValueError(f"Invalid backend state {new_state}, value must be one of the following: 'backend', 'disabled',  'referral',  'referral on update'")

        # Can not change state of replicated backend
        replicas = Replicas(self._instance)
        try:
            # Check if replication is enabled
            replicas.get(suffix)
            raise ValueError("Can not change the backend state of a replicated suffix")
        except ldap.NO_SUCH_OBJECT:
            pass

        # Ok, change the state
        mt.replace('nsslapd-state', new_state)


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

    @classmethod
    def lint_uid(cls):
        return 'backends'

    def import_ldif(self, be_name, ldifs, chunk_size=None, encrypted=False, gen_uniq_id=None, only_core=False,
                    include_suffixes=None, exclude_suffixes=None):
        """Do an import of the suffix"""

        if not ldifs:
            raise ValueError("import_ldif: LDIF filename is missing")
        ldif_paths = []
        for ldif in list(ldifs):
            if not ldif.startswith("/"):
                if ldif.endswith(".ldif"):
                    ldif = os.path.join(self._instance.ds_paths.ldif_dir, ldif)
                else:
                    ldif = os.path.join(self._instance.ds_paths.ldif_dir, "%s.ldif" % ldif)
            ldif_paths.append(ldif)

        task = ImportTask(self._instance)
        task_properties = {'nsInstance': be_name,
                           'nsFilename': ldif_paths}
        if include_suffixes is not None:
            task_properties['nsIncludeSuffix'] = include_suffixes
        if exclude_suffixes is not None:
            task_properties['nsExcludeSuffix'] = exclude_suffixes
        if encrypted:
            task_properties['nsExportDecrypt'] = 'true'
        if only_core:
            task_properties['nsImportIndexAttrs'] = 'false'
        if chunk_size is not None:
            task_properties['nsImportChunkSize'] = chunk_size
        if gen_uniq_id is not None:
            if gen_uniq_id in ("none", "empty") or gen_uniq_id.startswith("deterministic"):
                raise ValueError("'gen_uniq_id should be none (no unique ID) |"
                                 "empty (time-based ID) | deterministic namespace (name-based ID)")
            task_properties['nsUniqueIdGenerator'] = gen_uniq_id

        task.create(properties=task_properties)

        return task

    def export_ldif(self, be_names, ldif=None, use_id2entry=False, encrypted=False, min_base64=False, no_dump_uniq_id=False,
                    replication=False, not_folded=False, no_seq_num=False, include_suffixes=None, exclude_suffixes=None):
        """Do an export of the suffix"""

        task = ExportTask(self._instance)
        task_properties = {'nsInstance': be_names}
        if ldif == "":
            ldif = None
        if ldif is not None and not ldif.startswith("/"):
            if ldif.endswith(".ldif"):
                task_properties['nsFilename'] = os.path.join(self._instance.ds_paths.ldif_dir, ldif)
            else:
                task_properties['nsFilename'] = os.path.join(self._instance.ds_paths.ldif_dir, "%s.ldif" % ldif)
        elif ldif is not None and ldif.startswith("/"):
            if ldif.endswith(".ldif"):
                task_properties['nsFilename'] = ldif
            else:
                task_properties['nsFilename'] = "%s.ldif" % ldif
        else:
            tnow = datetime.now().strftime("%Y_%m_%d_%H_%M_%S")
            task_properties['nsFilename'] = os.path.join(self._instance.ds_paths.ldif_dir,
                                                         "%s-%s-%s.ldif" % (self._instance.serverid,
                                                                            "-".join(be_names), tnow))
        if include_suffixes is not None:
            task_properties['nsIncludeSuffix'] = include_suffixes
        if exclude_suffixes is not None:
            task_properties['nsExcludeSuffix'] = exclude_suffixes
        if use_id2entry:
            task_properties['nsUseId2Entry'] = 'true'
        if encrypted:
            task_properties['nsExportDecrypt'] = 'true'
        if replication:
            task_properties['nsExportReplica'] = 'true'
        if min_base64:
            task_properties['nsMinimalEncoding'] = 'true'
        if not_folded:
            task_properties['nsNoWrap'] = 'true'
        if no_dump_uniq_id:
            task_properties['nsDumpUniqId'] = 'false'
        if no_seq_num:
            task_properties['nsPrintKey'] = 'false'

        task = task.create(properties=task_properties)
        return task

    def delete_all_dangerous(self):
        """
        Delete all backends. This deletes from longest to shortest suffix
        to ensure correct delete ordering.
        """
        for be in sorted(self.list(), key=lambda be: len(be.get_suffix()), reverse=True):
            be.delete()


class DatabaseConfig(DSLdapObject):
    """Backend Database configuration

    The entire database configuration consists of the  main global configuration entry,
    and the underlying DB library configuration: whither BDB or LMDB.  The combined
    configuration should be presented as a single entity so the end user does not need
    to worry about what library is being used, and just focus on the configuration.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn="cn=config,cn=ldbm database,cn=plugins,cn=config"):
        super(DatabaseConfig, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._global_attrs = [
            'nsslapd-lookthroughlimit',
            'nsslapd-mode',
            'nsslapd-idlistscanlimit',
            'nsslapd-directory',
            'nsslapd-import-cachesize',
            'nsslapd-idl-switch',
            'nsslapd-search-bypass-filter-test',
            'nsslapd-search-use-vlv-index',
            'nsslapd-exclude-from-export',
            'nsslapd-serial-lock',
            'nsslapd-subtree-rename-switch',
            'nsslapd-pagedlookthroughlimit',
            'nsslapd-pagedidlistscanlimit',
            'nsslapd-rangelookthroughlimit',
            'nsslapd-backend-opt-level',
            'nsslapd-backend-implement',
            'nsslapd-db-durable-transaction',
            'nsslapd-search-bypass-filter-test',
            'nsslapd-serial-lock',
        ]
        self._db_attrs = {
            'bdb':
                [
                    'nsslapd-dbcachesize',
                    'nsslapd-db-logdirectory',
                    'nsslapd-db-home-directory',
                    'nsslapd-db-transaction-wait',
                    'nsslapd-db-checkpoint-interval',
                    'nsslapd-db-compactdb-interval',
                    'nsslapd-db-compactdb-time',
                    'nsslapd-db-page-size',
                    'nsslapd-db-transaction-batch-val',
                    'nsslapd-db-transaction-batch-min-wait',
                    'nsslapd-db-transaction-batch-max-wait',
                    'nsslapd-db-logbuf-size',
                    'nsslapd-db-locks',
                    'nsslapd-db-locks-monitoring-enabled',
                    'nsslapd-db-locks-monitoring-threshold',
                    'nsslapd-db-locks-monitoring-pause',
                    'nsslapd-db-private-import-mem',
                    'nsslapd-import-cache-autosize',
                    'nsslapd-cache-autosize',
                    'nsslapd-cache-autosize-split',
                    'nsslapd-import-cachesize',
                    'nsslapd-db-deadlock-policy',
                ],
            'mdb': [
                    'nsslapd-mdb-max-size',
                    'nsslapd-mdb-max-readers',
                    'nsslapd-mdb-max-dbs',
                ]
        }
        self._create_objectclasses = ['top', 'extensibleObject']
        self._protected = True
        # This could be "bdb" or "mdb", use what we have configured in the global config
        self._db_lib = self.get_attr_val_utf8_l('nsslapd-backend-implement')
        self._dn = "cn=config,cn=ldbm database,cn=plugins,cn=config"
        self._db_dn = f"cn={self._db_lib},cn=config,cn=ldbm database,cn=plugins,cn=config"
        self._globalObj = DSLdapObject(self._instance, dn=self._dn)
        self._dbObj = DSLdapObject(self._instance, dn=self._db_dn)
        # Assert there is no overlap in different config sets
        assert_c(len(set(self._global_attrs).intersection(set(self._db_attrs['bdb']), set(self._db_attrs['mdb']))) == 0)

    def get(self):
        """Get the combined config entries"""
        # Get and combine both sets of attributes
        global_attrs = self._globalObj.get_attrs_vals_utf8(self._global_attrs)
        db_attrs = self._dbObj.get_attrs_vals_utf8(self._db_attrs[self._db_lib])
        combined_attrs = {**global_attrs, **db_attrs}
        return combined_attrs

    def display(self):
        """Display the combined configuration"""
        global_attrs = self._globalObj.get_attrs_vals_utf8(self._global_attrs)
        db_attrs = self._dbObj.get_attrs_vals_utf8(self._db_attrs[self._db_lib])
        combined_attrs = {**global_attrs, **db_attrs}
        for (k, vo) in combined_attrs.items():
            if len(vo) == 0:
                vo = ""
            else:
                vo = vo[0]
            self._instance.log.info(f'{k}: {vo}')

    def get_db_lib(self):
        """Return the backend library, bdb, mdb, etc"""
        return self._db_lib

    def set(self, value_pairs):
        for attr, val in value_pairs:
            attr = attr.lower()
            if attr in self._global_attrs:
                global_config = DSLdapObject(self._instance, dn=self._dn)
                global_config.replace(attr, val)
            elif attr in self._db_attrs['bdb']:
                db_config = DSLdapObject(self._instance, dn=self._db_dn)
                db_config.replace(attr, val)
            elif attr in self._db_attrs['mdb']:
                db_config = DSLdapObject(self._instance, dn=self._db_dn)
                db_config.replace(attr, val)
            else:
                # Unknown attribute
                raise ValueError("Can not update database configuration with unknown attribute: " + attr)


class BackendSuffixView(CompositeDSLdapObject):
    """ Composite view between backend and mapping tree entries
        used by: dsconf instance backend suffix ...
    """

    def __init__(self, instance, be):
        super(BackendSuffixView, self).__init__(instance, be.dn)
        be_args = [
            'nsslapd-cachememsize',
            'nsslapd-cachesize',
            'nsslapd-dncachememsize',
            'nsslapd-readonly',
            'nsslapd-require-index',
            'nsslapd-suffix'
        ]
        mt_args = [
            'orphan',
            'nsslapd-state',
            'nsslapd-referral',
        ]
        mt = be._mts.get(be.get_suffix())
        self.add_component(be, be_args)
        self.add_component(mt, mt_args)

    def get_state(self):
        return self.get_attr_val_utf8('nsslapd-state')

    def set_state(self, new_state):
        new_state = new_state.lower()
        suffix = self.get_attr_val_utf8('nsslapd-suffix')

        if new_state not in ['backend', 'disabled',  'referral',  'referral on update']:
            raise ValueError(f"Invalid backend state {new_state}, value must be one of the following: 'backend', 'disabled',  'referral',  'referral on update'")

        # Can not change state of replicated backend
        replicas = Replicas(self._instance)
        try:
            # Check if replication is enabled
            replicas.get(suffix)
            raise ValueError("Can not change the backend state of a replicated suffix")
        except ldap.NO_SUCH_OBJECT:
            pass

        # Ok, change the state
        self.set('nsslapd-state', new_state)
