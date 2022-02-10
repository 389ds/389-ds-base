# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import collections
import ldap
import copy
import os.path
from lib389.utils import *
from lib389._entry import Entry
from lib389.idm.nscontainer import nsContainers
from lib389._mapped_object import DSLdapObjects, DSLdapObject
from lib389._constants import DEFAULT_SUFFIX

class Rewriter(DSLdapObject):
    """A single instance of a rewriter entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(Rewriter, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['nsslapd-libpath']
        self._create_objectclasses = ['top', 'rewriterEntry']


class Rewriters(DSLdapObjects):
    """A DSLdapObjects entity which represents rewriter entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all account entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn=None):
        super(Rewriters, self).__init__(instance=instance)
        self._objectclasses = ['top', 'rewriterEntry']
        self._filterattrs = ['cn', 'nsslapd-libpath']
        self._childobject = Rewriter
        self._basedn = 'cn=rewriters,cn=config'
        self._list_attrlist = ['dn', 'nsslapd-libpath']


class AdRewriter(Rewriter):
    """An instance of AD rewriter entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn="cn=adrewriter,cn=rewriter,cn=config"):
        super(AdRewriter, self).__init__(instance, dn)
        self._configuration_dn = None
        self._schema_dn = None

    def create_containers(self, suffix):
        conf_conts = nsContainers(self._instance, suffix)
        conf_cont = conf_conts.ensure_state(properties={'cn': 'Configuration'})
        schema_conts = nsContainers(self._instance, conf_cont.dn)
        schema_cont = schema_conts.ensure_state(properties={'cn': 'Schema'})
        self._configuration_dn = conf_cont.dn
        self._schema_dn = schema_cont.dn

    def get_schema_dn(self):
        return self._schema_dn


class AdRewriters(Rewriters):
    """A DSLdapObjects entity which represents AD rewriter entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all account entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn=None):
        super(AdRewriters, self).__init__(instance=instance)
        self._childobject = AdRewriter


