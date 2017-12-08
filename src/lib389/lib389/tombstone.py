# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import ldap

from lib389._entry import Entry

from lib389._mapped_object import DSLdapObject, DSLdapObjects, _gen_and, _gen_filter, _term_gen

class Tombstone(DSLdapObject):
    """A tombstone is created during a conflict or a delete in a
    replicated environment. It can be useful to access these, to
    see conflicts, or to restore deleted entries in some cases.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: The DN of the tombstone
    :type dn: str
    """
    def __init__(self, instance, dn=None):
        super(Tombstone, self).__init__(instance, dn)
        self._rdn_attribute = 'nsUniqueId'
        self._create_objectclasses = ['nsTombStone']
        self._protected = True
        # We need to always add this filter, else we won't see the ts
        self._object_filter = '(objectclass=nsTombStone)'

    def revive(self):
        """Revive this object within the tree.

        This duplicates "as much as possible", excluding some internal attributes.
        """
        orig_dn = self.get_attr_val_utf8('nscpEntryDN')
        self._log.info("Reviving %s -> %s" % (self.dn, orig_dn))
        # Get all our attributes
        properties = self.get_all_attrs()
        properties.pop('nsuniqueid', None)
        properties.pop('modifiersname', None)
        properties.pop('createtimestamp', None)
        properties.pop('creatorsname', None)
        properties.pop('modifytimestamp', None)
        properties.pop('entryid', None)
        properties.pop('entrydn', None)
        properties.pop('parentid', None)
        properties.pop('nsparentuniqueid', None)
        properties.pop('nstombstonecsn', None)
        properties.pop('nscpentrydn', None)
        properties['objectclass'].remove(b'nsTombstone')

        e = Entry(orig_dn)
        e.update(properties)
        self._instance.add_ext_s(e, serverctrls=self._server_controls, clientctrls=self._client_controls)

class Tombstones(DSLdapObjects):
    """Represents the set of tombstone objects that may exist on
    this replica. Tombstones are locally generated, so they are
    unique to individual masters, and may or may not correlate
    to tombstones on other masters.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Tree to search for tombstones in
    :type basedn: str
    """
    def __init__(self, instance, basedn):
        super(Tombstones, self).__init__(instance)
        self._objectclasses = ['nstombstone']
        # Try some common ones ....
        self._filterattrs = ['nsUniqueId', 'cn', 'uid', 'ou']
        self._childobject = Tombstone
        self._basedn = basedn

    # This gives us the ruv exclusion.
    def _get_objectclass_filter(self):
        """An internal function to help find tombstones. They require special
        additions to filters, and this is part of the DSLdapObjects framework
        that we can emit these for inclusion in our searches.

        Internal Only.
        """
        return _gen_and(
            _gen_filter(_term_gen('objectclass'), self._objectclasses, extra='(!(nsUniqueId=ffffffff-ffffffff-ffffffff-ffffffff))')
        )

