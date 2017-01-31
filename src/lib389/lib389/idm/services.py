# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects

RDN = 'cn'
MUST_ATTRIBUTES = [
    'cn',
]

class ServiceAccount(DSLdapObject):
    def __init__(self, instance, dn=None, batch=False):
        super(ServiceAccount, self).__init__(instance, dn, batch)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'netscapeServer',
        ]
        self._protected = False

class ServiceAccounts(DSLdapObjects):
    def __init__(self, instance, basedn, batch=False):
        super(ServiceAccounts, self).__init__(instance, batch)
        self._objectclasses = [
            'netscapeServer',
        ]
        self._filterattrs = [RDN]
        self._childobject = ServiceAccount
        self._basedn = 'ou=Services,%s' % basedn

