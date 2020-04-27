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
from dateutil.parser import parse as dt_parse
from glob import glob
from lib389.utils import ensure_bytes


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
    """Class of functions to working with the various DIrectory Server logs
    """
    def __init__(self, dirsrv):
        """Initial class
        @param dirsrv - DirSrv object
        """
        self.dirsrv = dirsrv
        self.log = self.dirsrv.log
        self.prog_timestamp = re.compile(r'\[(?P<day>\d*)\/(?P<month>\w*)\/(?P<year>\d*):(?P<hour>\d*):(?P<minute>\d*):(?P<second>\d*)(.(?P<nanosecond>\d*))+\s(?P<tz>[\+\-]\d*)')   # noqa
        self.prog_datetime = re.compile(r'^(?P<timestamp>\[.*\])')

    def _get_log_path(self):
        """Return the current log file location"""
        raise Exception("Log type not defined.")

    def _get_all_log_paths(self):
        """Return all the log paths"""
        return glob("%s.*-*" % self._get_log_path()) + [self._get_log_path()]

    def readlines_archive(self):
        """
        Returns an array of all the lines in all logs, included rotated logs
        and compressed logs. (gzip)
        Will likely be very slow. Try using match instead.

        @return - an array of all the lines in all logs
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
        """Returns an array of all the lines in the log.
        Will likely be very slow. Try using match instead.

        @return - an array of all the lines in the log.
        """
        lines = []
        self.lpath = self._get_log_path()
        if self.lpath is not None:
            # Open the log
            with open(self.lpath, 'r') as lf:
                lines = lf.readlines()
        return lines

    def match_archive(self, pattern):
        """Search all the log files, including "zipped" logs
        @param pattern - a regex pattern
        @return - results of the pattern matching
        """
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
        """Search the current log file for the pattern
        @param pattern - a regex pattern
        @return - results of the pattern matching
        """
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
        """Parse a logs timestamps and break it down into its individual parts
        @param ts - The timestamp string from a log
        @return - a "datetime" object
        """
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
        if timedata['nanosecond'] is not '':
            dt = dt.replace(microsecond=int(int(timedata['nanosecond']) / 1000))
        return dt

    def get_time_in_secs(self, log_line):
        """Take the timestamp (not the date) from a DS log and convert it
           to seconds:

              [25/May/2016:15:24:27.289341875 -0400]...

            @param log_line - A line of txt from a DS error/access log
            @return - time in seconds
        """

        total = 0
        index = log_line.index(':') + 1
        hms = log_line[index: index + 8]
        parts = hms.split(':')
        if int(parts[0]):
            total += int(parts[0]) * 3600
        if int(parts[1]):
            total += int(parts[1]) * 60
        total += int(parts[2])

        return total


class DirsrvAccessLog(DirsrvLog):
    """Class for process access logs"""
    def __init__(self, dirsrv):
        """Init the class
        @param dirsrv - A DirSrv object
        """
        super(DirsrvAccessLog, self).__init__(dirsrv)
        ## We precompile our regex for parse_line to make it faster.
        self.prog_m1 = re.compile(r'^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sop=(?P<op>\d*)\s(?P<action>\w*)\s(?P<rem>.*)')
        self.prog_con = re.compile(r'^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sfd=(?P<fd>\d*)\sslot=(?P<slot>\d*)\sconnection\sfrom\s(?P<remote>[^\s]*)\sto\s(?P<local>[^\s]*)')
        self.prog_discon = re.compile(r'^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sop=(?P<op>\d*)\sfd=(?P<fd>\d*)\s(?P<action>closed)\s-\s(?P<status>\w*)')
        # RESULT regex's (based off action.rem)
        self.prog_notes = re.compile(r'err=(?P<err>\d*)\stag=(?P<tag>\d*)\snentries=(?P<nentries>\d*)\setime=(?P<etime>[0-9.]*)\snotes=(?P<notes>\w*)')
        self.prog_repl = re.compile(r'err=(?P<err>\d*)\stag=(?P<tag>\d*)\snentries=(?P<nentries>\d*)\setime=(?P<etime>[0-9.]*)\scsn=(?P<csn>\w*)')
        self.prog_result = re.compile(r'err=(?P<err>\d*)\stag=(?P<tag>\d*)\snentries=(?P<nentries>\d*)\setime=(?P<etime>[0-9.]*)\s(?P<rem>.*)')
        # Lists for each regex type
        self.full_regexs = [self.prog_m1, self.prog_con, self.prog_discon]
        self.result_regexs = [self.prog_notes, self.prog_repl,
                              self.prog_result]

    def _get_log_path(self):
        """Return the current log file location"""
        return self.dirsrv.ds_paths.access_log

    def parse_line(self, line):
        """
        This knows how to break up an access log line into the specific fields.
        @param line - A text line from an access log
        @return - A dictionary of the log parts
        """
        line = line.strip()
        action = {
            'action': 'CONNECT'
        }
        # First, pull some well known info out.
        if self.dirsrv.verbose:
            self.log.info("--> %s ", line)

        for regex in self.full_regexs:
            result = regex.match(line)
            if result:
                action.update(result.groupdict())
                if regex == self.prog_discon:
                    action['action'] = 'DISCONNECT'
                break

        if action['action'] == 'RESULT':
            for regex in self.result_regexs:
                result = regex.match(action['rem'])
                if result:
                    action.update(result.groupdict())
                    break

        action['datetime'] = self.parse_timestamp(action['timestamp'])

        if self.dirsrv.verbose:
            self.log.info(action)
        return action

    def parse_lines(self, lines):
        """Parse multiple log lines
        @param lines - a list of log lines
        @return - A dictionary of the log parts for each line
        """
        return map(self.parse_line, lines)


class DirsrvErrorLog(DirsrvLog):
    """Directory Server Error log class"""
    def __init__(self, dirsrv):
        """Init the Error log class
        @param diursrv - A DirSrv object
        """
        super(DirsrvErrorLog, self).__init__(dirsrv)
        self.prog_m1 = re.compile(r'^(?P<timestamp>\[.*\])\s(?P<message>.*)')

    def _get_log_path(self):
        """Return the current log file location"""
        return self.dirsrv.ds_paths.error_log

    def parse_line(self, line):
        """Parse an errors log line
        @line - a text string from an errors log
        @return - A dictionary of the log parts
        """
        line = line.strip()
        action = self.prog_m1.match(line).groupdict()

        action['datetime'] = self.parse_timestamp(action['timestamp'])
        return action

    def parse_lines(self, lines):
        """Parse multiple lines from an errors log
        @param lines - a lits of strings/lines from an errors log
        @return - A dictionary of the log parts for each line
        """
        return map(self.parse_line, lines)
