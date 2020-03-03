# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects

MUST_ATTRIBUTES = [
    'c',
]
RDN = 'c'


class Country(DSLdapObject):
    """A single instance of Country entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(Country, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'country',
        ]
        self._protected = False


class Countries(DSLdapObjects):
    """DSLdapObjects that represents Country entries

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(Countries, self).__init__(instance)
        self._objectclasses = [
            'country',
        ]
        self._filterattrs = [RDN]
        self._childobject = Country
        self._basedn = basedn
