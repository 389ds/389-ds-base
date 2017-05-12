# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObjects
# Account derives DSLdapObject - it gives us the lock / unlock functions.
from lib389.idm.account import Account

MUST_ATTRIBUTES = [
    'uid',
    'cn',
    'sn',
    'uidNumber',
    'gidNumber',
    'homeDirectory',
]
RDN = 'uid'


class UserAccount(Account):
    def __init__(self, instance, dn=None, batch=False):
        super(UserAccount, self).__init__(instance, dn, batch)
        self._rdn_attribute = RDN
        # Can I generate these from schema?
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'account',
            'posixaccount',
            'inetOrgPerson',
            'nsMemberOf',
            'organizationalPerson',
            # This may not always work at sites?
            # Can we get this into core?
            # 'ldapPublicKey',
        ]
        user_compare_exclude = [
            'nsUniqueId', 
            'modifyTimestamp', 
            'createTimestamp', 
            'entrydn'
        ]
        self._compare_exclude = self._compare_exclude + user_compare_exclude
        self._protected = False

    def _validate(self, rdn, properties, basedn):
        if properties.has_key('ntUserDomainId') and 'ntUser' not in self._create_objectclasses:
            self._create_objectclasses.append('ntUser')

        return super(UserAccount, self)._validate(rdn, properties, basedn)

    # Add a set password function....
    # Can't I actually just set, and it will hash?

class UserAccounts(DSLdapObjects):
    def __init__(self, instance, basedn, batch=False, rdn='ou=People'):
        super(UserAccounts, self).__init__(instance, batch)
        self._objectclasses = [
            'account',
            'posixaccount',
            'inetOrgPerson',
            'organizationalPerson',
        ]
        self._filterattrs = [RDN]
        self._childobject = UserAccount
        self._basedn = '{},{}'.format(rdn, basedn)

