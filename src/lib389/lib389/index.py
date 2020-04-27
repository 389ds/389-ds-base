# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap

import sys
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import Tasks
from lib389 import Entry
from lib389.utils import ensure_str
from lib389._mapped_object import DSLdapObjects, DSLdapObject

MAJOR, MINOR, _, _, _ = sys.version_info

if MAJOR >= 3 or (MAJOR == 2 and MINOR >= 7):
    from ldap.controls.readentry import PostReadControl

DEFAULT_INDEX_DN = "cn=default indexes,%s" % DN_CONFIG_LDBM


class Index(DSLdapObject):
    """Index DSLdapObject with:
    - must attributes = ['cn', 'nsSystemIndex', 'nsIndexType']
    - RDN attribute is 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Index DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(Index, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn', 'nsSystemIndex', 'nsIndexType']
        self._create_objectclasses = ['top', 'nsIndex']
        self._protected = False
        self._lint_functions = []


class Indexes(DSLdapObjects):
    """DSLdapObjects that represents Index

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: DN of suffix container.
    :type basedn: str
    """

    def __init__(self, instance, basedn=DEFAULT_INDEX_DN):
        super(Indexes, self).__init__(instance=instance)
        self._objectclasses = ['nsIndex']
        self._filterattrs = ['cn']
        self._childobject = Index
        self._basedn = basedn


class VLVSearch(DSLdapObject):
    """VLVSearch DSLdapObject with:
    - must attributes = ['cn', 'vlvbase', 'vlvscope', 'vlvfilter']
    - RDN attribute is 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Index DN
    :type dn: str
    """
    def __init__(self, instance, dn=None):
        super(VLVSearch, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn', 'vlvbase', 'vlvscope', 'vlvfilter']
        self._create_objectclasses = ['top', 'vlvSearch']
        self._protected = False
        self._lint_functions = []
        self._be_name = None

    def get_sorts(self):
        """Return a list of child VLVIndex entries
        """
        return VLVIndexes(self._instance, basedn=self._dn).list()

    def add_sort(self, name, attrs):
        """Add a VLVIndex child entry to ths VLVSearch
        :param name - name of the child index entry
        :param attrs - String of space separated attributes for the sort
        """
        dn = "cn={},{}".format(name, self._dn)
        props = {'cn': name, 'vlvsort': attrs}
        new_index = VLVIndex(self._instance, dn=dn)
        new_index.create(properties=props)

    def delete_sort(self, name, sort_attrs=None):
        vlvsorts = VLVIndexes(self._instance, basedn=self._dn).list()
        for vlvsort in vlvsorts:
            sort_name = vlvsort.get_attr_val_utf8_l('cn').lower()
            sort = vlvsort.get_attr_val_utf8_l('vlvsort')
            if sort_attrs is not None and sort_attrs == sort:
                vlvsort.delete()
                return
            elif name is not None and sort_name == name.lower():
                vlvsort.delete()
                return
        raise ValueError("Can not delete vlv sort index because it does not exist")

    def delete_all(self):
        # Delete the child indexes, then the parent search entry
        print("deleting vlv search: " + self._dn)
        vlvsorts = VLVIndexes(self._instance, basedn=self._dn).list()
        for vlvsort in vlvsorts:
            print("Deleting vlv index: " + vlvsort._dn + " ...")
            vlvsort.delete()
        print("deleting vlv search entry...")
        self.delete()

    def reindex(self, be_name, vlv_index=None):
        reindex_task = Tasks(self._instance)
        if vlv_index is not None:
            reindex_task.reindex(suffix=be_name, attrname=vlv_index, vlv=True)
        else:
            attrs = []
            vlvsorts = VLVIndexes(self._instance, basedn=self._dn).list()
            if len(vlvsorts) > 0:
                for vlvsort in vlvsorts:
                    attrs.append(ensure_str(vlvsort.get_attr_val_bytes('cn')))
                reindex_task.reindex(suffix=be_name, attrname=attrs, vlv=True)


class VLVSearches(DSLdapObjects):
    """DSLdapObjects that represents VLVSearches

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: DN of suffix container.
    :type basedn: str
    """

    def __init__(self, instance, basedn=None, be_name=None):
        super(VLVSearches, self).__init__(instance=instance)
        self._objectclasses = ['top', 'vlvSearch']
        self._filterattrs = ['cn']
        self._childobject = VLVSearch
        self._basedn = basedn
        self._be_name = be_name


class VLVIndex(DSLdapObject):
    """DSLdapObject that represents a VLVIndex

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: index DN
    :type dn: str
    """
    def __init__(self, instance, dn=None):
        super(VLVIndex, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn', 'vlvsort']
        self._create_objectclasses = ['top', 'vlvIndex']
        self._protected = False
        self._lint_functions = []


class VLVIndexes(DSLdapObjects):
    """DSLdapObjects that represents VLVindexes

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: DN of index container.
    :type basedn: str
    """
    def __init__(self, instance, basedn=None):
        super(VLVIndexes, self).__init__(instance=instance)
        self._objectclasses = ['top', 'vlvIndex']
        self._filterattrs = ['cn']
        self._childobject = VLVIndex
        self._basedn = basedn


class IndexLegacy(object):

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def delete_all(self, benamebase):
        benamebase = ensure_str(benamebase)
        dn = "cn=index,cn=" + benamebase + "," + DN_LDBM

        # delete each defined index
        self.conn.delete_branch_s(dn, ldap.SCOPE_ONELEVEL)

        # Then delete the top index entry
        self.log.debug("Delete head index entry %s", dn)
        self.conn.delete_s(dn)

    def create(self, suffix=None, be_name=None, attr=None, args=None):
        if not suffix and not be_name:
            raise ValueError("suffix/backend name is mandatory parameter")

        if not attr:
            raise ValueError("attr is mandatory parameter")

        indexTypes = args.get(INDEX_TYPE, None)
        matchingRules = args.get(INDEX_MATCHING_RULE, None)

        return self.addIndex(suffix, be_name, attr, indexTypes=indexTypes,
                             matchingRules=matchingRules)

    def addIndex(self, suffix, be_name, attr, indexTypes, matchingRules,
                 postReadCtrl=None):
        """Specify the suffix (should contain 1 local database backend),
            the name of the attribute to index, and the types of indexes
            to create e.g. "pres", "eq", "sub"
        """
        msg_id = None
        if be_name:
            dn = ('cn=%s,cn=index,cn=%s,cn=ldbm database,cn=plugins,cn=config'
                  % (attr, be_name))
        else:
            entries_backend = self.conn.backend.list(suffix=suffix)
            # assume 1 local backend
            dn = "cn=%s,cn=index,%s" % (attr, entries_backend[0].dn)

        if postReadCtrl:
            add_record = [('nsSystemIndex', ['false']),
                          ('cn', [attr]),
                          ('objectclass', ['top', 'nsindex']),
                          ('nsIndexType', indexTypes)]
            if matchingRules:
                add_record.append(('nsMatchingRule', matchingRules))

        else:
            entry = Entry(dn)
            entry.setValues('objectclass', 'top', 'nsIndex')
            entry.setValues('cn', attr)
            entry.setValues('nsSystemIndex', "false")
            entry.setValues('nsIndexType', indexTypes)
            if matchingRules:
                entry.setValues('nsMatchingRule', matchingRules)

        if MAJOR >= 3 or (MAJOR == 2 and MINOR >= 7):
            try:
                if postReadCtrl:
                    pr = PostReadControl(criticality=True, attrList=['*'])
                    msg_id = self.conn.add_ext(dn, add_record, serverctrls=[pr])
                else:
                    self.conn.add_s(entry)
            except ldap.LDAPError as e:
                raise e

        return msg_id

    def modIndex(self, suffix, attr, mod):
        """just a wrapper around a plain old ldap modify, but will
        find the correct index entry based on the suffix and attribute"""
        entries_backend = self.conn.backend.list(suffix=suffix)
        # assume 1 local backend
        dn = "cn=%s,cn=index,%s" % (attr, entries_backend[0].dn)
        self.conn.modify_s(dn, mod)
