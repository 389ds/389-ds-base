# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017, Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject

class IpaDomain(DSLdapObject):
    """A single instance of an IpaDomain entry
    - must attributes = ['dc', 'info']
    - RDN attribute is 'dc'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    :param batch: Not implemented
    :type batch: bool
    """

    def __init__(self, instance, dn=None):
        super(IpaDomain, self).__init__(instance, dn)
        self._rdn_attribute = 'dc'
        self._must_attributes = ['dc', 'info']
        self._create_objectclasses = [
            'top',
            'domain',
            # Pilot object gives the info field.
            'pilotObject',
        ]
        self._protected = True


