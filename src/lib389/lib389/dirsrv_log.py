# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Helpers for managing the directory server internal logs.
"""

import re
import gzip
from datetime import datetime
from dateutil.parser import parse as dt_parse
from glob import glob
from lib389._constants import DN_CONFIG
from lib389.properties import LOG_ACCESS_PATH, LOG_ERROR_PATH
from lib389.utils import ensure_bytes, ensure_str

# Because many of these settings can change live, we need to check for certain
# attributes all the time.

MONTH_LOOKUP = {
    'Jan': 1,
    'Feb': 2,
    'Mar': 3,
    'Apr': 4,
    'May': 5,
    'Jun': 6,
    'Jul': 7,
    'Aug': 8,
    'Oct': 9,
    'Sep': 10,
    'Nov': 11,
    'Dec': 12,
}


class DirsrvLog(object):
    def __init__(self, dirsrv):
        self.dirsrv = dirsrv
        self.log = self.dirsrv.log
        self.prog_timestamp = re.compile('\[(?P<day>\d*)\/(?P<month>\w*)\/(?P<year>\d*):(?P<hour>\d*):(?P<minute>\d*):(?P<second>\d*)(.(?P<nanosecond>\d*))+\s(?P<tz>[\+\-]\d*)')  # noqa

    def _get_log_attr(self, attr):
        return self.dirsrv.getEntry(DN_CONFIG).__getattr__(attr)

    def _get_log_path(self):
        return self._get_log_attr(self.log_path_attr)

    def _get_all_log_paths(self):
        return glob("%s.*-*" % self._get_log_path()) + [self._get_log_path()]

    def readlines_archive(self):
        """
        Returns an array of all the lines in all logs, included rotated logs
        and compressed logs. (gzip)
        Will likely be very slow. Try using match instead.
        """
        lines = []
        for log in self._get_all_log_paths():
            # Open the log
            if log.endswith(ensure_bytes('.gz')):
                with gzip.open(log, 'r') as lf:
                    lines += lf.readlines()
            else:
                with open(log, 'r') as lf:
                    lines += lf.readlines()
        return lines

    def readlines(self):
        """
        Returns an array of all the lines in the log.
        Will likely be very slow. Try using match instead.
        """
        lines = []
        self.lpath = self._get_log_path()
        if self.lpath is not None:
            # Open the log
            with open(self.lpath, 'r') as lf:
                lines = lf.readlines()
        return lines

    def match_archive(self, pattern):
        results = []
        prog = re.compile(pattern)
        for log in self._get_all_log_paths():
            if log.endswith(ensure_bytes('.gz')):
                with gzip.open(log, 'r') as lf:
                    for line in lf:
                        mres = prog.match(line)
                        if mres:
                            results.append(line)
            else:
                with open(log, 'r') as lf:
                    for line in lf:
                        mres = prog.match(line)
                        if mres:
                            results.append(line)
        return results

    def match(self, pattern):
        results = []
        prog = re.compile(pattern)
        self.lpath = self._get_log_path()
        if self.lpath is not None:
            with open(self.lpath, 'r') as lf:
                for line in lf:
                    mres = prog.match(line)
                    if mres:
                        results.append(line)
        return results

    def parse_timestamp(self, ts):
        timedata = self.prog_timestamp.match(ts).groupdict()
        # Now, have to convert month to an int.
        dt_str = '{YEAR}-{MONTH}-{DAY} {HOUR}-{MINUTE}-{SECOND} {TZ}'.format(
            YEAR=timedata['year'],
            MONTH=MONTH_LOOKUP[timedata['month']],
            DAY=timedata['day'],
            HOUR=timedata['hour'],
            MINUTE=timedata['minute'],
            SECOND=timedata['second'],
            TZ=timedata['tz'],
            )
        dt = dt_parse(dt_str)
        dt = dt.replace(microsecond=int(int(timedata['nanosecond']) / 1000))
        return dt


class DirsrvAccessLog(DirsrvLog):
    def __init__(self, dirsrv):
        super(DirsrvAccessLog, self).__init__(dirsrv)
        self.log_path_attr = LOG_ACCESS_PATH
        # We precompile our regex for parse_line to make it faster.
        self.prog_m1 = re.compile('^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sop=(?P<op>\d*)\s(?P<action>\w*)\s(?P<rem>.*)')  # noqa
        self.prog_con = re.compile('^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sfd=(?P<fd>\d*)\sslot=(?P<slot>\d*)\sconnection\sfrom\s(?P<remote>[^\s]*)\sto\s(?P<local>[^\s]*)')  # noqa
        self.prog_discon = re.compile('^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sop=(?P<op>\d*)\sfd=(?P<fd>\d*)\s(?P<action>closed)\s-\s(?P<status>\w*)')  # noqa

    def parse_line(self, line):
        """
        This knows how to break up an access log line into the specific fields.
        """
        line = line.strip()
        action = {
            'action': 'CONNECT'
        }
        # First, pull some well known info out.
        if self.dirsrv.verbose:
            self.log.info("--> %s " % line)
        m1 = self.prog_m1.match(line)
        if m1:
            action.update(m1.groupdict())
            # Do more parsing.
            # Specifically, we need to break up action.rem based on action.action.

        con = self.prog_con.match(line)
        if con:
            action.update(con.groupdict())

        discon = self.prog_discon.match(line)
        if discon:
            action.update(discon.groupdict())
            action['action'] = 'DISCONNECT'

        action['datetime'] = self.parse_timestamp(action['timestamp'])

        if self.dirsrv.verbose:
            self.log.info(action)
        return action

    def parse_lines(self, lines):
        return map(self.parse_line, lines)


class DirsrvErrorLog(DirsrvLog):
    def __init__(self, dirsrv):
        super(DirsrvErrorLog, self).__init__(dirsrv)
        self.log_path_attr = LOG_ERROR_PATH
        self.prog_m1 = re.compile('^(?P<timestamp>\[.*\])\s(?P<message>.*)')

    def parse_line(self, line):
        line = line.strip()
        action = self.prog_m1.match(line).groupdict()

        action['datetime'] = self.parse_timestamp(action['timestamp'])
        return action

    def parse_lines(self, lines):
        return map(self.parse_line, lines)
