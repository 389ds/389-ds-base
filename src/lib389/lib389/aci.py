# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Aci class to help parse and create ACIs.

You will access this via the Entry Class.
"""

import ldap

from lib389._constants import *


class Aci(object):
    def __init__(self, conn):
        """
        """
        self.conn = conn
        self.log = conn.log

    def list(self, basedn, scope=ldap.SCOPE_SUBTREE):
        """
        List all acis in the directory server below the basedn confined by
        scope.

        A set of EntryAcis is returned.
        """
        acis = []
        rawacientries = self.conn.search_s(basedn, scope, 'aci=*', ['aci'])
        for rawacientry in rawacientries:
            acis += rawacientry.getAcis()
        return acis
