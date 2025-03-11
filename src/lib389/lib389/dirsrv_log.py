# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Helpers for managing the directory server internal logs.
"""

import copy
import json
import glob
import re
import gzip
from dateutil.parser import parse as dt_parse
from lib389.utils import ensure_bytes
from lib389._mapped_object_lint import DSLint
from lib389.lint import (
    DSLOGNOTES0001,  # Unindexed search
    DSLOGNOTES0002,  # Unknown attr in search filter
)

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


class DirsrvLog(DSLint):
    """Class of functions to working with the various Directory Server logs
    """
    def __init__(self, dirsrv):
        """Initial class
        @param dirsrv - DirSrv object
        """
        self.dirsrv = dirsrv
        self.log = self.dirsrv.log
        self.prog_timestamp = re.compile(r'\[(?P<day>\d*)\/(?P<month>\w*)\/(?P<year>\d*):(?P<hour>\d*):(?P<minute>\d*):(?P<second>\d*)(.(?P<nanosecond>\d*))+\s(?P<tz>[\+\-]\d*)')   # noqa
        #  JSON timestamp uses strftime %FT%T --> 2025-02-12T17:00:47.663123181 -0500
        self.prog_json_timestamp = re.compile(r'(?P<year>\d*)-(?P<month>\w*)-(?P<day>\d*)T(?P<hour>\d*):(?P<minute>\d*):(?P<second>\d*)(.(?P<nanosecond>\d*))+\s(?P<tz>[\+\-]\d*)')   # noqa
        self.prog_datetime = re.compile(r'^(?P<timestamp>\[.*\])')
        self.jsonFormat = False

    def _get_log_path(self):
        """Return the current log file location"""
        raise Exception("Log type not defined.")

    def _get_all_log_paths(self):
        """Return all the log paths"""
        return glob.glob("%s.*-*" % self._get_log_path()) + [self._get_log_path()]

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

    def match(self, pattern, after_pattern=None):
        """Search the current log file for the pattern
        @param pattern - a regex pattern
        @param after_pattern - None or a regex pattern
               if set only matches found seeing last occurance
               of after_pattern are returned
        @return - results of the pattern matching
        """
        results = []
        prog = re.compile(pattern)
        if after_pattern:
            aprog = re.compile(after_pattern)
            aprog_hit = False
        else:
            aprog_hit = True
        self.lpath = self._get_log_path()
        if self.lpath is not None:
            with open(self.lpath, 'r') as lf:
                for line in lf:
                    if after_pattern is not None:
                        if aprog.match(line):
                            aprog_hit = True
                            results.clear()
                    mres = prog.match(line)
                    if aprog_hit and mres:
                        results.append(line)
        return results

    def parse_timestamp(self, ts, json_format=False):
        """Parse a logs timestamps and break it down into its individual parts
        @param ts - The timestamp string from a log
        @return - a "datetime" object
        """
        if json_format:
            timedata = self.prog_json_timestamp.match(ts).groupdict()
        else:
            timedata = self.prog_timestamp.match(ts).groupdict()

        # Now, have to convert month to an int.
        dt_str = '{YEAR}-{MONTH}-{DAY} {HOUR}-{MINUTE}-{SECOND} {TZ}'.format(
            YEAR=timedata['year'],
            MONTH=timedata['month'],
            DAY=timedata['day'],
            HOUR=timedata['hour'],
            MINUTE=timedata['minute'],
            SECOND=timedata['second'],
            TZ=timedata['tz'],
            )
        dt = dt_parse(dt_str)
        if timedata['nanosecond']:
            dt = dt.replace(microsecond=int(int(timedata['nanosecond']) / 1000))
        return dt

    def get_time_in_secs(self, log_line):
        """Take the timestamp (not the date) from a DS access log and convert
           it to seconds:

              [25/May/2016:15:24:27.289341875 -0400]...

              JSON format

              2025-02-12T17:00:47.699153015 -0500

            @param log_line - A line of txt from a DS error/access log
            @return - time in seconds
        """

        total = 0

        try:
            log_obj = json.loads(log_line)
            # JSON format
            date_str = log_obj["local_time"]
            time_str = date_str.split('T')[1:]  # splice off the date
            hms = time_str[:8]
            parts = hms.split(':')

        except ValueError:
            # Old format
            index = log_line.index(':') + 1
            hms = log_line[index: index + 8]
            parts = hms.split(':')

        if int(parts[0]):
            total += int(parts[0]) * 3600
        if int(parts[1]):
            total += int(parts[1]) * 60
        total += int(parts[2])

        return total


class DirsrvAccessJSONLog(DirsrvLog):
    """Class for process access logs in JSON format"""
    def __init__(self, dirsrv):
        """Init the class
        @param dirsrv - A DirSrv object
        """
        super(DirsrvAccessJSONLog, self).__init__(dirsrv)
        self.lpath = None

    @classmethod
    def lint_uid(cls):
        return 'logs'

    def _log_get_search_stats(self, conn, op):
        self.lpath = self._get_log_path()
        if self.lpath is not None:
            with open(self.lpath, 'r') as lf:
                for line in lf:
                    line = line.strip()
                    action = json.loads(line)
                    if 'header' in action:
                        # This is the log title, skip it
                        continue
                    if action['operation'] == "SEARCH" and \
                       action['conn_id'] == op and \
                       action['op_id'] == op:
                        return action
        return None

    def _lint_notes(self):
        """
        Check for notes=A (fully unindexed searches), and
        notes=F (unknown attribute in filter)
        """
        for pattern, lint_report in [(".* notes=A", DSLOGNOTES0001), (".* notes=F", DSLOGNOTES0002)]:
            lines = self.match(pattern)
            if len(lines) > 0:
                count = 0
                searches = []
                for line in lines:
                    line = line.strip()
                    action = json.loads(line)
                    if action['operation'] == 'RESULT':
                        # Looks like a valid notes=A/F
                        conn = action['conn_id']
                        op = action['op_id']
                        etime = action['etime']
                        stats = self._log_get_search_stats(conn, op)
                        if stats is not None:
                            timestamp = stats['local_time']
                            base = stats['base_dn']
                            scope = stats['scope']
                            srch_filter = stats['filter']
                            count += 1
                            if lint_report == DSLOGNOTES0001:
                                searches.append(f'\n  [{count}] Unindexed Search\n'
                                                f'      - date:    {timestamp}\n'
                                                f'      - conn/op: {conn}/{op}\n'
                                                f'      - base:    {base}\n'
                                                f'      - scope:   {scope}\n'
                                                f'      - filter:  {srch_filter}\n'
                                                f'      - etime:   {etime}\n')
                            else:
                                searches.append(f'\n  [{count}] Invalid Attribute in Filter\n'
                                                f'      - date:    {timestamp}\n'
                                                f'      - conn/op: {conn}/{op}\n'
                                                f'      - filter:  {srch_filter}\n')
                if len(searches) > 0:
                    report = copy.deepcopy(lint_report)
                    report['items'].append(self._get_log_path())
                    report['detail'] = report['detail'].replace('NUMBER',
                                                                str(count))
                    for srch in searches:
                        report['detail'] += srch
                    report['check'] = 'logs:notes'
                    yield report

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
        try:
            action = json.loads(line)
        except ValueError:
            # Old format, skip this line
            return None

        # First, pull some well known info out.
        if self.dirsrv.verbose:
            self.log.info("parse_line --> %s ", line)

        action['datetime'] = self.parse_timestamp(action['local_time'],
                                                  json_format=True)

        if self.dirsrv.verbose:
            self.log.info(action)
        return action

    def parse_log(self):
        """
        Take the entire logs and parse it into a list of objects, this can
        handle "json_pretty" format
        """

        json_objects = []
        jobj = ""

        lines = self.readlines()
        for line in lines:
            line = line.rstrip()
            if line == '{':
                jobj = "{"
            elif line == '}':
                jobj += "}"
                json_objects.append(json.loads(jobj))
            else:
                if line[0] == '{' and line[-1] == '}':
                    # Complete json log line
                    json_objects.append(json.loads(line))
                else:
                    # Json pretty - append the line
                    jobj += line.strip()

        return json_objects


class DirsrvAccessLog(DirsrvLog):
    """Class for process access logs"""
    def __init__(self, dirsrv):
        """Init the class
        @param dirsrv - A DirSrv object
        """
        super(DirsrvAccessLog, self).__init__(dirsrv)
        # We precompile our regex for parse_line to make it faster.
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

    @classmethod
    def lint_uid(cls):
        return 'logs'

    def _log_get_search_stats(self, conn, op):
        lines = self.match(f".* conn={conn} op={op} SRCH base=.*")
        if len(lines) != 1:
            return None

        quoted_vals = re.findall('"([^"]*)"', lines[0])
        return {
            'base': quoted_vals[0],
            'filter': quoted_vals[1],
            'timestamp': re.findall('[(.*)]', lines[0])[0],
            'scope': lines[0].split(' scope=', 1)[1].split(' ',1)[0]
        }

    def _lint_notes(self):
        """
        Check for notes=A (fully unindexed searches), and
        notes=F (unknown attribute in filter)
        """
        for pattern, lint_report in [(".* notes=A", DSLOGNOTES0001), (".* notes=F", DSLOGNOTES0002)]:
            lines = self.match(pattern)
            if len(lines) > 0:
                count = 0
                searches = []
                for line in lines:
                    if ' RESULT err=' in line:
                        # Looks like a valid notes=A/F
                        conn = line.split(' conn=', 1)[1].split(' ', 1)[0]
                        op = line.split(' op=', 1)[1].split(' ', 1)[0]
                        etime = line.split(' etime=', 1)[1].split(' ', 1)[0]
                        stats = self._log_get_search_stats(conn, op)
                        if stats is not None:
                            timestamp = stats['timestamp']
                            base = stats['base']
                            scope = stats['scope']
                            srch_filter = stats['filter']
                            count += 1
                            if lint_report == DSLOGNOTES0001:
                                searches.append(f'\n  [{count}] Unindexed Search\n'
                                                f'      - date:    {timestamp}\n'
                                                f'      - conn/op: {conn}/{op}\n'
                                                f'      - base:    {base}\n'
                                                f'      - scope:   {scope}\n'
                                                f'      - filter:  {srch_filter}\n'
                                                f'      - etime:   {etime}\n')
                            else:
                                searches.append(f'\n  [{count}] Invalid Attribute in Filter\n'
                                                f'      - date:    {timestamp}\n'
                                                f'      - conn/op: {conn}/{op}\n'
                                                f'      - filter:  {srch_filter}\n')
                if len(searches) > 0:
                    report = copy.deepcopy(lint_report)
                    report['items'].append(self._get_log_path())
                    report['detail'] = report['detail'].replace('NUMBER',
                                                                str(count))
                    for srch in searches:
                        report['detail'] += srch
                    report['check'] = 'logs:notes'
                    yield report

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
        @param dirsrv - A DirSrv object
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
        @param lines - a list of strings/lines from an errors log
        @return - A dictionary of the log parts for each line
        """
        return map(self.parse_line, lines)


class DirsrvErrorJSONLog(DirsrvLog):
    """Directory Server Error JSON log class"""
    def __init__(self, dirsrv):
        """Init the Error log class
        @param dirsrv - A DirSrv object
        """
        super(DirsrvErrorJSONLog, self).__init__(dirsrv)
        self.jsonFormat = True
        self.lpath = ""

    def _get_log_path(self):
        """Return the current log file location"""
        return self.dirsrv.ds_paths.error_log

    def readlines(self):
        """Returns an array of all the lines in the log.

        @return - an array of all the lines in the log.
        """
        lines = []
        self.lpath = self._get_log_path()
        if self.lpath is not None:
            # Open the log
            with open(self.lpath, 'r', errors='ignore') as lf:
                lines = lf.readlines()
        return lines

    def parse_line(self, line):
        """Parse a error log line
        @line - a text string from a error log
        @return - A dictionary of the log parts
        """
        line = line.strip()

        try:
            action = json.loads(line)
            if 'header' in action:
                # This is the log title, return it as is
                return action

            action['datetime'] = self.parse_timestamp(action['local_time'],
                                                      json_format=True)
            return action

        except json.decoder.JSONDecodeError:
            # Maybe it's json pretty, regardless we can not parse this single
            # line
            pass

        return None

    def parse_log(self):
        """
        Take the entire logs and parse it into a list of objects, this can
        handle "json_pretty" format
        """

        json_objects = []
        jobj = ""

        lines = self.readlines()
        for line in lines:
            line = line.rstrip()
            if line == '{':
                jobj = "{"
            elif line == '}':
                jobj += "}"
                json_objects.append(json.loads(jobj))
            else:
                if line[0] == '{' and line[-1] == '}':
                    # Complete json log line
                    json_objects.append(json.loads(line))
                else:
                    # Json pretty - append the line
                    jobj += line.strip()

        return json_objects


class DirsrvSecurityLog(DirsrvLog):
    """
    Directory Server Security log class

    Currently this is only written in "json", not "json-pretty"
    """
    def __init__(self, dirsrv):
        """Init the Security log class
        @param dirsrv - A DirSrv object
        """
        super(DirsrvSecurityLog, self).__init__(dirsrv)
        self.jsonFormat = True

    def _get_log_path(self):
        """Return the current log file location"""
        return self.dirsrv.ds_paths.security_log

    def parse_line(self, line):
        """Parse a Security log line
        @line - a text string from a security log
        @return - A dictionary of the log parts
        """
        line = line.strip()
        action = json.loads(line)
        if 'header' in action:
            # This is the log title, return it as is
            return action
        action['datetime'] = action['date']
        return action

    def parse_lines(self, lines):
        """Parse multiple lines from a security log
        @param lines - a lits of strings/lines from a security log
        @return - A dictionary of the log parts for each line
        """
        return map(self.parse_line, lines)


class DirsrvAuditLog(DirsrvLog):
    """Directory Server Audit log class"""
    def __init__(self, dirsrv):
        """Init the Audit log class
        @param dirsrv - A DirSrv object
        """
        super(DirsrvAuditLog, self).__init__(dirsrv)

    def _get_log_path(self):
        """Return the current log file location"""
        return self.dirsrv.ds_paths.audit_log

    def parse_line(self, line):
        """Parse an audit log line
        @line - a text string from an audit log
        @return - A dictionary of the log parts
        """
        line = line.strip()
        return line.groupdict()

    def parse_lines(self, lines):
        """Parse multiple lines from an audit log
        @param lines - a list of strings/lines from an audit log
        @return - A dictionary of the log parts for each line
        """
        return map(self.parse_line, lines)


class DirsrvAuditJSONLog(DirsrvLog):
    """Directory Server Audit JSON log class"""
    def __init__(self, dirsrv):
        """Init the Audit log class
        @param dirsrv - A DirSrv object
        """
        super(DirsrvAuditJSONLog, self).__init__(dirsrv)
        self.jsonFormat = True
        self.lpath = ""

    def _get_log_path(self):
        """Return the current log file location"""
        return self.dirsrv.ds_paths.audit_log

    def readlines(self):
        """Returns an array of all the lines in the log. Need to ignore
        encoding errors when dealing with the audit log

        @return - an array of all the lines in the log.
        """
        lines = []
        self.lpath = self._get_log_path()
        if self.lpath is not None:
            # Open the log
            with open(self.lpath, 'r', errors='ignore') as lf:
                lines = lf.readlines()
        return lines

    def parse_line(self, line):
        """Parse a audit log line
        @line - a text string from a audit log
        @return - A dictionary of the log parts
        """
        line = line.strip()
        try:
            action = json.loads(line)
            if 'header' in action:
                # This is the log title, return it as is
                return action
            action['datetime'] = action['gm_time']
            return action
        except json.decoder.JSONDecodeError:
            # Maybe it's json pretty, regardless we can not parse this single
            # line
            pass

        return None

    def parse_log(self):
        """
        Take the entire logs and parse it into a list of objects, this can
        handle "json_pretty" format
        """

        json_objects = []
        jobj = ""

        lines = self.readlines()
        for line in lines:
            line = line.rstrip()
            if line == '{':
                jobj = "{"
            elif line == '}':
                jobj += "}"
                json_objects.append(json.loads(jobj))
            else:
                if line[0] == '{' and line[-1] == '}':
                    # Complete json log line
                    json_objects.append(json.loads(line))
                else:
                    # Json pretty - append the line
                    jobj += line.strip()

        return json_objects
