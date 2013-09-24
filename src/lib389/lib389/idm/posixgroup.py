# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects
from lib389.utils import ds_is_older

MUST_ATTRIBUTES = [
    'cn',
    'gidNumber',
]
RDN = 'cn'

class PosixGroup(DSLdapObject):
    def __init__(self, instance, dn=None, batch=False):
        super(PosixGroup, self).__init__(instance, dn, batch)
        self._rdn_attribute = RDN
        # Can I generate these from schema?
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'groupOfNames',
            'posixGroup',
        ]
        if not ds_is_older('1.3.7'):
            self._create_objectclasses.append('nsMemberOf')
        self._protected = False

    def check_member(self, dn):
        return dn in self.get_attr_vals('member')

    def add_member(self, dn):
        # Assert the DN exists?
        self.add('member', dn)


class PosixGroups(DSLdapObjects):
    def __init__(self, instance, basedn, batch=False, rdn='ou=Groups'):
        super(PosixGroups, self).__init__(instance, batch)
        self._objectclasses = [
            'groupOfNames',
            'posixGroup',
        ]
        self._filterattrs = [RDN]
        self._childobject = PosixGroup
        self._basedn = '{},{}'.format(rdn, basedn)


