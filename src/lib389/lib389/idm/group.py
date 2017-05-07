# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects

MUST_ATTRIBUTES = [
    'cn',
]
RDN = 'cn'

class Group(DSLdapObject):
    def __init__(self, instance, dn=None, batch=False):
        super(Group, self).__init__(instance, dn, batch)
        self._rdn_attribute = RDN
        # Can I generate these from schema?
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'groupOfNames',
            'nsMemberOf',
        ]
        self._protected = False

    def is_member(self, dn):
        # Check if dn is a member
        return self.present('member', dn)

    def add_member(self, dn):
        self.add('member', dn)

    def remove_member(self, dn):
        self.remove('member', dn)

class Groups(DSLdapObjects):
    def __init__(self, instance, basedn, batch=False, rdn='ou=Groups'):
        super(Groups, self).__init__(instance, batch)
        self._objectclasses = [
            'groupOfNames',
        ]
        self._filterattrs = [RDN]
        self._childobject = Group
        self._basedn = '{},{}'.format(rdn, basedn)

class UniqueGroup(DSLdapObject):
    # WARNING!!!
    # Use group, not unique group!!!
    def __init__(self, instance, dn=None, batch=False):
        super(UniqueGroup, self).__init__(instance, dn, batch)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'groupOfUniqueNames',
        ]
        self._protected = False

    def is_member(self, dn):
        # Check if dn is a member
        return self.present('uniquemember', dn)

    def add_member(self, dn):
        self.add('uniquemember', dn)

    def remove_member(self, dn):
        self.remove('uniquemember', dn)

class UniqueGroups(DSLdapObjects):
    # WARNING!!!
    # Use group, not unique group!!!
    def __init__(self, instance, basedn, batch=False, rdn='ou=Groups'):
        super(UniqueGroups, self).__init__(instance, batch)
        self._objectclasses = [
            'groupOfUniqueNames',
        ]
        self._filterattrs = [RDN]
        self._childobject = UniqueGroup
        self._basedn = '{},{}'.format(rdn, basedn)



