# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects

MUST_ATTRIBUTES = [
    'o',
]
RDN = 'o'


class Organization(DSLdapObject):
    """A single instance of Organization entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(Organization, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'organization',
        ]
        self._protected = False


class Organizations(DSLdapObjects):
    """DSLdapObjects that represents Organizations entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(Organizations, self).__init__(instance)
        self._objectclasses = [
            'organization',
        ]
        self._filterattrs = [RDN]
        self._childobject = Organization
        self._basedn = basedn
