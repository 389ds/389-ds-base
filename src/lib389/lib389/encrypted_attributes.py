# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


from lib389._mapped_object import DSLdapObjects, DSLdapObject


class EncryptedAttr(DSLdapObject):
    """Encrypted Attribute DSLdapObject with:
    - must attributes = ['cn', 'nsEncryptionAlgorithm']
    - RDN attribute is 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Encrypted Attribute DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(EncryptedAttr, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn', 'nsEncryptionAlgorithm']
        self._create_objectclasses = ['top', 'nsAttributeEncryption']
        self._protected = False


class EncryptedAttrs(DSLdapObjects):
    """DSLdapObjects that represents Encrypted Attribute

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: DN of suffix container.
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(EncryptedAttrs, self).__init__(instance=instance)
        self._objectclasses = ['nsAttributeEncryption']
        self._filterattrs = ['cn']
        self._childobject = EncryptedAttr
        self._basedn = basedn
