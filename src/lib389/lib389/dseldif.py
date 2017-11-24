# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
from lib389.paths import Paths


class DSEldif(object):
    """A class for working with dse.ldif file

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance):
        self._instance = instance

        ds_paths = Paths(self._instance.serverid, self._instance)
        self.path = os.path.join(ds_paths.config_dir, 'dse.ldif')

        with open(self.path, 'r') as file_dse:
            self._contents = file_dse.readlines()

    def _update(self):
        """Update the dse.ldif with a new contents"""

        with open(self.path, "w") as file_dse:
            file_dse.write("".join(self._contents))

    def _find_attr(self, entry_dn, attr):
        """Find all attribute values and indexes under a given entry

        Returns entry dn index and attribute data dict:
        relative attribute indexes and the attribute value
        """

        entry_dn_i = self._contents.index("dn: {}\n".format(entry_dn))
        attr_data = {}

        # Find where the entry ends
        try:
            dn_end_i = self._contents[entry_dn_i:].index("\n")
        except ValueError:
            # We are in the end of the list
            dn_end_i = len(self._contents)

        entry_slice = self._contents[entry_dn_i:entry_dn_i + dn_end_i]

        # Find the attribute
        for line in entry_slice:
            if line.startswith("{}:".format(attr)):
                attr_value = line.split(" ", 1)[1][:-1]
                attr_data.update({entry_slice.index(line): attr_value})

        if not attr_data:
            raise ValueError("Attribute {} wasn't found under dn: {}".format(attr, entry_dn))

        return entry_dn_i, attr_data

    def get(self, entry_dn, attr):
        """Return attribute values under a given entry

        :param entry_dn: a DN of entry we want to get attribute from
        :type entry_dn: str
        :param attr: an attribute name
        :type attr: str
        """

        try:
            _, attr_data = self._find_attr(entry_dn, attr)
        except ValueError:
            return None

        return attr_data.values()

    def add(self, entry_dn, attr, value):
        """Add an attribute under a given entry

        :param entry_dn: a DN of entry we want to edit
        :type entry_dn: str
        :param attr: an attribute name
        :type attr: str
        :param value: an attribute value
        :type value: str
        """

        entry_dn_i = self._contents.index("dn: {}\n".format(entry_dn))
        self._contents.insert(entry_dn_i+1, "{}: {}\n".format(attr, value))
        self._update()

    def delete(self, entry_dn, attr, value=None):
        """Delete attributes under a given entry

        :param entry_dn: a DN of entry we want to edit
        :type entry_dn: str
        :param attr: an attribute name
        :type attr: str
        :param value: an attribute value
        :type value: str
        """

        entry_dn_i, attr_data = self._find_attr(entry_dn, attr)

        if value is not None:
            for attr_i, attr_value in attr_data.items():
                if attr_value == value:
                    del self._contents[entry_dn_i + attr_i]
        else:
            for attr_i in sorted(attr_data.keys(), reverse=True):
                del self._contents[entry_dn_i + attr_i]
        self._update()

    def replace(self, entry_dn, attr, value):
        """Replace attribute values with a new one under a given entry

        :param entry_dn: a DN of entry we want to edit
        :type entry_dn: str
        :param attr: an attribute name
        :type attr: str
        :param value: an attribute value
        :type value: str
        """

        try:
            self.delete(entry_dn, attr)
        except ValueError as e:
            self._instance.log.debug("During replace operation: {}".format(e))
        self.add(entry_dn, attr, value)
        self._update()

