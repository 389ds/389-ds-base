# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016, William Brown <william at blackhats.net.au>
# Copyright (C) 2023 Red Hat, Inc.
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
]
RDN = 'cn'


class Group(DSLdapObject):
    """A single instance of Group entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(Group, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        # Can I generate these from schema?
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'groupOfNames',
        ]
        if not ds_is_older('1.3.7', instance=instance):
            self._create_objectclasses.append('nsMemberOf')
        self._protected = False

    def list_members(self):
        """List the members of this group.

        """
        return self.get_attr_vals_utf8('member')

    def is_member(self, dn):
        """Check if DN is a member

        :param dn: Entry DN
        :type dn: str
        """

        return self.present('member', dn)

    def add_member(self, dn):
        """Add DN as a member

        :param dn: Entry DN
        :type dn: str
        """

        self.add('member', dn)

    def remove_member(self, dn):
        """Remove a member with specified DN

        :param dn: Entry DN
        :type dn: str
        """

        self.remove('member', dn)

    def ensure_member(self, dn):
        """Ensure DN is a member

        :param dn: Entry DN
        :type dn: str
        """

        self.ensure_present('member', dn)


class Groups(DSLdapObjects):
    """DSLdapObjects that represents Groups entry
    By default it uses 'ou=Groups' as rdn.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn, rdn='ou=Groups'):
        super(Groups, self).__init__(instance)
        self._objectclasses = [
            'groupOfNames',
        ]
        self._filterattrs = [RDN]
        self._childobject = Group

        dsrc_inst = dsrc_to_ldap(DSRC_HOME, instance.serverid, self._log)
        if dsrc_inst is not None and 'groups_rdn' in dsrc_inst and dsrc_inst['groups_rdn'] is not None:
            rdn = dsrc_inst['groups_rdn']

        if rdn is None:
            self._basedn = ensure_str(basedn)
        else:
            self._basedn = '{},{}'.format(ensure_str(rdn), ensure_str(basedn))


class UniqueGroup(DSLdapObject):
    # WARNING!!!
    # Use group, not unique group!!!
    def __init__(self, instance, dn=None):
        super(UniqueGroup, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'groupOfUniqueNames',
        ]
        if not ds_is_older('1.3.7', instance=instance):
            self._create_objectclasses.append('nsMemberOf')
        self._protected = False

    def is_member(self, dn):
        # Check if dn is a member
        return self.present('uniquemember', dn)

    def add_member(self, dn):
        self.add('uniquemember', dn)

    def remove_member(self, dn):
        self.remove('uniquemember', dn)


class UniqueGroups(DSLdapObjects):
    # WARNING!!!
    # Use group, not unique group!!!
    def __init__(self, instance, basedn, rdn='ou=Groups'):
        super(UniqueGroups, self).__init__(instance)
        self._objectclasses = [
            'groupOfUniqueNames',
        ]
        self._filterattrs = [RDN]
        self._childobject = UniqueGroup

        dsrc_inst = dsrc_to_ldap(DSRC_HOME, instance.serverid, self._log)
        if dsrc_inst is not None and 'groups_rdn' in dsrc_inst and dsrc_inst['groups_rdn'] is not None:
            rdn = dsrc_inst['groups_rdn']

        if rdn is None:
            self._basedn = ensure_str(basedn)
        else:
            self._basedn = '{},{}'.format(ensure_str(rdn), ensure_str(basedn))


class nsAdminGroup(DSLdapObject):
    """A single instance of User nsAdminGroup entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(nsAdminGroup, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._must_attributes = MUST_ATTRIBUTES
        self._create_objectclasses = [
            'top',
            'nsAdminGroup'
        ]
        if ds_is_older('1.3.7', instance=instance):
            self._create_objectclasses.append('inetUser')
        else:
            self._create_objectclasses.append('nsMemberOf')
        if not ds_is_older('1.4.0', instance=instance):
            self._create_objectclasses.append('nsAccount')
        user_compare_exclude = [
            'nsUniqueId',
            'modifyTimestamp',
            'createTimestamp',
            'entrydn'
        ]
        self._compare_exclude = self._compare_exclude + user_compare_exclude
        self._protected = False


class nsAdminGroups(DSLdapObjects):
    """DSLdapObjects that represents all nsAdminGroups entries in suffix.
    By default it uses 'ou=People' as rdn.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    :param rdn: The DN that will be combined wit basedn
    :type rdn: str
    """

    def __init__(self, instance, basedn, rdn='ou=People'):
        super(nsAdminGroups, self).__init__(instance)
        self._objectclasses = [
            'nsAdminGroup'
        ]
        self._filterattrs = [RDN]
        self._childobject = nsAdminGroup

        dsrc_inst = dsrc_to_ldap(DSRC_HOME, instance.serverid, self._log)
        if dsrc_inst is not None and 'people_rdn' in dsrc_inst and dsrc_inst['people_rdn'] is not None:
            rdn = dsrc_inst['people_rdn']

        if rdn is None:
            self._basedn = basedn
        else:
            self._basedn = '{},{}'.format(rdn, basedn)
