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
from collections import defaultdict, Counter
import datetime
from dataclasses import dataclass, field
import heapq
from typing import Optional, Dict, List, Set, Tuple, DefaultDict, Union
import magic
import json
import inspect

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

logAnalyzerVersion = "8.4"

@dataclass
class VLVData:
    counters: Dict[str, int] = field(default_factory=lambda: defaultdict(
        int,
        {
            'vlv': 0
        }
    ))

    vlv_map: Dict = field(default_factory=lambda: defaultdict(dict))

@dataclass
class ServerData:
    counters: Dict[str, int] = field(default_factory=lambda: defaultdict(
        int,
        {
            'restart': 0,
            'lines_parsed': 0
        }
    ))

    first_time: Optional[str] = None
    last_time: Optional[str] = None
    parse_start_time: Optional[str] = None
    parse_stop_time: Optional[str] = None

@dataclass
class OperationData:
    counters: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(
        int,
        {
            'add': 0,
            'modify': 0,
            'delete': 0,
            'modrdn': 0,
            'compare': 0,
            'abandon': 0,
            'sort': 0,
            'internal': 0,
            'extnd': 0,
            'authzid': 0,
            'total': 0
        }
    ))

    op_map: DefaultDict[str, DefaultDict[str, int]] = field(
        default_factory=lambda: defaultdict(lambda: defaultdict(int))
    )

    extended: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))

@dataclass
class ConnectionData:
    counters: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(
        int,
        {
            'conn': 0,
            'fd_taken': 0,
            'fd_returned': 0,
            'fd_max': 0,
            'sim_conn': 0,
            'max_sim_conn': 0,
            'ldap': 0,
            'ldapi': 0,
            'ldaps': 0
        }
    ))

    start_time: DefaultDict[str, str] = field(default_factory=lambda: defaultdict(str))
    open_conns: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    exclude_ip: DefaultDict[Tuple[str, str], str] = field(default_factory=lambda: defaultdict(str))

    broken_pipe: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    resource_unavail: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    connection_reset: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    disconnect_code: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    disconect_map: DefaultDict[Tuple[int, str], int] = field(default_factory=lambda: defaultdict(int))
    
    ip_map: Dict[Tuple[int, str], str] = field(default_factory=dict)
    
    error_code: DefaultDict[str, DefaultDict[str, int]] = field(
        default_factory=lambda: defaultdict(lambda: defaultdict(int))
    )
    src_ip_map: DefaultDict[str, DefaultDict[str, object]] = field(
        default_factory=lambda: defaultdict(lambda: defaultdict(object))
    )

@dataclass
class BindData:
    counters: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(
        int,
        {
            'bind': 0,
            'unbind': 0,
            'sasl': 0,
            'anon': 0,
            'autobind': 0,
            'rootdn': 0
        }
    ))

    bind_dn_map: Dict[Tuple[int, str], str] = field(default_factory=dict)

    version: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    dns: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    sasl_mech: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    sasl_mech_map: DefaultDict[str, str] = field(default_factory=lambda: defaultdict(str))
    root_dn: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))

    report_dn: DefaultDict[str, Dict[str, Set[str]]] = field(
        default_factory=lambda: defaultdict(
            lambda: {
                'conn': set(),
                'ips': set()
            }
        )
    )

@dataclass
class ResultData:
    counters: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(
        int, {
            'result': 0,
            'notesA': 0,
            'notesF': 0,
            'notesM': 0,
            'notesP': 0,
            'notesU': 0,
            'timestamp': 0,
            'entry': 0,
            'referral': 0
        }
    ))

    notesA: DefaultDict[str, Dict] = field(default_factory=lambda: defaultdict(dict))
    notesU: DefaultDict[str, Dict] = field(default_factory=lambda: defaultdict(dict))
    notesF: DefaultDict[str, Dict] = field(default_factory=lambda: defaultdict(dict))
    notesP: DefaultDict[str, Dict] = field(default_factory=lambda: defaultdict(dict))

    timestamp_ctr: int = 0
    entry_count: int = 0
    referral_count: int = 0

    total_etime: float = 0.0
    total_wtime: float = 0.0
    total_optime: float = 0.0
    etime_stat: float = 0.0

    etime_duration: List[float] = field(default_factory=list)
    wtime_duration: List[float] = field(default_factory=list)
    optime_duration: List[float] = field(default_factory=list)

    nentries_num: List[int] = field(default_factory=list)
    nentries_set: Set[int] = field(default_factory=set)

    error_freq: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    bad_pwd_map: Dict[str, int] = field(default_factory=dict)

@dataclass
class SearchData:
    counters: Dict[str, int] = field(default_factory=lambda: defaultdict(
        int,
        {
            'search': 0,
            'base_search': 0,
            'persistent': 0,
            'vlv_requests': 0,
            'vlv_responses': 0,
            'sort_requests': 0,
            'sort_responses': 0,
            'paged_results': 0
        }
    ))
    attrs: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))
    bases: DefaultDict[str, int] = field(default_factory=lambda: defaultdict(int))

    base_map: Dict[Tuple[int, str, str], str] = field(default_factory=dict)
    scope_map: Dict[Tuple[int, str, str], str] = field(default_factory=dict)

    filter_dict: Dict[str, int] = field(default_factory=dict)
    filter_list: List[str] = field(default_factory=list)
    filter_map: Dict[Tuple[int, str, str], str] = field(default_factory=dict)

@dataclass
class AuthData:
    counters: Dict[str, int] = field(default_factory=lambda: defaultdict(
        int,
        {
            'client_bind': 0,
            'cert_map_fail': 0,
            'cipher': 0
        }
    ))
    auth_info: DefaultDict[str, str] = field(default_factory=lambda: defaultdict(str))

class logAnalyser:
    """
    Parses and analyses log files with configurable options.

    Attributes:
        verbose (bool): Enable verbose data gathering and reporting. Defaults to False.
        recommends (bool): Enable post-analysis recommendations. Defaults to False.
        size_limit (Optional[int]): Maximum size of entries to report. Defaults to None.
        root_dn (Optional[str]): Directory Manager's DN. Defaults to None.
        exclude_ip (Optional[List[str]]): List of IPs to exclude from analysis. Defaults to empty list.
        stats_file_sec (Optional[str]): Interval in seconds for statistics reporting. Defaults to None.
        stats_file_min (Optional[str]): Interval in minutes for statistics reporting. Defaults to None.
        report_dn (Optional[str]): DN for generating reports. Defaults to None.
        regexes (dict): Mapping of keys to tuples of (compiled regex pattern, handler function) for parsing legacy log formats.
        json_handlers (dict): Mapping of JSON log operations to handler function.
    """
    def __init__(self,
                 verbose: bool = False,
                 recommends: bool = False,
                 size_limit: Optional[int] = None,
                 root_dn: Optional[str] = None,
                 exclude_ip: Optional[List[str]] = None,
                 stats_file_sec: Optional[str] = None,
                 stats_file_min: Optional[str] = None,
                 report_dn: Optional[str] = None):

        self.verbose = verbose
        self.recommends = recommends
        self.size_limit = size_limit
        self.root_dn = root_dn
        self.exclude_ip = exclude_ip or []
        self.file_size = 0
        self.prev_stats = None
        (self.stats_interval, self.stats_file) = self._get_stats_interval(stats_file_sec, stats_file_min)
        self.csv_writer = self._setup_csv_writer(self.stats_file) if self.stats_file else None
        self.report_dn = report_dn
        self._setup_data_structures()
        self.regexes = self._setup_legacy_regexes()
        self.json_handlers = self._setup_json_handlers()

    def _setup_csv_writer(self, stats_file: str):
        """
        Initialize a CSV writer for statistics reporting.

        Args:
            stats_file (str): The path to the CSV file where statistics will be written.

        Returns:
            csv.writer | None: A CSV writer object, or None if setup fails.
        """
        try:
            file = open(stats_file, mode='w', newline='')
            self._stats_file_handle = file  # Save reference for later closing if needed
            return csv.writer(file)
        except (OSError, IOError) as e:
            self.logger.error(f"Could not open stats file '{stats_file}' for writing: {e}")
            return None

    def _setup_logger(self, log_level: int):
        """
        Setup logging.

        Args:
            log_level (int): Log level

        Returns:
            logger object.
        """
        logger = logging.getLogger("logAnalyser")
        formatter = logging.Formatter('%(name)s - %(levelname)s - %(message)s')

        handler = logging.StreamHandler()
        handler.setFormatter(formatter)

        logger.setLevel(log_level)
        logger.addHandler(handler)

        return logger

    def _setup_data_structures(self):
        """
        Set up data structures for parsing and storing log data.
        """
        self.notesA = {}
        self.notesF = {}
        self.notesM = {}
        self.notesP = {}
        self.notesU = {}

        self.vlv = VLVData()
        self.server = ServerData()
        self.operation = OperationData()
        self.connection = ConnectionData()
        self.bind = BindData()
        self.result = ResultData()
        self.search = SearchData()
        self.auth = AuthData()

    def _setup_legacy_regexes(self):
        """
        Compile and return a dictionary of legacy regex patterns used to parse access log entries.

        Each dictionary entry maps a descriptive key to a tuple:
        - A compiled regular expression that matches a specific format of log entry.
        - A corresponding handler method that processes matches for that regex.

        These regex patterns are primarily used for parsing legacy (non-JSON) log formats.

        The handler functions transform matched log lines into structured data or trigger
        internal state updates.

        Returns:
            dict: A mapping of pattern names to (regex, match handler function) tuple.
        """
        # Reusable patterns
        TIMESTAMP_PATTERN = r'''
            \[
                (?P<timestamp>
                    \d{2}/[A-Za-z]{3}/\d{4}:\d{2}:\d{2}:\d{2}\.\d{9}
                    \s[+-]\d{4}
                )
            \]
        '''
        CONN_ID_PATTERN = r'\sconn=(?P<conn_id>\d+)'
        CONN_ID_INTERNAL_PATTERN = r'\sconn=(?P<conn_id>\d+|Internal\(\d+\))'
        OP_ID_PATTERN = r'\s+op=(?P<op_id>-?\d+)'

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
            ''', re.VERBOSE), self._process_result_legacy),
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
            ''', re.VERBOSE), self._process_search_legacy),
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
            ''', re.VERBOSE), self._process_bind_legacy),
            'UNBIND_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                (?:\sop=(?P<op_id>\d+))?                            # Optional: op=int
                \sUNBIND                                            # UNBIND
            ''', re.VERBOSE), self._process_unbind_legacy),
            'CONNECT_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                \sfd=(?P<fd>\d+)                                    # fd=int
                \sslot=(?P<slot>\d+)                                # slot=int
                \s(?P<ssl>SSL\s)?                                   # Optional: SSL
                connection\sfrom\s                                  # connection from
                (?P<src_ip>\S+)\sto\s                               # IP to
                (?P<dst_ip>\S+)                                     # IP
            ''', re.VERBOSE), self._process_connect_legacy),
            'DISCONNECT_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}
                {OP_ID_PATTERN} 
                \s+fd=(?P<fd>\d+)
                \s+(?P<status>closed|Disconnect)
                \s*-\s*
                (?P<close_reason>.+)
                $
            ''', re.VERBOSE), self._process_disconnect_legacy),
            'EXTEND_OP_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \sEXT                                               # EXT
                \soid="(?P<oid>[^"]+)"                              # oid="string"
               (?:\sname="(?P<name>[^"]+)")?                        # Optional: namme="string"
            ''', re.VERBOSE), self._process_extend_op_legacy),
            'AUTOBIND_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                \s+AUTOBIND                                         # AUTOBIND
                \sdn="(?P<bind_dn>.*?)"                             # Optional: dn="strings"
            ''', re.VERBOSE), self._process_autobind_legacy),
            'AUTH_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN} 
                \s+(?P<tls_version>TLS\d+(?:\.\d+)?)     # TLS1.3
                (?:
                    \s+(?P<keysize>\d+)-bit
                    \s+(?P<cipher>[A-Za-z0-9\-]+)
                    (?:;\s*client\s+CN=(?P<subject>[^;]+))?
                    (?:;\s*issuer\s+CN=(?P<issuer>[^;]+))?
                    |
                    \s+(?P<msg>
                        client\s+bound\s+as\s+.+ |
                        failed\s+to\s+map\s+client\s+certificate\s+to\s+LDAP\s+DN(?:\s*\(.*?\))?
                    )
                )?
                ''', re.VERBOSE), self._process_auth_legacy),
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
            ''', re.VERBOSE), self._process_vlv_legacy),
            'ABANDON_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \sABANDON                                           # ABANDON
                \stargetop=(?P<targetop>[\w\s]+)                    # targetop=string
                \smsgid=(?P<msgid>\d+)                              # msgid=int
            ''', re.VERBOSE), self._process_abandon_legacy),
            'SORT_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \sSORT                                              # SORT
                \s+(?P<attribute>\w+)                               # Currently not used
                (?:\s+\((?P<status>\d+)\))?                         # Currently not used
            ''', re.VERBOSE), self._process_sort_legacy),
            'CRUD_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_INTERNAL_PATTERN}                          # conn=int | conn=Internal(int)
                (\s\((?P<internal>Internal)\))?                     # Optional: (Internal)
                \sop=(?P<op_id>\d+)(?:\(\d+\)\(\d+\))?              # Optional: op=int, op=int(int)(int)
                \s(?P<op_type>ADD|CMP|MOD|DEL|MODRDN)               # ADD|CMP|MOD|DEL|MODRDN
                \sdn="(?P<dn>[^"]*)"                                # dn="", dn="strings"
                (?:\sauthzid="(?P<authzid_dn>[^"]*)")?              # Optional: dn="", dn="strings"
            ''', re.VERBOSE), self._process_crud_legacy),
            'ENTRY_REFERRAL_REGEX': (re.compile(rf'''
                {TIMESTAMP_PATTERN}
                {CONN_ID_PATTERN}                                   # conn=int
                {OP_ID_PATTERN}                                     # op=int
                \s(?P<op_type>ENTRY|REFERRAL)                       # ENTRY|REFERRAL
                (?:\sdn="(?P<dn>[^"]*)")?                           # Optional: dn="", dn="string"
            ''', re.VERBOSE), self._process_entry_referral_entry)
        }
    
    def _setup_json_handlers(self):
        """
        Set up a mapping between JSON log operation types and their corresponding handler methods.

        Returns:
            dict: A dictionary mapping operation types to handler methods.
        """
        return {
            "SEARCH": self._process_search_entry,
            "RESULT": self._process_result_entry,
            "CONNECTION": self._process_connect_entry,
            "DISCONNECT": self._process_disconnect_entry,
            "BIND": self._process_bind_entry,
            "BIND": self._process_bind_entry,
            "UNBIND": self._process_unbind_entry,
            "AUTOBIND": self._process_autobind_entry,
            "EXTENDED_OP": self._process_extend_op_entry,
            "VLV": self._process_vlv_entry,
            "SORT": self._process_sort_entry,
            "ABANDON": self._process_abandon_entry,
            "ADD": self._process_crud_entry,
            "MODIFY": self._process_crud_entry,
            "DELETE": self._process_crud_entry,
            "COMPARE": self._process_crud_entry,
            "MODRDN": self._process_crud_entry,
            "ENTRY": self._process_entry_referral_entry,
            "REFERRAL": self._process_entry_referral_entry,
            "TLS_INFO": self._process_auth_entry,
            "TLS_CLIENT_INFO": self._process_auth_entry
        }

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

        self.logger.debug(f"Processing file: {filepath}")

        try:
            # Is log compressed
            comptype = self._is_file_compressed(filepath)
            if comptype:
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
                self.logger.debug(f"{filehandle.name} size (bytes): {file_size}")

                # Back to the start
                filehandle.seek(0)
                print(f"[{log_num:03d}] {filehandle.name:<30}\tsize (bytes): {file_size:>12}")

                for line in filehandle:
                    line_number += 1
                    try:
                        line_content = line.decode('utf-8').strip()
                        # Entry to parsing logic
                        proceed = self._match_line(line_content, filehandle.tell())
                        if not proceed:
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
                        self.logger.error(f"non-decodable line at position {filehandle.tell()} - {de}")

        except FileNotFoundError:
            self.logger.error(f"File not found: {filepath}")
        except IOError as ie:
            self.logger.error(f"IO error processing file {filepath} - {ie}")

    def _is_file_compressed(self, filepath: str):
        """
        Determines whether a file is compressed using a supported compression method (gzip).

        Args:
            filepath (str): The path to the file.

        Returns:
            Optional[Tuple[bool, Optional[str]]]:
                - (True, <mime_type>) if the file is compressed using a supported method.
                - (False, None) if the file is not compressed.
                - None if the file does not exist or an error occurs.
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
                self.logger.debug(f"File is compressed: {filepath} (MIME: {filetype.mime_type})")
                return True, filetype.mime_type
            else:
                self.logger.debug(f"File is not compressed: {filepath}")
                return False

        except Exception as e:
            self.logger.error(f"Error while determining compression for file {filepath} - {e}")
            return None

    def _is_timestamp_in_range(self, timestamp: datetime):
        """
        Check if a datetime timestamp is within the configured parse time range.

        Args:
            timestamp (datetime): The datetime object to check.

        Returns:
            bool: True if timestamp is within range, False otherwise.
        """
        parse_start = self.server.parse_start_time
        parse_stop = self.server.parse_stop_time

        if parse_start and parse_stop:
            if parse_start.microsecond == 0 and parse_stop.microsecond == 0:
                timestamp = timestamp.replace(microsecond=0)
            return parse_start <= timestamp <= parse_stop

        # If no range is set, allow all timestamps
        return True

    def _finalise_match(self, timestamp: datetime, bytes_read: int):
        """
        Common logic to run after a line has been successfully matched and handled.

        Args:
            timestamp (datetime): Normalized timestamp of the log line.
            bytes_read (int): Number of bytes read so far.
        """
        if self.server.first_time is None:
            self.server.first_time = timestamp
        self.server.last_time = timestamp

        self.server.counters['lines_parsed'] += 1

        if self.stats_interval and self.stats_file:
            self._process_and_write_stats(timestamp, bytes_read)
            self.logger.debug(f"Stats processed for timestamp: {timestamp}.")
            
    def _match_line(self, line: str, bytes_read: int):
        """
        Process a single access log line (JSON or legacy format).

        Args:
            line (str): Single log line.
            bytes_read (int): Number of bytes read so far.

        Returns:
            bool: True if a match was found and processed, False otherwise.
        """
        if line.lstrip().startswith('{'):
            self.logger.debug(f"JSON format detected - line: {line}")
            try:
                log_entry = json.loads(line)
            except json.JSONDecodeError:
                self.logger.error(f"Malformed JSON line: {line}")
                return False

            timestamp_raw = log_entry.get("local_time")
            if timestamp_raw:
                try:
                    # Convert raw timestamp to datetime object for easier comparison,
                    # and remove the original 'local_time' entry
                    timestamp_dt = self.convert_timestamp_to_datetime(timestamp_raw)
                    log_entry["timestamp_dt"] = timestamp_dt
                    del log_entry["local_time"] 
                except (ValueError, TypeError) as e:
                    self.logger.error(f"Failed to convert timestamp to datetime: {timestamp_raw} - {e}")
                    return False

            if not self._is_timestamp_in_range(timestamp_dt):
                self.logger.debug(f"Timestamp {timestamp_dt} is out of range. Skipping line.")
                return False

            operation = log_entry.get("operation")
            handler = self.json_handlers.get(operation)
            if not handler:
                self.logger.debug(f"No handler found for JSON operation: {operation}")
                return False

            handler(log_entry)

            self._finalise_match(timestamp_dt, bytes_read)

            return True

        elif line.lstrip().startswith('['):
            self.logger.debug(f"LEGACY format detected - line:{line}")
            for name, (pattern, action) in self.regexes.items():
                match = pattern.match(line)
                if not match:
                    continue

                self.logger.debug(f"Matched legacy pattern: {name}")

                try:
                    groups = match.groupdict()
                except AttributeError as e:
                    self.logger.error(f"Failed to get group from match line: {line} - {e}.")
                    return False

                timestamp_raw = groups.get('timestamp')
                if timestamp_raw:
                    try:
                        # Convert raw timestamp to datetime object for easier comparison,
                        # and remove the original 'timestamp' entry
                        timestamp_dt = self.convert_timestamp_to_datetime(timestamp_raw)
                        groups["timestamp_dt"] = timestamp_dt
                        del groups["timestamp"]
                    except (ValueError, TypeError) as e:
                        self.logger.error(f"Failed to convert timestamp to datetime: {timestamp_raw} - {e}")
                        return False

                if not self._is_timestamp_in_range(timestamp_dt):
                    self.logger.debug(f"Timestamp {timestamp_dt} is out of range. Skipping line.")
                    return False

                action(groups)

                self._finalise_match(timestamp_dt, bytes_read)

                return True

        self.logger.debug(f"No match found on line: {line}")

        return False
  
    def convert_to_int(self, value: str, default=None):
        """
        Convert the given value string to an integer.

        Args:
            value: The input string to convert.
            default: The value to return if conversion fails.

        Returns:
            int: The int conversion of input string or default.
        """
        try:
            return int(value)
        except (TypeError, ValueError):
            return default

    def convert_op_format(self, op_type: str, default=None):
        """
        Converts a legacy operation type to a JSON-style operation string.

        Args:
            op_type (str): The legacy operation type (ADD, DEL, CMP, MOD, MODRDN)
            default (str): The default value to return if no match is found.

        Returns:
            str: Operation string (add, modify, delete) or the default.
        """
        operation_map = {
            'add': 'add',
            'mod': 'modify',
            'del': 'delete',
            'cmp': 'compare',
            'modrdn': 'modrdn'
        }

        if not isinstance(op_type, str):
            return default
        return operation_map.get(op_type.lower(), default)

    def map_legacy_control_name(self, legacy_name: str):
        """
        Maps a legacy control to its corresponding LDAP control name.

        Args:
            legacy_name (str): The legacy name of the control

        Returns:
            str: The LDAP control name if known, else the original value.
        """
        control_name_map = {
            "persistent": "LDAP_CONTROL_PERSISTENTSEARCH",
            "vlv_requests": "LDAP_CONTROL_VLVREQUEST",
            "sort_requests": "LDAP_CONTROL_SORTREQUEST",
            "sort_responses": "LDAP_CONTROL_SORTRESPONSE",
            "vlv_responses": "LDAP_CONTROL_VLVRESPONSE",
            "paged_results": "LDAP_CONTROL_PAGEDRESULTS",
        }

        if isinstance(legacy_name, str):
            return control_name_map.get(legacy_name.lower(), legacy_name)

        return legacy_name

    def get_control_counter_key(self, control_name: str, default=None):
        """
        Maps an LDAP control name to its corresponding counter key.

        Args:
            control_name (str): The LDAP control name.
            default (str): Value to return if the control name is not found.

        Returns:
            str: The counter key or default.
        """
        control_map = {
            "LDAP_CONTROL_PERSISTENTSEARCH": "persistent",
            "LDAP_CONTROL_VLVREQUEST": "vlv_requests",
            "LDAP_CONTROL_SORTREQUEST": "sort_requests",
            "LDAP_CONTROL_SORTRESPONSE": "sort_responses",
            "LDAP_CONTROL_VLVRESPONSE": "vlv_responses",
            "LDAP_CONTROL_PAGEDRESULTS": "paged_results",
        }

        return control_map.get(control_name, default)

    def extract_close_code(self, log_entry: dict, default=None):
        """
        Extracts a close reason code from a legacy log entry.

        Args:
            log_entry (dict): Log entry containing 'close_reason'.
            default (str): Value to return if no code is found.

        Returns:
            str: Extracted close reason code or default.
        """
        reason = log_entry.get("close_reason")
        if not reason:
            return default

        # If it's just the code return it.
        if re.fullmatch(r'[A-Z]\d+', reason.strip()):
            return reason.strip()

        # If it's a sentence ending in code.
        match = re.search(r'([A-Z]\d+)$', reason.strip())
        if match:
            return match.group(1)

        return default

    def build_op_scope_key(self, conn_scope_key, op_id):
        """
        Build a flat operation scoped key from a connection scoped key and operation ID.

        Args:
            conn_scope_key (str | tuple): Either a legacy tuple (restart, conn_id),
                                        or a new-format string like "1231231-2".
            op_id (int | str): The operation ID associated with this log entry.

        Returns:
            tuple: A flattened tuple of the form (restart, conn_id, op_id) that
                uniquely identifies an operation.

        Raises:
            ValueError: If the input format is invalid or op_id is None.
        """
        if op_id is None:
            raise ValueError("op_id cannot be None")

        # Convert op_id to int if needed
        op_id = int(op_id)

        # Legacy format
        if isinstance(conn_scope_key, tuple) and len(conn_scope_key) == 2:
            restart, conn_id = conn_scope_key
            return (restart, conn_id, op_id)

        # New format: string like "1231231-2"
        if isinstance(conn_scope_key, str) and '-' in conn_scope_key:
            parts = conn_scope_key.split('-')
            if len(parts) == 2 and all(part.isdigit() for part in parts):
                restart, conn_id = map(int, parts)
                return (restart, conn_id, op_id)

        raise ValueError(f"Invalid conn_scope_key format: {conn_scope_key}")

    def _process_result_legacy(self, log_entry: dict):
        """
        Process a legacy RESULT log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Normalises legacy notes format into a list of note dicts.
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        notes = log_entry.get('notes', None)
        normalised_notes = []
        if isinstance(notes, str):
            note_code = log_entry.get('notes', "")
            note_desc = log_entry.get('details', "")
            if note_code or note_desc:
                normalised_notes.append({
                    "note": note_code,
                    "description": note_desc
                })

        log_entry_norm = {
            "timestamp_dt": log_entry.get('timestamp_dt', None),
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "etime": log_entry.get('etime', None),
            "wtime": log_entry.get('wtime', None),
            "optime": log_entry.get('optime', None),
            "nentries": self.convert_to_int(log_entry.get('nentries', None)),
            "tag": self.convert_to_int(log_entry.get('tag', None)),
            "err": self.convert_to_int(log_entry.get('err', None)),
            "internal_op": log_entry.get('internal', None),
            "notes": normalised_notes 
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_result_entry(log_entry_norm)

    def _process_result_entry(self, log_entry: dict):
        """
        Process a RESULT line from access logs.

        Args:
            log_entry (dict): A dictionary representing a parsed log entry.

        Notes:
            - Includes error handling and logging for unexpected parsing issues.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        timestamp_dt = log_entry.get("timestamp_dt", None)
        op_id = log_entry.get("op_id", None)
        bind_dn = log_entry.get("bind_dn", "")
        etime = log_entry.get("etime", "")
        wtime = log_entry.get("wtime", "")
        optime = log_entry.get("optime", "")
        nentries = log_entry.get("nentries", None)
        tag = log_entry.get("tag", None)
        err = log_entry.get("err", None)
        internal_op = log_entry.get("internal_op", None)
        notes = log_entry.get("notes", [])

        # Tracking key
        conn_scope_key = log_entry.get("key", "")
        try:
            op_scope_key = self.build_op_scope_key(conn_scope_key, op_id)
        except (ValueError, TypeError) as e:
            self.logger.error(f"Invalid key format for conn_scope_key={conn_scope_key}, op_id={op_id} - {e}")
            op_scope_key = None

        if isinstance(timestamp_dt, datetime.datetime):
            timestamp_str = timestamp_dt.isoformat()
        else:
            timestamp_str = "Invalid timestamp"

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None
        
        # Global counters
        self.result.counters['result'] +=  1
        self.result.counters['timestamp'] += 1

        # Operation time fields
        if etime and isinstance(etime, str):
            try:
                etime_f = float(etime)
                heapq.heappush(self.result.etime_duration, etime_f)
                if len(self.result.etime_duration) > self.size_limit:
                    heapq.heappop(self.result.etime_duration)
                self.result.total_etime += etime_f
            except ValueError:
                self.logger.debug(f"Invalid etime format: {etime}")

        if wtime and isinstance(wtime, str):
            try:
                wtime_f = float(wtime)
                heapq.heappush(self.result.wtime_duration, wtime_f)
                if len(self.result.wtime_duration) > self.size_limit:
                    heapq.heappop(self.result.wtime_duration)
                self.result.total_wtime += wtime_f
            except ValueError:
                self.logger.debug(f"Invalid wtime format: {wtime}")

        if optime and isinstance(optime, str):
            try:
                optime_f = float(optime)
                heapq.heappush(self.result.optime_duration, optime_f)
                if len(self.result.optime_duration) > self.size_limit:
                    heapq.heappop(self.result.optime_duration)
                self.result.total_optime += optime_f
            except ValueError:
                self.logger.debug(f"Invalid optime format: {optime}")

        # Stat reporting to csv file
        try:
            self.result.etime_stat = round(self.result.etime_stat + float(etime), 8)
        except (ValueError, TypeError):
            self.logger.debug(f"Invalid etime for: {etime}")

        # Track error if present
        if err is not None:
            self.result.error_freq[err] += 1

        # Internal operation
        if internal_op:
            self.operation.counters['internal'] +=1
        
        if isinstance(notes, list):
            for note in notes:
                note_code = note.get("note")
                if not note_code:
                    continue 

                self.result.counters[f'notes{note_code}'] += 1

                # Exclude VLV
                if op_scope_key not in self.vlv.vlv_map:
                    result_notes = getattr(self.result, f'notes{note_code}', None)
                    entry = result_notes.setdefault(op_scope_key, {})

                    # Resolve base, scope, filter (new format or fallback to maps)
                    base = note.get("base_dn") or self.search.base_map.get(op_scope_key, "Unknown")
                    scope = note.get("scope") or self.search.scope_map.get(op_scope_key, "Unknown")
                    search_filter = note.get("filter") or self.search.filter_map.get(op_scope_key, "Unknown")
                    ip = log_entry.get("client_ip") or self.connection.ip_map.get(conn_scope_key, "Unknown")
                    bind_dn = self.bind.bind_dn_map.get(conn_scope_key, "Unknown")

                    entry.update({
                        "time": timestamp_str,
                        "etime": etime,
                        "nentries": nentries,
                        "ip": ip,
                        "bind_dn": bind_dn,
                        "base": base,
                        "scope": scope,
                        "filter": search_filter
                    })

                    # Remove legacy map state
                    self.search.base_map.pop(op_scope_key, None)
                    self.search.scope_map.pop(op_scope_key, None)
                    self.search.filter_map.pop(op_scope_key, None)
                else:
                    self.vlv.vlv_map[op_scope_key] = note_code

        # Process bind result
        if tag == 97:
            # Invalid credentials|Entry does not exist
            if err == 49:
                bad_pwd_dn = self.bind.bind_dn_map.get(conn_scope_key, "Unknown DN")
                bad_pwd_ip = self.connection.ip_map.get(conn_scope_key, None)
                self.result.bad_pwd_map[(bad_pwd_dn, bad_pwd_ip)] = (
                    self.result.bad_pwd_map.get((bad_pwd_dn, bad_pwd_ip), 0) + 1
                )
                # Trim items to size_limit
                if len(self.result.bad_pwd_map) > self.size_limit:
                    within_size_limit = dict(
                        sorted(
                            self.result.bad_pwd_map.items(),
                            key=lambda item: item[1],
                            reverse=True
                        )[:self.size_limit])
                    self.result.bad_pwd_map = within_size_limit

            # This result is involved in the SASL bind process, decrement bind count, etc
            elif err == 14:
                self.bind.counters['bind'] -= 1
                self.operation.counters['total'] -= 1
                self.bind.counters['sasl'] -= 1
                self.bind.version['3'] -= 1

                # Drop the sasl mech count also
                mech = self.bind.sasl_mech_map[op_scope_key]
                if mech:
                    self.bind.sasl_mech[mech] -= 1

            # Successful SASL bind
            else:
                result_dn = bind_dn
                if result_dn:
                    if result_dn != "":
                        # If this is a result of a sasl bind, grab the dn
                        if op_scope_key in self.bind.sasl_mech_map:
                            if result_dn is not None:
                                self.bind.bind_dn_map[conn_scope_key] = result_dn.lower()
                                self.bind.dns[result_dn] = (
                                    self.bind.dns.get(result_dn, 0) + 1
                                )
            
        # Handle other tag values
        elif isinstance(tag, int) and tag in [100, 101, 111, 115]:
            if nentries >= 0 and nentries not in self.result.nentries_set:
                heapq.heappush(self.result.nentries_num, nentries)
                self.result.nentries_set.add(nentries)

                if len(self.result.nentries_num) > self.size_limit:
                    removed = heapq.heappop(self.result.nentries_num)
                    self.result.nentries_set.remove(removed)

    def _process_bind_legacy(self, log_entry: dict):
        """
        Processes a legacy BIND log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "bind_dn": log_entry.get('bind_dn', ""),
            "method": log_entry.get('bind_method', ""),
            "mech": log_entry.get('sasl_mech', ""),
            "version": self.convert_to_int(log_entry.get('bind_version', None))
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_bind_entry(log_entry_norm)

    def _process_bind_entry(self, log_entry: dict):
        """
        Process a BIND line from access logs.

        Args:
            log_entry (dict): A dictionary containing the parsed bind log entry.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        conn_id = log_entry.get("conn_id", None)
        op_id = log_entry.get("op_id",None)
        bind_dn = log_entry.get("bind_dn", "")
        method = log_entry.get("method", "")
        mech = log_entry.get("mech", "")
        version = log_entry.get("version", None)

        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        # Normalise the bind_dn
        bind_dn = bind_dn.strip() if bind_dn else ""
        if not bind_dn:
            normalised_bind_dn = "Anonymous"
        else:
            normalised_bind_dn = bind_dn.lower()

        # Update counters
        self.bind.counters['bind'] += 1
        self.operation.counters['total'] += 1
        self.bind.version[str(version)] += 1

        # If we need to report on this DN, capture some info for tracking
        bind_dn_key = self._report_dn_key(normalised_bind_dn, self.report_dn)
        if bind_dn_key:
            self.bind.report_dn[bind_dn_key]['bind'] += 1
            self.bind.report_dn[bind_dn_key]['conn'].add(conn_id)

            # Loop over IPs captured at connection time to find the associated IP
            for (ip, ip_info) in self.connection.src_ip_map.items():
                if conn_scope_key in ip_info['keys']:
                    self.bind.report_dn[bind_dn_key]['ips'].add(ip)

        # Handle SASL or simple bind
        if method == 'sasl':
            self.bind.counters['sasl'] += 1
            if mech:
                sasl_mech_key = (conn_id, op_id)
                self.bind.sasl_mech[mech] += 1
                self.bind.sasl_mech_map[sasl_mech_key] = mech

            if normalised_bind_dn == self.root_dn.casefold():
                self.bind.counters['rootdn'] += 1

            # Dont count anonymous SASL binds
            if normalised_bind_dn != "Anonymous":
                self.bind.dns[normalised_bind_dn] += 1

        else:
            # Track DN statistics
            if normalised_bind_dn == "Anonymous":
                self.bind.counters['anon'] += 1
                self.bind.dns[normalised_bind_dn] += 1
            else:
                if normalised_bind_dn == self.root_dn.casefold():
                    self.bind.counters['rootdn'] += 1

                self.bind.dns[normalised_bind_dn] += 1

        self.bind.bind_dn_map[conn_scope_key] = normalised_bind_dn

    def _process_search_legacy(self, log_entry: dict):
        """
         Processes a legacy SRCH log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Normalises legacy request control format.
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "base_dn": log_entry.get('search_base', ""),
            "scope": self.convert_to_int(log_entry.get('search_scope', None)),
            "filter": log_entry.get('search_filter', ""),
            "attrs": log_entry.get('search_attrs', []),
            "authzid": log_entry.get("authzid", ""),
            "request_controls": [
                {
                    "oid_name": self.map_legacy_control_name(log_entry.get('options', None))
                }
            ]
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_search_entry(log_entry_norm)

    def _process_search_entry(self, log_entry: dict):
        """
        Processes a search log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed search log entry.
        Notes:
            - Includes error handling and logging for unexpected parsing issues.
        """
        self.logger.debug(f"_process_search_entry - Start - {log_entry}")

        conn_id = log_entry.get("conn_id", None)
        op_id = log_entry.get("op_id",None)
        base_dn = log_entry.get("base_dn", "")
        scope = log_entry.get("scope", None)
        search_filter = log_entry.get("filter", "")
        attrs = log_entry.get("attrs", [])
        controls = log_entry.get("request_controls", [])
        authzid = log_entry.get("authzid", "")

        # Tracking key
        conn_scope_key = log_entry.get("key", "")
        try:
            op_scope_key = self.build_op_scope_key(conn_scope_key, op_id)
        except (ValueError, TypeError) as e:
            self.logger.error(f"Invalid key format for conn_scope_key={conn_scope_key}, op_id={op_id} - {e}")
            op_scope_key = None

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        # Bump search and global op count
        self.search.counters['search'] +=  1
        self.operation.counters['total'] += 1

        # Search attributes
        if attrs is not None:
            if isinstance(attrs, list):
                for attr in attrs:
                    attr = attr.strip('"')
                    self.search.attrs[attr] += 1

            elif isinstance(attrs, str):
                if attrs == 'ALL':
                    self.search.attrs['All Attributes'] += 1
                else:
                    for attr in attrs.split():
                        attr = attr.strip('"')
                        self.search.attrs[attr] += 1

        # Bind DN report
        for dn in self.bind.report_dn:
            conns = self.bind.report_dn[dn]['conn']
            if conn_id in conns:
                bind_dn_key = self._report_dn_key(dn, self.report_dn)
                if bind_dn_key:
                    self.bind.report_dn[bind_dn_key]['srch'] = self.bind.report_dn[bind_dn_key].get('srch', 0) + 1

        # Search base
        if base_dn is not None:
            if base_dn:
                base = base_dn
            else:
                base = "Root DSE"
            search_base = base.lower()
            if search_base:
                if self.verbose:
                    self.search.bases[search_base] += 1
                    self.search.base_map[op_scope_key] = search_base

        # Search scope
        if scope is not None:
            if self.verbose:
                self.search.scope_map[op_scope_key] = SCOPE_LABEL[scope]

        # Search filter
        if search_filter and isinstance(search_filter, str):
            if self.verbose:
                # Unique tracking
                self.search.filter_map[op_scope_key] = search_filter

                # Count total occurrences of this filter
                self.search.filter_dict[search_filter] = self.search.filter_dict.get(search_filter, 0) + 1

                 # Update the top-N filter list (heap)
                found = False
                for idx, (count, existing_filter) in enumerate(self.search.filter_list):
                    if existing_filter == search_filter:
                        found = True
                        self.search.filter_list[idx] = (self.search.filter_dict[search_filter], search_filter)
                        heapq.heapify(self.search.filter_list)
                        break

                if not found:
                    if len(self.search.filter_list) < self.size_limit:
                        heapq.heappush(self.search.filter_list, (self.search.filter_dict[search_filter], search_filter))
                    else:
                        heapq.heappushpop(self.search.filter_list, (self.search.filter_dict[search_filter], search_filter))


        # Check for an entire base search
        if "objectclass=*" in search_filter.lower() or "objectclass=top" in search_filter.lower():
            if scope == 2:
                self.search.counters['base_search'] += 1

        # Search controls
        if controls and isinstance(controls, list):
            for ctrl in controls:
                oid = ctrl.get("oid")
                oid_name = ctrl.get("oid_name")
                # Increment the requried control counter
                if oid_name:
                    ctrl_key = self.get_control_counter_key(oid_name)
                    if ctrl_key:
                        self.search.counters[ctrl_key] += 1

        # Authorisation identity
        if authzid and isinstance(authzid, str):
            self.search['authzid'] += 1

    def _process_connect_legacy(self, log_entry: dict):
        """
         Processes a legacy connection log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "timestamp_dt": log_entry.get('timestamp_dt', None),
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "fd": self.convert_to_int(log_entry.get('fd', None)),
            "tls": log_entry.get('ssl', None),
            "client_ip": log_entry.get('src_ip', "")
        }

        # Compute and inject connection scoped key
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))
        restart = self.server.counters['restart']
        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_connect_entry(log_entry_norm)

    def _process_connect_entry(self, log_entry: dict):
        """
        Processes a connect log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed connect log entry.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        timestamp_dt = log_entry.get("timestamp_dt", None)
        conn_id = log_entry.get("conn_id", None)
        fd = log_entry.get("fd", None)
        tls = log_entry.get("tls",None)
        client_ip = log_entry.get("client_ip", "")

        # If conn=1, server has started a new lifecycle
        # For legacy logs, the 'key' is a tuple (restart, conn_id), not a string as in JSON logs.
        # So we need to recreate the conn_scope_key with updated restart count in the legacy case.
        conn_scope_key = log_entry.get("key", "")
        if conn_id == 1:
            self.server.counters['restart'] += 1
            if not isinstance(conn_scope_key, str):
                conn_scope_key = (self.server.counters['restart'], conn_id)

        # Should we add this IP to excluded
        if self.exclude_ip and client_ip in self.exclude_ip:
            self.connection.exclude_ip[conn_scope_key] = client_ip
            return None

        if self.verbose:
            self.connection.open_conns[client_ip] += 1
            self.connection.start_time[conn_id] = timestamp_dt

        # Update general connection counters
        self.connection.counters['conn'] += 1
        self.connection.counters['sim_conn'] += 1

        # Update the maximum number of simultaneous connections seen
        self.connection.counters['max_sim_conn'] = max(
            self.connection.counters['max_sim_conn'],
            self.connection.counters['sim_conn']
        )

        # Update protocol counters
        if tls:
            self.connection.counters['ldaps'] += 1
        elif client_ip == 'local':
            self.connection.counters['ldapi'] += 1
        else:
            self.connection.counters['ldap'] += 1

        # Track file descriptor counters
        self.connection.counters['fd_max'] = max(self.connection.counters['fd_taken'], int(fd))
        self.connection.counters['fd_taken'] += 1

        # Track source IP
        self.connection.ip_map[conn_scope_key] = client_ip

        self.connection.src_ip_map[client_ip]['count'] = self.connection.src_ip_map[client_ip].get('count', 0) + 1

        if 'keys' not in self.connection.src_ip_map[client_ip]:
            self.connection.src_ip_map[client_ip]['keys'] = set()

        self.connection.src_ip_map[client_ip]['keys'].add(conn_scope_key)

    def _process_unbind_legacy(self, log_entry: dict):
        """
         Processes a legacy UNBIND log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {}

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_unbind_entry(log_entry_norm)


    def _process_unbind_entry(self, log_entry: dict):
        """
        Processes a connect log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed unbind log entry.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        # Bump unbind count
        self.bind.counters['unbind'] += 1

    def _process_auth_legacy(self, log_entry: dict):
        """
         Processes a legacy TLS/SSL log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        # Hack to extract client_dn from legacy AUTH log entry
        msg = log_entry.get("msg", "")
        client_dn = None
        if msg and msg.startswith("client bound as"):
            match = re.search(r"client bound as (?P<client_dn>.+)", msg)
            if match:
                client_dn = match.group("client_dn")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "tls_version": log_entry.get('tls_version', ""),
            "keysize": self.convert_to_int(log_entry.get('keysize', None)),
            "cipher": log_entry.get('cipher', ""),
            "subject": log_entry.get('subject', ""),
            "issuer": log_entry.get('issuer', ""),
            "client_dn": client_dn,
            "msg": log_entry.get('msg', ""),
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_auth_entry(log_entry_norm)

    def _process_auth_entry(self, log_entry: dict):
        """
        Processes an SSL log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        tls_version = log_entry.get('tls_version', "")
        keysize = log_entry.get('keysize', None)
        cipher = log_entry.get('cipher', "")
        subject = log_entry.get('subject', "")
        issuer = log_entry.get('issuer', "")
        client_dn = log_entry.get('client_dn', "")
        msg = log_entry.get('msg', "")

        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        if tls_version:
            if conn_scope_key not in self.auth.auth_info:
                self.auth.auth_info[conn_scope_key] = {
                    'tls_version': tls_version,
                    'keysize': keysize,
                    'cipher': cipher,
                    'count': 0,
                    'subject': subject,
                    'issuer': issuer,
                    'client_dn': client_dn,
                    'msg': msg,
                    }

                if cipher:
                    self.auth.counters['cipher'] += 1
            else:
                if tls_version:
                    self.auth.auth_info[conn_scope_key]['tls_version'] = tls_version
                if keysize:
                    self.auth.auth_info[conn_scope_key]['keysize'] = keysize
                if cipher:
                    self.auth.auth_info[conn_scope_key]['cipher'] = cipher
                if subject:
                    self.auth.auth_info[conn_scope_key]['subject'] = subject
                if issuer:
                    self.auth.auth_info[conn_scope_key]['issuer'] = issuer
                if client_dn:
                    self.auth.auth_info[conn_scope_key]['client_dn'] = client_dn
                if msg:
                    self.auth.auth_info[conn_scope_key]['msg'] = msg

                if cipher:
                    self.auth.counters['cipher'] += 1

            if cipher:
                self.auth.auth_info[conn_scope_key]['count'] += 1

            if client_dn:
                self.auth.counters['client_bind'] += 1

            if msg:
                if "failed to map client certificate" in msg:
                    self.auth.counters['cert_map_fail'] += 1

    def _process_vlv_legacy(self, log_entry: dict):
        """
        Processes a legacy VLV log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Normalises legacy vlv_request format.
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "vlv_request": {
                "request_before_count": self.convert_to_int(log_entry.get('result_code', None)),
                "request_after_count": self.convert_to_int(log_entry.get('target_pos', None)),
                "request_index": self.convert_to_int(log_entry.get('context_id', None)),
                "request_content_count": self.convert_to_int(log_entry.get('target_pos', None)),
                "request_value_len": self.convert_to_int(log_entry.get('list_size', None)),
            },
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_vlv_entry(log_entry_norm)

    def _process_vlv_entry(self, log_entry: dict):
        """
        Processes a vlv log entry in JSON format.


        Args:
            log_entry (dict): A dictionary containing the parsed log entry.

        Notes:
            - Includes error handling and logging for unexpected parsing issues.
        """
        self.logger.debug(f"_process_vlv_entry - Start - {log_entry}")

        op_id = log_entry.get("op_id")

        # Extract VLV request
        vlv_req = log_entry.get("vlv_request", {})

        # Request Fields
        sort = vlv_req.get("request_sort", "")

       # Tracking key
        conn_scope_key = log_entry.get("key", "")
        try:
            op_scope_key = self.build_op_scope_key(conn_scope_key, op_id)
        except (ValueError, TypeError) as e:
            self.logger.error(f"Invalid key format for conn_scope_key={conn_scope_key}, op_id={op_id} - {e}")
            op_scope_key = None

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        # Bump vlv and global op stats
        self.vlv.counters['vlv'] += 1
        self.operation.counters['total'] = self.operation.counters['total'] + 1
        self.vlv.vlv_map[op_scope_key] = op_scope_key

        if sort:
            self.operation.counters['sort'] += 1

        self.logger.debug(f"_process_vlv_entry - End")

    def _process_abandon_legacy(self, log_entry: dict):
        """
        Processes a legacy ABANDON log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "target_op": self.convert_to_int(log_entry.get('targetop', None)),
            "msgid": self.convert_to_int(log_entry.get('msgid', None)),
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_abandon_entry(log_entry_norm)

    def _process_abandon_entry(self, log_entry: dict):
        """
        Processes an abandon operation log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed log entry.
        """
        self.logger.debug(f"_process_abandon_entry - Start - {log_entry}")

        conn_id = log_entry.get("conn_id", None)
        op_id = log_entry.get("op_id", None)
        target_op = log_entry.get('target_op', "")
        msgid = log_entry.get("msgid", None)
        
        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        # Bump some stats
        self.result.counters['result'] += 1
        self.operation.counters['total'] += 1
        self.operation.counters['abandon']  += 1

        # There could be multiple abandon ops per connection so store them in a list, keyed on conn_scope_key
        self.operation.op_map.setdefault('abandoned', {}).setdefault((conn_scope_key, conn_id), []) \
            .append((op_id, target_op, msgid))

    def _process_sort_legacy(self, log_entry: dict):
        """
        Processes a legacy SORT log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "attribute": self.convert_to_int(log_entry.get('attribute', "")),
            "status": self.convert_to_int(log_entry.get('status', None)),
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_sort_entry(log_entry_norm)

    def _process_sort_entry(self, log_entry: dict):
        """
        Processes an abandon operation log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed log entry.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        self.operation.counters['sort'] += 1

    def _process_extend_op_legacy(self, log_entry: dict):
        """
        Processes a legacy EXT log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "oid": log_entry.get('oid', ""),
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_extend_op_entry(log_entry_norm)

    def _process_extend_op_entry(self, log_entry: dict):
        """
        Processes an extended operation log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed log entry.
        """
        self.logger.debug(f"_process_extend_op_entry - Start - {log_entry}")

        conn_id = log_entry.get("conn_id", None)
        op_id = log_entry.get("op_id",None)
        oid = log_entry.get("oid", "")

        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        # Increment global operation counters
        self.operation.counters['total'] += 1
        self.operation.counters['extnd'] += 1

        # Track extended operation data if an OID is present
        if oid is not None:
            oid_key = (self.server.counters['restart'], conn_id, op_id)
            self.operation.extended[oid] += 1
            self.operation.op_map['extnd'][oid_key] = (
                self.operation.op_map['extnd'].get(oid_key, 0) + 1
            )

        # If the conn_id is associated with this DN, update op counter
        for dn in self.bind.report_dn:
            conns = self.bind.report_dn[dn]['conn']
            if conn_id in conns:
                bind_dn_key = self._report_dn_key(dn, self.report_dn)
                if bind_dn_key:
                    self.bind.report_dn[bind_dn_key]['ext'] += 1

        self.logger.debug(f"_process_extend_op_entry - End")

    def _process_autobind_legacy(self, log_entry: dict):
        """
        Processes a legacy AUTOBIND log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "bind_dn": log_entry.get('bind_id', ""),
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_autobind_entry(log_entry_norm)

    def _process_autobind_entry(self, log_entry: dict):
        """
        Processes an extended operation log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed log entry.
        """
        self.logger.debug(f"_process_extend_op_entry - Start - {log_entry}")

        bind_dn = log_entry.get("bind_dn", "")
        
        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        # Bump relevant counters
        self.bind.counters['bind'] += 1
        self.bind.counters['autobind'] += 1
        self.operation.counters['total'] += 1

        existing_bind_dn = self.bind.bind_dn_map.get(conn_scope_key, bind_dn)
        if existing_bind_dn:
            if existing_bind_dn.casefold() == self.root_dn.casefold():
                self.bind.counters['rootdn'] += 1
            existing_bind_dn = existing_bind_dn.lower()
            self.bind.dns[existing_bind_dn] += 1

    def _process_disconnect_legacy(self, log_entry: dict):
        """
        Processes a legacy Disconnect log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "timestamp_dt": log_entry.get('timestamp_dt', None),
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "fd": self.convert_to_int(log_entry.get('fd', None)),
            "close_error": log_entry.get('error_code', ""),
            "close_reason": log_entry.get('close_reason', ""),
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_disconnect_entry(log_entry_norm)

    def _process_disconnect_entry(self, log_entry: dict):
        """
        Processes a disconnect log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed log entry.
        """
        self.logger.debug(f"_process_disconnect_entry - Start - {log_entry}")

        timestamp_dt = log_entry.get("timestamp_dt", None)
        conn_id = log_entry.get("conn_id", None)
        close_reason = log_entry.get("close_reason","")
        close_error = log_entry.get("close_error","")

        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        if self.verbose:
            # Handle verbose logging for open connections and IP addresses
            src_ip = self.connection.ip_map.get(conn_scope_key)
            if src_ip and src_ip in self.connection.open_conns:
                open_conns = self.connection.open_conns
                if open_conns[src_ip] > 1:
                    open_conns[src_ip] -= 1
                else:
                    del open_conns[src_ip]

        # Handle latency and disconnect times
        if self.verbose:
            start_time = self.connection.start_time[conn_id]
            finish_time = timestamp_dt
            if start_time and finish_time:
                latency = self.get_elapsed_time(start_time, finish_time, "seconds")
                bucket = self._group_latencies(latency)
                LATENCY_GROUPS[bucket] += 1

                # Reset start time for the connection
                self.connection.start_time[conn_id] = None

        # Update connection stats
        self.connection.counters['sim_conn'] -= 1
        self.connection.counters['fd_returned'] += 1

        # Manage disconnect code
        if close_reason:
            if " - " in close_reason:
                disconnect_reason, disconnect_code = close_reason.rsplit(" - ", 1)
            else:
                disconnect_reason = close_reason.strip()
                disconnect_code = "Unknown"
            if disconnect_code:
                self.connection.disconnect_code[disconnect_code] += 1
                self.connection.disconect_map[conn_scope_key] = disconnect_code

        # Manage error codes if provided
        if close_error:
            # Try legacy disconnect error code fitst
            error_type = None
            error_type = DISCONNECT_ERRORS.get(close_error.strip())
            # Fallback to using close error directly
            if error_type is None:
                error_type = close_error
            self.connection.error_code[error_type][disconnect_code] += 1

    def _process_crud_legacy(self, log_entry):
        """
        Processes a legacy ADD, MOD, DEL, CMP log entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Maps legacy op format to JSON format.
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "operation": self.convert_op_format(log_entry.get('op_type', None)),
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_crud_entry(log_entry_norm)

    def _process_crud_entry(self, log_entry: dict):
        """
        Processes an ADD, MODIFY, or DELETE log entry in JSON format.

        Args:
            log_entry (dict): A dictionary containing the parsed log entry.
        
        Notes:
            - Includes error handling and logging for unexpected parsing issues.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        operation = log_entry.get("operation", "")
        conn_id = log_entry.get("conn_id", None)
        op_id = log_entry.get("op_id", None)
        authzid = log_entry.get("authzid", "")

        # Tracking key
        conn_scope_key = log_entry.get("key", "")
        try:
            op_scope_key = self.build_op_scope_key(conn_scope_key, op_id)
        except (ValueError, TypeError) as e:
            self.logger.error(f"Invalid key format for conn_scope_key={conn_scope_key}, op_id={op_id} - {e}")
            op_scope_key = None

        operation = operation.lower()
        self.operation.counters['total'] += 1

        # Use operation type as key for stats
        if operation:
            # Increment op type counter
            self.operation.counters[operation] += 1

            # Increment the op type map counter
            self.operation.op_map[operation][conn_scope_key] += 1

        # If the conn_id is associated with this DN, update op counter
        for dn in self.bind.report_dn:
            conns = self.bind.report_dn[dn]['conn']
            if conn_id in conns:
                bind_dn_key = self._report_dn_key(dn, self.report_dn)
                if bind_dn_key:
                    self.bind.report_dn[bind_dn_key][op_scope_key] += 1

        # Authorisation identity
        if authzid:
            self.operation.counters['authzid'] += 1

            self.logger.debug(f"_process_result_entry - End")

    def _process_entry_referral_legacy(self, log_entry):
        """
        Processes a legacy REFERRAL or ENTRY entry from access logs.

        Args:
            log_entry (dict): Parsed legacy log entry dictionary.

        Notes:
            - Maps legacy op format to JSON format.
            - Converts legacy log string numeric fields to integers where appropriate.
            - Normalises keys and formats to align with the newer JSON log entries.
            - Delegates further handling to a unified bind processing method.
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        log_entry_norm = {
            "conn_id": self.convert_to_int(log_entry.get('conn_id', None)),
            "op_id": self.convert_to_int(log_entry.get('op_id', None)),
            "target_dn": log_entry.get('dn', ""),
            "operation": self.convert_op_format(log_entry.get('op_type', None)),
        }

        # Compute and inject connection scoped key
        restart = self.server.counters['restart']
        conn_id = self.convert_to_int(log_entry.get('conn_id', None))

        if restart is not None and conn_id is not None:
            log_entry_norm["key"] = (restart, conn_id)
        else:
            log_entry_norm["key"] = None

        del log_entry

        self._process_entry_referral_entry(log_entry_norm)

    def _process_entry_referral_entry(self, log_entry: dict):
        """
        Process a log entry related to LDAP referral or entry operations.

        Args:
            log_entry (dict): A dictionary containing the parsed log entry.
        """
        self.logger.error(f"{inspect.currentframe().f_code.co_name} - log_entry:{log_entry}")

        op_type = log_entry.get('operation', "")

        # Tracking key
        conn_scope_key = log_entry.get("key", "")

        # Should we ignore this operation
        if conn_scope_key in self.connection.exclude_ip:
            return None

        # Process operation type
        if op_type is not None:
            if op_type == 'ENTRY':
                self.result.counters['entry'] += 1
            elif op_type == 'REFERRAL':
                self.result.counters['referral'] += 1

    def display_bind_report(self):
        """
        Display info on the tracked DNs.
        """
        print("\nBind Report")
        print("====================================================================\n")
        for k, v in self.bind.report_dn.items():
            print(f"\nBind DN: {k}")
            print("--------------------------------------------------------------------\n")
            print("   Client Addresses:\n")
            ips = self.bind.report_dn[k].get('ips', set())
            for i, ip in enumerate(ips, start=1):
                print(f"        {i}:      {ip}")
            print("\n   Operations Performed:\n")
            print(f"        Binds:      {self.bind.report_dn[k].get('bind', 0)}")
            print(f"        Searches:   {self.bind.report_dn[k].get('srch', 0)}")
            print(f"        Modifies:   {self.bind.report_dn[k].get('mod', 0)}")
            print(f"        Adds:       {self.bind.report_dn[k].get('add', 0)}")
            print(f"        Deletes:    {self.bind.report_dn[k].get('del', 0)}")
            print(f"        Compares:   {self.bind.report_dn[k].get('cmp', 0)}")
            print(f"        ModRDNs:    {self.bind.report_dn[k].get('modrdn', 0)}")
            print(f"        Ext Ops:    {self.bind.report_dn[k].get('ext', 0)}")

        print("Done.")

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

    def _process_and_write_stats(self, timestamp: datetime, bytes_read: int):
        """
        Processes statistics and writes them to the CSV file at defined intervals.

        Args:
            norm_timestamp: Normalized datetime for the current match
            bytes_read: Number of bytes read in the current file

        Returns:
            None
        """
        self.logger.debug(f"{inspect.currentframe().f_code.co_name} timestamp:{timestamp} bytes_read:{bytes_read}")

        if self.csv_writer is None:
            self.logger.error("CSV writer not enabled.")
            return

        # Define the stat mapping for CSV output
        stats = {
            'result': self.result.counters,
            'search': self.search.counters,
            'add': self.operation.counters,
            'modify': self.operation.counters,
            'modrdn': self.operation.counters,
            'compare': self.operation.counters,
            'delete': self.operation.counters,
            'abandon': self.operation.counters,
            'conn': self.connection.counters,
            'ldaps': self.connection.counters,
            'bind': self.bind.counters,
            'anon': self.bind.counters,
            'unbind': self.bind.counters,
            'notesA': self.result.counters,
            'notesU': self.result.counters,
            'notesF': self.result.counters,
            'etime_stat': self.result.counters
        }

        # Build the current stat block
        curr_stat_block = [timestamp]
        for key, refdict in stats.items():
            curr_stat_block.append(refdict.get(key, 0)) 

        curr_time = curr_stat_block[0]
        # Check for previous stats for differences
        if self.prev_stats is not None:
            prev_stat_block = self.prev_stats
            prev_time = prev_stat_block[0]

            # Prepare the output block
            out_stat_block = [prev_stat_block[0], int(prev_time.timestamp())]
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
                self.csv_writer.writerow(out_stat_block)
                self.result.etime_stat = 0.0

                # Update previous stats for the next interval
                self.prev_stats = curr_stat_block

        else:
            # First run, add the csv header for each column
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
            self.csv_writer.writerow(out_stat_block)
            self.result.etime_stat = 0.0

    def _get_stats_interval(self, report_stats_sec: str, report_stats_min: str):
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

    def _parse_log_timestamp(self, timestamp: str):
        """
        Parses a timestamp string into a datetime object with timezone support.

        Handles multiple formats, truncates nanoseconds to microseconds
        (as datetime only supports up to microsecond precision), and
        returns a timezone-aware datetime object.

        Supported formats include:
        - '[28/Mar/2002:13:14:22 -0800]'
        - '[07/Jun/2023:09:55:50.638781123 +0000]'
        - '2025-07-09T14:00:57.318795049 +0000'

        Args:
            timestamp (str): The timestamp string to parse.

        Returns:
            datetime: A timezone aware datetime object.

        Raises:
            ValueError: If the timestamp format is not recognized or parsing fails.
        """
        if not isinstance(timestamp, str):
            raise TypeError("Timestamp must be a string.")
        try:
            timestamp = timestamp.strip("[]")

            # JSON ISO 8601 format: 2025-07-09T14:02:48.319979321 +0000
            if re.match(r"\d{4}-\d{2}-\d{2}T", timestamp):
                datetime_part, tz_offset = timestamp.rsplit(" ", 1)
                if '.' in datetime_part:
                    base, frac = datetime_part.split('.')
                    frac = frac[:6].ljust(6, '0')  # nanoseconds to microseconds
                    datetime_part = f"{base}.{frac}"
                    fmt = "%Y-%m-%dT%H:%M:%S.%f"
                else:
                    fmt = "%Y-%m-%dT%H:%M:%S"
            else:
                # Legacy format: [09/Jul/2025:13:58:20.780553563 +0000]
                datetime_part, tz_offset = timestamp.rsplit(" ", 1)
                if '.' in datetime_part:
                    base, frac = datetime_part.rsplit('.', 1)
                    frac = frac[:6].ljust(6, '0')   # nanoseconds to microseconds
                    datetime_part = f"{base}.{frac}"
                    fmt = "%d/%b/%Y:%H:%M:%S.%f"
                else:
                    fmt = "%d/%b/%Y:%H:%M:%S"

            dt = datetime.datetime.strptime(datetime_part, fmt)
            if tz_offset[0] == "+":
                tz_offset_sign = 1
            else:
                tz_offset_sign = -1
            hrs_offset = int(tz_offset[1:3])
            min_offset = int(tz_offset[3:5])
            tz_diff = datetime.timedelta(hours=hrs_offset, minutes=min_offset)
            return dt.replace(tzinfo=datetime.timezone(tz_offset_sign * tz_diff))

        except Exception as e:
            self.logger.error(f"Failed to parse timestamp: {timestamp} - {e}")
            raise

    def convert_timestamp_to_datetime(self, timestamp: str):
        """
        Converts a supported timestamp string to a datetime object with timezone info.

        Internally wraps _parse_log_timestamp() for reuse.

        Args:
            timestamp (str): Timestamp in supported formats.

        Returns:
            datetime: Timezone-aware datetime object.

        Raises:
            Exception: For invalid input or failed parsing.
        """
        return self._parse_log_timestamp(timestamp)

    def get_elapsed_time(self, start: datetime, finish: datetime, time_format: str = "seconds"):
        """
        Calculates the elapsed time between start and finish datetime objects.

        Args:
            start (datetime): The start time.
            finish (datetime): The finish time.
            time_format (str): Output format ("seconds" or "hms").

        Returns:
            float or str: Elapsed time in seconds or human-readable string.
        """
        if not start or not finish:
            return 0 if time_format == "seconds" else "0 hours, 0 minutes, 0 seconds"

        try:
            elapsed = finish - start
            total_seconds = int(elapsed.total_seconds())

            if time_format == "seconds":
                return total_seconds

            days, remainder = divmod(total_seconds, 86400)
            hours, remainder = divmod(remainder, 3600)
            minutes, seconds = divmod(remainder, 60)

            if days > 0:
                return f"{days} days, {hours} hours, {minutes} minutes, {seconds} seconds"
            else:
                return f"{hours} hours, {minutes} minutes, {seconds} seconds"

        except Exception as e:
            self.logger.error(f"Error calculating elapsed time. - {e}")
            return 0 if time_format == "seconds" else "0 hours, 0 minutes, 0 seconds"

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
            IndexError: Can be raised by convert_timestamp_to_datetime.
            TypeError: If start_time or stop_time is not a string.
        """
        if not isinstance(start_time, str) or not isinstance(stop_time, str):
            raise TypeError("Start time and stop time must be strings.")

        if not start_time or not stop_time:
            raise ValueError("Start time and stop time cannot be empty.")

        try:
            # Convert timestamps to datetime objects
            norm_start_time = self.convert_timestamp_to_datetime(start_time)
            norm_stop_time = self.convert_timestamp_to_datetime(stop_time)

            # No timetravel (stop time should not be earlier than start time)
            if norm_stop_time <= norm_start_time:
                raise ValueError(f"End time: {norm_stop_time} is before or equal to start time: {norm_start_time}.")

            # Store the parse times
            self.server.parse_start_time = norm_start_time
            self.server.parse_stop_time = norm_stop_time
            self.logger.debug(f"Parse times set. Start: {norm_start_time}, Finish: {norm_stop_time}")

        except (ValueError, IndexError, TypeError) as e:
            self.logger.error(f"Error setting parse times. - {e}")
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
        logconv.py -X 127.0.0.1 -X 1.2.3.4 /var/log/dirsrv/slapd-host/access*

    Analyze logs within a specific range:
        logconv.py -S "2025-07-09T14:00:56.682303279 +0000"  -E "2025-07-09T14:02:50.693400135 +0000" /var/log/dirsrv/slapd-host/access*
        logconv.py -S "[09/Jul/2025:13:58:20.945149534 +0000]"  -E "[09/Jul/2025:13:58:23.226704200 +0000]" /var/log/dirsrv/slapd-host/access*

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
        help='Start analysing logfile from a specific time.'
                '\nE.g. "[04/Jun/2024:10:31:20.014629085 +0200]"\nE.g. "[04/Jun/2024:10:31:20 +0200]"'
    )
    time_group.add_argument(
        '-E', '--endTime',
        type=str,
        metavar="END_TIME",
        action='store',
        help='Stop analysing logfile at this time.'
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

    debug_group = parser.add_argument_group("DEBUG mode")
    debug_group.add_argument(
        '-l', '--loglevel',
        type=str,
        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL'],
        default='INFO',
        help='Set logging level (default: INFO)'
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

        # Set log level
        log_level = getattr(logging, args.loglevel.upper(), logging.INFO)
        db.logger = db._setup_logger(log_level)

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
        access_logs = []
        access_log_name = ""
        for log_path in existing_logs:
            log_name = log_path.rsplit('/', 1)[-1]
            if 'access' in log_name:
                access_logs.append(log_path)
                if 'access' == log_name:
                    access_log_name = log_path

        access_logs.sort(key=lambda x: os.path.getctime(x))

        # Put partial/current log at the end of the list as it's the newest log
        if access_log_name in access_logs:
            access_logs.append(access_logs.pop(access_logs.index(access_log_name)))

        num_logs = len(access_logs)
        print(f"Processing {num_logs} access log{'s' if num_logs > 1 else ''}...\n")

        # File processing loop
        for (num, accesslog) in enumerate(access_logs, start=1):
            if os.path.isfile(accesslog):
                db.process_file(num, accesslog)
            else:
                db.logger.error(f"Invalid file: {accesslog}")

    except Exception as e:
        print("An error occurred: %s", e)
        sys.exit(1)

    # Prep for display
    elapsed_time = db.get_elapsed_time(db.server.first_time, db.server.last_time, "hms")
    elapsed_secs = db.get_elapsed_time(db.server.first_time, db.server.last_time, "seconds")
    num_ops = db.operation.counters['total']
    num_results = db.result.counters['result']
    num_conns = db.connection.counters['conn']
    num_ldap = db.connection.counters['ldap']
    num_ldapi = db.connection.counters['ldapi']
    num_ldaps = db.connection.counters['ldaps']
    num_startls = db.operation.extended.get(STLS_OID, 0)
    num_search = db.search.counters['search']
    num_mod = db.operation.counters['modify']
    num_add = db.operation.counters['add']
    num_del = db.operation.counters['delete']
    num_modrdn = db.operation.counters['modrdn']
    num_cmp = db.operation.counters['compare']
    num_bind = db.bind.counters['bind']
    num_unbind = db.bind.counters['unbind']
    num_proxyd_auths = db.operation.counters['authzid'] + db.search.counters['authzid']
    num_time_count = db.result.counters['timestamp']
    if num_time_count:
        avg_wtime = round(db.result.total_wtime/num_time_count, 9)
        avg_optime = round(db.result.total_optime/num_time_count, 9)
        avg_etime = round(db.result.total_etime/num_time_count, 9)
    num_fd_taken = db.connection.counters['fd_taken']
    num_fd_rtn = db.connection.counters['fd_returned']

    num_DM_binds = db.bind.counters['rootdn']
    num_base_search = db.search.counters['base_search']
    try:
        log_start_time = db.server.first_time
    except ValueError:
        log_start_time = "Unknown"

    try:
        log_end_time = db.server.last_time
    except ValueError:
        log_end_time = "Unknown"

    print(f"\n\nTotal Log Lines Analysed:{db.server.counters['lines_parsed']}\n")
    print("\n----------- Access Log Output ------------\n")
    print(f"Start of Logs:                  {log_start_time}")
    print(f"End of Logs:                    {log_end_time}")
    print(f"\nProcessed Log Time:             {elapsed_time}")
    # Display DN report
    if db.report_dn:
        db.display_bind_report()
        sys.exit(1)

    print(f"\nRestarts:                       {db.server.counters['restart']}")
    if db.auth.counters['cipher'] > 0:
        tls_passwd = defaultdict(int)
        tls_cert = []

        for details in db.auth.auth_info.values():
            tls_version = details['tls_version']
            count = details['count']

            if details.get('subject') or details.get('issuer'):
                tls_cert.append({
                    'tls_version': tls_version,
                    'count': count,
                    'keysize': details.get('keysize'),
                    'cipher': details.get('cipher'),
                    'subject': details.get('subject'),
                    'issuer': details.get('issuer'),
                    'client_dn': details['client_dn']
                })
                continue

            key = (tls_version, details.get('keysize'), details.get('cipher'))
            tls_passwd[key] += count

        print("TLS Password authentication:")
        for (tls_version, keysize, cipher), count in tls_passwd.items():
            keysize_str = f"{keysize}-bit" if keysize else ""
            print(f"- {tls_version} {keysize_str} {cipher}    ({count} connection{'s' if count > 1 else ''})")
        print()

        print("TLS Certificate authentication:")
        for entry in tls_cert:
            keysize_str = f"{entry['keysize']}-bit" if entry['keysize'] else ""
            print(f"- {entry['tls_version']} {keysize_str} {entry['cipher']}   ({entry['count']} connection{'s' if entry['count'] > 1 else ''})")
            if entry['client_dn']:
                print(f"  Client DN: {entry['client_dn']}")
            if entry['subject']:
                print(f"  Client CN: {entry['subject']}")
            if entry['issuer']:
                print(f"  Issuer CN: {entry['issuer']}")
            print()

    print(f"Peak Concurrent connections:    {db.connection.counters['max_sim_conn']}")
    print(f"Total Operations:               {num_ops}")
    print(f"Total Results:                  {num_results}")
    print(f"Overall Performance:            {db.get_overall_perf(num_results, num_ops)}%")
    if elapsed_secs > 0:
        print(f"\nTotal connections:              {num_conns:<10}{num_conns/elapsed_secs:>10.2f}/sec {(num_conns/elapsed_secs) * 60:>10.2f}/min")
        print(f"- LDAP connections:             {num_ldap:<10}{num_ldap/elapsed_secs:>10.2f}/sec {(num_ldap/elapsed_secs) * 60:>10.2f}/min")
        print(f"- LDAPI connections:            {num_ldapi:<10}{num_ldapi/elapsed_secs:>10.2f}/sec {(num_ldapi/elapsed_secs) * 60:>10.2f}/min")
        print(f"- LDAPS connections:            {num_ldaps:<10}{num_ldaps/elapsed_secs:>10.2f}/sec {(num_ldaps/elapsed_secs) * 60:>10.2f}/min")
        print(f"- StartTLS Extended Ops:        {num_startls:<10}{num_startls/elapsed_secs:>10.2f}/sec {(num_startls/elapsed_secs) * 60:>10.2f}/min")
        print(f"\nSearches:                       {num_search:<10}{num_search/elapsed_secs:>10.2f}/sec {(num_search/elapsed_secs) * 60:>10.2f}/min")
        print(f"Modifications:                  {num_mod:<10}{num_mod/elapsed_secs:>10.2f}/sec {(num_mod/elapsed_secs) * 60:>10.2f}/min")
        print(f"Adds:                           {num_add:<10}{num_add/elapsed_secs:>10.2f}/sec {(num_add/elapsed_secs) * 60:>10.2f}/min")
        print(f"Deletes:                        {num_del:<10}{num_del/elapsed_secs:>10.2f}/sec {(num_del/elapsed_secs) * 60:>10.2f}/min")
        print(f"Mod RDNs:                       {num_modrdn:<10}{num_modrdn/elapsed_secs:>10.2f}/sec {(num_modrdn/elapsed_secs) * 60:>10.2f}/min")
        print(f"Compares:                       {num_cmp:<10}{num_cmp/elapsed_secs:>10.2f}/sec {(num_cmp/elapsed_secs) * 60:>10.2f}/min")
        print(f"Binds:                          {num_bind:<10}{num_bind/elapsed_secs:>10.2f}/sec {(num_bind/elapsed_secs) * 60:>10.2f}/min")
    else:
        print("\nElapsed time is unavailable, rate based metrics will not be shown.")
        print(f"Total connections:              {num_conns}")
        print(f"- LDAP connections:             {num_ldap}")
        print(f"- LDAPI connections:            {num_ldapi}")
        print(f"- LDAPS connections:            {num_ldaps}")
        print(f"- StartTLS Extended Ops:        {num_startls}")
        print(f"Searches:                       {num_search}")
        print(f"Modifications:                  {num_mod}")
        print(f"Adds:                           {num_add}")
        print(f"Deletes:                        {num_del}")
        print(f"Mod RDNs:                       {num_modrdn}")
        print(f"Compares:                       {num_cmp}")
        print(f"Binds:                          {num_bind}")

    if num_time_count:
        print(f"\nAverage wtime (wait time):      {avg_wtime:.9f}")
        print(f"Average optime (op time):       {avg_optime:.9f}")
        print(f"Average etime (elapsed time):   {avg_etime:.9f}")
    print(f"\nMulti-factor Authentications:   {db.result.counters['notesM']}")
    print(f"Proxied Auth Operations:        {num_proxyd_auths}")
    print(f"Persistent Searches:            {db.search.counters['persistent']}")
    print(f"Internal Operations:            {db.operation.counters['internal']}")
    print(f"Entry Operations:               {db.result.counters['entry']}")
    print(f"Extended Operations:            {db.operation.counters['extnd']}")
    print(f"Abandoned Requests:             {db.operation.counters['abandon']}")
    print(f"Smart Referrals Received:       {db.result.counters['referral']}")
    print(f"\nVLV Operations:                 {db.vlv.counters['vlv']}")
    print(f"VLV Unindexed Searches:         {len([key for key, value in db.vlv.vlv_map.items() if value == 'A'])}")
    print(f"VLV Unindexed Components:       {len([key for key, value in db.vlv.vlv_map.items() if value == 'U'])}")
    print(f"SORT Operations:                {db.operation.counters['sort']}")
    print(f"\nEntire Search Base Queries:     {num_base_search}")
    print(f"Paged Searches:                 {db.result.counters['notesP']}")
    num_unindexed_search = len(db.result.notesA)
    print(f"Unindexed Searches:             {num_unindexed_search}")
    if db.verbose and num_unindexed_search > 0:
        for num, key in enumerate(db.result.notesA, start=1):
            data = db.result.notesA[key]
            if isinstance(key, tuple):
                _, conn, op = key

            print(f"\n  Unindexed Search #{num} (notes=A)")
            print(f"    - Date/Time:           {data.get('time', '-')}")
            print(f"    - Connection Number:   {conn}")
            print(f"    - Operation Number:    {op}")
            print(f"    - Etime:               {data.get('etime', '-')}")
            print(f"    - Nentries:            {data.get('nentries', 0)}")
            print(f"    - IP Address:          {data.get('ip', '-')}")
            print(f"    - Search Base:         {data.get('base', '-')}")
            print(f"    - Search Scope:        {data.get('scope', '-')}")
            print(f"    - Search Filter:       {data.get('filter', '-')}")
            print(f"    - Bind DN:             {data.get('bind_dn', '-')}\n")

    num_unindexed_component = len(db.result.notesU)
    print(f"Unindexed Components:           {num_unindexed_component}")
    if db.verbose and num_unindexed_component > 0:
        for num, key in enumerate(db.result.notesU, start=1):
            data = db.result.notesU[key]
            if isinstance(key, tuple):
                _, conn, op = key

            print(f"\n  Unindexed Component #{num} (notes=U)")
            print(f"    - Date/Time:           {data.get('time', '-')}")
            print(f"    - Connection Number:   {conn}")
            print(f"    - Operation Number:    {op}")
            print(f"    - Etime:               {data.get('etime', '-')}")
            print(f"    - Nentries:            {data.get('nentries', 0)}")
            print(f"    - IP Address:          {data.get('ip', '-')}")
            print(f"    - Search Base:         {data.get('base', '-')}")
            print(f"    - Search Scope:        {data.get('scope', '-')}")
            print(f"    - Search Filter:       {data.get('filter', '-')}")
            print(f"    - Bind DN:             {data.get('bind_dn', '-')}\n")

    num_invalid_filter = len(db.result.notesF)
    print(f"Invalid Attribute Filters:      {num_invalid_filter}")
    if db.verbose and num_invalid_filter > 0:
        for num, key in enumerate(db.result.notesF, start=1):
            data = db.result.notesF[key]
            if isinstance(key, tuple):
                _, conn, op = key

            print(f"\n  Invalid Attribute Filter #{num} (notes=F)")
            print(f"    - Date/Time:           {data.get('time', '-')}")
            print(f"    - Connection Number:   {conn}")
            print(f"    - Operation Number:    {op}")
            print(f"    - Etime:               {data.get('etime', '-')}")
            print(f"    - Nentries:            {data.get('nentries', 0)}")
            print(f"    - IP Address:          {data.get('ip', '-')}")
            print(f"    - Search Filter:       {data.get('filter', '-')}")
            print(f"    - Bind DN:             {data.get('bind_dn', '-')}\n")

    print(f"FDs Taken:                      {num_fd_taken}")
    print(f"FDs Returned:                   {num_fd_rtn}")
    print(f"Highest FD Taken:               {db.connection.counters['fd_max']}\n")
    num_broken_pipe = len(db.connection.broken_pipe)
    print(f"Broken Pipes:                   {num_broken_pipe}")
    if num_broken_pipe > 0:
        for code, count in db.connection.broken_pipe.items():
            print(f"    - {count} ({code}) {DISCONNECT_MSG.get(code, 'unknown')}")
        print()
    num_reset_peer = len(db.connection.connection_reset)
    print(f"Connection Reset By Peer:       {num_reset_peer}")
    if num_reset_peer > 0:
        for code, count in db.connection.connection_reset.items():
            print(f"    - {count} ({code}) {DISCONNECT_MSG.get(code, 'unknown')}")
        print()
    num_resource_unavail = len(db.connection.resource_unavail)
    print(f"Resource Unavailable:           {num_resource_unavail}")
    if num_resource_unavail > 0:
        for code, count in db.connection.resource_unavail.items():
            print(f"    - {count} ({code}) {DISCONNECT_MSG.get(code, 'unknown')}")
        print()
    print(f"Max BER Size Exceeded:          {db.connection.disconnect_code.get('B2', 0)}\n")
    print(f"Binds:                          {db.bind.counters['bind']}")
    print(f"Unbinds:                        {db.bind.counters['unbind']}")
    print(f"----------------------------------")
    print(f"- LDAP v2 Binds:                {db.bind.version.get('2', 0)}")
    print(f"- LDAP v3 Binds:                {db.bind.version.get('3', 0)}")
    print(f"- AUTOBINDs(LDAPI):             {db.bind.counters['autobind']}")
    print(f"- SSL Client Binds:             {db.auth.counters['client_bind']}")
    print(f"- Failed SSL Client Binds:      {db.auth.counters['cert_map_fail']}")
    print(f"- SASL Binds:                   {db.bind.counters['sasl']}")
    if db.bind.counters['sasl'] > 0:
        saslmech = db.bind.sasl_mech
        for saslb in sorted(saslmech.keys(), key=lambda k: saslmech[k], reverse=True):
            print(f"   - {saslb:<4}: {saslmech[saslb]}")
    print(f"- Directory Manager Binds:      {num_DM_binds}")
    print(f"- Anonymous Binds:              {db.bind.counters['anon']}\n")
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
        open_conns = db.connection.open_conns
        if len(open_conns) > 0:
            print(f"\n ----- Current Open Connection IDs -----\n")
            for conn in sorted(open_conns.keys(), key=lambda k: open_conns[k], reverse=True):
                print(f"{conn:<16} {open_conns[conn]:>10}")

        # Error Codes
        print(f"\n----- Errors -----\n")
        error_freq = db.result.error_freq
        for err in sorted(error_freq.keys(), key=lambda k: error_freq[k], reverse=True):
            print(f"err={err:<2} {error_freq[err]:>10}  {LDAP_ERR_CODES[str(err)]:<30}")

        # Failed Logins
        bad_pwd_map = db.result.bad_pwd_map
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
        disconnect_codes = db.connection.disconnect_code
        if len(disconnect_codes) > 0:
            print(f"\n----- Total Connection Codes ----\n")
            for code in disconnect_codes:
                print(f"{code:<8} {disconnect_codes[code]:>10}  {DISCONNECT_MSG.get(code, 'Unknown'):<30}")

        # Unique IPs
        ip_map = db.connection.ip_map
        src_ip_map = db.connection.src_ip_map
        ips_len = len(src_ip_map)
        if ips_len > 0:
            print(f"\n----- Top {db.size_limit} Clients -----\n")
            print(f"Number of Clients:  {ips_len}")
            for num, (outer_ip, ip_info) in enumerate(src_ip_map.items(), start=1):
                print(f"\n[{num}] Client: {outer_ip}")
                print(f"    {ip_info['count']} - Connection{'s' if ip_info['count'] > 1 else ''}")
                temp = {}
                for key, inner_ip in ip_map.items():
                    # JSON style key: e.g. "1752069692-2"
                    if isinstance(key, str) and '-' in key:
                        try:
                            key_tuple = key
                        except ValueError:
                            continue

                    # Legacy style key: (restart_count, conn_id)
                    elif isinstance(key, tuple):
                        key_tuple = key
                    else:
                        continue

                    if inner_ip == outer_ip:
                        code = db.connection.disconect_map.get(key_tuple)
                        if code:
                            temp[code] = temp.get(code, 0) + 1

                for code, count in temp.items():
                    print(f"    {count} - {code} ({DISCONNECT_MSG.get(code, 'unknown')})")

                if num >= db.size_limit:
                    break

        # Unique Bind DN's
        binds = db.bind.dns
        binds_len = len(binds)
        if binds_len > 0:
            print(f"\n----- Top {db.size_limit} Bind DN's ----\n")
            print(f"Number of Unique Bind DN's: {binds_len}\n")
            for num, bind in enumerate(sorted(binds.keys(), key=lambda k: binds[k], reverse=True)):
                if num >= db.size_limit:
                    break
                print(f"{db.bind.dns[bind]:<10}  {bind:<30}")

        # Unique search bases
        bases = db.search.bases
        num_bases = len(bases)
        if num_bases > 0:
            print(f"\n----- Top {db.size_limit} Search Bases -----\n")
            print(f"Number of Unique Search Bases: {num_bases}\n")
            for num, base in enumerate(sorted(bases.keys(), key=lambda k: bases[k], reverse=True)):
                if num >= db.size_limit:
                    break
                print(f"{db.search.bases[base]:<10}  {base}")

        # Unique search filters
        filters = sorted(db.search.filter_list, reverse=True)
        num_filters = len(filters)
        if num_filters > 0:
            print(f"\n----- Top {db.size_limit} Search Filters -----\n")
            for num, (count, existing_filter) in enumerate(filters):
                if num >= db.size_limit:
                    break
                print(f"{count:<10} {existing_filter}")

        # Longest elapsed times
        etimes = sorted(db.result.etime_duration, reverse=True)
        num_etimes = len(etimes)
        if num_etimes > 0:
            print(f"\n----- Top {db.size_limit} Longest etimes (elapsed times) -----\n")
            for num, etime in enumerate(etimes):
                if num >= db.size_limit:
                    break
                print(f"etime={etime:<12}")

        # Longest wait times
        wtimes = sorted(db.result.wtime_duration, reverse=True)
        num_wtimes = len(wtimes)
        if num_wtimes > 0:
            print(f"\n----- Top {db.size_limit} Longest wtimes (wait times) -----\n")
            for num, wtime in enumerate(wtimes):
                if num >= db.size_limit:
                    break
                print(f"wtime={wtime:<12}")

        # Longest operation times
        optimes = sorted(db.result.optime_duration, reverse=True)
        num_optimes = len(optimes)
        if num_optimes > 0:
            print(f"\n----- Top {db.size_limit} Longest optimes (actual operation times) -----\n")
            for num, optime in enumerate(optimes):
                if num >= db.size_limit:
                    break
                print(f"optime={optime:<12}")

        # Largest nentries returned
        nentries = sorted(db.result.nentries_num, reverse=True)
        num_nentries = len(nentries)
        if num_nentries > 0:
            print(f"\n----- Top {db.size_limit} Largest nentries -----\n")
            for num, nentry in enumerate(nentries):
                if num >= db.size_limit:
                    break
                print(f"nentries={nentry:<10}")
            print()

        # Extended operations
        oids = db.operation.extended
        num_oids = len(oids)
        if num_oids > 0:
            print(f"\n----- Top {db.size_limit} Extended Operations -----\n")
            for num, oid in enumerate(sorted(oids, key=lambda k: oids[k], reverse=True)):
                if num >= db.size_limit:
                    break
                print(f"{oids[oid]:<12} {oid:<30} {OID_MSG.get(oid, 'Other'):<60}")

        # Commonly requested attributes
        attrs = db.search.attrs
        num_nattrs = len(attrs)
        if num_nattrs > 0:
            print(f"\n----- Top {db.size_limit} Most Requested Attributes -----\n")
            for num, attr in enumerate(sorted(attrs, key=lambda k: attrs[k], reverse=True)):
                if num >= db.size_limit:
                    break
                print(f"{attrs[attr]:<11} {attr:<10}")
            print()

        abandoned_map = db.operation.op_map.get('abandoned', {})
        if abandoned_map:
            print(f"\n----- Abandon Request Stats -----\n")
            num = 1
            for (restart, conn), abandon_list in abandoned_map.items():
                for op, target_op, msgid in abandon_list:
                    client = db.connection.ip_map.get((restart, conn), 'Unknown')
                    print(f"{num:<6} conn={conn} op={op} msgid={msgid} target_op:{target_op} client={client}")
                    num += 1
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

        if db.connection.disconnect_code.get('T1', 0) > 0:
            print(f"\n {rec_count}. You have some connections being closed by the idletimeout setting. You may want to increase the idletimeout if it is set low.\n")
            rec_count += 1

        if db.connection.disconnect_code.get('T2', 0) > 0:
            print(f"\n {rec_count}. You have some connections being closed by the ioblocktimeout setting. You may want to increase the ioblocktimeout.\n")
            rec_count += 1

        if db.connection.disconnect_code.get('T3', 0) > 0:
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

        num_success = db.result.error_freq.get('0', 0)
        num_err = sum(v for k, v in db.result.error_freq.items() if k != '0')
        if num_err > num_success:
            print(f"\n {rec_count}. You have more unsuccessful operations than successful operations. You should investigate this difference.\n")
            rec_count += 1

        num_close_clean = db.connection.disconnect_code.get('U1', 0)
        num_close_total = num_err = sum(v for k, v in db.connection.disconnect_code.items())
        if num_close_clean < (num_close_total - num_close_clean):
            print(f"\n {rec_count}. You have more abnormal connection codes than cleanly closed connections. You may want to investigate this difference.\n")
            rec_count += 1

        if num_time_count:
            if round(avg_etime, 9) > 0:
                print(f"\n {rec_count}. Your average etime is {avg_etime:.9f}. You may want to investigate this performance problem.\n")
                rec_count += 1

            if round(avg_wtime, 9) > 0.5:
                print(f"\n {rec_count}. Your average wtime is {avg_wtime:.9f}. You may need to increase the number of worker threads (nsslapd-threadnumber).\n")
                rec_count += 1

            if round(avg_optime, 9) > 0:
                print(f"\n {rec_count}. Your average optime is {avg_optime:.9f}. You may want to investigate this performance problem.\n")
                rec_count += 1

        if num_base_search > (num_search * 0.25):
            print(f"\n {rec_count}. You have a high number of searches that query the entire search base. Although this is not necessarily bad, it could be resource-intensive if the search base contains many entries.\n")
            rec_count += 1

        if rec_count == 1:
            print("\nNone.")

    print("Done.")

if __name__ == "__main__":
    main()
    