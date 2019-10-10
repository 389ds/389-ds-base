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


class sampleentries(object):
    def __init__(self, instance, basedn):
        self._instance = instance
        self._basedn = ensure_str(basedn)
        self.description = None
        self.version = None

    def apply(self):
        self._apply()

    def _apply(self):
        raise Exception('Not implemented')


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
