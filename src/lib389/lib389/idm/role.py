# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


from enum import Enum
import ldap
from lib389._mapped_object import DSLdapObject, DSLdapObjects
from lib389.cos import CosTemplates, CosClassicDefinitions
from lib389.mappingTree import MappingTrees
from lib389.idm.nscontainer import nsContainers

MUST_ATTRIBUTES = [
    'cn',
]
MUST_ATTRIBUTES_NESTED = [
    'cn',
    'nsRoleDN'
]
RDN = 'cn'

class RoleState(Enum):
    ACTIVATED = "activated"
    DIRECTLY_LOCKED = "directly locked through nsDisabledRole"
    INDIRECTLY_LOCKED = "indirectly locked through a Role"
    PROBABLY_ACTIVATED = '''probably activated or nsDisabledRole setup and its CoS entries are not
in a valid state or there is no access to the settings.'''

    def describe(self, role_dn=None):
        if self.name == "INDIRECTLY_LOCKED" and role_dn is not None:
            return f'{self.value} - {role_dn}'
        else:
            return f'{self.value}'


class Role(DSLdapObject):
    """A single instance of Role entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(Role, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._create_objectclasses = [
            'top',
            'LDAPsubentry',
            'nsRoleDefinition',
        ]

    def _format_status_message(self, message, role_dn=None):
        return {"state": message, "role_dn": role_dn}

    def status(self):
        """Check if role is locked in nsDisabledRole (directly or indirectly)

        :returns: a dict
        """

        inst = self._instance
        disabled_roles = {}
        try:
            mapping_trees = MappingTrees(inst)
            root_suffix = mapping_trees.get_root_suffix_by_entry(self.dn)
            roles = Roles(inst, root_suffix)
            disabled_roles = roles.get_disabled_roles()
            nested_roles = NestedRoles(inst, root_suffix)
            disabled_role = nested_roles.get("nsDisabledRole")
            inact_containers = nsContainers(inst, basedn=root_suffix)
            inact_container = inact_containers.get('nsAccountInactivationTmp')

            cos_templates = CosTemplates(inst, inact_container.dn)
            cos_template = cos_templates.get(f'{disabled_role.dn}')
            cos_template.present('cosPriority', '1')
            cos_template.present('nsAccountLock', 'true')

            cos_classic_defs = CosClassicDefinitions(inst, root_suffix)
            cos_classic_def = cos_classic_defs.get('nsAccountInactivation_cos')
            cos_classic_def.present('cosAttribute', 'nsAccountLock operational')
            cos_classic_def.present('cosTemplateDn', inact_container.dn)
            cos_classic_def.present('cosSpecifier', 'nsRole')
        except ldap.NO_SUCH_OBJECT:
            return self._format_status_message(RoleState.PROBABLY_ACTIVATED)

        for role, parent in disabled_roles.items():
            if str.lower(self.dn) == str.lower(role.dn):
                if parent is None:
                    return self._format_status_message(RoleState.DIRECTLY_LOCKED)
                else:
                    return self._format_status_message(RoleState.INDIRECTLY_LOCKED, parent)

        return self._format_status_message(RoleState.ACTIVATED)

    def lock(self):
        """Set the entry dn to nsDisabledRole and ensure it exists"""

        current_status = self.status()
        if current_status["state"] == RoleState.DIRECTLY_LOCKED:
            raise ValueError(f"Role is already {current_status['state'].describe()}")

        inst = self._instance

        mapping_trees = MappingTrees(inst)
        root_suffix = ""
        root_suffix = mapping_trees.get_root_suffix_by_entry(self.dn)

        if root_suffix:
            managed_roles = ManagedRoles(inst, root_suffix)
            managed_role = managed_roles.ensure_state(properties={"cn": "nsManagedDisabledRole"})
            nested_roles = NestedRoles(inst, root_suffix)
            try:
                disabled_role = nested_roles.get("nsDisabledRole")
            except ldap.NO_SUCH_OBJECT:
                # We don't use "ensure_state" because we want to preserve the existing attributes
                disabled_role = nested_roles.create(properties={"cn": "nsDisabledRole",
                                                                "nsRoleDN": managed_role.dn})
            disabled_role.add("nsRoleDN", self.dn)

            inact_containers = nsContainers(inst, basedn=root_suffix)
            inact_container = inact_containers.ensure_state(properties={'cn': 'nsAccountInactivationTmp'})

            cos_templates = CosTemplates(inst, inact_container.dn)
            cos_templates.ensure_state(properties={'cosPriority': '1',
                                                   'nsAccountLock': 'true',
                                                   'cn': f'{disabled_role.dn}'})

            cos_classic_defs = CosClassicDefinitions(inst, root_suffix)
            cos_classic_defs.ensure_state(properties={'cosAttribute': 'nsAccountLock operational',
                                                      'cosSpecifier': 'nsRole',
                                                      'cosTemplateDn': inact_container.dn,
                                                      'cn': 'nsAccountInactivation_cos'})

    def unlock(self):
        """Remove the entry dn from nsDisabledRole if it exists"""

        inst = self._instance
        current_status = self.status()
        if current_status["state"] == RoleState.ACTIVATED:
            raise ValueError("Role is already active")

        mapping_trees = MappingTrees(inst)
        root_suffix = mapping_trees.get_root_suffix_by_entry(self.dn)
        roles = NestedRoles(inst, root_suffix)
        try:
            disabled_role = roles.get("nsDisabledRole")
            # Still we want to ensure that it is not locked directly too
            disabled_role.ensure_removed("nsRoleDN", self.dn)
        except ldap.NO_SUCH_OBJECT:
            pass

        # Notify if it's locked indirectly
        if current_status["state"] == RoleState.INDIRECTLY_LOCKED:
            raise ValueError(f"Role is {current_status['state'].describe(current_status['role_dn'])}. Please, deal with it separately")


class Roles(DSLdapObjects):
    """DSLdapObjects that represents all Roles entries

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(Roles, self).__init__(instance)
        self._objectclasses = [
            'top',
            'LDAPsubentry',
            'nsRoleDefinition',
        ]
        self._filterattrs = ['cn']
        self._basedn = basedn
        self._childobject = Role

    def get_with_type(self, selector=[], dn=None):
        """Get the correct role type

        :param dn: DN of wanted entry
        :type dn: str
        :param selector: An additional filter to search for, i.e. 'backend_name'. The attributes
                         selected are based on object type, ie user will search for uid and cn.
        :type dn: str

        :returns: FilteredRole, ManagedRole or NestedRole
        """

        ROLE_OBJECTCLASSES = {FilteredRole: ['nscomplexroledefinition',
                                             'nsfilteredroledefinition'],
                              ManagedRole: ['nssimpleroledefinition',
                                            'nsmanagedroledefinition'],
                              NestedRole: ['nscomplexroledefinition',
                                           'nsnestedroledefinition']}
        entry = self.get(selector=selector, dn=dn, json=False)
        entry_objectclasses = entry.get_attr_vals_utf8_l("objectClass")
        role_found = False
        for role, objectclasses in ROLE_OBJECTCLASSES.items():
            role_found = all(oc in entry_objectclasses for oc in objectclasses)
            if role_found:
                return role(self._instance, entry.dn)
        if not role_found:
            raise ldap.NO_SUCH_OBJECT("Role definition was not found")

    def get_disabled_roles(self):
        """Get disabled roles that are usually defined in the cn=nsDisabledRole,ROOT_SUFFIX

        :returns: A dict {role: its_parent, }
        """

        disabled_role = self.get("nsDisabledRole")
        roles_inactive = {}
        result = {}

        # Do this on 0 level of nestedness
        for role_dn in disabled_role.get_attr_vals_utf8_l("nsRoleDN"):
            roles_inactive[role_dn] = None

        # We go through the list and check if the role is Nested and
        # then add its 'nsrole' attributes to the processing list
        while roles_inactive.items():
            processing_role_dn, parent = roles_inactive.popitem()
            # Check if already seen the role and skip it then
            if processing_role_dn in result.keys():
                continue

            processing_role = self.get_with_type(dn=processing_role_dn)
            if isinstance(processing_role, NestedRole):
                for role_dn in processing_role.get_attr_vals_utf8_l("nsRoleDN"):
                    # We don't need to process children which are already present in the list
                    if role_dn in result.keys() or role_dn in roles_inactive.keys():
                        continue
                    # We are deeper - return its children to the processing and assign the original parent
                    if parent in [role.dn for role in result.keys()]:
                        roles_inactive[role_dn] = parent
                    else:
                        roles_inactive[role_dn] = processing_role_dn
            # Set the processed role to list
            result[processing_role] = parent

        return result

class FilteredRole(Role):
    """A single instance of FilteredRole entry to create FilteredRole role

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(FilteredRole, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._create_objectclasses = ['nsComplexRoleDefinition', 'nsFilteredRoleDefinition']

        self._protected = False



class FilteredRoles(Roles):
    """DSLdapObjects that represents all filtered role entries

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(FilteredRoles, self).__init__(instance, basedn)
        self._objectclasses = ['LDAPsubentry', 'nsComplexRoleDefinition', 'nsFilteredRoleDefinition']
        self._filterattrs = ['cn']
        self._basedn = basedn
        self._childobject = FilteredRole


class ManagedRole(Role):
    """A single instance of Managed Role entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(ManagedRole, self).__init__(instance, dn)
        self._rdn_attribute = RDN
        self._create_objectclasses = ['nsSimpleRoleDefinition', 'nsManagedRoleDefinition']

        self._protected = False

class ManagedRoles(Roles):
    """DSLdapObjects that represents all Managed Roles entries

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    :param rdn: The DN that will be combined wit basedn
    :type rdn: str
    """

    def __init__(self, instance, basedn):
        super(ManagedRoles, self).__init__(instance, basedn)
        self._objectclasses = ['LDAPsubentry', 'nsSimpleRoleDefinition', 'nsManagedRoleDefinition']
        self._filterattrs = ['cn']
        self._basedn = basedn
        self._childobject = ManagedRole


class NestedRole(Role):
    """A single instance of Nested Role entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(NestedRole, self).__init__(instance, dn)
        self._must_attributes = MUST_ATTRIBUTES_NESTED
        self._rdn_attribute = RDN
        self._create_objectclasses = ['nsComplexRoleDefinition', 'nsNestedRoleDefinition']

        self._protected = False

class NestedRoles(Roles):
    """DSLdapObjects that represents all NestedRoles entries in suffix.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    :param rdn: The DN that will be combined wit basedn
    :type rdn: str
    """

    def __init__(self, instance, basedn):
        super(NestedRoles, self).__init__(instance, basedn)
        self._objectclasses = ['LDAPsubentry', 'nsComplexRoleDefinition', 'nsNestedRoleDefinition']
        self._filterattrs = ['cn']
        self._basedn = basedn
        self._childobject = NestedRole
