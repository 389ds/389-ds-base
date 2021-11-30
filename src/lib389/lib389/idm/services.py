# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObjects
from lib389.idm.account import Account

from lib389.utils import ds_is_older

RDN = 'cn'
MUST_ATTRIBUTES = [
    'cn',
]

class ServiceAccount(Account):
    """A single instance of Service entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """
    _must_attributes = [
        'cn',
        'description',
    ]

    def __init__(self, instance, dn=None):
        super(ServiceAccount, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'applicationProcess',
        ]
        if ds_is_older('1.4.0'):
            # This is a HORRIBLE HACK for older versions that DON'T have
            # correct updated schema!
            #
            # I feel physically ill having wrtten this line of code. :(
            self._create_objectclasses.append('extensibleobject')
        else:
            self._create_objectclasses.append('nsMemberOf')
            self._create_objectclasses.append('nsAccount')
        self._protected = False

class ServiceAccounts(DSLdapObjects):
    """DSLdapObjects that represents Services entry
    By default it uses 'ou=Services' as rdn.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn, rdn='ou=Services'):
        super(ServiceAccounts, self).__init__(instance)
        self._objectclasses = [
            'applicationProcess',
        ]
        self._filterattrs = [RDN]
        self._childobject = ServiceAccount
        self._basedn = '{},{}'.format(rdn, basedn)


    def create_test_service(self, description="Test Service"):
        """Create a test service with cn=test_service rdn

        :param description: Service description
        :type description: str

        :returns: DSLdapObject of the created entry
        """

        rdn_value = "test_service"
        rdn = "cn={}".format(rdn_value)
        properties = {
            'cn': rdn_value,
            'description': description,
        }
        return super(ServiceAccounts, self).create(rdn, properties)

