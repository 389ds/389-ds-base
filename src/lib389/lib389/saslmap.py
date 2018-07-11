# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects


class SaslMapping(DSLdapObject):
    """A sasl map providing a link from a sasl user and realm
    to a valid directory server entry.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    _must_attributes = [
            'cn',
            'nsSaslMapRegexString',
            'nsSaslMapBaseDNTemplate',
            'nsSaslMapFilterTemplate',
            'nsSaslMapPriority'
        ]

    def __init__(self, instance, dn=None):
        super(SaslMapping, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'

        self._create_objectclasses = [
            'top',
            'nsSaslMapping',
        ]
        self._protected = False


class SaslMappings(DSLdapObjects):
    """DSLdapObjects that represents SaslMappings in the server.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, dn=None):
        super(SaslMappings, self).__init__(instance)
        self._objectclasses = [
            'nsSaslMapping',
        ]
        self._filterattrs = ['cn']
        self._childobject = SaslMapping
        self._basedn = 'cn=mapping,cn=sasl,cn=config'


