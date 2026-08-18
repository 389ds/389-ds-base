# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

# Implement types for COS handling with lib389 and DS.


from lib389._mapped_object import DSLdapObject, DSLdapObjects

from lib389.utils import ensure_str


class CosTemplate(DSLdapObject):
    """A Cos Template defining the values to override on a target.

    :param instance: DirSrv instance
    :type instance: DirSrv
    :param dn: The dn of the template
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(CosTemplate, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        # This is the ONLY TIME i'll allow extensible object ...
        # You have been warned ...
        self._create_objectclasses = [
            'top',
            'cosTemplate',
            'extensibleObject',
        ]
        self._protected = False


class CosTemplates(DSLdapObjects):
    """The set of costemplates that exist for direct and indirect
    implementations.

    :param instance: A dirsrv instance
    :type instance: DirSrv
    :param basedn: The basedn of the templates
    :type basedn: str
    :param rdn: The rdn of the templates
    :type rdn: str
    """

    def __init__(self, instance, basedn, rdn=None):
        super(CosTemplates, self).__init__(instance)
        self._objectclasses = [
            'cosTemplate'
        ]
        self._filterattrs = ['cn']
        self._childobject = CosTemplate
        self._basedn = basedn
        if rdn is not None:
            self._basedn = '{},{}'.format(ensure_str(rdn), ensure_str(basedn))


class CosIndirectDefinition(DSLdapObject):
    """A Cos Indirect Definition associating an attr:value pair as a link
    attr to a template type.

    :param instance: DirSrv instance
    :type instance: DirSrv
    :param dn: The dn of the template
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(CosIndirectDefinition, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn', 'cosIndirectSpecifier', 'cosattribute']
        self._create_objectclasses = [
            'top',
            'ldapsubentry',
            'cosSuperDefinition',
            'cosIndirectDefinition',
        ]
        self._protected = False


class CosIndirectDefinitions(DSLdapObjects):
    """The set of cos indirect definitions that exist.

    :param instance: A dirsrv instance
    :type instance: DirSrv
    :param basedn: The basedn of the templates
    :type basedn: str
    :param rdn: The rdn of the templates
    :type rdn: str
    """

    def __init__(self, instance, basedn, rdn=None):
        super(CosIndirectDefinitions, self).__init__(instance)
        self._objectclasses = [
            'top',
            'ldapsubentry',
            'cosSuperDefinition',
            'cosIndirectDefinition',
        ]
        self._filterattrs = ['cn']
        self._childobject = CosIndirectDefinition
        self._basedn = basedn
        if rdn is not None:
            self._basedn = '{},{}'.format(ensure_str(rdn), ensure_str(basedn))


class CosPointerDefinition(DSLdapObject):
    """A Cos Pointer Definition associating a dn syntax type as a link
    attr to a template type.

    :param instance: DirSrv instance
    :type instance: DirSrv
    :param dn: The dn of the template
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(CosPointerDefinition, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn', 'cosTemplateDn', 'cosAttribute']
        self._create_objectclasses = [
            'top',
            'ldapsubentry',
            'cosSuperDefinition',
            'cosPointerDefinition',
        ]
        self._protected = False


class CosPointerDefinitions(DSLdapObjects):
    """The set of cos pointer definitions that exist.

    :param instance: A dirsrv instance
    :type instance: DirSrv
    :param basedn: The basedn of the templates
    :type basedn: str
    :param rdn: The rdn of the templates
    :type rdn: str
    """

    def __init__(self, instance, basedn, rdn=None):
        super(CosPointerDefinitions, self).__init__(instance)
        self._objectclasses = [
            'top',
            'ldapsubentry',
            'cosSuperDefinition',
            'cosPointerDefinition',
        ]
        self._filterattrs = ['cn']
        self._childobject = CosPointerDefinition
        self._basedn = basedn
        if rdn is not None:
            self._basedn = '{},{}'.format(ensure_str(rdn), ensure_str(basedn))


class CosClassicDefinition(DSLdapObject):
    """A Cos CosClassicDefinition associating a dn syntax type as a link
        attr to a template type.

        :param instance: DirSrv instance
        :type instance: DirSrv
        :param dn: The dn of the template
        :type dn: str
        """
    def __init__(self, instance, dn=None):
        super(CosClassicDefinition, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._create_objectclasses = [
            'top',
            'ldapsubentry',
            'cossuperdefinition',
            'cosClassicDefinition',
        ]
        self._protected = False


class CosClassicDefinitions(DSLdapObjects):
    """The set of cos CosClassicDefinition  that exist.
    :param instance: A dirsrv instance
    :type instance: DirSrv
    :param basedn: The basedn of the templates
    :type basedn: str
    :param rdn: The rdn of the templates
    :type rdn: str
    """

    def __init__(self, instance, basedn):
        super(CosClassicDefinitions, self).__init__(instance)
        self._objectclasses = [
            'top',
            'ldapsubentry',
            'cossuperdefinition',
            'cosClassicDefinition',
        ]
        self._filterattrs = ['cn']
        self._childobject = CosClassicDefinition
        self._basedn = basedn
