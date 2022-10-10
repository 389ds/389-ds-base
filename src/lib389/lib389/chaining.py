# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389._constants import DN_CHAIN
from lib389.properties import BACKEND_OBJECTCLASS_VALUE
from lib389.monitor import MonitorChaining
from lib389.mappingTree import MappingTrees
from lib389._mapped_object import DSLdapObjects, DSLdapObject
from lib389.backend import Backends
from lib389.rootdse import RootDSE


class ChainingConfig(DSLdapObject):
    """Chaining Config DSLdapObject with:
    - must attributes = ['cn']
    - RDN attribute is 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    _must_attributes = ['cn']

    def __init__(self, instance, dn=None):
        super(ChainingConfig, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._create_objectclasses = ['top', 'extensibleObject']
        self._protected = True
        self._dn = "cn=config,cn=chaining database,cn=plugins,cn=config"

    def get_controls(self):
        """Get a list of the supported controls from the root DSE entry
        :return list of OIDs
        """
        rootdse = RootDSE(self._instance)
        ctrls = rootdse.get_supported_ctrls()
        ctrls.sort()
        return ctrls

    def get_comps(self):
        """Return a list of the available plugin components
        :return list of plugin components
        """
        comps = self.get_attr_vals_utf8_l('nspossiblechainingcomponents')
        comps.sort()
        return comps


class ChainingDefault(DSLdapObject):
    """Chaining Default Config settings DSLdapObject with:
    - must attributes = ['cn']
    - RDN attribute is 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    _must_attributes = ['cn']

    def __init__(self, instance, dn=None):
        super(ChainingDefault, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._create_objectclasses = ['top', 'extensibleObject']
        self._protected = True
        self._dn = "cn=default instance config,cn=chaining database,cn=plugins,cn=config"


class ChainingLink(DSLdapObject):
    """Chaining Backend DSLdapObject with:
    - must attributes = ['cn', 'nsslapd-suffix', 'nsmultiplexorbinddn',
                         'nsmultiplexorcredentials', 'nsfarmserverurl'
    - RDN attribute is 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    _must_attributes = ['nsslapd-suffix', 'cn']

    def __init__(self, instance, dn=None, rdn=None):
        super(ChainingLink, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['nsslapd-suffix', 'cn', 'nsmultiplexorbinddn',
                                 'nsmultiplexorcredentials', 'nsfarmserverurl']
        self._create_objectclasses = ['top', 'extensibleObject', BACKEND_OBJECTCLASS_VALUE]
        self._protected = False
        self._basedn = "cn=chaining database,cn=plugins,cn=config"
        self._mts = MappingTrees(self._instance)

    def get_monitor(self):
        """Get a MonitorChaining(DSLdapObject) for the chaining link
        :returns - chaining monitor entry
        """
        return MonitorChaining(instance=self._instance, dn="cn=monitor,%s" % self._dn)

    def del_link(self):
        """
        Remove the link from the parent suffix backend entry
        Delete chaining monitor entry
        Delete chaining entry
        """

        rdn = self.get_attr_val_utf8_l('nsslapd-suffix')
        try:
            mt = self._mts.get(selector=rdn)
            mt.delete()
        except ldap.NO_SUCH_OBJECT:
            # Righto, it's already gone! Do nothing ...
            pass

        # Delete the monitoring entry
        monitor = self.get_monitor()
        monitor.delete()

        # Delete the link
        self.delete()

    def create(self, rdn=None, properties=None, basedn=None):
        """Create the link entry, and the mapping tree entry(if needed)
        """

        # Create chaining entry
        super(ChainingLink, self).create(rdn, properties, basedn)

        # Create mapping tree entry
        dn_comps = ldap.explode_dn(properties['nsslapd-suffix'][0])
        parent_suffix = ','.join(dn_comps[1:])
        mt_properties = {
            'cn': properties['nsslapd-suffix'][0],
            'nsslapd-state': 'backend',
            'nsslapd-backend': properties['cn'][0],
            'nsslapd-parent-suffix': parent_suffix
        }
        try:
            self._mts.ensure_state(properties=mt_properties)
        except ldap.ALREADY_EXISTS:
            pass


class ChainingLinks(DSLdapObjects):
    """DSLdapObjects that represents DN_LDBM base DN
    This only does ldbm backends. Chaining backends are a special case
    of this, so they can be subclassed off.

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance, basedn=None):
        # Basedn has to be here, despite not being used to satisfy
        # cli_base _generic_create.
        super(ChainingLinks, self).__init__(instance=instance)
        self._objectclasses = [BACKEND_OBJECTCLASS_VALUE]
        self._filterattrs = ['cn', 'nsslapd-suffix', 'nsslapd-directory']
        self._childobject = ChainingLink
        self._basedn = DN_CHAIN

    def add_link(self, props):
        """
        Create chaining entry
        Add link to parent suffix backend entry
        """
        self.create(properties=props)
        suffix = props['nsslapd-suffix'][0].lower()
        rdn = props['cn'][0]
        be_insts = Backends(self._instance).list()
        for be in be_insts:
            be_suffix = be.get_attr_val_utf8_l('nsslapd-suffix')
            if suffix == be_suffix:
                # Add chaining link as backend under the suffix
                be.add('nsslapd-backend', rdn)
