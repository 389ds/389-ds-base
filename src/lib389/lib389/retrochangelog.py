# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Retro Changelog entry helpers.

These classes wrap entries under the Retro Changelog suffix (cn=changelog).
This is NOT the replication changelog, and NOT the Retro Changelog plugin
config entry under cn=plugins,cn=config.
"""

from lib389._constants import RETROCL_SUFFIX
from lib389._mapped_object import DSLdapObject, DSLdapObjects


class RetroChangelogEntry(DSLdapObject):
    """A single Retro Changelog entry (objectClass changeLogEntry).

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(RetroChangelogEntry, self).__init__(instance, dn)
        self._rdn_attribute = 'changeNumber'
        self._must_attributes = ['changeNumber', 'targetDN', 'changeType']
        self._create_objectclasses = ['top', 'changeLogEntry']
        # Entries are created by the plugin; keep protected by default
        self._protected = True


class RetroChangelogEntries(DSLdapObjects):
    """Container for Retro Changelog entries under cn=changelog.

    Example::

        from lib389.retrochangelog import RetroChangelogEntries
        entries = RetroChangelogEntries(inst)
        hits = entries.filter('(targetDN=uid=user1,ou=people,dc=example,dc=com)')

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Search base (default: cn=changelog)
    :type basedn: str
    """

    def __init__(self, instance, basedn=None):
        super(RetroChangelogEntries, self).__init__(instance)
        self._objectclasses = ['changeLogEntry']
        self._filterattrs = ['changeNumber', 'targetDN']
        self._childobject = RetroChangelogEntry
        self._basedn = basedn if basedn is not None else RETROCL_SUFFIX
