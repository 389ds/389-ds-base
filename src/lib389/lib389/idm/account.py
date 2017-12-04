# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects, _gen_or, _gen_filter, _term_gen


class Account(DSLdapObject):
    """A single instance of Account entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def is_locked(self):
        """Check if nsAccountLock is set

        :returns: True if account is locked
        """

        return self.present('nsAccountLock')

    def lock(self):
        """Set nsAccountLock to 'true'"""

        self.replace('nsAccountLock', 'true')

    def unlock(self):
        """Unset nsAccountLock"""

        self.remove('nsAccountLock', None)

class Accounts(DSLdapObjects):
    """DSLdapObjects that represents Account entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all account entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(Accounts, self).__init__(instance)
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

