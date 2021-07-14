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
import base64
import time
import fnmatch
from struct import pack, unpack
from datetime import timedelta
from stat import ST_MODE
# from lib389.utils import print_nice_time
from lib389.paths import Paths
from lib389._mapped_object_lint import DSLint
from lib389.lint import (
    DSPERMLE0001,
    DSPERMLE0002,
    DSSKEWLE0001,
    DSSKEWLE0002,
    DSSKEWLE0003
)


class DSEldif(DSLint):
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
            if serverid.startswith("slapd-"):
                serverid = serverid.replace("slapd-", "", 1)
            self.path = "{}/etc/dirsrv/slapd-{}/dse.ldif".format(prefix[0], serverid)
        else:
            ds_paths = Paths(self._instance.serverid, self._instance)
            self.path = os.path.join(ds_paths.config_dir, 'dse.ldif')

        with open(self.path, 'r') as file_dse:
            processed_line = ""
            for line in file_dse.readlines():
                if not line.startswith(' '):
                    if processed_line:
                        self._contents.append(processed_line)
                    if line.startswith('dn:'):
                        processed_line = line.lower()
                    else:
                        processed_line = line
                else:
                    processed_line = processed_line[:-1] + line[1:]

    @classmethod
    def lint_uid(cls):
        return 'dseldif'

    def _lint_nsstate(self):
        suffixes = self.readNsState()
        for suffix in suffixes:
            # Check the local offset first
            report = None
            skew = int(suffix['time_skew'])
            if skew >= 86400:
                # 24 hours - replication will break
                report = copy.deepcopy(DSSKEWLE0003)
            elif skew >= 43200:
                # 12 hours
                report = copy.deepcopy(DSSKEWLE0002)
            elif skew >= 21600:
                # 6 hours
                report = copy.deepcopy(DSSKEWLE0001)
            if report is not None:
                report['items'].append(suffix['suffix'])
                report['items'].append('Time Skew')
                report['items'].append('Skew: ' + suffix['time_skew_str'])
                report['fix'] = report['fix'].replace('YOUR_INSTANCE', self._instance.serverid)
                report['check'] = f'dseldif:nsstate'
                yield report

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

    def get_indexes(self, backend):
        """Return a list of backend indexes

        :param backend: a backend to get the indexes of
        """
        indexes = []
        for entry in self._contents:
            if fnmatch.fnmatch(entry, "*,cn=index,cn={}*".format(backend.lower())):
                start = entry.find("cn=")
                end = entry.find(",")
                indexes.append(entry[start+len('cn='):end])

        return indexes

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

    # Read NsState helper functions
    def _flipend(self, end):
        if end == '<':
            return '>'
        if end == '>':
            return '<'

    def _getGenState(self, dn, replica_suffix, nsstate, flip):
        """Return a dict ofall the nsState properties
        """
        from lib389.utils import print_nice_time
        if pack('<h', 1) == pack('=h',1):
            endian = "Little Endian"
            end = '<'
            if flip:
                end = self._flipend(end)
        elif pack('>h', 1) == pack('=h',1):
            endian = "Big Endian"
            end = '>'
            if flip:
                end = self._flipend(end)
        else:
            raise ValueError("Unknown endian, unable to proceed")

        thelen = len(nsstate)
        if thelen <= 20:
            pad = 2 # padding for short H values
            timefmt = 'I' # timevals are unsigned 32-bit int
        else:
            pad = 6 # padding for short H values
            timefmt = 'Q' # timevals are unsigned 64-bit int

        base_fmtstr = "H%dx3%sH%dx" % (pad, timefmt, pad)
        fmtstr = end + base_fmtstr
        (rid, sampled_time, local_offset, remote_offset, seq_num) = unpack(fmtstr, nsstate)
        now = int(time.time())
        tdiff = now-sampled_time
        wrongendian = False
        try:
            tdelta = timedelta(seconds=tdiff)
            wrongendian = tdelta.days > 10*365
        except OverflowError: # int overflow
            wrongendian = True

        # if the sampled time is more than 20 years off, this is
        # probably the wrong endianness
        if wrongendian:
            end = self._flipend(end)
            fmtstr = end + base_fmtstr
            (rid, sampled_time, local_offset, remote_offset, seq_num) = unpack(fmtstr, nsstate)
            tdiff = now-sampled_time
            tdelta = timedelta(seconds=tdiff)

        return {
            'dn': dn,
            'suffix': replica_suffix,
            'endian': endian,
            'rid': str(rid),
            'gen_time': str(sampled_time),
            'gencsn': "%08x%04d%04d0000" % (sampled_time, seq_num, rid),
            'gen_time_str': time.ctime(sampled_time),
            'local_offset': str(local_offset),
            'local_offset_str': print_nice_time(local_offset),
            'remote_offset': str(remote_offset),
            'remote_offset_str': print_nice_time(remote_offset),
            'time_skew': str(local_offset + remote_offset),
            'time_skew_str': print_nice_time(local_offset + remote_offset),
            'seq_num': str(seq_num),
            'sys_time': str(time.ctime(now)),
            'diff_secs': str(tdiff),
            'diff_days_secs': "%d:%d" % (tdelta.days, tdelta.seconds),
        }

    def readNsState(self, suffix=None, flip=False):
        """Look for the nsState attribute in replication configuration entries,
        then decode the base64 value and  provide a dict of all stats it
        contains

        :param suffix: specific suffix to read nsState from
        :type suffix: str
        """
        found_replica = False
        found_suffix = False
        replica_suffix = ""
        nsstate = ""
        states = []

        for line in self._contents:
            if line.startswith("dn: "):
                dn = line[4:].strip()
                if dn.startswith("cn=replica"):
                    found_replica = True
                else:
                    found_replica = False
            else:
                if line.lower().startswith("nsstate:: ") and dn.startswith("cn=replica"):
                    b64val = line[10:].strip()
                    nsstate = base64.decodebytes(b64val.encode())
                elif line.lower().startswith("nsds5replicaroot"):
                    found_suffix = True
                    replica_suffix = line.lower().split(':')[1].strip()

            if found_replica and found_suffix and nsstate != "":
                # We have everything we need to proceed
                if suffix is not None and suffix == replica_suffix:
                    states.append(self._getGenState(dn, replica_suffix, nsstate, flip))
                    break
                else:
                    states.append(self._getGenState(dn, replica_suffix, nsstate, flip))
                    # reset flags for next round...
                    found_replica = False
                    found_suffix = False
                    replica_suffix = ""
                    nsstate = ""

        return states

    def _increaseTimeSkew(self, suffix, timeSkew):
        # Increase csngen state local_offset by timeSkew
        # Warning: instance must be stopped before calling this function
        assert (timeSkew >= 0)
        nsState = self.readNsState(suffix)[0]
        self._instance.log.debug(f'_increaseTimeSkew nsState is {nsState}')
        oldNsState = self.get(nsState['dn'], 'nsState', True)
        self._instance.log.debug(f'oldNsState is {oldNsState}')

        # Lets reencode the new nsState
        from lib389.utils import print_nice_time
        if pack('<h', 1) == pack('=h',1):
            end = '<'
        elif pack('>h', 1) == pack('=h',1):
            end = '>'
        else:
            raise ValueError("Unknown endian, unable to proceed")

        thelen = len(oldNsState)
        if thelen <= 20:
            pad = 2 # padding for short H values
            timefmt = 'I' # timevals are unsigned 32-bit int
        else:
            pad = 6 # padding for short H values
            timefmt = 'Q' # timevals are unsigned 64-bit int
        fmtstr = "%sH%dx3%sH%dx" % (end, pad, timefmt, pad)
        newNsState = base64.b64encode(pack(fmtstr, int(nsState['rid']),
           int(nsState['gen_time']), int(nsState['local_offset'])+timeSkew,
           int(nsState['remote_offset']), int(nsState['seq_num'])))
        newNsState = newNsState.decode('utf-8')
        self._instance.log.debug(f'newNsState is {newNsState}')
        # Lets replace the value.
        (entry_dn_i, attr_data) = self._find_attr(nsState['dn'], 'nsState')
        attr_i = next(iter(attr_data))
        self._contents[entry_dn_i + attr_i] = f"nsState:: {newNsState}"
        self._update()


class FSChecks(DSLint):
    """This is for the healthcheck feature, check commonly used system config files the
    server uses.  This is here for lack of a better place to add this class.
    """
    def __init__(self, dirsrv=None):
        self.dirsrv = dirsrv
        self._certdb = self.dirsrv.get_cert_dir()
        self.ds_files = [
            {
                'name': '/etc/resolv.conf',
                'perms': [644],
                'report': DSPERMLE0001
            },
            {
                'name': self._certdb + "/pin.txt",
                'perms': [400, 600],
                'report': DSPERMLE0002
            },
            {
                'name': self._certdb + "/pwdfile.txt",
                'perms': [400, 600],
                'report': DSPERMLE0002
            },
        ]

    @classmethod
    def lint_uid(cls):
        return 'fschecks'

    def _lint_file_perms(self):
        """Test file permissions are safe
        """
        for ds_file in self.ds_files:
            try:
                perms = int(oct(os.stat(ds_file['name'])[ST_MODE])[-3:])
                if perms not in ds_file['perms']:
                    perms = str(ds_file['perms'][0])
                    report = copy.deepcopy(ds_file['report'])
                    report['items'].append(ds_file['name'])
                    report['detail'] = report['detail'].replace('FILE', ds_file['name'])
                    report['detail'] = report['detail'].replace('PERMS', perms)
                    report['fix'] = report['fix'].replace('FILE', ds_file['name'])
                    report['fix'] = report['fix'].replace('PERMS', perms)
                    report['check'] = f'fschecks:file_perms'
                    yield report
            except FileNotFoundError:
                pass
