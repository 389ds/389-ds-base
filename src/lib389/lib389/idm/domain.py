# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject

class Domain(DSLdapObject):
    """A single instance of Domain entry
    - must attributes = ['dc']
    - RDN attribute is 'dc'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn):
        super(Domain, self).__init__(instance, dn)
        self._rdn_attribute = 'dc'
        self._must_attributes = ['dc']
        self._create_objectclasses = [
            'top',
            'domain',
        ]
        self._protected = True

