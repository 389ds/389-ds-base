# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects

class Domain(DSLdapObject):
    def __init__(self, instance, dn=None, batch=False):
        super(Domain, self).__init__(instance, dn, batch)
        self._rdn_attribute = 'dc'
        self._must_attributes = ['dc']
        self._create_objectclasses = [
            'top',
            'domain',
        ]
        self._protected = True

