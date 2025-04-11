#!/usr/bin/python3

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import os
import gzip
import re
import argparse
import logging
import sys
import csv
from datetime import datetime, timedelta, timezone
import heapq
from collections import Counter
from collections import defaultdict
from typing import Optional
import magic

# Globals
LATENCY_GROUPS = {
    "<= 1": 0,
    "== 2": 0,
    "== 3": 0,
    "4-5": 0,
    "6-10": 0,
    "11-15": 0,
    "> 15": 0
}

DISCONNECT_ERRORS = {
    '32': 'broken_pipe',
    '11': 'resource_unavail',
    '131': 'connection_reset',
    '-5961': 'connection_reset'
}

LDAP_ERR_CODES = {
    '0': "Successful Operations",
    '1': "Operations Error(s)",
    '2': "Protocol Errors",
    '3': "Time Limit Exceeded",
    '4': "Size Limit Exceeded",
    '5': "Compare False",
    '6': "Compare True",
    '7': "Strong Authentication Not Supported",
    '8': "Strong Authentication Required",
    '9': "Partial Results",
    '10': "Referral Received",
    '11': "Administrative Limit Exceeded (Look Through Limit)",
    '12': "Unavailable Critical Extension",
    '13': "Confidentiality Required",
    '14': "SASL Bind in Progress",
    '16': "No Such Attribute",
    '17': "Undefined Type",
    '18': "Inappropriate Matching",
    '19': "Constraint Violation",
    '20': "Type or Value Exists",
    '21': "Invalid Syntax",
    '32': "No Such Object",
    '33': "Alias Problem",
    '34': "Invalid DN Syntax",
    '35': "Is Leaf",
    '36': "Alias Deref Problem",
    '48': "Inappropriate Authentication (No password presented, etc)",
    '49': "Invalid Credentials (Bad Password)",
    '50': "Insufficient (write) Privileges",
    '51': "Busy",
    '52': "Unavailable",
    '53': "Unwilling To Perform",
    '54': "Loop Detected",
    '60': "Sort Control Missing",
    '61': "Index Range Error",
    '64': "Naming Violation",
    '65': "Objectclass Violation",
    '66': "Not Allowed on Non Leaf",
    '67': "Not Allowed on RDN",
    '68': "Already Exists",
    '69': "No Objectclass Mods",
    '70': "Results Too Large",
    '71': "Effect Multiple DSA's",
    '80': "Other :-)",
    '81': "Server Down",
    '82': "Local Error",
    '83': "Encoding Error",
    '84': "Decoding Error",
    '85': "Timeout",
    '86': "Authentication Unknown",
    '87': "Filter Error",
    '88': "User Canceled",
    '89': "Parameter Error",
    '90': "No Memory",
    '91': "Connect Error",
    '92': "Not Supported",
    '93': "Control Not Found",
    '94': "No Results Returned",
    '95': "More Results To Return",
    '96': "Client Loop",
    '97': "Referral Limit Exceeded"
}

DISCONNECT_MSG = {
    "A1": "Client Aborted Connections",
    "B1": "Bad Ber Tag Encountered",
    "B4": "Server failed to flush data (response) back to Client",
    "T1": "Idle Timeout Exceeded",
    "T2": "IO Block Timeout Exceeded or NTSSL Timeout",
    "T3": "Paged Search Time Limit Exceeded",
    "B2": "Ber Too Big",
    "B3": "Ber Peek",
    "R1": "Revents",
    "P1": "Plugin",
    "P2": "Poll",
    "U1": "Cleanly Closed Connections"
}

OID_MSG = {
    "2.16.840.1.113730.3.5.1": "Transaction Request",
    "2.16.840.1.113730.3.5.2": "Transaction Response",
    "2.16.840.1.113730.3.5.3": "Start Replication Request (incremental update)",
    "2.16.840.1.113730.3.5.4": "Replication Response",
    "2.16.840.1.113730.3.5.5": "End Replication Request (incremental update)",
    "2.16.840.1.113730.3.5.6": "Replication Entry Request",
    "2.16.840.1.113730.3.5.7": "Start Bulk Import",
    "2.16.840.1.113730.3.5.8": "Finished Bulk Import",
    "2.16.840.1.113730.3.5.9": "DS71 Replication Entry Request",
    "2.16.840.1.113730.3.6.1": "Incremental Update Replication Protocol",
    "2.16.840.1.113730.3.6.2": "Total Update Replication Protocol (Initialization)",
    "2.16.840.1.113730.3.4.13": "Replication Update Info Control",
    "2.16.840.1.113730.3.6.4": "DS71 Replication Incremental Update Protocol",
    "2.16.840.1.113730.3.6.3": "DS71 Replication Total Update Protocol",
    "2.16.840.1.113730.3.5.12": "DS90 Start Replication Request",
    "2.16.840.1.113730.3.5.13": "DS90 Replication Response",
    "1.2.840.113556.1.4.841": "Replication Dirsync Control",
    "1.2.840.113556.1.4.417": "Replication Return Deleted Objects",
    "1.2.840.113556.1.4.1670": "Replication WIN2K3 Active Directory",
    "2.16.840.1.113730.3.6.5": "Replication CleanAllRUV",
    "2.16.840.1.113730.3.6.6": "Replication Abort CleanAllRUV",
    "2.16.840.1.113730.3.6.7": "Replication CleanAllRUV Get MaxCSN",
    "2.16.840.1.113730.3.6.8": "Replication CleanAllRUV Check Status",
    "2.16.840.1.113730.3.5.10": "DNA Plugin Request",
    "2.16.840.1.113730.3.5.11": "DNA Plugin Response",
    "1.3.6.1.4.1.1466.20037": "Start TLS",
    "1.3.6.1.4.1.4203.1.11.1": "Password Modify",
    "2.16.840.1.113730.3.4.20": "MTN Control Use One Backend",
}

SCOPE_LABEL = {
    0: "0 (base)",
    1: "1 (one)",
    2: "2 (subtree)"
}

STLS_OID = '1.3.6.1.4.1.1466.20037'

# Version
logAnalyzerVersion = "8.3"


class logAnalyser:
    """
    A class to parse and analyse log files with configurable options.

    Attributes:
        verbose (Optional[bool]): Enable verbose data gathering and reporting.
        recommends (Optional[bool]): Provide some recommendations post analysis.
        size_limit (Optional[int]): Maximum size of entries to report.
        root_dn (Optional[str]): Directory Managers DN.
        exclude_ip (Optional[str]): IPs to exclude from analysis.
        stats_file_sec (Optional[str]): Interval (in seconds) for statistics reporting.
        stats_file_min (Optional[str]): Interval (in minutes) for statistics reporting.
        report_dn (Optional[str]): Generate a report on DN activity.
    """
    def __init__(self,
                 verbose: Optional[bool] = False,
                 recommends: Optional[bool] = False,
                 size_limit: Optional[int] = None,
                 root_dn: Optional[str] = None,
                 exclude_ip: Optional[str] = None,
                 stats_file_sec: Optional[str] = None,
                 stats_file_min: Optional[str] = None,
                 report_dn: Optional[str] = None):

        self.verbose = verbose
        self.recommends = recommends
        self.size_limit = size_limit
        self.root_dn = root_dn
        self.exclude_ip = exclude_ip
        self.file_size = 0
        # Stats reporting
        self.prev_stats = None
        (self.stats_interval, self.stats_file) = self._get_stats_info(stats_file_sec, stats_file_min)
        self.csv_writer = self._init_csv_writer(self.stats_file) if self.stats_file else None
        # Bind reporting
        self.report_dn = report_dn
        # Init internal data structures
        self._init_data_structures()
        # Init regex patterns and corresponding actions
        self.regexes = self._init_regexes()
        # Init logger
        self.logger = self._setup_logger(logging.ERROR)

    def _get_stats_info(self,
                        report_stats_sec: str,
                        report_stats_min: str):
        """
        Get the configured interval for statistics.

        Args:
            report_stats_sec (str): Statistic reporting interval in seconds.
            report_stats_min (str): Statistic reporting interval in minutes.

        Returns:
            A tuple where the first element indicates the multiplier for the interval
            (1 for seconds, 60 for minutes), and the second element is the file to
            write statistics to. Returns (None, None) if no interval is provided.
        """
        if report_stats_sec:
            return 1, report_stats_sec
        elif report_stats_min:
            return 60, report_stats_min
        else:
            return None, None

    def _init_csv_writer(self, stats_file: str):
        """
        Initialize a CSV writer for statistics reporting.

        Args:
            stats_file (str): The path to the CSV file where statistics will be written.

        Returns:
            csv.writer: A CSV writer object for writing to the specified file.

        Raises:
            IOError: If the file cannot be opened for writing.
        """
        try:
            file = open(stats_file, mode='w', newline='')
            return csv.writer(file)
        except IOError as io_err:
            raise IOError(f"Failed to open file '{stats_file}' for writing: {io_err}")

    def _setup_logger(self, log_level: int):
        """
        Setup logging
        """
        logger = logging.getLogger("logAnalyser")
        formatter = logging.Formatter('%(name)s - %(levelname)s - %(message)s')

        handler = logging.StreamHandler()
        handler.setFormatter(formatter)

        logger.setLevel(log_level)
        logger.addHandler(handler)

        return logger

    def _init_data_structures(self):
        """
        Set up data structures for parsing and storing log data.
        """
        self.notesA = {}
        self.notesF = {}
        self.notesM = {}
        self.notesP = {}
        self.notesU = {}

        self.vlv = {
            'vlv_ctr': 0,
            'vlv_map_rco': {}
        }

        self.server = {
            'restart_ctr': 0,
            'first_time': None,
            'last_time': None,
            'parse_start_time': None,
            'parse_stop_time': None,
            'lines_parsed': 0
        }

        self.operation = {
            'all_op_ctr': 0,
            'add_op_ctr': 0,
            'mod_op_ctr': 0,
            'del_op_ctr': 0,
            'modrdn_op_ctr': 0,
            'cmp_op_ctr': 0,
            'abandon_op_ctr': 0,
            'sort_op_ctr': 0,
            'extnd_op_ctr': 0,
            'add_map_rco': {},
            'mod_map_rco': {},
            'del_map_rco': {},
            'cmp_map_rco': {},
            'modrdn_map_rco': {},
            'extop_dict': {},
            'extop_map_rco': {},
            'abandoned_map_rco': {}
        }

        self.connection = {
            'conn_ctr': 0,
            'fd_taken_ctr': 0,
            'fd_returned_ctr': 0,
            'fd_max_ctr': 0,
            'sim_conn_ctr': 0,
            'max_sim_conn_ctr': 0,
            'ldap_ctr': 0,
            'ldapi_ctr': 0,
            'ldaps_ctr': 0,
            'start_time': {},
            'open_conns': {},
            'exclude_ip_map': {},
            'broken_pipe': {},
            'resource_unavail': {},
            'connection_reset': {},
            'disconnect_code': {},
            'disconnect_code_map': {},
            'ip_map': {},
            'restart_conn_ip_map': {}
        }

        self.bind = {
            'bind_ctr': 0,
            'unbind_ctr': 0,
            'sasl_bind_ctr': 0,
            'anon_bind_ctr': 0,
            'autobind_ctr': 0,
            'rootdn_bind_ctr': 0,
            'version': {},
            'dn_freq': {},
            'dn_map_rc': {},
            'sasl_mech_freq': {},
            'sasl_map_co': {},
            'root_dn': {},
            'report_dn': defaultdict(lambda: defaultdict(int, conn=set(), ips=set()))
        }

        self.result = {
            'result_ctr': 0,
            'notesA_ctr': 0,    # dynamically referenced
            'notesF_ctr': 0,    # dynamically referenced
            'notesM_ctr': 0,    # dynamically referenced
            'notesP_ctr': 0,    # dynamically referenced
            'notesU_ctr': 0,    # dynamically referenced
            'timestamp_ctr': 0,
            'entry_count': 0,
            'referral_count': 0,
            'total_etime': 0.0,
            'total_wtime': 0.0,
            'total_optime': 0.0,
            'notesA_map': {},
            'notesF_map': {},
            'notesM_map': {},
            'notesP_map': {},
            'notesU_map': {},
            'etime_stat': 0.0,
            'etime_counts': defaultdict(int),
            'etime_freq': [],
            'etime_duration': [],
            'wtime_counts': defaultdict(int),
            'wtime_freq': [],
            'wtime_duration': [],
            'optime_counts': defaultdict(int),
            'optime_freq': [],
            'optime_duration': [],
            'nentries_dict': defaultdict(int),
            'nentries_num': [],
            'nentries_set': set(),
            'nentries_returned': [],
            'error_freq': defaultdict(str),
            'bad_pwd_map': {}
        }

        self.search = {
            'search_ctr': 0,
            'search_map_rco': {},
            'attr_dict': defaultdict(int),
            'base_search_ctr': 0,
            'base_map': {},
            'base_map_rco': {},
            'scope_map_rco': {},
            'filter_dict': {},
            'filter_list': [],
            'filter_seen': set(),
            'filter_counter': Counter(),
            'filter_map_rco': {},
            'persistent_ctr': 0
        }

        self.auth = {
            'ssl_client_bind_ctr': 0,
            'ssl_client_bind_failed_ctr': 0,
            'cipher_ctr': 0,
            'auth_info': {}
        }

    def _init_regexes(self):
        """
        Initialise a dictionary of regex patterns and their match processing methods.

        Returns:
            dict: A mapping of regex pattern key to (compiled regex, match handler function) value.
        """

        # Reusable patterns
        TIMESTAMP_PATTERN = r'''
            \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
        '''
        CONN_ID_PATTERN = r'\sconn=(?P<conn_id>\d+)'
        CONN_ID_INTERNAL_PATTERN = r'\sconn=(?P<conn_id>\d+|Internal\(\d+\))'
        OP_ID_PATTERN = r'\sop=(?P<op_id>\d+)'

        return {
            'RESULT_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_INTERNAL_PATTERN}                          # conn=int | conn=Internal(int)
                (\s\((?P<internal>Internal)\))?                     # Optional: (Internal)
                \sop=(?P<op_id>\d+)(?:\(\d+\)\(\d+\))?              # Optional: op=int, op=int(int)(int)
                \sRESULT                                            # RESULT
                \serr=(?P<err>\d+)                                  # err=int
                \stag=(?P<tag>\d+)                                  # tag=int
                \snentries=(?P<nentries>\d+)                        # nentries=int
                \swtime=(?P<wtime>\d+\.\d+)                         # wtime=float
                \soptime=(?P<optime>\d+\.\d+)                       # optime=float
                \setime=(?P<etime>\d+\.\d+)                         # etime=float
                (?:\sdn="(?P<dn>[^"]*)")?                           # Optional: dn="", dn="strings"
                (?:,\s+(?P<sasl_msg>SASL\s+bind\s+in\s+progress))?  # Optional: SASL bind in progress
                (?:\s+notes=(?P<notes>[A-Z]))?                      # Optional: notes[A-Z]
                (?:\s+details=(?P<details>"[^"]*"|))?               # Optional: details="string"
                (?:\s+pr_idx=(?P<pr_idx>\d+))?                      # Optional: pr_idx=int
                (?:\s+pr_cookie=(?P<pr_cookie>-?\d+))?              # Optional: pr_cookie=int, -int
            ''', re.VERBOSE), self._process_result_stats),
            'SEARCH_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int | conn=Internal(int)
                (\s\((?P<internal>Internal)\))?                     # Optional: (Internal)
                \sop=(?P<op_id>\d+)(?:\(\d+\)\(\d+\))?              # Optional: op=int, op=int(int)(int)
                \sSRCH                                              # SRCH
                \sbase="(?P<search_base>[^"]*)"                     # base="", "string"
                \sscope=(?P<search_scope>\d+)                       # scope=int
                \sfilter="(?P<search_filter>[^"]+)"                 # filter="string"
                (?:\s+attrs=(?P<search_attrs>ALL|\"[^"]*\"))?       # Optional: attrs=ALL | attrs="strings"
                (\s+options=(?P<options>\S+))?                      # Optional: options=persistent
                (?:\sauthzid="(?P<authzid_dn>[^"]*)")?              # Optional: dn="", dn="strings"
            ''', re.VERBOSE), self._process_search_stats),
            'BIND_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \sBIND                                              # BIND
                \sdn="(?P<bind_dn>.*?)"                             # Optional: dn=string
                (?:\smethod=(?P<bind_method>sasl|\d+))?             # Optional: method=int|sasl
                (?:\sversion=(?P<bind_version>\d+))?                # Optional: version=int
                (?:\smech=(?P<sasl_mech>[\w-]+))?                   # Optional: mech=string
                (?:\sauthzid="(?P<authzid_dn>[^"]*)")?              # Optional: authzid=string
            ''', re.VERBOSE), self._process_bind_stats),
            'UNBIND_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                (?:\sop=(?P<op_id>\d+))?                            # Optional: op=int
                \sUNBIND                                            # UNBIND
            ''', re.VERBOSE), self._process_unbind_stats),
            'CONNECT_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                \sfd=(?P<fd>\d+)                                    # fd=int
                \sslot=(?P<slot>\d+)                                # slot=int
                \s(?P<ssl>SSL\s)?                                   # Optional: SSL
                connection\sfrom\s                                  # connection from
                (?P<src_ip>\S+)\sto\s                               # IP to
                (?P<dst_ip>\S+)                                     # IP
            ''', re.VERBOSE), self._process_connect_stats),
            'DISCONNECT_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                \s+op=(?P<op_id>-?\d+)                              # op=int
                \s+fd=(?P<fd>\d+)                                   # fd=int
                \s*(?P<status>closed|Disconnect)                    # closed|Disconnect
                \s(?: [^ ]+)*
                \s(?:\s*(?P<error_code>-?\d+))?                     # Optional:
                \s*(?:.*-\s*(?P<disconnect_code>[A-Z]\d))?          # Optional: [A-Z]int
            ''', re.VERBOSE), self._process_disconnect_stats),
            'EXTEND_OP_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \sEXT                                               # EXT
                \soid="(?P<oid>[^"]+)"                              # oid="string"
                \sname="(?P<name>[^"]+)"                            # namme="string"
            ''', re.VERBOSE), self._process_extend_op_stats),
            'AUTOBIND_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                \s+AUTOBIND                                         # AUTOBIND
                \sdn="(?P<bind_dn>.*?)"                             # Optional: dn="strings"
            ''', re.VERBOSE), self._process_autobind_stats),
            'AUTH_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                \s+(?P<auth_protocol>SSL|TLS)                       # Match SSL or TLS
                (?P<auth_version>\d(?:\.\d)?)?                      # Capture the version (X.Y)
                \s+                                                 # Match one or more spaces
                (?P<auth_message>.+)                                # Capture an associated message
            ''', re.VERBOSE), self._process_auth_stats),
            'VLV_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \sVLV\s                                             # VLV
                (?P<result_code>\d+):                               # Currently not used
                (?P<target_pos>\d+):                                # Currently not used
                (?P<context_id>[A-Z0-9]+)                           # Currently not used
                (?::(?P<list_size>\d+))?\s                          # Currently not used
                (?P<first_index>\d+):                               # Currently not used
                (?P<last_index>\d+)\s                               # Currently not used
                \((?P<list_count>\d+)\)                             # Currently not used
            ''', re.VERBOSE), self._process_vlv_stats),
            'ABANDON_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \sABANDON                                           # ABANDON
                \stargetop=(?P<targetop>[\w\s]+)                    # targetop=string
                \smsgid=(?P<msgid>\d+)                              # msgid=int
            ''', re.VERBOSE), self._process_abandon_stats),
            'SORT_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \sSORT                                              # SORT
                \s+(?P<attribute>\w+)                               # Currently not used
                (?:\s+\((?P<status>\d+)\))?                         # Currently not used
            ''', re.VERBOSE), self._process_sort_stats),
            'CRUD_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_INTERNAL_PATTERN}                          # conn=int | conn=Internal(int)
                (\s\((?P<internal>Internal)\))?                     # Optional: (Internal)
                \sop=(?P<op_id>\d+)(?:\(\d+\)\(\d+\))?              # Optional: op=int, op=int(int)(int)
                \s(?P<op_type>ADD|CMP|MOD|DEL|MODRDN)               # ADD|CMP|MOD|DEL|MODRDN
                \sdn="(?P<dn>[^"]*)"                                # dn="", dn="strings"
                (?:\sauthzid="(?P<authzid_dn>[^"]*)")?              # Optional: dn="", dn="strings"
            ''', re.VERBOSE), self._process_crud_stats),
            'ENTRY_REFERRAL_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \s(?P<op_type>ENTRY|REFERRAL)                       # ENTRY|REFERRAL
                (?:\sdn="(?P<dn>[^"]*)")?                           # Optional: dn="", dn="string"
            ''', re.VERBOSE), self._process_entry_referral_stats)
        }

    def display_bind_report(self):
        """
        Display info on the tracked DNs.
        """
        print("\nBind Report")
        print("====================================================================\n")
        for k, v in self.bind['report_dn'].items():
            print(f"\nBind DN: {k}")
            print("--------------------------------------------------------------------\n")
            print("   Client Addresses:\n")
            ips = self.bind['report_dn'][k].get('ips', set())
            for i, ip in enumerate(ips, start=1):
                print(f"        {i}:      {ip}")
            print("\n   Operations Performed:\n")
            print(f"        Binds:      {self.bind['report_dn'][k].get('bind', 0)}")
            print(f"        Searches:   {self.bind['report_dn'][k].get('srch', 0)}")
            print(f"        Modifies:   {self.bind['report_dn'][k].get('mod', 0)}")
            print(f"        Adds:       {self.bind['report_dn'][k].get('add', 0)}")
            print(f"        Deletes:    {self.bind['report_dn'][k].get('del', 0)}")
            print(f"        Compares:   {self.bind['report_dn'][k].get('cmp', 0)}")
            print(f"        ModRDNs:    {self.bind['report_dn'][k].get('modrdn', 0)}")
            print(f"        Ext Ops:    {self.bind['report_dn'][k].get('ext', 0)}")

        print("Done.")

    def _match_line(self, line: str, bytes_read: int):
        """
        Process a single line from an access log, match it against predefined patterns,
        and handle the match by calling its corresponding handler function.

        Args:
            line (str): A single line from the access log.
            bytes_read (int): Total bytes read so far.

        Returns:
            bool: True if a match was found and processed, False otherwise.
        """
        for pattern, action in self.regexes.values():
            match = pattern.match(line)
            if not match:
                continue

            try:
                groups = match.groupdict()
            except AttributeError as e:
                self.logger.error(f"Error: {e} getting groups from match on line: {line}.")
                return False

            timestamp = groups.get('timestamp')
            if not timestamp:
                self.logger.error(f"Timestamp missing in line: {line}")
                return False

            # datetime library doesnt support nanoseconds so we need to "normalise"the timestamp
            try:
                norm_timestamp = self._convert_timestamp_to_datetime(timestamp)
            except (ValueError, IndexError, TypeError) as e:
                self.logger.error(f"Converting timestamp: {timestamp} to datetime failed with: {e}")
                return False

            # Add server restart count to groups for connection tracking
            groups['restart_ctr'] = self.server.get('restart_ctr', 0)

            # Are there time range restrictions
            parse_start = self.server.get('parse_start_time', None)
            parse_stop = self.server.get('parse_stop_time', None)

            if parse_start and parse_stop:
                if parse_start.microsecond == 0 and parse_stop.microsecond == 0:
                    norm_timestamp = norm_timestamp.replace(microsecond=0)

                if not (parse_start <= norm_timestamp <= parse_stop):
                    self.logger.debug(f"Timestamp {norm_timestamp} is out of range ({parse_start} - {parse_stop}). Skipping.")
                    return False

            # Get the first and last timestamps
            if self.server.get('first_time') is None:
                self.server['first_time'] = timestamp
            self.server['last_time'] = timestamp

            # Bump lines parsed
            self.server['lines_parsed'] = self.server.get('lines_parsed', 0) + 1

            # Call the associated method for this match
            action(groups)

            # Should we gather stats for this match
            if self.stats_interval and self.stats_file:
                self._process_and_write_stats(norm_timestamp, bytes_read)
                self.logger.debug(f"Stats processed for timestamp {norm_timestamp}.")

            return True

        self.logger.debug(f"No match found on line: {line}")
        return False

    def _process_result_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): Parsed groups from the log line.


        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'timestamp': The timestamp of the connection event.
                - 'conn_id': Connection identifier.
                - 'op_id': Operation identifier.
                - 'restart_ctr': Server restart count.
                - 'etime': Result elapsed time.
                - 'wtime': Result wait time.
                - 'optime': Result operation time.
                - 'nentries': Result number of entries returned.
                - 'tag': Bind response tag.
                - 'err': Result error code.
                - 'internal': Server internal operation.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            timestamp = groups.get('timestamp')
            conn_id = groups.get('conn_id')
            op_id = groups.get('op_id')
            restart_ctr = groups.get('restart_ctr')
            etime = float(groups.get('etime'))
            wtime = float(groups.get('wtime'))
            optime = float(groups.get('optime'))
            tag = groups.get('tag')
            err = groups.get('err')
            nentries = int(groups.get('nentries'))
            internal = groups.get('internal')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Mapping keys for this entry
        restart_conn_op_key = (restart_ctr, conn_id, op_id)
        restart_conn_key = (restart_ctr, conn_id)
        conn_op_key = (conn_id, op_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Bump global result count
        self.result['result_ctr'] = self.result.get('result_ctr', 0) + 1

        # Bump global result count
        self.result['timestamp_ctr'] = self.result.get('timestamp_ctr', 0) + 1

        # Longest etime, push current etime onto the heap
        heapq.heappush(self.result['etime_duration'], float(etime))

        # If the heap exceeds size_limit, pop the smallest element from root
        if len(self.result['etime_duration']) > self.size_limit:
            heapq.heappop(self.result['etime_duration'])

        # Longest wtime, push current wtime onto the heap
        heapq.heappush(self.result['wtime_duration'], float(wtime))

        # If the heap exceeds size_limit, pop the smallest element from root
        if len(self.result['wtime_duration']) > self.size_limit:
            heapq.heappop(self.result['wtime_duration'])

        # Longest optime, push current optime onto the heap
        heapq.heappush(self.result['optime_duration'], float(optime))

        # If the heap exceeds size_limit, pop the smallest element from root
        if len(self.result['optime_duration']) > self.size_limit:
            heapq.heappop(self.result['optime_duration'])

        # Total result times
        self.result['total_etime'] = self.result.get('total_etime', 0) + float(etime)
        self.result['total_wtime'] = self.result.get('total_wtime', 0) + float(wtime)
        self.result['total_optime'] = self.result.get('total_optime', 0) + float(optime)

        # Statistic reporting
        self.result['etime_stat'] = round(self.result['etime_stat'] + float(etime), 8)

        if err:
            # Capture error code
            self.result['error_freq'][err] = self.result['error_freq'].get(err, 0) + 1

        # Check for internal operations based on either conn_id or internal flag
        if 'Internal' in conn_id or internal:
            self.server['internal_op_ctr'] = self.server.get('internal_op_ctr', 0) + 1

        # Process result notes if present
        notes = groups['notes']
        if notes is not None:
            # match.group('notes') can be A|U|F
            self.result[f'notes{notes}_ctr'] = self.result.get(f'notes{notes}_ctr', 0) + 1
            # Track result times using server restart count, conn id and op_id as key
            self.result[f'notes{notes}_map'][restart_conn_op_key] = restart_conn_op_key

            # Construct the notes dict
            note_dict = getattr(self, f'notes{notes}')

            # Exclude VLV
            if restart_conn_op_key not in self.vlv['vlv_map_rco']:
                if restart_conn_op_key in note_dict:
                    note_dict[restart_conn_op_key]['time'] = timestamp
                else:
                    # First time round
                    note_dict[restart_conn_op_key] = {'time': timestamp}

                note_dict[restart_conn_op_key]['etime'] = etime
                note_dict[restart_conn_op_key]['nentries'] = nentries
                note_dict[restart_conn_op_key]['ip'] = (
                    self.connection['restart_conn_ip_map'].get(restart_conn_key, '')
                )

                if restart_conn_op_key in self.search['base_map_rco']:
                    note_dict[restart_conn_op_key]['base'] = self.search['base_map_rco'][restart_conn_op_key]
                    del self.search['base_map_rco'][restart_conn_op_key]

                if restart_conn_op_key in self.search['scope_map_rco']:
                    note_dict[restart_conn_op_key]['scope'] = self.search['scope_map_rco'][restart_conn_op_key]
                    del self.search['scope_map_rco'][restart_conn_op_key]

                if restart_conn_op_key in self.search['filter_map_rco']:
                    note_dict[restart_conn_op_key]['filter'] = self.search['filter_map_rco'][restart_conn_op_key]
                    del self.search['filter_map_rco'][restart_conn_op_key]

                note_dict[restart_conn_op_key]['bind_dn'] = self.bind['dn_map_rc'].get(restart_conn_key, '')

            elif restart_conn_op_key in self.vlv['vlv_map_rco']:
                # This "note" result is VLV, assign the note type for later filtering
                self.vlv['vlv_map_rco'][restart_conn_op_key] = notes

        # Trim the search data we dont need (not associated with a notes=X)
        if restart_conn_op_key in self.search['base_map_rco']:
            del self.search['base_map_rco'][restart_conn_op_key]

        if restart_conn_op_key in self.search['scope_map_rco']:
            del self.search['scope_map_rco'][restart_conn_op_key]

        if restart_conn_op_key in self.search['filter_map_rco']:
            del self.search['filter_map_rco'][restart_conn_op_key]

        # Process bind response based on the tag and error code.
        if tag == '97':
            # Invalid credentials|Entry does not exist
            if err == '49':
                # if self.verbose:
                bad_pwd_dn = self.bind['dn_map_rc'].get(restart_conn_key, None)
                bad_pwd_ip = self.connection['restart_conn_ip_map'].get(restart_conn_key, None)
                self.result['bad_pwd_map'][(bad_pwd_dn, bad_pwd_ip)] = (
                    self.result['bad_pwd_map'].get((bad_pwd_dn, bad_pwd_ip), 0) + 1
                )
                # Trim items to size_limit
                if len(self.result['bad_pwd_map']) > self.size_limit:
                    within_size_limit = dict(
                        sorted(
                            self.result['bad_pwd_map'].items(),
                            key=lambda item: item[1],
                            reverse=True
                        )[:self.size_limit])
                    self.result['bad_pwd_map'] = within_size_limit

            # Ths result is involved in the SASL bind process, decrement bind count, etc
            elif err == '14':
                self.bind['bind_ctr'] = self.bind.get('bind_ctr', 0) - 1
                self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) - 1
                self.bind['sasl_bind_ctr'] = self.bind.get('sasl_bind_ctr', 0) - 1
                self.bind['version']['3'] = self.bind['version'].get('3', 0) - 1

                # Drop the sasl mech count also
                mech = self.bind['sasl_map_co'].get(conn_op_key, 0)
                if mech:
                    self.bind['sasl_mech_freq'][mech] = self.bind['sasl_mech_freq'].get(mech, 0) - 1
            # Is this is a result to a sasl bind
            else:
                result_dn = groups['dn']
                if result_dn:
                    if result_dn != "":
                        # If this is a result of a sasl bind, grab the dn
                        if conn_op_key in self.bind['sasl_map_co']:
                            if result_dn is not None:
                                self.bind['dn_map_rc'][restart_conn_key] = result_dn.lower()
                                self.bind['dn_freq'][result_dn] = (
                                    self.bind['dn_freq'].get(result_dn, 0) + 1
                                )
        # Handle other tag values
        elif tag in ['100', '101', '111', '115']:

            # Largest nentry, push current nentry onto the heap, no duplicates
            if int(nentries) not in self.result['nentries_set']:
                heapq.heappush(self.result['nentries_num'], int(nentries))
                self.result['nentries_set'].add(int(nentries))

            # If the heap exceeds size_limit, pop the smallest element from root
            if len(self.result['nentries_num']) > self.size_limit:
                removed = heapq.heappop(self.result['nentries_num'])
                self.result['nentries_set'].remove(removed)

    def _process_search_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'op_id': Operation identifier.
                - 'restart_ctr': Server restart count.
                - 'search_base': Search base.
                - 'search_scope': Search scope.
                - 'search_attrs':  Search attributes.
                - 'search_filter':  Search filter.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            op_id = groups.get('op_id')
            restart_ctr = groups.get('restart_ctr')
            search_base = groups['search_base']
            search_scope = groups['search_scope']
            search_attrs = groups['search_attrs']
            search_filter = groups['search_filter']
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking keys for this entry
        restart_conn_op_key = (restart_ctr, conn_id, op_id)
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Bump search and global op count
        self.search['search_ctr'] = self.search.get('search_ctr', 0) + 1
        self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

        # Search attributes
        if search_attrs is not None:
            if search_attrs == 'ALL':
                self.search['attr_dict']['All Attributes'] += 1
            else:
                for attr in search_attrs.split():
                    attr = attr.strip('"')
                    self.search['attr_dict'][attr] += 1

        # If the associated conn id for the bind DN matches update op counter
        for dn in self.bind['report_dn']:
            conns = self.bind['report_dn'][dn]['conn']
            if conn_id in conns:
                bind_dn_key = self._report_dn_key(dn, self.report_dn)
                if bind_dn_key:
                    self.bind['report_dn'][bind_dn_key]['srch'] = self.bind['report_dn'][bind_dn_key].get('srch', 0) + 1

        # Search base
        if search_base is not None:
            if search_base:
                base = search_base
            # Empty string ("")
            else:
                base = "Root DSE"
            search_base = base.lower()
            if search_base:
                if self.verbose:
                    self.search['base_map'][search_base] = self.search['base_map'].get(search_base, 0) + 1
                    self.search['base_map_rco'][restart_conn_op_key] = search_base

        # Search scope
        if search_scope is not None:
            if self.verbose:
                self.search['scope_map_rco'][restart_conn_op_key] = SCOPE_LABEL[int(search_scope)]

        # Search filter
        if search_filter is not None:
            if self.verbose:
                self.search['filter_map_rco'][restart_conn_op_key] = search_filter
                self.search['filter_dict'][search_filter] = self.search['filter_dict'].get(search_filter, 0) + 1

                found = False
                for idx, (count, filter) in enumerate(self.search['filter_list']):
                    if filter == search_filter:
                        found = True
                        self.search['filter_list'][idx] = (self.search['filter_dict'][search_filter] + 1, search_filter)
                        heapq.heapify(self.search['filter_list'])
                        break

                if not found:
                    if len(self.search['filter_list']) < self.size_limit:
                        heapq.heappush(self.search['filter_list'], (1, search_filter))
                    else:
                        heapq.heappushpop(self.search['filter_list'], (self.search['filter_dict'][search_filter], search_filter))

        # Check for an entire base search
        if "objectclass=*" in search_filter.lower() or "objectclass=top" in search_filter.lower():
            if search_scope == '2':
                self.search['base_search_ctr'] = self.search.get('base_search_ctr', 0) + 1

        # Persistent search
        if groups['options'] is not None:
            options = groups['options']
            if options == 'persistent':
                self.search['persistent_ctr'] = self.search.get('persistent_ctr', 0) + 1

        # Authorization identity
        if groups['authzid_dn'] is not None:
            self.search['authzid'] = self.search.get('authzid', 0) + 1

    def _process_bind_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'op_id': Operation identifier.
                - 'restart_ctr': Server restart count.
                - 'bind_dn': Bind DN.
                - 'bind_method': Bind method (sasl, simple).
                - 'bind_version': Bind version.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            op_id = groups.get('op_id')
            restart_ctr = groups.get('restart_ctr')
            bind_dn = groups.get('bind_dn')
            bind_method = groups['bind_method']
            bind_version = groups['bind_version']
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # If this is the first connection (indicating a server restart), increment restart counter
        if conn_id == '1':
            self.server['restart_ctr'] = self.server.get('restart_ctr', 0) + 1

        # Create a tracking keys for this entry
        restart_conn_key = (restart_ctr, conn_id)
        conn_op_key = (conn_id, op_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Bump bind and global op count
        self.bind['bind_ctr'] = self.bind.get('bind_ctr', 0) + 1
        self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

        # Update bind version count
        self.bind['version'][bind_version] = self.bind['version'].get(bind_version, 0) + 1
        if bind_dn == "":
            bind_dn = 'Anonymous'

        # If we need to report on this DN, capture some info for tracking
        bind_dn_key = self._report_dn_key(bind_dn, self.report_dn)
        if bind_dn_key:
            # Update bind count
            self.bind['report_dn'][bind_dn_key]['bind'] = self.bind['report_dn'][bind_dn_key].get('bind', 0) + 1
            # Connection ID
            self.bind['report_dn'][bind_dn_key]['conn'].add(conn_id)
            # Loop over IPs captured at connection time to find the associated IP
            for (ip, ip_info) in self.connection['ip_map'].items():
                if restart_conn_key in ip_info['keys']:
                    self.bind['report_dn'][bind_dn_key]['ips'].add(ip)

        # sasl or simple bind
        if bind_method == 'sasl':
            self.bind['sasl_bind_ctr'] = self.bind.get('sasl_bind_ctr', 0) + 1
            sasl_mech = groups['sasl_mech']
            if sasl_mech is not None:
                # Bump sasl mechanism count
                self.bind['sasl_mech_freq'][sasl_mech] = self.bind['sasl_mech_freq'].get(sasl_mech, 0) + 1

                # Keep track of bind key to handle sasl result later
                self.bind['sasl_map_co'][conn_op_key] = sasl_mech

            if bind_dn != "Anonymous":
                if bind_dn.casefold() == self.root_dn.casefold():
                    self.bind['rootdn_bind_ctr'] = self.bind.get('rootdn_bind_ctr', 0) + 1

                # if self.verbose:
                self.bind['dn_freq'][bind_dn] = self.bind['dn_freq'].get(bind_dn, 0) + 1
                self.bind['dn_map_rc'][restart_conn_key] = bind_dn.lower()
        else:
            if bind_dn == "Anonymous":
                self.bind['anon_bind_ctr'] = self.bind.get('anon_bind_ctr', 0) + 1
                self.bind['dn_freq']['Anonymous'] = self.bind['dn_freq'].get('Anonymous', 0) + 1
                self.bind['dn_map_rc'][restart_conn_key] = "anonymous"
            else:
                if bind_dn.casefold() == self.root_dn.casefold():
                    self.bind['rootdn_bind_ctr'] = self.bind.get('rootdn_bind_ctr', 0) + 1

                # if self.verbose:
                self.bind['dn_freq'][bind_dn] = self.bind['dn_freq'].get(bind_dn, 0) + 1
                self.bind['dn_map_rc'][restart_conn_key] = bind_dn.lower()

    def _process_unbind_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'restart_ctr': Server restart count.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            restart_ctr = groups.get('restart_ctr')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Bump unbind count
        self.bind['unbind_ctr'] = self.bind.get('unbind_ctr', 0) + 1

    def _process_connect_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'restart_ctr': Server restart count.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            timestamp = groups.get('timestamp')
            conn_id = groups.get('conn_id')
            restart_ctr = groups.get('restart_ctr')
            src_ip = groups.get('src_ip')
            fd = groups['fd']
            ssl = groups['ssl']
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_key = (restart_ctr, conn_id)

        # Should we exclude this IP
        if self.exclude_ip and src_ip in self.exclude_ip:
            self.connection['exclude_ip_map'][restart_conn_key] = src_ip
            return None

        if self.verbose:
            # Update open connection count
            self.connection['open_conns'][src_ip] = self.connection['open_conns'].get(src_ip, 0) + 1

            # Track the connection start normalised datetime object for latency report
            self.connection['start_time'][conn_id] = groups.get('timestamp')

        # Update general connection counters
        for key in ['conn_ctr', 'sim_conn_ctr']:
            self.connection[key] = self.connection.get(key, 0) + 1

        # Update the maximum number of simultaneous connections seen
        self.connection['max_sim_conn_ctr'] = max(
            self.connection.get('max_sim_conn_ctr', 0),
            self.connection['sim_conn_ctr']
        )

        # Update protocol counters
        src_ip_tmp = 'local' if src_ip == 'local' else 'ldap'
        if ssl:
            stat_count_key = 'ldaps_ctr'
        else:
            stat_count_key = 'ldapi_ctr' if src_ip_tmp == 'local' else 'ldap_ctr'
        self.connection[stat_count_key] = self.connection.get(stat_count_key, 0) + 1

        # Track file descriptor counters
        self.connection['fd_max_ctr'] = (
            max(self.connection.get('fd_max_ctr', 0), int(fd))
        )
        self.connection['fd_taken_ctr'] = (
            self.connection.get('fd_taken_ctr', 0) + 1
        )

        # Track source IP
        self.connection['restart_conn_ip_map'][restart_conn_key] = src_ip

        # Update the count of connections seen from this IP
        if src_ip not in self.connection['ip_map']:
            self.connection['ip_map'][src_ip] = {}

        self.connection['ip_map'][src_ip]['count'] = self.connection['ip_map'][src_ip].get('count', 0) + 1

        if 'keys' not in self.connection['ip_map'][src_ip]:
            self.connection['ip_map'][src_ip]['keys'] = set()

        self.connection['ip_map'][src_ip]['keys'].add(restart_conn_key)
        # self.connection['ip_map']['ip_key'] = restart_conn_key

    def _process_auth_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'restart_ctr': Server restart count.
                - 'auth_protocol': Auth protocol (SSL, TLS).
                - 'auth_version': Auth version.
                - 'auth_message': Optional auth message.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            restart_ctr = groups.get('restart_ctr')
            auth_protocol = groups.get('auth_protocol')
            auth_version = groups.get('auth_version')
            auth_message = groups.get('auth_message')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        if auth_protocol:
            if restart_conn_key not in self.auth['auth_info']:
                self.auth['auth_info'][restart_conn_key] = {
                    'proto': auth_protocol,
                    'version': auth_version,
                    'count': 0,
                    'message': []
                    }

            if auth_message:
                # Increment counters and add auth message
                self.auth['auth_info'][restart_conn_key]['message'].append(auth_message)

            # Bump auth related counters
            self.auth['cipher_ctr'] = self.auth.get('cipher_ctr', 0) + 1
            self.auth['auth_info'][restart_conn_key]['count'] = (
                self.auth['auth_info'][restart_conn_key].get('count', 0) + 1
            )

        if auth_message:
            if auth_message == 'client bound as':
                self.auth['ssl_client_bind_ctr'] = self.auth.get('ssl_client_bind_ctr', 0) + 1
            elif auth_message == 'failed to map client certificate to LDAP DN':
                self.auth['ssl_client_bind_failed_ctr'] = self.auth.get('ssl_client_bind_failed_ctr', 0) + 1

    def _process_vlv_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'op_id': Operation identifier.
                - 'restart_ctr': Server restart count.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            op_id = groups.get('op_id')
            restart_ctr = groups.get('restart_ctr')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create tracking keys
        restart_conn_op_key = (restart_ctr, conn_id, op_id)
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Bump vlv and global op stats
        self.vlv['vlv_ctr'] = self.vlv.get('vlv_ctr', 0) + 1
        self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

        # Key and value are the same, makes set operations easier later on
        self.vlv['vlv_map_rco'][restart_conn_op_key] = restart_conn_op_key

    def _process_abandon_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'op_id': Operation identifier.
                - 'restart_ctr': Server restart count.
                - 'targetop': The target operation.
                - 'msgid': Message ID.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            op_id = groups.get('op_id')
            restart_ctr = groups.get('restart_ctr')
            targetop = groups.get('targetop')
            msgid = groups.get('msgid')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking keys
        restart_conn_op_key = (restart_ctr, conn_id, op_id)
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Bump some stats
        self.result['result_ctr'] = self.result.get('result_ctr', 0) + 1
        self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1
        self.operation['abandon_op_ctr'] = self.operation.get('abandon_op_ctr', 0) + 1

        # Track abandoned operation for later processing
        self.operation['abandoned_map_rco'][restart_conn_op_key] = (conn_id, op_id, targetop, msgid)

    def _process_sort_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'restart_ctr': Server restart count.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            restart_ctr = groups.get('restart_ctr')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        self.operation['sort_op_ctr'] = self.operation.get('sort_op_ctr', 0) + 1

    def _process_extend_op_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'op_id': Operation identifier.
                - 'restart_ctr': Server restart count.
                - 'oid': Extended operation identifier.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            op_id = groups.get('op_id')
            restart_ctr = groups.get('restart_ctr')
            oid = groups.get('oid')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_op_key = (restart_ctr, conn_id, op_id)
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Increment global operation counters
        self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1
        self.operation['extnd_op_ctr'] = self.operation.get('extnd_op_ctr', 0) + 1

        # Track extended operation data if an OID is present
        if oid is not None:
            self.operation['extop_dict'][oid] = self.operation['extop_dict'].get(oid, 0) + 1
            self.operation['extop_map_rco'][restart_conn_op_key] = (
                self.operation['extop_map_rco'].get(restart_conn_op_key, 0) + 1
            )

        # If the conn_id is associated with this DN, update op counter
        for dn in self.bind['report_dn']:
            conns = self.bind['report_dn'][dn]['conn']
            if conn_id in conns:
                bind_dn_key = self._report_dn_key(dn, self.report_dn)
                if bind_dn_key:
                    self.bind['report_dn'][bind_dn_key]['ext'] = self.bind['report_dn'][bind_dn_key].get('ext', 0) + 1

    def _process_autobind_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'restart_ctr': Server restart count.
                - 'bind_dn': Bind DN ("cn=Directory Manager")

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            restart_ctr = groups.get('restart_ctr')
            bind_dn = groups.get('bind_dn')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Bump relevant counters
        self.bind['bind_ctr'] = self.bind.get('bind_ctr', 0) + 1
        self.bind['autobind_ctr'] = self.bind.get('autobind_ctr', 0) + 1
        self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

        # Handle an anonymous autobind (empty bind_dn)
        if bind_dn == "":
            self.bind['anon_bind_ctr'] = self.bind.get('anon_bind_ctr', 0) + 1
        else:
            # Process non-anonymous binds, does the bind_dn if exist in dn_map_rc
            bind_dn = self.bind['dn_map_rc'].get(restart_conn_key, bind_dn)
            if bind_dn:
                if bind_dn.casefold() == self.root_dn.casefold():
                    self.bind['rootdn_bind_ctr'] = self.bind.get('rootdn_bind_ctr', 0) + 1
                bind_dn = bind_dn.lower()
                self.bind['dn_freq'][bind_dn] = self.bind['dn_freq'].get(bind_dn, 0) + 1

    def _process_disconnect_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'restart_ctr': Server restart count.
                - 'timestamp': The timestamp of the disconnect event.
                - 'error_code': Error code associated with the disconnect, if any.
                - 'disconnect_code': Disconnect code, if any.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            restart_ctr = groups.get('restart_ctr')
            timestamp = groups.get('timestamp')
            error_code = groups.get('error_code')
            disconnect_code = groups.get('disconnect_code')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        if self.verbose:
            # Handle verbose logging for open connections and IP addresses
            src_ip = self.connection['restart_conn_ip_map'].get(restart_conn_key)
            if src_ip and src_ip in self.connection.get('open_conns', {}):
                open_conns = self.connection['open_conns']
                if open_conns[src_ip] > 1:
                    open_conns[src_ip] -= 1
                else:
                    del open_conns[src_ip]

        # Handle latency and disconnect times
        if self.verbose:
            start_time = self.connection['start_time'].get(conn_id, None)
            finish_time = groups.get('timestamp')
            if start_time and timestamp:
                latency = self.get_elapsed_time(start_time, finish_time, "seconds")
                bucket = self._group_latencies(latency)
                LATENCY_GROUPS[bucket] += 1

                # Reset start time for the connection
                self.connection['start_time'][conn_id] = None

        # Update connection stats
        self.connection['sim_conn_ctr'] = self.connection.get('sim_conn_ctr', 0) - 1
        self.connection['fd_returned_ctr'] = (
            self.connection.get('fd_returned_ctr', 0) + 1
        )

        # Track error and disconnect codes if provided
        if error_code is not None:
            error_type = DISCONNECT_ERRORS.get(error_code, 'unknown')
            if disconnect_code is not None:
                # Increment the count for the specific error and disconnect code
                error_map = self.connection.setdefault(error_type, {})
                error_map[disconnect_code] = error_map.get(disconnect_code, 0) + 1

        # Handle disconnect code and update stats
        if disconnect_code is not None:
            self.connection['disconnect_code'][disconnect_code] = (
                self.connection['disconnect_code'].get(disconnect_code, 0) + 1
            )
            self.connection['disconnect_code_map'][restart_conn_key] = disconnect_code

    def _group_latencies(self, latency_seconds: int):
        """
        Group latency values into predefined categories.

        Args:
            latency_seconds (int): The latency in seconds.

        Returns:
            str: A group corresponding to the latency.
        """
        if latency_seconds <= 1:
            return "<= 1"
        elif latency_seconds == 2:
            return "== 2"
        elif latency_seconds == 3:
            return "== 3"
        elif 4 <= latency_seconds <= 5:
            return "4-5"
        elif 6 <= latency_seconds <= 10:
            return "6-10"
        elif 11 <= latency_seconds <= 15:
            return "11-15"
        else:
            return "> 15"

    def _process_crud_stats(self, groups):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'restart_ctr': Server restart count.
                - 'op_id': Operation identifier.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            restart_ctr = groups.get('restart_ctr')
            op_type = groups.get('op_type')
            internal = groups.get('internal')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

        # Use operation type as key for stats
        if op_type is not None:
            op_key = op_type.lower()
            self.operation[f"{op_key}_op_ctr"] = self.operation.get(f"{op_key}_op_ctr", 0) + 1
            self.operation[f"{op_key}_map_rco"][restart_conn_key] = (
                self.operation[f"{op_key}_map_rco"].get(restart_conn_key, 0) + 1
            )

        # If the conn_id is associated with this DN, update op counter
        for dn in self.bind['report_dn']:
            conns = self.bind['report_dn'][dn]['conn']
            if conn_id in conns:
                bind_dn_key = self._report_dn_key(dn, self.report_dn)
                if bind_dn_key:
                    self.bind['report_dn'][bind_dn_key][op_key] = self.bind['report_dn'][bind_dn_key].get(op_key, 0) + 1

        # Authorization identity
        if groups['authzid_dn'] is not None:
            self.operation['authzid'] = self.operation.get('authzid', 0) + 1

    def _process_entry_referral_stats(self, groups: dict):
        """
        Process and update statistics based on the parsed result group.

        Args:
            groups (dict): A dictionary containing operation information. Expected keys:
                - 'conn_id': Connection identifier.
                - 'restart_ctr': Server restart count.
                - 'op_id': Operation identifier.

        Raises:
            KeyError: If required keys are missing in the `groups` dictionary.
        """
        try:
            conn_id = groups.get('conn_id')
            restart_ctr = groups.get('restart_ctr')
            op_type = groups.get('op_type')
        except KeyError as e:
            self.logger.error(f"Missing key in groups: {e}")
            return

        # Create a tracking key for this entry
        restart_conn_key = (restart_ctr, conn_id)

        # Should we ignore this operation
        if restart_conn_key in self.connection['exclude_ip_map']:
            return None

        # Process operation type
        if op_type is not None:
            if op_type == 'ENTRY':
                self.result['entry_count'] = self.result.get('entry_count', 0) + 1
            elif op_type == 'REFERRAL':
                self.result['referral_count'] = self.result.get('referral_count', 0) + 1

    def _process_and_write_stats(self, norm_timestamp: str, bytes_read: int):
        """
        Processes statistics and writes them to the CSV file at defined intervals.

        Args:
            norm_timestamp: Normalized datetime for the current match
            bytes_read: Number of bytes read in the current file

        Returns:
            None
        """

        if self.csv_writer is None:
            self.logger.error("CSV writer not enabled.")
            return

        # Define the stat mapping
        stats = {
            'result_ctr': self.result,
            'search_ctr': self.search,
            'add_op_ctr': self.operation,
            'mod_op_ctr': self.operation,
            'modrdn_op_ctr': self.operation,
            'cmp_op_ctr': self.operation,
            'del_op_ctr': self.operation,
            'abandon_op_ctr': self.operation,
            'conn_ctr': self.connection,
            'ldaps_ctr': self.connection,
            'bind_ctr': self.bind,
            'anon_bind_ctr': self.bind,
            'unbind_ctr': self.bind,
            'notesA_ctr': self.result,
            'notesU_ctr': self.result,
            'notesF_ctr': self.result,
            'etime_stat': self.result
        }

        # Build the current stat block
        curr_stat_block = [norm_timestamp]
        curr_stat_block.extend([refdict[key] for key, refdict in stats.items()])

        curr_time = curr_stat_block[0]

        # Check for previous stats for differences
        if self.prev_stats is not None:
            prev_stat_block = self.prev_stats
            prev_time = prev_stat_block[0]

            # Prepare the output block
            out_stat_block = [prev_stat_block[0], int(prev_time.timestamp())]
            # out_stat_block = [prev_stat_block[0], prev_time]

            # Get the time difference, check is it > the specified interval
            time_diff = (curr_time - prev_time).total_seconds()
            if time_diff >= self.stats_interval:
                # Compute differences between current and previous stats
                diff_stats = [
                    curr - prev if isinstance(prev, int) else curr
                    for curr, prev in zip(curr_stat_block[1:], prev_stat_block[1:])
                ]
                out_stat_block.extend(diff_stats)

                # Write the stat block to csv and reset elapsed time for the next interval

                # out_stat_block[0] = self._convert_datetime_to_timestamp(out_stat_block[0])
                self.csv_writer.writerow(out_stat_block)

                self.result['etime_stat'] = 0.0

                # Update previous stats for the next interval
                self.prev_stats = curr_stat_block

        else:
            # This is the first run, add the csv header for each column
            stats_header = [
                'Time', 'time_t', 'Results', 'Search', 'Add', 'Mod', 'Modrdn', 'Compare',
                'Delete', 'Abandon', 'Connections', 'SSL Conns', 'Bind', 'Anon Bind', 'Unbind',
                'Unindexed search', 'Unindexed component', 'Invalid filter', 'ElapsedTime'
            ]
            self.csv_writer.writerow(stats_header)
            self.prev_stats = curr_stat_block

        # end of file and a previous block needs to be written
        if bytes_read >= self.file_size and self.prev_stats is not None:
            # Final write for the last block of stats
            prev_stat_block = self.prev_stats
            diff_stats = [
                curr - prev if isinstance(prev, int) else curr
                for curr, prev in zip(curr_stat_block[1:], prev_stat_block[1:])
            ]
            out_stat_block = [prev_stat_block[0], int(curr_time.timestamp())]
            out_stat_block.extend(diff_stats)

            # Write the stat block to csv and reset elapsed time for the next interval
            # out_stat_block[0] = self._convert_datetime_to_timestamp(out_stat_block[0])
            self.csv_writer.writerow(out_stat_block)
            self.result['etime_stat'] = 0.0

    def process_file(self, log_num: str, filepath: str):
        """
        Process a file line by line, supporting both compressed and uncompressed formats.

        Args:
            log_num (str): Log file number (Used for multiple log files).
            filepath (str): Path to the file.

        Returns:
            None
        """
        file_size = 0
        curr_position = 0
        line_number = 0
        lines_read = 0
        line_count = 0
        line_count_limit = 25000

        self.logger.info(f"Processing file: {filepath}")

        try:
            # Is log compressed
            comptype = self._is_file_compressed(filepath)
            if (comptype):
                # If comptype is True, comptype[1] is MIME type
                if comptype[1] == 'application/gzip':
                    filehandle = gzip.open(filepath, 'rb')
                else:
                    self.logger.warning(f"Unsupported compression type: {comptype}. Attempting to process as uncompressed.")
                    filehandle = open(filepath, 'rb')
            else:
                filehandle = open(filepath, 'rb')

            with filehandle:
                # Seek to the end
                filehandle.seek(0, os.SEEK_END)
                file_size = filehandle.tell()
                self.file_size = file_size
                self.logger.info(f"{filehandle.name} size (bytes): {file_size}")

                # Back to the start
                filehandle.seek(0)
                print(f"[{log_num:03d}] {filehandle.name:<30}\tsize (bytes): {file_size:>12}")

                for line in filehandle:
                    line_number += 1
                    try:
                        line_content = line.decode('utf-8').strip()
                        if line_content.startswith('['):
                            # Entry to parsing logic
                            proceed = self._match_line(line_content, filehandle.tell())
                            if proceed is False:
                                self.logger.debug(f"Skipping line: {filehandle.name}:{line_number}.")
                                continue

                            line_count += 1
                            lines_read += 1

                        # Is it time to give an update
                        if line_count >= line_count_limit:
                            curr_position = filehandle.tell()
                            percent = curr_position/file_size * 100.0
                            print(f"{lines_read:10d} Lines Processed     {curr_position:12d} of {file_size:12d} bytes ({percent:.3f}%)")
                            line_count = 0

                    except UnicodeDecodeError as de:
                        self.logger.error(f"non-decodable line at position {filehandle.tell()}: {de}")

        except FileNotFoundError:
            self.logger.error(f"File not found: {filepath}")
        except IOError as ie:
            self.logger.error(f"IO error processing file {filepath}: {ie}")

    def _is_file_compressed(self, filepath: str):
        """
        Determines if a file is compressed based on its MIME type.

        Args:
            filepath (str): The path to the file.

        Returns:
            TrueCompressionStatus | str:
                - CompressionStatus.COMPRESSED and the MIME type if compressed.
                - CompressionStatus.NOT_COMPRESSED if not compressed.
                - CompressionStatus.FILE_NOT_FOUND if the file does not exist.
        Returns:
            A tuple where the first element indicates if the file is compressed with a supported
            method, the second element is the supported compression method.
            False, when a non supported compression method is detected.
            None, If the file does not exist or on exception.
        """
        if not os.path.exists(filepath):
            self.logger.error(f"File not found: {filepath}")
            return None

        try:
            filetype = magic.detect_from_filename(filepath)

            # List of supported compression types
            compressed_mime_types = [
                'application/gzip',             # gz, tar.gz, tgz
                'application/x-gzip',           # gz, tgz
            ]

            if filetype.mime_type in compressed_mime_types:
                self.logger.info(f"File is compressed: {filepath} (MIME: {filetype.mime_type})")
                return True, filetype.mime_type
            else:
                self.logger.info(f"File is not compressed: {filepath}")
                return False

        except Exception as e:
            self.logger.error(f"Error while determining compression for file {filepath}: {e}")
            return None


    def _report_dn_key(self, dn_to_check: str, report_dn: str):
        """
        Check if we need to report on this DN

        Args:
            dn_to_check (str): DN to check.
            report_dn (str): Report DN specified as argument.

        Returns:
            str: The DN key or None
        """
        if dn_to_check and report_dn:
            norm_dn_to_check = dn_to_check.lower()
            norm_report_dn_key = report_dn.lower()

            if norm_report_dn_key == 'all':
                if norm_dn_to_check == 'anonymous':
                    return 'Anonymous'
                return norm_dn_to_check

            if norm_report_dn_key == 'anonymous' and norm_dn_to_check == "anonymous":
                return 'Anonymous'

            if norm_report_dn_key == norm_dn_to_check:
                return norm_dn_to_check

        return None

    def _convert_timestamp_to_datetime(self, timestamp: str):
        """
        Converts a timestamp in the formats:
        '[28/Mar/2002:13:14:22 -0800]' or
        '[07/Jun/2023:09:55:50.638781270 +0000]'
        to a Python datetime object. Nanoseconds are truncated to microseconds.

        Args:
            timestamp (str): The timestamp string to convert.

        Returns:
            datetime: The equivalent datetime object with timezone.

        Raises:
            ValueError: If the timestamp format is invalid.
            IndexError: If the timestamp does not have the expected number of components.
            TypeError: If the timestamp is not a string or has an invalid type.
        """
        if not isinstance(timestamp, str):
            raise TypeError("Timestamp must be a string.")

        try:
            timestamp = timestamp.strip("[]")
            # Separate datetime and timezone components
            datetime_part, tz_offset = timestamp.rsplit(" ", 1)

            # Timestamp includes nanoseconds
            if '.' in datetime_part:
                datetime_part, nanos = datetime_part.rsplit(".", 1)
                # Truncate
                nanos = nanos[:6]
                datetime_with_micros = f"{datetime_part}.{nanos}"
                timeformat = "%d/%b/%Y:%H:%M:%S.%f"
            else:
                datetime_with_micros = datetime_part
                timeformat = "%d/%b/%Y:%H:%M:%S"

            # Parse the datetime component
            dt = datetime.strptime(datetime_with_micros, timeformat)

            # Calc the timezone offset
            if tz_offset[0] == "+":
                sign = 1
            else:
                sign = -1

            hours_offset = int(tz_offset[1:3])
            minutes_offset = int(tz_offset[3:5])
            delta = timedelta(hours=hours_offset, minutes=minutes_offset)

            # Apply the timezone offset
            dt_with_tz = dt.replace(tzinfo=timezone(sign * delta))
            return dt_with_tz

        except (ValueError, IndexError, TypeError) as e:
            self.logger.error(f"Error converting timestamp: {timestamp}. Exception: {e}")
            raise

    def convert_timestamp_to_string(self, timestamp: str):
        """
        Truncate an access log timestamp and convert to datetime
        the timestamp '[07/Jun/2023:09:55:50.638781123 +0000]' to '[07/Jun/2023:09:55:50.638781 +0000]'
        '[28/Mar/2002:13:14:22 -0800]' or
        '[07/Jun/2023:09:55:50.638781 +0000]' or
        '[07/Jun/2023:09:55:50.638781123 +0000]'

        Args:
            timestamp (str): Access log timestamp.

        Returns:
            str: The formatted timestamp string.

        Raises:
            ValueError: If the timestamp format is invalid or empty.
        """
        if not timestamp:
            raise ValueError("Timestamp is empty or None.")

        try:
            # Ensure timestamp is long enough before slicing
            if len(timestamp) >= 29:
                timestamp = timestamp[:26] + timestamp[29:]

            dt = datetime.strptime(timestamp, "%d/%b/%Y:%H:%M:%S.%f %z")
            return dt.strftime("%d/%b/%Y:%H:%M:%S")

        except (ValueError) as e:
            self.logger.error(f"Converting timestamp: {timestamp} to datetime failed - {e}" )
            raise

    def get_elapsed_time(self, start: str, finish: str, time_format=None):
        """
        Calculates the elapsed time between start and finish datetimes.

        Args:
            start (str): The start time.
            finish (str): The finish time.
            time_format (str): Output format ("seconds" or "hms").

        Returns:
            float:Elapsed time in seconds or tuple:(hours, minutes, seconds).

        Raises:
            ValueError: If the timestamp format is invalid.
            IndexError: If the timestamp does not have the expected number of components.
            TypeError: If the timestamp is not a string or has an invalid type.
        """
        # Default time_format to "seconds"
        if time_format is None:
            time_format = "seconds"

        # If start or finish is missing, return 0 or "0 hours, 0 minutes, 0 seconds"
        if not start or not finish:
            return 0 if time_format == "seconds" else "0 hours, 0 minutes, 0 seconds"

        try:
            first_time = self._convert_timestamp_to_datetime(start)
            last_time = self._convert_timestamp_to_datetime(finish)
        except (ValueError, IndexError, TypeError) as e:
            self.logger.error(f"Error converting timestamps: {e}. Start: {start}, Finish: {finish}")
            return 0 if time_format == "seconds" else "0 hours, 0 minutes, 0 seconds"

        if first_time is None or last_time is None:
            if time_format == "seconds":
                return (0)
            else:
                return (0, 0, 0)

        # Get elapsed time, format for output
        elapsed_time = (last_time - first_time)
        total_seconds = elapsed_time.total_seconds()

        if time_format == "seconds":
            return total_seconds

        # Convert to hours, minutes, and seconds
        days = elapsed_time.days
        remainder_seconds = total_seconds - (days * 24 * 3600)
        hours, remainder = divmod(remainder_seconds, 3600)
        minutes, seconds = divmod(remainder, 60)

        if days > 0:
            return f"{int(days)} days, {int(hours)} hours, {int(minutes)} minutes, {int(seconds)} seconds"
        else:
            return f"{int(hours)} hours, {int(minutes)} minutes, {int(seconds)} seconds"

    def get_overall_perf(self, num_results: int, num_ops: int):
        """
        Calculate the overall performance as a percentage.

        Args:
            num_results (int): Number of results.
            num_ops (int): Number of operations.

        Returns:
            float: Performance percentage, limited to a maximum of 100.0

        Raises:
            ValueError: On negative args.
        """
        if num_results < 0 or num_ops < 0:
            raise ValueError("Inputs num_results and num_ops must be non-negative.")

        if num_ops == 0:
            return 0.0

        perf = min((num_results / num_ops) * 100, 100.0)
        return round(perf, 1)

    def set_parse_times(self, start_time: str, stop_time: str):
        """
        Validate and set log parse start and stop times.

        Args:
            start_time (str): The start time as a timestamp string.
            stop_time (str): The stop time as a timestamp string.

        Raises:
            ValueError: If stop_time is earlier than start_time or timestamps are invalid.
            IndexError: Can be raised by _convert_timestamp_to_datetime.
            TypeError: If start_time or stop_time is not a string.
        """
        if not isinstance(start_time, str) or not isinstance(stop_time, str):
            raise TypeError("Start time and stop time must be strings.")

        if not start_time or not stop_time:
            raise ValueError("Start time and stop time cannot be empty.")

        try:
            # Convert timestamps to datetime objects
            norm_start_time = self._convert_timestamp_to_datetime(start_time)
            norm_stop_time = self._convert_timestamp_to_datetime(stop_time)

            # No timetravel (stop time should not be earlier than start time)
            if norm_stop_time <= norm_start_time:
                raise ValueError(f"End time: {norm_stop_time} is before or equal to start time: {norm_start_time}.")

            # Store the parse times
            self.server['parse_start_time'] = norm_start_time
            self.server['parse_stop_time'] = norm_stop_time
            self.logger.info(f"Parse times set. Start: {norm_start_time}, Finish: {norm_stop_time}")

        except (ValueError, IndexError, TypeError) as e:
            self.logger.error(f"Error setting parse times: {e}")
            raise

def main():
    """
    Entry point for the Access Log Analyzer script.

    Processes server access logs to generate performance
    metrics and statistical reports based on the provided options.

    Raises:
        SystemExit: If no valid logs are provided we exit.

    Outputs:
        - Performance metrics and statistics to the console.
        - Optional CSV files for second and minute based performance stats.
    """
    parser = argparse.ArgumentParser(
        description="Analyze server access logs to generate statistics and reports.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog="""
    Examples:

    Analyze logs in verbose mode:
        logconv.py -V /var/log/dirsrv/slapd-host/access*

    Specify a custom root DN:
        logconv.py --rootDN "cn=custom manager" /var/log/dirsrv/slapd-host/access*

    Generate a report for anonymous binds:
        logconv.py -B ANONYMOUS /var/log/dirsrv/slapd-host/access*

    Exclude specific IP address(s) from log analysis:
        logconv.py -X 192.168.1.1 --exclude_ip 11.22.33.44 /var/log/dirsrv/slapd-host/access*

    Analyze logs within a specific time range:
        logconv.py -S "[04/Jun/2024:10:31:20.014629085 +0200]" --endTime "[04/Jun/2024:11:30:05 +0200]" /var/log/dirsrv/slapd-host/access*

    Limit results to 10 entries per category:
        logconv.py --sizeLimit 10 /var/log/dirsrv/slapd-host/access*

    Generate performance stats at second intervals:
        logconv.py -m log-second-stats.csv /var/log/dirsrv/slapd-host/access*

    Generate performance stats at minute intervals:
        logconv.py -M log-minute-stats.csv /var/log/dirsrv/slapd-host/access*

    Display recommendations for log analysis:
        logconv.py -j /var/log/dirsrv/slapd-host/access*
    """
    )

    parser.add_argument(
        'logs',
        type=str,
        nargs='*',
        help='Single or multiple (*) access logs'
    )

    general_group = parser.add_argument_group("General options")
    general_group.add_argument(
        '-v', '--version',
        action='store_true',
        help='Display log analyzer version'
    )
    general_group.add_argument(
        '-V', '--verbose',
        action='store_true',
        help='Enable verbose mode for detailed statistic processing'
    )
    general_group.add_argument(
        '-s', '--sizeLimit',
        type=int,
        metavar="SIZE_LIMIT",
        default=20,
        help="Number of results to return per category."
    )

    connection_group = parser.add_argument_group("Connection options")
    connection_group.add_argument(
        '-d', '--rootDN',
        type=str,
        metavar="ROOT_DN",
        default="cn=Directory Manager",
        help="Specify the Directory Managers DN.\nDefault: \"cn=Directory Manager\""
    )
    connection_group.add_argument(
        '-B', '--bind',
        type=str,
        metavar="BIND_DN",
        help='Generate a bind report for specified DN.\nOptions: [ALL | ANONYMOUS | Actual bind DN]'
    )
    connection_group.add_argument(
        '-X', '--exclude_ip',
        type=list,
        metavar="EXCLUDE_IP",
        action='append',
        help='Exclude specific IP address(s) from log analysis'
    )

    time_group = parser.add_argument_group("Time options")
    time_group.add_argument(
        '-S', '--startTime',
        type=str,
        metavar="START_TIME",
        action='store',
        help='Start analyzing logfile from a specific time.'
                '\nE.g. "[04/Jun/2024:10:31:20.014629085 +0200]"\nE.g. "[04/Jun/2024:10:31:20 +0200]"'
    )
    time_group.add_argument(
        '-E', '--endTime',
        type=str,
        metavar="END_TIME",
        action='store',
        help='Stop analyzing logfile at this time.'
                '\nE.g. "[04/Jun/2024:11:30:05.435779416 +0200]"\nE.g. "[04/Jun/2024:11:30:05 +0200]"'
    )

    report_group = parser.add_argument_group("Reporting options")
    report_group.add_argument(
        "-m", '--reportFileSecs',
        type=str,
        metavar="SEC_STATS_FILENAME",
        help="Capture operation stats at second intervals and write to csv file"
    )
    report_group.add_argument(
        "-M", '--reportFileMins',
        type=str,
        metavar="MIN_STATS_FILENAME",
        help="Capture operation stats at minute intervals and write to csv file"
    )

    misc_group = parser.add_argument_group("Miscellaneous options")
    misc_group.add_argument(
        '-j', '--recommends',
        action='store_true',
        help='Display log analysis recommendations'
    )

    args = parser.parse_args()

    if args.version:
        print(f"Access Log Analyzer {logAnalyzerVersion}")
        sys.exit(0)

    if not args.logs:
        print("No logs provided. Use '-h' for help.")
        sys.exit(1)

    try:
        db = logAnalyser(
            verbose=args.verbose,
            size_limit=args.sizeLimit,
            root_dn=args.rootDN,
            exclude_ip=args.exclude_ip,
            stats_file_sec=args.reportFileSecs,
            stats_file_min=args.reportFileMins,
            report_dn=args.bind,
            recommends=args.recommends)

        if args.startTime and args.endTime:
            try:
                db.set_parse_times(args.startTime, args.endTime)
            except (ValueError, IndexError, TypeError) as e:
                db.server['parse_start_time'] = None
                db.server['parse_stop_time'] = None

        print(f"Access Log Analyzer {logAnalyzerVersion}")
        print(f"Command: {' '.join(sys.argv)}")

        # Sanitise list of log files
        existing_logs = [
            file for file in args.logs
            if not re.search(r'access\.rotationinfo', file) and os.path.isfile(file)
        ]
        if not existing_logs:
            db.logger.error("No log files provided.")
            sys.exit(1)

        # Sort by creation time
        existing_logs.sort(key=lambda x: os.path.getctime(x))
        # We shoud never reach here, if we do put "access" and the end of the log file list
        if 'access' in existing_logs:
            existing_logs.append(existing_logs.pop(existing_logs.index('access')))

        num_logs = len(existing_logs)
        print(f"Processing {num_logs} access log{'s' if num_logs > 1 else ''}...\n")

        # File processing loop
        for (num, accesslog) in enumerate(existing_logs, start=1):
            if os.path.isfile(accesslog):
                db.process_file(num, accesslog)
            else:
                db.logger.error(f"Invalid file: {accesslog}")

    except Exception as e:
        print("An error occurred: %s", e)
        sys.exit(1)

    # Prep for display
    elapsed_time = db.get_elapsed_time(db.server['first_time'], db.server['last_time'], "hms")
    elapsed_secs = db.get_elapsed_time(db.server['first_time'], db.server['last_time'], "seconds")
    num_ops = db.operation.get('all_op_ctr', 0)
    num_results = db.result.get('result_ctr', 0)
    num_conns = db.connection.get('conn_ctr', 0)
    num_ldap = db.connection.get('ldap_ctr', 0)
    num_ldapi = db.connection.get('ldapi_ctr', 0)
    num_ldaps = db.connection.get('ldaps_ctr', 0)
    num_startls = db.operation['extop_dict'].get(STLS_OID, 0)
    num_search = db.search.get('search_ctr', 0)
    num_mod = db.operation.get('mod_op_ctr', 0)
    num_add = db.operation.get('add_op_ctr', 0)
    num_del = db.operation.get('del_op_ctr', 0)
    num_modrdn = db.operation.get('modrdn_op_ctr', 0)
    num_cmp = db.operation.get('cmp_op_ctr', 0)
    num_bind = db.bind.get('bind_ctr', 0)
    num_unbind = db.bind.get('unbind_ctr', 0)
    num_proxyd_auths = db.operation.get('authzid', 0) + db.search.get('authzid', 0)
    num_time_count = db.result.get('timestamp_ctr')
    if num_time_count:
        avg_wtime = round(db.result.get('total_wtime', 0)/num_time_count, 9)
        avg_optime = round(db.result.get('total_optime', 0)/num_time_count, 9)
        avg_etime = round(db.result.get('total_etime', 0)/num_time_count, 9)
    num_fd_taken = db.connection.get('fd_taken_ctr', 0)
    num_fd_rtn = db.connection.get('fd_returned_ctr', 0)

    num_DM_binds = db.bind.get('rootdn_bind_ctr', 0)
    num_base_search = db.search.get('base_search_ctr', 0)
    try:
        log_start_time = db.convert_timestamp_to_string(db.server.get('first_time', ""))
    except ValueError:
        log_start_time = "Unknown"

    try:
        log_end_time = db.convert_timestamp_to_string(db.server.get('last_time', ""))
    except ValueError:
        log_end_time = "Unknown"

    print(f"\n\nTotal Log Lines Analysed:{db.server['lines_parsed']}\n")
    print("\n----------- Access Log Output ------------\n")
    print(f"Start of Logs:                  {log_start_time}")
    print(f"End of Logs:                    {log_end_time}")
    print(f"\nProcessed Log Time:             {elapsed_time}")
    # Display DN report
    if db.report_dn:
        db.display_bind_report()
        sys.exit(1)

    print(f"\nRestarts:                       {db.server.get('restart_ctr', 0)}")
    if db.auth.get('cipher_ctr', 0) > 0:
        print(f"Secure Protocol Versions:")
        # Group data by protocol + version + unique message
        grouped_data = defaultdict(lambda: {'count': 0, 'messages': set()})
        for _, details in db.auth['auth_info'].items():
            # If there is no protocol version
            if details['version']:
                proto_version = f"{details['proto']}{details['version']}"
            else:
                proto_version = f"{details['proto']}"

            for message in details['message']:
                # Unique key for protocol-version and message
                unique_key = (proto_version, message)
                grouped_data[unique_key]['count'] += details['count']
                grouped_data[unique_key]['messages'].add(message)

        for ((proto_version, message), data) in grouped_data.items():
            print(f"  - {proto_version} {message} ({data['count']} connection{'s' if data['count'] > 1 else ''})")

    print(f"Peak Concurrent connections:    {db.connection.get('max_sim_conn_ctr', 0)}")
    print(f"Total Operations:               {num_ops}")
    print(f"Total Results:                  {num_results}")
    print(f"Overall Performance:            {db.get_overall_perf(num_results, num_ops)}%")
    if elapsed_secs:
        print(f"\nTotal connections:              {num_conns:<10}{num_conns/elapsed_secs:>10.2f}/sec {(num_conns/elapsed_secs) * 60:>10.2f}/min")
        print(f"- LDAP connections:             {num_ldap:<10}{num_ldap/elapsed_secs:>10.2f}/sec {(num_ldap/elapsed_secs) * 60:>10.2f}/min")
        print(f"- LDAPI connections:            {num_ldapi:<10}{num_ldapi/elapsed_secs:>10.2f}/sec {(num_ldapi/elapsed_secs) * 60:>10.2f}/min")
        print(f"- LDAPS connections:            {num_ldaps:<10}{num_ldaps/elapsed_secs:>10.2f}/sec {(num_ldaps/elapsed_secs) * 60:>10.2f}/min")
        print(f"- StartTLS Extended Ops         {num_startls:<10}{num_startls/elapsed_secs:>10.2f}/sec {(num_startls/elapsed_secs) * 60:>10.2f}/min")
        print(f"\nSearches:                       {num_search:<10}{num_search/elapsed_secs:>10.2f}/sec {(num_search/elapsed_secs) * 60:>10.2f}/min")
        print(f"Modifications:                  {num_mod:<10}{num_mod/elapsed_secs:>10.2f}/sec {(num_mod/elapsed_secs) * 60:>10.2f}/min")
        print(f"Adds:                           {num_add:<10}{num_add/elapsed_secs:>10.2f}/sec {(num_add/elapsed_secs) * 60:>10.2f}/min")
        print(f"Deletes:                        {num_del:<10}{num_del/elapsed_secs:>10.2f}/sec {(num_del/elapsed_secs) * 60:>10.2f}/min")
        print(f"Mod RDNs:                       {num_modrdn:<10}{num_modrdn/elapsed_secs:>10.2f}/sec {(num_modrdn/elapsed_secs) * 60:>10.2f}/min")
        print(f"Compares:                       {num_cmp:<10}{num_cmp/elapsed_secs:>10.2f}/sec {(num_cmp/elapsed_secs) * 60:>10.2f}/min")
        print(f"Binds:                          {num_bind:<10}{num_bind/elapsed_secs:>10.2f}/sec {(num_bind/elapsed_secs) * 60:>10.2f}/min")
    if num_time_count:
        print(f"\nAverage wtime (wait time):      {avg_wtime:.9f}")
        print(f"Average optime (op time):       {avg_optime:.9f}")
        print(f"Average etime (elapsed time):   {avg_etime:.9f}")
    print(f"\nMulti-factor Authentications:   {db.result.get('notesM_ctr', 0)}")
    print(f"Proxied Auth Operations:        {num_proxyd_auths}")
    print(f"Persistent Searches:            {db.search.get('persistent_ctr', 0)}")
    print(f"Internal Operations:            {db.server.get('internal_op_ctr', 0)}")
    print(f"Entry Operations:               {db.result.get('entry_count', 0)}")
    print(f"Extended Operations:            {db.operation.get('extnd_op_ctr', 0)}")
    print(f"Abandoned Requests:             {db.operation.get('abandon_op_ctr', 0)}")
    print(f"Smart Referrals Received:       {db.result.get('referral_count', 0)}")
    print(f"\nVLV Operations:                 {db.vlv.get('vlv_ctr', 0)}")
    print(f"VLV Unindexed Searches:         {len([key for key, value in db.vlv['vlv_map_rco'].items() if value == 'A'])}")
    print(f"VLV Unindexed Components:       {len([key for key, value in db.vlv['vlv_map_rco'].items() if value == 'U'])}")
    print(f"SORT Operations:                {db.operation.get('sort_op_ctr', 0)}")
    print(f"\nEntire Search Base Queries:     {db.search.get('base_search_ctr', 0)}")
    print(f"Paged Searches:                 {db.result.get('notesP_ctr', 0)}")
    num_unindexed_search = len(db.notesA.keys())
    print(f"Unindexed Searches:             {num_unindexed_search}")
    if db.verbose:
        if num_unindexed_search > 0:
            for num, key in enumerate(db.notesA, start=1):
                src, conn, op = key
                restart_conn_op_key = (src, conn, op)
                print(f"\nUnindexed Search #{num} (notes=A)")
                print(f"  -  Date/Time:             {db.notesA[restart_conn_op_key]['time']}")
                print(f"  -  Connection Number:     {conn}")
                print(f"  -  Operation Number:      {op}")
                print(f"  -  Etime:                 {db.notesA[restart_conn_op_key]['etime']}")
                print(f"  -  Nentries:              {db.notesA[restart_conn_op_key]['nentries']}")
                print(f"  -  IP Address:            {db.notesA[restart_conn_op_key]['ip']}")
                print(f"  -  Search Base:           {db.notesA[restart_conn_op_key]['base']}")
                print(f"  -  Search Scope:          {db.notesA[restart_conn_op_key]['scope']}")
                print(f"  -  Search Filter:         {db.notesA[restart_conn_op_key]['filter']}")
                print(f"  -  Bind DN:               {db.notesA[restart_conn_op_key]['bind_dn']}\n")

    num_unindexed_component = len(db.notesU.keys())
    print(f"Unindexed Components:           {num_unindexed_component}")
    if db.verbose:
        if num_unindexed_component > 0:
            for num, key in enumerate(db.notesU, start=1):
                src, conn, op = key
                restart_conn_op_key = (src, conn, op)
                print(f"\nUnindexed Component #{num} (notes=U)")
                print(f"  -  Date/Time:             {db.notesU[restart_conn_op_key]['time']}")
                print(f"  -  Connection Number:     {conn}")
                print(f"  -  Operation Number:      {op}")
                print(f"  -  Etime:                 {db.notesU[restart_conn_op_key]['etime']}")
                print(f"  -  Nentries:              {db.notesU[restart_conn_op_key]['nentries']}")
                print(f"  -  IP Address:            {db.notesU[restart_conn_op_key]['ip']}")
                print(f"  -  Search Base:           {db.notesU[restart_conn_op_key]['base']}")
                print(f"  -  Search Scope:          {db.notesU[restart_conn_op_key]['scope']}")
                print(f"  -  Search Filter:         {db.notesU[restart_conn_op_key]['filter']}")
                print(f"  -  Bind DN:               {db.notesU[restart_conn_op_key]['bind_dn']}\n")

    num_invalid_filter = len(db.notesF.keys())
    print(f"Invalid Attribute Filters:      {num_invalid_filter}")
    if db.verbose:
        if num_invalid_filter > 0:
            for num, key in enumerate(db.notesF, start=1):
                src, conn, op = key
                restart_conn_op_key = (src, conn, op)
                print(f"\nInvalid Attribute Filter #{num} (notes=F)")
                print(f"  -  Date/Time:             {db.notesF[restart_conn_op_key]['time']}")
                print(f"  -  Connection Number:     {conn}")
                print(f"  -  Operation Number:      {op}")
                print(f"  -  Etime:                 {db.notesF[restart_conn_op_key]['etime']}")
                print(f"  -  Nentries:              {db.notesF[restart_conn_op_key]['nentries']}")
                print(f"  -  IP Address:            {db.notesF[restart_conn_op_key]['ip']}")
                print(f"  -  Search Filter:         {db.notesF[restart_conn_op_key]['filter']}")
                print(f"  -  Bind DN:               {db.notesF[restart_conn_op_key]['bind_dn']}\n")
    print(f"FDs Taken:                      {num_fd_taken}")
    print(f"FDs Returned:                   {num_fd_rtn}")
    print(f"Highest FD Taken:               {db.connection.get('fd_max_ctr', 0)}\n")
    num_broken_pipe = len(db.connection['broken_pipe'])
    print(f"Broken Pipes:                   {num_broken_pipe}")
    if num_broken_pipe > 0:
        for code, count in db.connection['broken_pipe'].items():
            print(f"    - {count} ({code}) {DISCONNECT_MSG.get(code, 'unknown')}")
        print()
    num_reset_peer = len(db.connection['connection_reset'])
    print(f"Connection Reset By Peer:       {num_reset_peer}")
    if num_reset_peer > 0:
        for code, count in db.connection['connection_reset'].items():
            print(f"    - {count} ({code}) {DISCONNECT_MSG.get(code, 'unknown')}")
        print()
    num_resource_unavail = len(db.connection['resource_unavail'])
    print(f"Resource Unavailable:           {num_resource_unavail}")
    if num_resource_unavail > 0:
        for code, count in db.connection['resource_unavail'].items():
            print(f"    - {count} ({code}) {DISCONNECT_MSG.get(code, 'unknown')}")
        print()
    print(f"Max BER Size Exceeded:          {db.connection['disconnect_code'].get('B2', 0)}\n")
    print(f"Binds:                          {db.bind.get('bind_ctr', 0)}")
    print(f"Unbinds:                        {db.bind.get('unbind_ctr', 0)}")
    print(f"----------------------------------")
    print(f"- LDAP v2 Binds:                {db.bind.get('version', {}).get('2', 0)}")
    print(f"- LDAP v3 Binds:                {db.bind.get('version', {}).get('3', 0)}")
    print(f"- AUTOBINDs(LDAPI):             {db.bind.get('autobind_ctr', 0)}")
    print(f"- SSL Client Binds              {db.auth.get('ssl_client_bind_ctr', 0)}")
    print(f"- Failed SSL Client Binds:      {db.auth.get('ssl_client_bind_failed_ctr', 0)}")
    print(f"- SASL Binds:                   {db.bind.get('sasl_bind_ctr', 0)}")
    if db.bind.get('sasl_bind_ctr', 0) > 0:
        saslmech = db.bind['sasl_mech_freq']
        for saslb in sorted(saslmech.keys(), key=lambda k: saslmech[k], reverse=True):
            print(f"   - {saslb:<4}: {saslmech[saslb]}")
    print(f"- Directory Manager Binds:      {num_DM_binds}")
    print(f"- Anonymous Binds:              {db.bind.get('anon_bind_ctr', 0)}\n")
    if db.verbose:
        # Connection Latency
        print(f"\n ----- Connection Latency Details -----\n")
        print(f" (in seconds){' ' * 10}{'<=1':^7}{'2':^7}{'3':^7}{'4-5':^7}{'6-10':^7}{'11-15':^7}{'>15':^7}")
        print('-' * 72)
        print(
            f"{' (# of connections)    ':<17}"
            f"{LATENCY_GROUPS['<= 1']:^7}"
            f"{LATENCY_GROUPS['== 2']:^7}"
            f"{LATENCY_GROUPS['== 3']:^7}"
            f"{LATENCY_GROUPS['4-5']:^7}"
            f"{LATENCY_GROUPS['6-10']:^7}"
            f"{LATENCY_GROUPS['11-15']:^7}"
            f"{LATENCY_GROUPS['> 15']:^7}")

        # Open Connections
        open_conns = db.connection['open_conns']
        if len(open_conns) > 0:
            print(f"\n ----- Current Open Connection IDs -----\n")
            for conn in sorted(open_conns.keys(), key=lambda k: open_conns[k], reverse=True):
                print(f"{conn:<16} {open_conns[conn]:>10}")

        # Error Codes
        print(f"\n----- Errors -----\n")
        error_freq = db.result['error_freq']
        for err in sorted(error_freq.keys(), key=lambda k: error_freq[k], reverse=True):
            print(f"err={err:<2} {error_freq[err]:>10}  {LDAP_ERR_CODES[err]:<30}")

        # Failed Logins
        bad_pwd_map = db.result['bad_pwd_map']
        bad_pwd_map_len = len(bad_pwd_map)
        if bad_pwd_map_len > 0:
            print(f"\n----- Top {db.size_limit} Failed Logins ------\n")
            for num, (dn, ip) in enumerate(bad_pwd_map):
                if num > db.size_limit:
                    break
                count = bad_pwd_map.get((dn, ip))
                print(f"{count:<10} {dn}")
            print(f"\nFrom IP address:{'s' if bad_pwd_map_len > 1 else ''}\n")
            for num, (dn, ip) in enumerate(bad_pwd_map):
                if num > db.size_limit:
                    break
                count = bad_pwd_map.get((dn, ip))
                print(f"{count:<10} {ip}")

        # Connection Codes
        disconnect_codes = db.connection['disconnect_code']
        if len(disconnect_codes) > 0:
            print(f"\n----- Total Connection Codes ----\n")
            for code in disconnect_codes:
                print(f"{code:<2} {disconnect_codes[code]:>10}  {DISCONNECT_MSG.get(code, 'unknown'):<30}")

        # Unique IPs
        restart_conn_ip_map = db.connection['restart_conn_ip_map']
        ip_map = db.connection['ip_map']
        ips_len = len(ip_map)
        if ips_len > 0:
            print(f"\n----- Top {db.size_limit} Clients -----\n")
            print(f"Number of Clients:  {ips_len}")
            for num, (outer_ip, ip_info) in enumerate(ip_map.items(), start=1):
                temp = {}
                print(f"\n[{num}] Client: {outer_ip}")
                print(f"    {ip_info['count']} - Connection{'s' if ip_info['count'] > 1 else ''}")
                for id, inner_ip in restart_conn_ip_map.items():
                    (src, conn) = id
                    if outer_ip == inner_ip:
                        code = db.connection['disconnect_code_map'].get((src, conn), 0)
                        if code:
                            temp[code] = temp.get(code, 0) + 1
                for code, count in temp.items():
                    print(f"    {count} - {code} ({DISCONNECT_MSG.get(code, 'unknown')})")
                if num > db.size_limit - 1:
                    break

        # Unique Bind DN's
        binds = db.bind.get('dn_freq', 0)
        binds_len = len(binds)
        if binds_len > 0:
            print(f"\n----- Top {db.size_limit} Bind DN's ----\n")
            print(f"Number of Unique Bind DN's: {binds_len}\n")
            for num, bind in enumerate(sorted(binds.keys(), key=lambda k: binds[k], reverse=True)):
                if num >= db.size_limit:
                    break
                print(f"{db.bind['dn_freq'][bind]:<10}  {bind:<30}")

        # Unique search bases
        bases = db.search['base_map']
        num_bases = len(bases)
        if num_bases > 0:
            print(f"\n----- Top {db.size_limit} Search Bases -----\n")
            print(f"Number of Unique Search Bases: {num_bases}\n")
            for num, base in enumerate(sorted(bases.keys(), key=lambda k: bases[k], reverse=True)):
                if num >= db.size_limit:
                    break
                print(f"{db.search['base_map'][base]:<10}  {base}")

        # Unique search filters
        filters = sorted(db.search['filter_list'], reverse=True)
        num_filters = len(filters)
        if num_filters > 0:
            print(f"\n----- Top {db.size_limit} Search Filters -----\n")
            for num, (count, filter) in enumerate(filters):
                if num >= db.size_limit:
                    break
                print(f"{count:<10} {filter}")

        # Longest elapsed times
        etimes = sorted(db.result['etime_duration'], reverse=True)
        num_etimes = len(etimes)
        if num_etimes > 0:
            print(f"\n----- Top {db.size_limit} Longest etimes (elapsed times) -----\n")
            for num, etime in enumerate(etimes):
                if num >= db.size_limit:
                    break
                print(f"etime={etime:<12}")

        # Longest wait times
        wtimes = sorted(db.result['wtime_duration'], reverse=True)
        num_wtimes = len(wtimes)
        if num_wtimes > 0:
            print(f"\n----- Top {db.size_limit} Longest wtimes (wait times) -----\n")
            for num, wtime in enumerate(wtimes):
                if num >= db.size_limit:
                    break
                print(f"wtime={wtime:<12}")

        # Longest operation times
        optimes = sorted(db.result['optime_duration'], reverse=True)
        num_optimes = len(optimes)
        if num_optimes > 0:
            print(f"\n----- Top {db.size_limit} Longest optimes (actual operation times) -----\n")
            for num, optime in enumerate(optimes):
                if num >= db.size_limit:
                    break
                print(f"optime={optime:<12}")

        # Largest nentries returned
        nentries = sorted(db.result['nentries_num'], reverse=True)
        num_nentries = len(nentries)
        if num_nentries > 0:
            print(f"\n----- Top {db.size_limit} Largest nentries -----\n")
            for num, nentry in enumerate(nentries):
                if num >= db.size_limit:
                    break
                print(f"nentries={nentry:<10}")
            print()

        # Extended operations
        oids = db.operation['extop_dict']
        num_oids = len(oids)
        if num_oids > 0:
            print(f"\n----- Top {db.size_limit} Extended Operations -----\n")
            for num, oid in enumerate(sorted(oids, key=lambda k: oids[k], reverse=True)):
                if num >= db.size_limit:
                    break
                print(f"{oids[oid]:<12} {oid:<30} {OID_MSG.get(oid, 'Other'):<60}")

        # Commonly requested attributes
        attrs = db.search['attr_dict']
        num_nattrs = len(attrs)
        if num_nattrs > 0:
            print(f"\n----- Top {db.size_limit} Most Requested Attributes -----\n")
            for num, attr in enumerate(sorted(attrs, key=lambda k: attrs[k], reverse=True)):
                if num >= db.size_limit:
                    break
                print(f"{attrs[attr]:<11} {attr:<10}")
            print()

        abandoned = db.operation['abandoned_map_rco']
        num_abandoned = len(abandoned)
        if num_abandoned > 0:
            print(f"\n----- Abandon Request Stats -----\n")
            for num, abandon in enumerate(abandoned, start=1):
                (restart, conn, op) = abandon
                conn, op, target_op, msgid = db.operation['abandoned_map_rco'][(restart, conn, op)]
                print(f"{num:<6} conn={conn} op={op} msgid={msgid} target_op:{target_op} client={db.connection['restart_conn_ip_map'].get((restart, conn), 'Unknown')}")
            print()

    if db.recommends or db.verbose:
        print(f"\n----- Recommendations -----")
        rec_count = 1

        if num_unindexed_search > 0:
            print(f"\n {rec_count}. You have unindexed searches. This can be caused by a search on an unindexed attribute or by returned results exceeding the nsslapd-idlistscanlimit. Unindexed searches are very resource-intensive and should be prevented or corrected. To refuse unindexed searches, set 'nsslapd-require-index' to 'on' under your database entry (e.g. cn=UserRoot,cn=ldbm database,cn=plugins,cn=config).\n")
            rec_count += 1

        if num_unindexed_component > 0:
            print(f"\n {rec_count}. You have unindexed components. This can be caused by a search on an unindexed attribute or by returned results exceeding the nsslapd-idlistscanlimit. Unindexed components are not recommended. To refuse unindexed searches, set 'nsslapd-require-index' to 'on' under your database entry (e.g. cn=UserRoot,cn=ldbm database,cn=plugins,cn=config).\n")
            rec_count += 1

        if db.connection['disconnect_code'].get('T1', 0) > 0:
            print(f"\n {rec_count}. You have some connections being closed by the idletimeout setting. You may want to increase the idletimeout if it is set low.\n")
            rec_count += 1

        if db.connection['disconnect_code'].get('T2', 0) > 0:
            print(f"\n {rec_count}. You have some connections being closed by the ioblocktimeout setting. You may want to increase the ioblocktimeout.\n")
            rec_count += 1

        if db.connection['disconnect_code'].get('T3', 0) > 0:
            print(f"\n {rec_count}. You have some connections being closed because a paged result search limit has been exceeded. You may want to increase the search time limit.\n")
            rec_count += 1

        if (num_bind - num_unbind) > (num_bind * 0.3):
            print(f"\n {rec_count}. You have a significant difference between binds and unbinds. You may want to investigate this difference.\n")
            rec_count += 1

        if (num_fd_taken - num_fd_rtn) > (num_fd_taken * 0.3):
            print(f"\n {rec_count}. You have a significant difference between file descriptors taken and file descriptors returned. You may want to investigate this difference.\n")
            rec_count += 1

        if num_DM_binds > (num_bind * 0.2):
            print(f"\n {rec_count}. You have a high number of Directory Manager binds. The Directory Manager account should only be used under certain circumstances. Avoid using this account for client applications.\n")
            rec_count += 1

        num_success = db.result['error_freq'].get('0', 0)
        num_err = sum(v for k, v in db.result['error_freq'].items() if k != '0')
        if num_err > num_success:
            print(f"\n {rec_count}. You have more unsuccessful operations than successful operations. You should investigate this difference.\n")
            rec_count += 1

        num_close_clean = db.connection['disconnect_code'].get('U1', 0)
        num_close_total = num_err = sum(v for k, v in db.connection['disconnect_code'].items())
        if num_close_clean < (num_close_total - num_close_clean):
            print(f"\n {rec_count}. You have more abnormal connection codes than cleanly closed connections. You may want to investigate this difference.\n")
            rec_count += 1

        if num_time_count:
            if round(avg_etime, 1) > 0:
                print(f"\n {rec_count}. Your average etime is {avg_etime:.1f}. You may want to investigate this performance problem.\n")
                rec_count += 1

            if round(avg_wtime, 1) > 0.5:
                print(f"\n {rec_count}. Your average wtime is {avg_wtime:.1f}. You may need to increase the number of worker threads (nsslapd-threadnumber).\n")
                rec_count += 1

            if round(avg_optime, 1) > 0:
                print(f"\n {rec_count}. Your average optime is {avg_optime:.1f}. You may want to investigate this performance problem.\n")
                rec_count += 1

        if num_base_search > (num_search * 0.25):
            print(f"\n {rec_count}. You have a high number of searches that query the entire search base. Although this is not necessarily bad, it could be resource-intensive if the search base contains many entries.\n")
            rec_count += 1

        if rec_count == 1:
            print("\nNone.")

    print("Done.")


if __name__ == "__main__":
    main()
