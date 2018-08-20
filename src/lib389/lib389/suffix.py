# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389 import DirSrv, InvalidArgumentError
from lib389.properties import *


class Suffix(object):
    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Suffix.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def list(self):
        '''
            Returns the list of suffixes DN for which it exists a mapping tree
            entry

            @param None

            @return list of suffix DN

            @raise None
        '''
        suffixes = []
        ents = self.conn.mappingtree.list()
        for ent in ents:
            vals = self.conn.mappingtree.toSuffix(entry=ent)
            suffixes.append(vals[0])
        return suffixes

    def toBackend(self, suffix=None):
        '''
            Returns the backend entry that stores the provided suffix

            @param suffix - suffix DN of the backend

            @return backend - LDAP entry of the backend

            @return ValueError - if suffix is not provided

        '''
        if not suffix:
            raise ValueError("suffix is mandatory")

        return self.conn.backend.list(suffix=suffix)

    def getParent(self, suffix=None):
        '''
            Returns the DN of a suffix that is the parent of the provided
            'suffix'.
            If 'suffix' has no parent, it returns None

            @param suffix - suffix DN of the backend

            @return parent suffix DN

            @return ValueError - if suffix is not provided
                    InvalidArgumentError - if suffix is not implemented on the
                                           server

        '''
        if not suffix:
            raise ValueError("suffix is mandatory")

        ents = self.conn.mappingtree.list(suffix=suffix)
        if len(ents) == 0:
            raise InvalidArgumentError(
                "suffix %s is not implemented on that server" % suffix)

        mapping_tree = ents[0]
        if mapping_tree.hasValue(MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX]):
            return mapping_tree.getValue(
                MT_PROPNAME_TO_ATTRNAME[MT_PARENT_SUFFIX])
        else:
            return None

    def setProperties(self, suffix):
        '''
            Supported properties:

        '''

        if not suffix:
            raise ValueError("suffix is mandatory")
