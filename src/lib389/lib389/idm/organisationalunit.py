# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects

MUST_ATTRIBUTES = [
    'ou',
]
RDN = 'ou'

class OrganisationalUnit(DSLdapObject):
    """A single instance of OrganizationalUnit entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    :param batch: Not implemented
    :type batch: bool
    """

    def __init__(self, instance, dn=None, batch=False):
        super(OrganisationalUnit, self).__init__(instance, dn, batch)
        self._rdn_attribute = RDN
        # Can I generate these from schema?
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'organizationalunit',
        ]
        self._protected = False

class OrganisationalUnits(DSLdapObjects):
    """DSLdapObjects that represents OrganizationalUnits entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    :param batch: Not implemented
    :type batch: bool
    """

    def __init__(self, instance, basedn, batch=False):
        super(OrganisationalUnits, self).__init__(instance, batch)
        self._objectclasses = [
            'organizationalunit',
        ]
        self._filterattrs = [RDN]
        self._childobject = OrganisationalUnit
        self._basedn = basedn


