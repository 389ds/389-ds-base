# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObjects
# Account derives DSLdapObject - it gives us the lock / unlock functions.
from lib389.idm.account import Account
from lib389.utils import ds_is_older
from lib389.cli_base.dsrc import dsrc_to_ldap
from lib389._constants import DSRC_HOME

MUST_ATTRIBUTES = [
    'uid',
    'cn',
    'sn',
    'uidNumber',
    'gidNumber',
    'homeDirectory',
]
RDN = 'uid'
DEFAULT_BASEDN_RDN = 'ou=People'

TEST_USER_PROPERTIES = {
    'uid': 'testuser',
    'cn' : 'testuser',
    'sn' : 'user',
    'uidNumber' : '1000',
    'gidNumber' : '2000',
    'homeDirectory' : '/home/testuser'
}


# Modern userAccounts

class nsUserAccount(Account):
    _must_attributes = [
        'uid',
        'cn',
        'displayName',
        'uidNumber',
        'gidNumber',
        'homeDirectory',
    ]

    """A single instance of an nsPerson, capable of posix login, certificate
    authentication, sshkey distribution, and more.

    This is the modern and correct userAccount type to choose for DS 1.4.0 and above.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """
    def __init__(self, instance, dn=None):
        if ds_is_older('1.4.0', instance=instance):
            raise Exception("Not supported")
        super(nsUserAccount, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._must_attributes = nsUserAccount._must_attributes
        # Can I generate these from schema?
        self._create_objectclasses = [
            'top',
            'nsPerson',
            'nsAccount',
            'nsOrgPerson',
            'posixAccount',
        ]
        user_compare_exclude = [
            'nsUniqueId',
            'modifyTimestamp',
            'createTimestamp',
            'entrydn'
        ]
        self._compare_exclude = self._compare_exclude + user_compare_exclude
        self._protected = False


class nsUserAccounts(DSLdapObjects):
    """DSLdapObjects that represents all nsUserAccount entries in suffix.
    By default it uses 'ou=People' as rdn.

    This is the modern and correct userAccount type to choose for DS 1.4.0 and above.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    :param rdn: The DN that will be combined wit basedn
    :type rdn: str
    """

    def __init__(self, instance, basedn, rdn='ou=people'):
        super(nsUserAccounts, self).__init__(instance)
        self._objectclasses = [
            'nsPerson',
            'nsAccount',
            'nsOrgPerson',
            'posixAccount',
        ]
        self._filterattrs = [RDN, 'displayName', 'cn']
        self._childobject = nsUserAccount

        dsrc_inst = dsrc_to_ldap(DSRC_HOME, instance.serverid, self._log)
        if dsrc_inst is not None and 'people_rdn' in dsrc_inst and dsrc_inst['people_rdn'] is not None:
            rdn = dsrc_inst['people_rdn']

        if rdn is None:
            self._basedn = basedn
        else:
            self._basedn = '{},{}'.format(rdn, basedn)

    def create_test_user(self, uid=1000, gid=2000):
        """Create a test user with uid=test_user_UID rdn

        :param uid: User id
        :type uid: int
        :param gid: Group id
        :type gid: int

        :returns: DSLdapObject of the created entry
        """

        rdn_value = "test_user_{}".format(uid)
        rdn = "uid={}".format(rdn_value)
        properties = {
            'uid': rdn_value,
            'cn': rdn_value,
            'displayName': rdn_value,
            'uidNumber': str(uid),
            'gidNumber': str(gid),
            'homeDirectory': '/home/{}'.format(rdn_value),
        }
        return super(nsUserAccounts, self).create(rdn, properties)


# Traditional style userAccounts.

class UserAccount(Account):
    """A single instance of User Account entry

    This is the classic "user account" style of cn + sn. You should consider
    nsUserAccount instead.

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
            'inetOrgPerson',
            'organizationalPerson',
        ]
        if ds_is_older('1.3.7', instance=instance):
            self._create_objectclasses.append('inetUser')
        else:
            self._create_objectclasses.append('nsMemberOf')
        if not ds_is_older('1.4.0', instance=instance):
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


class UserAccounts(DSLdapObjects):
    """DSLdapObjects that represents all User Account entries in suffix.
    By default it uses 'ou=People' as rdn.

    This is the classic "user account" style of cn + sn. You should consider
    nsUserAccounts instead.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    :param rdn: The DN that will be combined wit basedn
    :type rdn: str
    """

    def __init__(self, instance, basedn, rdn=DEFAULT_BASEDN_RDN):
        super(UserAccounts, self).__init__(instance)
        self._objectclasses = [
            'account',
            'posixaccount',
            'inetOrgPerson',
            'organizationalPerson',
        ]
        self._filterattrs = [RDN]
        self._childobject = UserAccount

        dsrc_inst = dsrc_to_ldap(DSRC_HOME, instance.serverid, self._log)
        if dsrc_inst is not None and 'people_rdn' in dsrc_inst and dsrc_inst['people_rdn'] is not None:
            rdn = dsrc_inst['people_rdn']

        if rdn is None:
            self._basedn = basedn
        else:
            self._basedn = '{},{}'.format(rdn, basedn)

    def create_test_user(self, uid=1000, gid=2000):
        """Create a test user with uid=test_user_UID rdn

        :param uid: User id
        :type uid: int
        :param gid: Group id
        :type gid: int

        :returns: DSLdapObject of the created entry
        """

        rdn_value = "test_user_{}".format(uid)
        rdn = "uid={}".format(rdn_value)
        properties = {
            'uid': rdn_value,
            'cn': rdn_value,
            'sn': rdn_value,
            'uidNumber': str(uid),
            'gidNumber': str(gid),
            'homeDirectory': '/home/{}'.format(rdn_value)
        }
        return super(UserAccounts, self).create(rdn, properties)
