# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from ldap.dn import str2dn, dn2str
import six
from lib389._constants import *
from lib389.properties import *
from lib389.utils import suffixfilt, normalizeDN
from lib389 import Entry
from lib389.exceptions import NoSuchEntryError, InvalidArgumentError


from lib389._mapped_object import DSLdapObjects, DSLdapObject


class MappingTreeLegacy(object):
    '''
    classdocs
    '''

    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """
        @param conn - a DirSrv instance
        """
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in MappingTree.proxied_methods:
            from lib389 import DirSrv
            return DirSrv.__getattr__(self.conn, name)

    def list(self, suffix=None, bename=None):
        '''
            Returns a search result of the mapping tree entries with all their
            attributes

            If 'suffix'/'bename' are specified. It uses 'benamebase' first,
            then 'suffix'.

            If neither 'suffix' and 'bename' are specified, it returns all
            the mapping tree entries

            @param suffix - suffix of the backend
            @param benamebase - backend common name (e.g. 'userRoot')

            @return mapping tree entries

            @raise if search fails

        '''
        if bename:
            filt = "(%s=%s)" % (MT_PROPNAME_TO_ATTRNAME[MT_BACKEND], bename)
        elif suffix:
            filt = "(%s=%s)" % (MT_PROPNAME_TO_ATTRNAME[MT_SUFFIX], suffix)
        else:
            filt = "(objectclass=%s)" % MT_OBJECTCLASS_VALUE

        try:
            ents = self.conn.search_s(DN_MAPPING_TREE, ldap.SCOPE_ONELEVEL,
                                      filt)
            for ent in ents:
                self.log.debug('list: %r', ent)
        except:
            raise

        return ents

    def create(self, suffix=None, bename=None, parent=None):
        '''
            Create a mapping tree entry (under "cn=mapping tree,cn=config"),
            for the 'suffix' and that is stored in 'bename' backend.
            'bename' backend must exist before creating the mapping tree entry.

            If a 'parent' is provided that means that we are creating a
            sub-suffix mapping tree.

            @param suffix - suffix mapped by this mapping tree entry. It will
                            be the common name ('cn') of the entry
            @param benamebase - backend common name (e.g. 'userRoot')
            @param parent - if provided is a parent suffix of 'suffix'

            @return DN of the mapping tree entry

            @raise ldap.NO_SUCH_OBJECT - if the backend entry or parent mapping
                                         tree does not exist
                   ValueError - if missing a parameter,

        '''
        # Check suffix is provided
        if not suffix:
            raise ValueError("suffix is mandatory")
        else:
            nsuffix = normalizeDN(suffix)

        # Check backend name is provided
        if not bename:
            raise ValueError("backend name is mandatory")

        # Check that if the parent suffix is provided then
        # it exists a mapping tree for it
        if parent:
            nparent = normalizeDN(parent)
            filt = suffixfilt(parent)
            try:
                entry = self.conn.getEntry(DN_MAPPING_TREE, ldap.SCOPE_SUBTREE,
                                           filt)
                pass
            except NoSuchEntryError:
                raise ValueError("parent suffix has no mapping tree")
        else:
            nparent = ""

        # Check if suffix exists, return
        filt = suffixfilt(suffix)
        try:
            entry = self.conn.getEntry(DN_MAPPING_TREE, ldap.SCOPE_SUBTREE,
                                       filt)
            return entry
        except ldap.NO_SUCH_OBJECT:
            entry = None

        #
        # Now start the real work
        #

        # fix me when we can actually used escaped DNs
        dn = ','.join(('cn="%s"' % nsuffix, DN_MAPPING_TREE))
        entry = Entry(dn)
        entry.update({
            'objectclass': ['top', 'extensibleObject', MT_OBJECTCLASS_VALUE],
            'nsslapd-state': 'backend',
            # the value in the dn has to be DN escaped
            # internal code will add the quoted value - unquoted value is
            # useful for searching.
            MT_PROPNAME_TO_ATTRNAME[MT_SUFFIX]: nsuffix,
            MT_PROPNAME_TO_ATTRNAME[MT_BACKEND]: bename
        })

        # possibly add the parent
        if parent:
            entry.setValues(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX], nparent)

        try:
            self.log.debug("Creating entry: %s", entry.dn)
            self.log.info("Entry %r", entry)
            self.conn.add_s(entry)
        except ldap.LDAPError as e:
            raise ldap.LDAPError("Error adding suffix entry " + dn, e)

        ret = self.conn._test_entry(dn, ldap.SCOPE_BASE)
        return ret

    def delete(self, suffix=None, bename=None, name=None):
        '''
            Delete a mapping tree entry (under "cn=mapping tree,cn=config"),
            for the 'suffix' and that is stored in 'benamebase' backend.
            'benamebase' backend is not changed by the mapping tree deletion.

            If 'name' is specified. It uses it to retrieve the mapping tree
            to delete.  Else if 'suffix'/'benamebase' are specified. It uses
            both to retrieve the mapping tree to delete

            @param suffix - suffix mapped by this mapping tree entry. It is
            the common name ('cn') of the entry
            @param benamebase - backend common name (e.g. 'userRoot')
            @param name - DN of the mapping tree entry

            @return None

            @raise ldap.NO_SUCH_OBJECT - the entry is not found
                                         KeyError if 'name', 'suffix' and
                                         'benamebase' are missing
                   UnwillingToPerformError - If the mapping tree has
                                             subordinates
        '''
        if name:
            filt = "(objectclass=%s)" % MT_OBJECTCLASS_VALUE
            try:
                ent = self.conn.getEntry(name, ldap.SCOPE_BASE, filt)
                self.log.debug("delete: %s found by its DN", ent.dn)
            except NoSuchEntryError:
                raise ldap.NO_SUCH_OBJECT("mapping tree DN not found: %s" %
                                          name)
        else:
            filt = None

            if suffix:
                filt = suffixfilt(suffix)

            if bename:
                if filt:
                    filt = ("(&(%s=%s)%s)" %
                            (MT_PROPNAME_TO_ATTRNAME[MT_BACKEND],
                             bename,
                             filt))
                else:
                    filt = ("(%s=%s)" %
                            (MT_PROPNAME_TO_ATTRNAME[MT_BACKEND], bename))

            try:
                ent = self.conn.getEntry(DN_MAPPING_TREE, ldap.SCOPE_ONELEVEL,
                                         filt)
                self.log.debug("delete: %s found by with %s", ent.dn, filt)
            except NoSuchEntryError:
                raise ldap.NO_SUCH_OBJECT("mapping tree DN not found: %s" %
                                          name)

        #
        # At this point 'ent' contains the mapping tree entry to delete
        #

        # First Check there is no child (replica, replica agreements)
        try:
            ents = self.conn.search_s(ent.dn, ldap.SCOPE_SUBTREE,
                                      "objectclass=*")
        except:
            raise
        if len(ents) != 1:
            for entry in ents:
                self.log.warning("Error: it exists %s under %s",
                                 entry.dn, ent.dn)
            raise ldap.UNWILLING_TO_PERFORM(
                "Unable to delete %s, it is not a leaf" % ent.dn)
        else:
            for entry in ents:
                self.log.warning("Warning: %s (%s)", entry.dn, ent.dn)
            self.conn.delete_s(ent.dn)

    def getProperties(self, suffix=None, bename=None, name=None,
                      properties=None):
        '''
            Returns a dictionary of the requested properties.
            If properties is missing, it returns all the properties.

            The returned properties are those of the 'suffix' and that is
            stored in 'benamebase' backend.

            If 'name' is specified. It uses it to retrieve the mapping tree
            to delete Else if 'suffix'/'benamebase' are specified. It uses
            both to retrieve the mapping tree to

            If 'name', 'benamebase' and 'suffix' are missing it raise an
            exception

            @param suffix - suffix mapped by this mapping tree entry.
                            It is the common name ('cn') of the entry
            @param benamebase - backend common name (e.g. 'userRoot')
            @param name - DN of the mapping tree entry
            @param - properties - list of properties

            @return - returns a dictionary of the properties

            @raise ValueError - if some name of properties are not valid
                   KeyError   - if some name of properties are not valid
                   ldap.NO_SUCH_OBJECT - if the mapping tree entry is not found
        '''

        if name:
            filt = "(objectclass=%s)" % MT_OBJECTCLASS_VALUE

            try:
                ent = self.conn.getEntry(name, ldap.SCOPE_BASE, filt,
                                         list(MT_PROPNAME_TO_ATTRNAME.values())
                                         )
                self.log.debug("delete: %s found by its DN", ent.dn)
            except NoSuchEntryError:
                raise ldap.NO_SUCH_OBJECT("mapping tree DN not found: %s" %
                                          name)
        else:
            filt = None

            if suffix:
                filt = suffixfilt(suffix)

            if bename:
                if filt:
                    filt = ("(&(%s=%s)%s)" %
                            (MT_PROPNAME_TO_ATTRNAME[MT_BACKEND],
                             bename, filt))
                else:
                    filt = ("(%s=%s)" % (MT_PROPNAME_TO_ATTRNAME[MT_BACKEND],
                                         bename))

            try:
                ent = self.conn.getEntry(DN_MAPPING_TREE, ldap.SCOPE_ONELEVEL,
                                         filt,
                                         list(MT_PROPNAME_TO_ATTRNAME.values())
                                         )
                self.log.debug("delete: %s found by with %s", ent.dn, filt)
            except NoSuchEntryError:
                raise ldap.NO_SUCH_OBJECT("mapping tree DN not found: %s" %
                                          name)

        result = {}
        attrs = []
        if properties:
            #
            # build the list of attributes we are looking for

            for prop_name in properties:
                prop_attr = MT_PROPNAME_TO_ATTRNAME[prop_name]
                if not prop_attr:
                    raise ValueError("Improper property name: %s ", prop_name)
                self.log.debug("Look for attr %s (property: %s)",
                               prop_attr, prop_name)
                attrs.append(prop_attr)

        # now look for each attribute from the MT entry
        for attr in ent.getAttrs():
            # given an attribute name retrieve the property name
            props = [k for k, v in six.iteritems(MT_PROPNAME_TO_ATTRNAME)
                     if v.lower() == attr.lower()]

            # If this attribute is present in the MT properties and was
            # requested, adds it to result.
            if len(props) > 0:
                if len(attrs) > 0:
                    if MT_PROPNAME_TO_ATTRNAME[props[0]] in attrs:
                        # if the properties was requested
                        self.log.debug("keep only attribute %s ", props[0])
                        result[props[0]] = ent.getValues(attr)
                else:
                    result[props[0]] = ent.getValues(attr)
        return result

    def setProperties(self, suffix=None, bename=None, name=None,
                      properties=None):
        raise NotImplementedError

    def toSuffix(self, entry=None, name=None):
        '''
            Return, for a given mapping tree entry, the suffix values.
            Suffix values are identical from a LDAP point of views.
            Suffix values may be surrounded by ", or containing '\'
            escape characters.

            @param entry - LDAP entry of the mapping tree
            @param name  - mapping tree DN

            @result list of values of suffix attribute (aka 'cn')

            @raise ldap.NO_SUCH_OBJECT - in name is invalid DN
                   ValueError - entry does not contains the suffix attribute
                   InvalidArgumentError - if both entry/name are missing
        '''
        attr_suffix = MT_PROPNAME_TO_ATTRNAME[MT_SUFFIX]
        if entry:
            if not entry.hasValue(attr_suffix):
                raise ValueError("Entry has no %s attribute %r" %
                                 (attr_suffix, entry))
            return entry.getValues(attr_suffix)
        elif name:
            filt = "(objectclass=%s)" % MT_OBJECTCLASS_VALUE

            try:
                attrs = [attr_suffix]
                ent = self.conn.getEntry(name, ldap.SCOPE_BASE, filt, attrs)
                self.log.debug("toSuffix: %s found by its DN", ent.dn)
            except NoSuchEntryError:
                raise ldap.NO_SUCH_OBJECT("mapping tree DN not found: %s" %
                                          name)

            if not ent.hasValue(attr_suffix):
                raise ValueError("Entry has no %s attribute %r" %
                                 (attr_suffix, ent))
            return ent.getValues(attr_suffix)
        else:
            raise InvalidArgumentError("entry or name are mandatory")


class MappingTree(DSLdapObject):
    """Mapping tree DSLdapObject with:
    - must attributes = ['cn']
    - RDN attribute is 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    _must_attributes = ['cn']

    def __init__(self, instance, dn=None):
        super(MappingTree, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._create_objectclasses = ['top', 'extensibleObject', 'nsMappingTree']
        self._protected = False

    def set_parent(self, parent):
        """
        Set the parent suffix to create a tree of backends. For example:

        be_1 = bes.create(properties={...})
        be_2 = bes.create(properties={...})
        mt = be_2.get_mapping_tree()
        mt.set_parent(be_1.get_attr_val_bytes('nsslapd-suffix'))

        :param parent: The parent suffix above this mapping tree.
        :type parent: str
        """
        self.replace('nsslapd-parent-suffix', parent)


class MappingTrees(DSLdapObjects):
    """DSLdapObjects that represents Mapping trees

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance):
        super(MappingTrees, self).__init__(instance=instance)
        self._objectclasses = ['nsMappingTree']
        self._filterattrs = ['cn', 'nsslapd-backend']
        self._childobject = MappingTree
        self._basedn = DN_MAPPING_TREE

    def get_root_suffix_by_entry(self, entry_dn):
        """Get the root suffix to which the entry belongs

        :param entry_dn: An entry DN
        :type entry_dn: str
        :returns: str
        """

        mapping_tree_list = sorted(self.list(), key=lambda b: len(b.dn), reverse=True)

        entry_dn_parts = str2dn(entry_dn)
        processing = True
        while processing:
            compare_dn = dn2str(entry_dn_parts)
            for mapping_tree in mapping_tree_list:
                if str.lower(compare_dn) == str.lower(mapping_tree.rdn):
                    processing = False
                    return mapping_tree.rdn
            if entry_dn_parts:
                entry_dn_parts.pop(0)
            else:
                processing = False
        raise ldap.NO_SUCH_OBJECT(f"{entry_dn} doesn't belong to any suffix")
