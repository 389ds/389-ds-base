# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from ldap import dn

from lib389.idm.domain import Domain
from lib389.idm.organization import Organization
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.nscontainer import nsContainer
from lib389.utils import ensure_str


def create_base_domain(instance, basedn):
    """Create the base domain object"""

    domain = Domain(instance, dn=basedn)
    # Explode the dn to get the first bit.
    avas = dn.str2dn(basedn)
    dc_ava = avas[0][0][1]

    domain.create(properties={
        # I think in python 2 this forces unicode return ...
        'dc': dc_ava,
        'description': basedn,
    })
    # ACI can be added later according to your needs

    return domain


def create_base_org(instance, basedn):
    """Create the base organization object"""

    org = Organization(instance, dn=basedn)
    # Explode the dn to get the first bit.
    avas = dn.str2dn(basedn)
    o_ava = avas[0][0][1]

    org.create(properties={
        # I think in python 2 this forces unicode return ...
        'o': o_ava,
        'description': basedn,
    })

    return org


def create_base_orgunit(instance, basedn):
    """Create the base org unit object for a org unit"""

    orgunit = OrganizationalUnit(instance, dn=basedn)
    # Explode the dn to get the first bit.
    avas = dn.str2dn(basedn)
    ou_ava = avas[0][0][1]

    orgunit.create(properties={
        # I think in python 2 this forces unicode return ...
        'ou': ou_ava,
        'description': basedn,
    })

    return orgunit


def create_base_cn(instance, basedn):
    """Create the base nsContainer object"""

    cn = nsContainer(instance, dn=basedn)
    # Explode the dn to get the first bit.
    avas = dn.str2dn(basedn)
    cn_ava = avas[0][0][1]

    cn.create(properties={
        # I think in python 2 this forces unicode return ...
        'cn': cn_ava,
        'description': basedn,
    })

    return cn


class sampleentries(object):
    def __init__(self, instance, basedn):
        self._instance = instance
        self._basedn = ensure_str(basedn)
        self.description = None
        self.version = None

    def apply(self):
        self._apply()

    def _configure_base(self):
        suffix_rdn_attr = self._basedn.split('=')[0].lower()
        suffix_obj = None
        if suffix_rdn_attr == 'dc':
            suffix_obj = create_base_domain(self._instance, self._basedn)
            aci_vals = ['dc', 'domain']
        elif suffix_rdn_attr == 'o':
            suffix_obj = create_base_org(self._instance, self._basedn)
            aci_vals = ['o', 'organization']
        elif suffix_rdn_attr == 'ou':
            suffix_obj = create_base_orgunit(self._instance, self._basedn)
            aci_vals = ['ou', 'organizationalunit']
        elif suffix_rdn_attr == 'cn':
            suffix_obj = create_base_cn(self._instance, self._basedn)
            aci_vals = ['cn', 'nscontainer']
        else:
            # Unsupported rdn
            raise ValueError("Suffix RDN is not supported for creating sample entries.  Only 'dc', 'o', 'ou', and 'cn' are supported.")

        return suffix_obj

    def _apply(self):
        raise Exception('Not implemented')


