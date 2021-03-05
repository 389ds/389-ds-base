# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import ldap
from lib389._mapped_object import DSLdapObject, DSLdapObjects, _gen_filter
from lib389.utils import is_a_dn


class ConflictEntry(DSLdapObject):
    """A replication conflict entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: The DN of the conflict entry
    :type dn: str
    """
    def __init__(self, instance, dn=None):
        super(ConflictEntry, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._create_objectclasses = ['ldapsubentry']
        self._protected = False
        self._object_filter = '(objectclass=ldapsubentry)'

    def convert(self, new_rdn):
        """Convert conflict entry to a valid entry, but we need to
        give the conflict entry a new rdn since we are not replacing
        the existing valid counterpart entry.
        """

        if not is_a_dn(new_rdn):
            raise ValueError("The new RDN (" + new_rdn + ") is not a valid DN")

        # Get the conflict entry info
        conflict_value = self.get_attr_val_utf8('nsds5ReplConflict')
        entry_dn = conflict_value.split(' ', 2)[2]
        entry_rdn = ldap.explode_dn(entry_dn, 1)[0]
        rdn_attr = entry_dn.split('=', 1)[0]

        # Rename conflict entry
        self.rename(new_rdn, deloldrdn=False)

        # Cleanup entry
        self.remove(rdn_attr, entry_rdn)
        if self.present('objectclass', 'ldapsubentry'):
            self.remove('objectclass', 'ldapsubentry')
        self.remove_all('nsds5ReplConflict')

    def swap(self):
        """Make the conflict entry the real valid entry.  Delete old valid entry,
        and rename the conflict
        """

        # Get the conflict entry info
        conflict_value = self.get_attr_val_utf8('nsds5ReplConflict')
        entry_dn = conflict_value.split(' ', 2)[2]
        entry_rdn = ldap.explode_dn(entry_dn, 1)[0]

        # Gather the RDN details
        rdn_attr = entry_dn.split('=', 1)[0]
        new_rdn = "{}={}".format(rdn_attr, entry_rdn)
        tmp_rdn = new_rdn + 'tmp'

        # Delete valid entry and its children (to be replaced by conflict entry)
        original_entry = DSLdapObject(self._instance, dn=entry_dn)
        original_entry._protected = False
        filterstr = "(|(objectclass=*)(objectclass=ldapsubentry))"
        ents = self._instance.search_s(original_entry._dn, ldap.SCOPE_SUBTREE, filterstr, escapehatch='i am sure')
        for ent in sorted(ents, key=lambda e: len(e.dn), reverse=True):
            self._instance.delete_ext_s(ent.dn, serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')

        # Rename conflict entry to tmp rdn so we can clean up the rdn attr
        self.rename(tmp_rdn, deloldrdn=False)

        # Cleanup entry
        self.remove(rdn_attr, entry_rdn)
        if self.present('objectclass', 'ldapsubentry'):
            self.remove('objectclass', 'ldapsubentry')
        self.remove_all('nsds5ReplConflict')

        # Rename to the final/correct rdn
        self.rename(new_rdn, deloldrdn=True)

    def get_valid_entry(self):
        """Get the conflict entry's valid counterpart entry
        """
        # Get the conflict entry info
        conflict_value = self.get_attr_val_utf8('nsds5ReplConflict')
        entry_dn = conflict_value.split(' ', 2)[2]

        # Get the valid entry
        return DSLdapObject(self._instance, dn=entry_dn)


class ConflictEntries(DSLdapObjects):
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
        super(ConflictEntries, self).__init__(instance)
        self._objectclasses = ['ldapsubentry']
        # Try some common ones ....
        self._filterattrs = ['nsds5replconflict', 'objectclass']
        self._childobject = ConflictEntry
        self._basedn = basedn

    def _get_objectclass_filter(self):
        return "(&(objectclass=ldapsubentry)(nsds5replconflict=*))"


class GlueEntry(DSLdapObject):
    """A replication glue entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: The DN of the conflict entry
    :type dn: str
    """
    def __init__(self, instance, dn=None):
        super(GlueEntry, self).__init__(instance, dn)
        self._rdn_attribute = ''
        self._create_objectclasses = ['glue']
        self._protected = False
        self._object_filter = '(objectclass=glue)'

    def convert(self):
        """Convert entry into real entry
        """
        self.remove_all('nsds5replconflict')
        self.remove('objectclass', 'glue')

    def delete_all(self):
        """Remove glue entry and its children.  Depending on the situation the URP
        mechanism can turn the parent glue entry into a tombstone before we get
        a chance to delete it.  This results in a NO_SUCH_OBJECT exception
        """
        delete_count = 0
        filterstr = "(|(objectclass=*)(objectclass=ldapsubentry))"
        ents = self._instance.search_s(self._dn, ldap.SCOPE_SUBTREE, filterstr, escapehatch='i am sure')
        for ent in sorted(ents, key=lambda e: len(e.dn), reverse=True):
            try:
                self._instance.delete_ext_s(ent.dn, serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')
                delete_count += 1
            except ldap.NO_SUCH_OBJECT as e:
                if len(ents) > 0 and delete_count == (len(ents) - 1):
                    # This is the parent glue entry - it was removed by URP
                    pass
                else:
                    raise e


class GlueEntries(DSLdapObjects):
    """Represents the set of glue entries that may exist on
    this replica.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Tree to search for tombstones in
    :type basedn: str
    """
    def __init__(self, instance, basedn):
        super(GlueEntries, self).__init__(instance)
        self._objectclasses = ['glue']
        # Try some common ones ....
        self._filterattrs = ['nsds5replconflict', 'objectclass']
        self._childobject = GlueEntry
        self._basedn = basedn

    def _get_objectclass_filter(self):
        return _gen_filter(['objectclass'], ['glue'])
