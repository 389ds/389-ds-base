# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects
from lib389.utils import ds_is_older, ensure_str
from lib389.cli_base.dsrc import dsrc_to_ldap
from lib389._constants import DSRC_HOME

MUST_ATTRIBUTES = [
    'cn',
    'gidNumber',
]
RDN = 'cn'
DEFAULT_BASEDN_RDN = 'ou=Groups'


class PosixGroup(DSLdapObject):
    """A single instance of PosixGroup entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(PosixGroup, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        # Can I generate these from schema?
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'groupOfNames',
            'posixGroup',
        ]
        if not ds_is_older('1.3.7', instance=instance):
            self._create_objectclasses.append('nsMemberOf')
        self._protected = False

    def check_member(self, dn):
        """Check if DN is a member

        :param dn: Entry DN
        :type dn: str
        """

        return dn in self.get_attr_vals('member')

    def add_member(self, dn):
        """Add DN as a member

        :param dn: Entry DN
        :type dn: str
        """

        # Assert the DN exists?
        self.add('member', dn)


class PosixGroups(DSLdapObjects):
    """DSLdapObjects that represents PosixGroups entry
    By default it uses 'ou=Groups' as rdn.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn, rdn=DEFAULT_BASEDN_RDN):
        super(PosixGroups, self).__init__(instance)
        self._objectclasses = [
            'groupOfNames',
            'posixGroup',
        ]
        self._filterattrs = [RDN]
        self._childobject = PosixGroup

        dsrc_inst = dsrc_to_ldap(DSRC_HOME, instance.serverid, self._log)
        if dsrc_inst is not None and 'groups_rdn' in dsrc_inst and dsrc_inst['groups_rdn'] is not None:
            rdn = dsrc_inst['groups_rdn']

        if rdn is None:
            self._basedn = ensure_str(basedn)
        else:
            self._basedn = '{},{}'.format(ensure_str(rdn), ensure_str(basedn))
