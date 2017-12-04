# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap

from lib389._mapped_object import DSLdapObjects, DSLdapObject


class Referral(DSLdapObject):
    def __init__(self, instance, dn=None):
        super(Referral, self).__init__(instance, dn)
        self._rdn_attribute = "cn"
        self._must_attributes = ["ref"]
        self._create_objectclasses = ['referral', 'nsContainer']
        self._protected = False
        managedsait_ctrl = ldap.controls.simple.ManageDSAITControl()
        self._server_controls = [managedsait_ctrl]
        self._client_controls = None

class Referrals(DSLdapObjects):
    def __init__(self, instance, basedn):
        super(Referrals, self).__init__(instance)
        self._objectclasses = ['referral']
        self._filterattrs = 'cn'
        self._childobject = Referral
        self._basedn = basedn
        managedsait_ctrl = ldap.controls.simple.ManageDSAITControl()
        self._server_controls = [managedsait_ctrl]
        self._client_controls = None

