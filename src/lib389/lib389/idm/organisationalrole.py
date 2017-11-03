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


class OrganisationalRole(DSLdapObject):
    """A single instance of OrganizationalRole entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    :param batch: Not implemented
    :type batch: bool
    """

    def __init__(self, instance, dn=None, batch=False):
        super(OrganisationalRole, self).__init__(instance, dn, batch)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'organizationalrole',
        ]
        self._protected = False


class OrganisationalRoles(DSLdapObjects):
    """DSLdapObjects that represents OrganizationalRole entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    :param batch: Not implemented
    :type batch: bool
    """

    def __init__(self, instance, basedn, batch=False):
        super(OrganisationalRoles, self).__init__(instance, batch)
        self._objectclasses = [
            'organizationalrole',
        ]
        self._filterattrs = [RDN]
        self._childobject = OrganisationalRole
        self._basedn = basedn

