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
from lib389.utils import ds_is_older

MUST_ATTRIBUTES = [
    'uid',
    'cn',
    'sn',
    'uidNumber',
    'gidNumber',
    'homeDirectory',
]
RDN = 'uid'

TEST_USER_PROPERTIES = {
    'uid': 'testuser',
    'cn' : 'testuser',
    'sn' : 'user',
    'uidNumber' : '1000',
    'gidNumber' : '2000',
    'homeDirectory' : '/home/testuser'
}


class UserAccount(Account):
    """A single instance of User Account entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(UserAccount, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        # Can I generate these from schema?
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'account',
            'posixaccount',
            # inetOrgPerson allows userCertificate
            'inetOrgPerson',
            'organizationalPerson',
            # This may not always work at sites?
            # Can we get this into core?
            # 'ldapPublicKey',
        ]
        if ds_is_older('1.3.7'):
            self._create_objectclasses.append('inetUser')
        else:
            self._create_objectclasses.append('nsMemberOf')
        if not ds_is_older('1.4.0'):
            self._create_objectclasses.append('nsAccount')
        user_compare_exclude = [
            'nsUniqueId', 
            'modifyTimestamp', 
            'createTimestamp', 
            'entrydn'
        ]
        self._compare_exclude = self._compare_exclude + user_compare_exclude
        self._protected = False

    def _validate(self, rdn, properties, basedn):
        if 'ntUserDomainId' in properties and 'ntUser' not in self._create_objectclasses:
            self._create_objectclasses.append('ntUser')

        return super(UserAccount, self)._validate(rdn, properties, basedn)

    def enroll_certificate(self, der_path):
        """Enroll a certificate for certmap verification. Because of the userCertificate
        attribute, we have to do this on userAccount which has support for it.

        :param der_path: the certificate file in DER format to include.
        :type der_path: str
        """
        if ds_is_older('1.4.0'):
            raise Exception("This version of DS does not support nsAccount")
        # Given a cert path, add this to the object as a userCertificate
        crt = None
        with open(der_path, 'rb') as f:
            crt = f.read()
        self.add('usercertificate;binary', crt)

    # Add a set password function....
    # Can't I actually just set, and it will hash?


class UserAccounts(DSLdapObjects):
    """DSLdapObjects that represents all User Account entries in suffix.
    By default it uses 'ou=People' as rdn.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    :param rdn: The DN that will be combined wit basedn
    :type rdn: str
    """

    def __init__(self, instance, basedn, rdn='ou=People'):
        super(UserAccounts, self).__init__(instance)
        self._objectclasses = [
            'account',
            'posixaccount',
            'inetOrgPerson',
            'organizationalPerson',
        ]
        self._filterattrs = [RDN]
        self._childobject = UserAccount
        if rdn is None:
            self._basedn = basedn
        else:
            self._basedn = '{},{}'.format(rdn, basedn)

