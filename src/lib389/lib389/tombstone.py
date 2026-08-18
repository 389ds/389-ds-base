# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import ldap
from lib389._entry import Entry
from lib389._constants import DIRSRV_STATE_ONLINE
from lib389._mapped_object import DSLdapObject, DSLdapObjects, _gen_and, _gen_filter, _term_gen
from lib389.utils import ensure_bytes


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
        self._entry_rdn = self._dn.split(',')[1]
        self._grandparent_dn = self._dn.split(',', 2)[-1]
        # We need to always add this filter, else we won't see the ts
        self._object_filter = '(&(objectclass=nsTombStone)({}))'.format(self._entry_rdn)

    def _unsafe_raw_entry(self):
        """Get an Entry object

        :returns: Entry object
        """

        return self._instance.search_ext_s(self._grandparent_dn, ldap.SCOPE_SUBTREE, self._object_filter, attrlist=["*"],
                                           serverctrls=self._server_controls, clientctrls=self._client_controls)[0]

    def exists(self):
        """Check if the entry exists

        :returns: True if it exists
        """

        try:
            self._instance.search_ext_s(self._grandparent_dn, ldap.SCOPE_SUBTREE, self._object_filter, attrsonly=1,
                                        serverctrls=self._server_controls, clientctrls=self._client_controls)
        except ldap.NO_SUCH_OBJECT:
            return False

        return True

    def display(self):
        """Get an entry but represent it as a string LDIF

        :returns: LDIF formatted string
        """

        e = self._instance.search_ext_s(self._grandparent_dn, ldap.SCOPE_SUBTREE, self._object_filter, attrlist=["*"],
                                        serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
        return e.__repr__()

    def present(self, attr, value=None):
        """Assert that some attr, or some attr / value exist on the entry.

        :param attr: an attribute name
        :type attr: str
        :param value: an attribute value
        :type value: str

        :returns: True if attr is present
        """

        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get presence on instance that is not ONLINE")
        self._log.debug("%s present(%r) %s", self._dn, attr, value)

        self._instance.search_ext_s(self._grandparent_dn, ldap.SCOPE_SUBTREE, self._object_filter, attrlist=[attr, ],
                                    serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
        values = self.get_attr_vals_bytes(attr)
        self._log.debug("%s contains %s", self._dn, values)

        if value is None:
            # We are just checking if SOMETHING is present ....
            return len(values) > 0
        else:
            # Check if a value really does exist.
            return ensure_bytes(value).lower() in [x.lower() for x in values]

    def get_all_attrs(self, use_json=False):
        """Get a dictionary having all the attributes of the entry

        :returns: Dict with real attributes and operational attributes
        """

        self._log.debug("%s get_all_attrs", self._dn)
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
        else:
            # retrieving real(*) and operational attributes(+)
            attrs_entry = self._instance.search_ext_s(self._grandparent_dn, ldap.SCOPE_SUBTREE, self._object_filter, attrlist=["*", "+"],
                                                      serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
            # getting dict from 'entry' object
            attrs_dict = attrs_entry.data
            return attrs_dict

    def get_attrs_vals(self, keys, use_json=False):
        self._log.debug("%s get_attrs_vals(%r)", self._dn, keys)
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
        else:
            entry = self._instance.search_ext_s(self._grandparent_dn, ldap.SCOPE_SUBTREE, self._object_filter, attrlist=keys,
                                                serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
            return entry.getValuesSet(keys)

    def get_attr_vals(self, key, use_json=False):
        self._log.debug("%s get_attr_vals(%r)", self._dn, key)
        # We might need to add a state check for NONE dn.
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we
            # can use get on dse.ldif to get values offline.
        else:
            # It would be good to prevent the entry code intercepting this ....
            # We have to do this in this method, because else we ignore the scope base.
            entry = self._instance.search_ext_s(self._grandparent_dn, ldap.SCOPE_SUBTREE, self._object_filter, attrlist=[key],
                                                serverctrls=self._server_controls, clientctrls=self._client_controls)[0]

            vals = entry.getValues(key)
            if use_json:
                result = {key: []}
                for val in vals:
                    result[key].append(val)
                return result
            else:
                return vals

    def get_attr_val(self, key, use_json=False):
        self._log.debug("%s getVal(%r)", self._dn, key)
        # We might need to add a state check for NONE dn.
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we
            # can use get on dse.ldif to get values offline.
        else:
            entry = self._instance.search_ext_s(self._grandparent_dn, ldap.SCOPE_SUBTREE, self._object_filter, attrlist=[key],
                                                serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
            return entry.getValue(key)

    def revive(self):
        """Revive this object within the tree.

        This duplicates "as much as possible", excluding some internal attributes.
        """

        orig_dn = self.get_attr_val_utf8('nscpEntryDN')
        self._log.info("Reviving %s -> %s", self.dn, orig_dn)
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
    unique to individual suppliers, and may or may not correlate
    to tombstones on other suppliers.

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

