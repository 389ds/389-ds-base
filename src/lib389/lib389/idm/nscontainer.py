# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023, Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects


class nsContainer(DSLdapObject):
    """A single instance of a nsContainer. This is similar to OU
    for organization of a directory tree.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(nsContainer, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._create_objectclasses = [
            'top',
            'nscontainer',
        ]
        self._protected = False


class nsContainers(DSLdapObjects):
    """The set of nsContainers on the server.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(nsContainers, self).__init__(instance)
        self._objectclasses = [
            'nscontainer',
        ]
        self._filterattrs = ['cn']
        self._childobject = nsContainer
        self._basedn = basedn


class nsHiddenContainer(DSLdapObject):
    """A single instance of a hidden container. This is a combination
    of nsContainer and ldapsubentry.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(nsHiddenContainer, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._create_objectclasses = [
            'top',
            'nscontainer',
            'ldapsubentry',
        ]
        self._protected = False


class nsHiddenContainers(DSLdapObjects):
    """The set of nsHiddenContainers on the server.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(nsHiddenContainers, self).__init__(instance)
        self._objectclasses = [
            'nscontainer',
            'ldapsubentry',
        ]
        self._filterattrs = ['cn']
        self._childobject = nsHiddenContainer
        self._basedn = basedn
