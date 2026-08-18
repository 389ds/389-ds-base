# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject

# Collection of generic LDAP objects/entries

class DSContainer(DSLdapObject):
    """Just a basic container entry

    :param instance: DirSrv instance
    :type instance: DirSrv
    :param dn: The dn of the entry
    :type dn: str
    """

    def __init__(self, instance, dn):
        super(DSContainer, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._create_objectclasses = [
            'top',
            'nsContainer',
        ]
        self._protected = True
