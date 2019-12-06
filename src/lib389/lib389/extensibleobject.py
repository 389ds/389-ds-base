# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects
from lib389.utils import ensure_str

class UnsafeExtensibleObject(DSLdapObject):
    """A single instance of an extensible object. Extensible object by it's
    nature is unsafe, eliminating rules around attribute checking. It may
    cause unsafe or other unknown behaviour if not handled correctly.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(UnsafeExtensibleObject, self).__init__(instance, dn)
        self._rdn_attribute = "cn"
        # Can I generate these from schema?
        self._must_attributes = []
        self._create_objectclasses = [
            'top',
            'extensibleObject',
        ]
        self._protected = False

class UnsafeExtensibleObjects(DSLdapObjects):
    """DSLdapObjects that represents all extensible objects. Extensible Objects
    are unsafe in their nature, disabling many checks around schema and attribute
    handling. You should really really REALLY not use this unless you have specific
    needs for testing.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all group entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(UnsafeExtensibleObjects, self).__init__(instance)
        self._objectclasses = [
            'extensibleObject',
        ]
        self._filterattrs = ["cn"]
        self._childobject = UnsafeExtensibleObject
        self._basedn = ensure_str(basedn)
