# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


from lib389._mapped_object import DSLdapObject, DSLdapObjects


class nsFilterRole(DSLdapObject):
    """A single instance of nsfilter entry to create nsFIltered role.

        :param instance: An instance
        :type instance: lib389.DirSrv
        :param dn: Entry DN
        :type dn: str
        Usages:
        user1 = 'cn=anuj,ou=people,dc=example,ed=com'
        user2 = 'cn=unknownuser,ou=people,dc=example,ed=com'
        role=nsFilterRole(topo.standalone,'cn=NameofRole,ou=People,dc=example,dc=com')
        role_props={'cn':'Anuj', 'nsRoleFilter':'cn=anuj*'}
        role.create(properties=role_props, basedn=SUFFIX)
        The user1 entry matches the filter (possesses the cn=anuj* attribute with the value anuj)
        therefore, it is a member of this filtered role automatically.
    """
    def __init__(self, instance, dn=None):
        super(nsFilterRole, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._create_objectclasses = [
            'top',
            'nsRoleDefinition',
            'nsComplexRoleDefinition',
            'nsFilteredRoleDefinition'
        ]


class nsFilterRoles(DSLdapObjects):
    """DSLdapObjects that represents all nsfiltertrole entries in suffix.

        This instance is used mainly for search operation  nsfiltred role

        :param instance: An instance
        :type instance: lib389.DirSrv
        :param basedn: Suffix DN
        :type basedn: str
        :param rdn: The DN that will be combined wit basedn
        :type rdn: str
        Usages:
        role_props={'cn':'Anuj', 'nsRoleFilter':'cn=*'}
        nsFilterRoles(topo.standalone, DEFAULT_SUFFIX).create(properties=role_props)
        nsFilterRoles(topo.standalone, DEFAULT_SUFFIX).list()
        user1 = 'cn=anuj,ou=people,dc=example,ed=com'
        user2 = 'uid=unknownuser,ou=people,dc=example,ed=com'
        The user1 entry matches the filter (possesses the cn=* attribute with the value cn)
        therefore, it is a member of this filtered role automatically.
        """
    def __init__(self, instance, basedn):
        super(nsFilterRoles, self).__init__(instance)
        self._objectclasses = [
            'top',
            'nsRoleDefinition',
            'nsComplexRoleDefinition',
            'nsFilteredRoleDefinition'
        ]
        self._filterattrs = ['cn']
        self._basedn = basedn
        self._childobject = nsFilterRole
