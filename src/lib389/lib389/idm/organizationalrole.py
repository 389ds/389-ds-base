# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
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


class OrganizationalRole(DSLdapObject):
    """A single instance of OrganizationalRole entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(OrganizationalRole, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'organizationalrole',
        ]
        self._protected = False


class OrganizationalRoles(DSLdapObjects):
    """DSLdapObjects that represents OrganizationalRole entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(OrganizationalRoles, self).__init__(instance)
        self._objectclasses = [
            'organizationalrole',
        ]
        self._filterattrs = [RDN]
        self._childobject = OrganizationalRole
        self._basedn = basedn

