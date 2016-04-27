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
from lib389._constants import DN_CONFIG
from lib389.properties import LOG_ACCESS_PATH, LOG_ERROR_PATH

# Because many of these settings can change live, we need to check for certain
# attributes all the time.

class DirsrvLog(object):
    def __init__(self, dirsrv):
        self.dirsrv = dirsrv
        self.log = self.dirsrv.log

    def _get_log_attr(self, attr):
        return self.dirsrv.getEntry(DN_CONFIG).__getattr__(attr)

    def _get_log_path(self):
        return self._get_log_attr(self.log_path_attr)

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
            # return a readlines fn?
        return lines

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
        ## do a read lines, then match with the associated RE.
        return results

class DirsrvAccessLog(DirsrvLog):
    def __init__(self, dirsrv):
        super(DirsrvAccessLog, self).__init__(dirsrv)
        self.log_path_attr = LOG_ACCESS_PATH
        ## We precompile our regex for parse_line to make it faster.
        self.prog_m1 = re.compile('^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sop=(?P<op>\d*)\s(?P<action>\w*)\s(?P<rem>.*)')
        self.prog_con = re.compile('^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sfd=(?P<fd>\d*)\sslot=(?P<slot>\d*)\sconnection\sfrom\s(?P<remote>[^\s]*)\sto\s(?P<local>[^\s]*)')
        self.prog_discon = re.compile('^(?P<timestamp>\[.*\])\sconn=(?P<conn>\d*)\sop=(?P<op>\d*)\sfd=(?P<fd>\d*)\s(?P<action>closed)\s-\s(?P<status>\w*)')

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
        return self.prog_m1.match(line).groupdict()

    def parse_lines(self, line):
        return map(self.parse_line, lines)
