# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import copy
import os
from stat import ST_MODE
from lib389.paths import Paths
from lib389.lint import DSPERMLE0001, DSPERMLE0002

class DSEldif(object):
    """A class for working with dse.ldif file

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance, serverid=None):
        self._instance = instance
        self._contents = []

        if serverid:
            # Get the dse.ldif from the instance name
            prefix = os.environ.get('PREFIX', ""),
            serverid = serverid.replace("slapd-", "")
            self.path = "{}/etc/dirsrv/slapd-{}/dse.ldif".format(prefix[0], serverid)
        else:
            ds_paths = Paths(self._instance.serverid, self._instance)
            self.path = os.path.join(ds_paths.config_dir, 'dse.ldif')

        with open(self.path, 'r') as file_dse:
            for line in file_dse.readlines():
                if line.startswith('dn'):
                    self._contents.append(line.lower())
                else:
                    self._contents.append(line)

    def _update(self):
        """Update the dse.ldif with a new contents"""

        with open(self.path, "w") as file_dse:
            file_dse.write("".join(self._contents))

    def _find_attr(self, entry_dn, attr):
        """Find all attribute values and indexes under a given entry

        Returns entry dn index and attribute data dict:
        relative attribute indexes and the attribute value
        """

        entry_dn_i = self._contents.index("dn: {}\n".format(entry_dn.lower()))
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
            raise ValueError("Attribute {} wasn't found under dn: {}".format(attr, entry_dn.lower()))

        return entry_dn_i, attr_data

    def get(self, entry_dn, attr, single=False):
        """Return attribute values under a given entry

        :param entry_dn: a DN of entry we want to get attribute from
        :type entry_dn: str
        :param attr: an attribute name
        :type attr: str
        :param single: Return a single value instead of a list
        :type sigle: boolean
        """

        try:
            _, attr_data = self._find_attr(entry_dn, attr)
        except ValueError:
            return None

        vals = list(attr_data.values())
        if single:
            return vals[0] if len(vals) > 0 else None
        return vals

    def add(self, entry_dn, attr, value):
        """Add an attribute under a given entry

        :param entry_dn: a DN of entry we want to edit
        :type entry_dn: str
        :param attr: an attribute name
        :type attr: str
        :param value: an attribute value
        :type value: str
        """

        entry_dn_i = self._contents.index("dn: {}\n".format(entry_dn.lower()))
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


class FSChecks(object):
    """This is for the healthcheck feature, check commonly used system config files the
    server uses.  This is here for lack of a better place to add this class.
    """
    def __init__(self, dirsrv=None):
        self.dirsrv = dirsrv
        self._certdb = self.dirsrv.get_cert_dir()
        self.ds_files = [
            ('/etc/resolv.conf', '644', DSPERMLE0001),
            (self._certdb + "/pin.txt", '600', DSPERMLE0002),
            (self._certdb + "/pwdfile.txt", '600', DSPERMLE0002),
        ]
        self._lint_functions = [self._lint_file_perms]

    def lint(self):
        results = []
        for fn in self._lint_functions:
            for result in fn():
                if result is not None:
                    results.append(result)
        return results

    def _lint_file_perms(self):
        # Check file permissions are correct
        for ds_file in self.ds_files:
            perms = str(oct(os.stat(ds_file[0])[ST_MODE])[-3:])
            if perms != ds_file[1]:
                report = copy.deepcopy(ds_file[2])
                report['items'].append(ds_file[0])
                report['detail'] = report['detail'].replace('FILE', ds_file[0])
                report['detail'] = report['detail'].replace('PERMS', ds_file[1])
                report['fix'] = report['fix'].replace('FILE', ds_file[0])
                report['fix'] = report['fix'].replace('PERMS', ds_file[1])
                yield report
