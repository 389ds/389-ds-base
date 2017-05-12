# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects, _gen_or, _gen_filter, _term_gen


class Account(DSLdapObject):
    def is_locked(self):
        # Check if nsAccountLock is set.
        return self.present('nsAccountLock')

    def lock(self):
        self.replace('nsAccountLock', 'true')

    def unlock(self):
        self.remove('nsAccountLock', None)

class Accounts(DSLdapObjects):
    def __init__(self, instance, basedn, batch=False):
        super(Accounts, self).__init__(instance, batch)
        # These are all the objects capable of holding a password.
        self._objectclasses = [
            'simpleSecurityObject',
            'organization',
            'personperson',
            'organizationalUnit',
            'netscapeServer',
            'domain',
            'posixAccount',
            'shadowAccount',
            'posixGroup',
            'mailRecipient',
        ]
        # MUST BE NONE.
        self._filterattrs = None
        self._childobject = Account
        self._basedn = basedn

    #### This is copied from DSLdapObjects, but change _gen_and to _gen_or!!!

    def _get_objectclass_filter(self):
        return _gen_or(
            _gen_filter(_term_gen('objectclass'), self._objectclasses)
        )

