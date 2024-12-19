# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
# 

"""
TODO
- Fix parse start/end times
"""

import os
import psutil
import magic
import gzip
import re
import argparse
import glob
import logging
import sys
import csv
from datetime import datetime
import heapq
from collections import Counter
from collections import defaultdict

# Profiling, will be removed
import cProfile


# Globals
latency_groups = {
    "<= 1": 0,
    "== 2": 0,
    "== 3": 0,
    "4-5": 0,
    "6-10": 0,
    "11-15": 0,
    "> 15": 0
}
disconect_errors = {
    '32': 'broken_pipe',
    '11': 'resource_unavail',
    '131': 'connection_reset',
    '-5961': 'connection_reset'
}
ldap_error_codes = {
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

disconnect_msg = {
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
oid_messages = {
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
scope_txt = ["0 (base)", "1 (one)", "2 (subtree)"]


class logAnalyzer:
    def __init__(self, verbose=False, sizelimit=None, rootdn=None, excludeip=None,
                 reportStatsSec=None, reportStatsMin=None):
        self.version = 8.3
        self.verbose = verbose
        self.sizelimit = sizelimit
        self.rootDN = rootdn
        self.excludeIP = excludeip
        self.prevstats = None
        self.file_size = 0
        self.stats_interval = None
        self.stats_file = None
        self.csv_writer = None
        self.timeformat = "%d/%b/%Y:%H:%M:%S.%f %z"

        if reportStatsSec:
            self.stats_interval = 1
            self.stats_file = reportStatsSec
        if reportStatsMin: 
            self.stats_interval = 60
            self.stats_file = reportStatsMin

        # Are we writing stats
        if self.stats_interval:
            self.stats_file = open(self.stats_file, mode='w', newline='')
            self.csv_writer = csv.writer(self.stats_file)

        self.notesA = {}
        self.notesF = {}
        self.notesP = {}
        self.notesU = {}
        self.meta = {}
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
            'end_time': {},
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
            'error_freq': {},
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
            'auth_protocol': {}
        }
        self.regexes = {
        'RESULT_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+|Internal\(\d+\))             # conn=int | conn=Internal(int)
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
                        ''', re.VERBOSE)),
        'SEARCH_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+|\w+\(\d+\))                  # conn=int | conn=Internal(int)
                        (?:\s+\((?P<internal>Internal)\))?                  # Optional: (Internal)
                        \sop=(?P<op_id>\d+)(?:\(\d+\)\(\d+\))?              # Optional: op=int, op=int(int)(int)
                        \sSRCH                                              # SRCH
                        \sbase="(?P<search_base>[^"]*)"                     # base="", "string"
                        \sscope=(?P<search_scope>\d+)                       # scope=int
                        \sfilter="(?P<search_filter>[^"]+)"                 # filter="string"
                        (?:\s+attrs=(?P<search_attrs>ALL|\"[^"]*\"))?       # Optional: attrs=ALL | attrs="strings"
                        (\s+options=(?P<options>\S+))?                      # Optional: options=persistent
                        (?:\sauthzid="(?P<authzid_dn>[^"]*)")?              # Optional: dn="", dn="strings"
                        ''', re.VERBOSE)),
        'BIND_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \sop=(?P<op_id>\d+)                                 # op=int
                        \sBIND                                              # BIND
                        \sdn="(?P<bind_dn>.*?)"                             # Optional: dn=string
                        (?:\smethod=(?P<bind_method>sasl|\d+))?             # Optional: method=int|sasl
                        (?:\sversion=(?P<bind_version>\d+))?                # Optional: version=int
                        (?:\smech=(?P<sasl_mech>[\w-]+))?                   # Optional: mech=string
                        (?:\sauthzid="(?P<authzid_dn>[^"]*)")?              # Currently not used
                        ''', re.VERBOSE)),
        'UNBIND_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        (?:\sop=(?P<op_id>\d+))?                            # Optional: op=int
                        \sUNBIND                                            # UNBIND
                        ''', re.VERBOSE)),
        'CONNECT_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \sfd=(?P<fd>\d+)                                    # fd=int 
                        \sslot=(?P<slot>\d+)                                # slot=int
                        \s(?P<ssl>SSL\s)?                                   # Optional: SSL
                        connection\sfrom\s                                  # connection from
                        (?P<src_ip>\S+)\sto\s                               # IP to
                        (?P<dst_ip>\S+)                                     # IP
                        ''', re.VERBOSE)),
        'DISCONNECT_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \s+op=(?P<op_id>-?\d+)                              # op=int
                        \s+fd=(?P<fd>\d+)                                   # fd=int 
                        \s*(?P<status>closed|Disconnect)                    # closed|Disconnect
                        \s(?: [^ ]+)*                                       # 
                        \s(?:\s*(?P<error_code>-?\d+))?                     # Optional: 
                        \s*(?:.*-\s*(?P<disconnect_code>[A-Z]\d))?          # Optional: [A-Z]int
                        ''', re.VERBOSE)),
        'EXTEND_OP_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \sop=(?P<op_id>\d+)                                 # op=int
                        \sEXT                                               # EXT
                        \soid="(?P<oid>[^"]+)"                              # oid="string"
                        \sname="(?P<name>[^"]+)"                            # namme="string"
                        ''', re.VERBOSE)),
        'AUTOBIND_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \s+AUTOBIND                                         # AUTOBIND
                        \sdn="(?P<bind_dn>.*?)"                             # Optional: dn="strings"
                        ''', re.VERBOSE)),
        'AUTH_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \s(?P<auth_protocol>SSL[\w\.\s-]+|TLS[\w\.\s-]+)    # SSLX.Y|TLSX.Y
                        (\s(?P<auth_message>client\sbound\sas                   
                        |                                                                               
                        failed\sto\smap\sclient\scertificate\sto\sLDAP\sDN))?   
                        (\s(?P<auth_details>.*?))?                          # Currently not used
                        (?:\s“(?P<auth_error>.*)”|$)?                       # Currently not used
                        ''', re.VERBOSE)),
        'VLV_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \sop=(?P<op_id>\d+)                                 # op=int
                        \sVLV\s                                             # VLV
                        (?P<result_code>\d+):                               # Currently not used
                        (?P<target_pos>\d+):                                # Currently not used
                        (?P<context_id>[A-Z0-9]+)                           # Currently not used
                        (?::(?P<list_size>\d+))?\s                          # Currently not used
                        (?P<first_index>\d+):                               # Currently not used
                        (?P<last_index>\d+)\s                               # Currently not used
                        \((?P<list_count>\d+)\)                             # Currently not used
                        ''', re.VERBOSE)), 
        'ABANDON_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \sop=(?P<op_id>\d+)                                 # op=int
                        \sABANDON                                           # ABANDON
                        \stargetop=(?P<targetop>[\w\s]+)                    # targetop=string
                        \smsgid=(?P<msgid>\d+)                              # msgid=int
                        ''', re.VERBOSE)),
        'SORT_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \sop=(?P<op_id>\d+)                                 # op=int
                        \sSORT                                              # SORT
                        \s+(?P<attribute>\w+)                               # Currently not used
                        (?:\s+\((?P<status>\d+)\))?                         # Currently not used
                        ''', re.VERBOSE)),
        'CRUD_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+|Internal\(\d+\))             # conn=int | conn=Internal(int)
                        \sop=(?P<op_id>\d+)(?:\(\d+\)\(\d+\))?              # Optional: op=int, op=int(int)(int)
                        \s(?P<op_type>ADD|CMP|MOD|DEL|MODRDN)               # ADD|CMP|MOD|DEL|MODRDN
                        \sdn="(?P<dn>[^"]*)"                                # dn="", dn="strings"
                        (?:\sauthzid="(?P<authzid_dn>[^"]*)")?              # Optional: dn="", dn="strings"
                        ''', re.VERBOSE)),

        'ENTRY_REFERRAL_REGEX': (re.compile(r'''
                        \[(?P<timestamp>(?P<day>\d{2})\/(?P<month>[A-Za-z]{3})\/(?P<year>\d{4}):(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\.(?P<nanosecond>\d{9})\s(?P<timezone>[+-]\d{4}))\]
                        \sconn=(?P<conn_id>\d+)                             # conn=int 
                        \sop=(?P<op_id>\d+)                                 # op=int
                        \s(?P<op_type>ENTRY|REFERRAL)                       # ENTRY|REFERRAL
                        (?:\sdn="(?P<dn>[^"]*)")?                           # Optional: dn="", dn="string"
                        ''', re.VERBOSE))
        }

    def match_line(self, line, bytes_read):
        for key, pattern in self.regexes.items():
            match = pattern.match(line)
            if match:
                # General match stuff

                #print(f"match_line - timestamp:{match.group('timestamp')} - Required group missing")
                
                # datetime library doesnt support nano seconds so we need to "normalise" the timestamp
                timestamp = match.group('timestamp')
                norm_timestamp = timestamp[:26] + timestamp[29:]

                # Are there time range restrictions
                if self.server['parse_start_time'] is not None:
                    # Compare datetime objects
                    parse_start = self.server.get('parse_start_time', 0.0)
                    parse_stop = self.server.get('parse_stop_time', 0.0)
                    if not parse_start <= norm_timestamp <= parse_stop:
                        return
                   
                # Get the first and last timestamps
                self.server['first_time'] = (
                    self.server['first_time'] or norm_timestamp
                )
                self.server['last_time'] = norm_timestamp

                # Bump lines parsed
                self.server['lines_parsed'] = self.server.get('lines_parsed', 0) + 1

                # RESULT match. Capture times, notes if present and manage a client bind response
                if key == 'RESULT_REGEX':
                    #print(f"match_line - key {key} match groups:{match.groups()}")

                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'op_id', 'err', 'tag', 'etime', 'wtime', 'optime', 
                                       'timestamp', 'nentries']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        op_id = match.group('op_id')
                        err = match.group('err')
                        tag = match.group('tag')
                        etime = match.group('etime')
                        wtime = match.group('wtime')
                        optime = match.group('optime')
                        timestamp = match.group('timestamp')
                        nentries = match.group('nentries')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()

                    # Mapping keys for this entry
                    restart_conn_op_key = (self.server.get('restart_ctr', 0), conn_id, op_id)
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)
                    conn_op_key = (conn_id, op_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    # Bump global result count
                    self.result['result_ctr'] = self.result.get('result_ctr', 0) + 1
                    
                    # Track result times
                    self.result['timestamp_ctr'] = self.result.get('timestamp_ctr', 0) + 1

                    # Longest etime, push current etime onto the heap
                    heapq.heappush(self.result['etime_duration'], float(etime))

                    # If the heap exceeds sizelimit, pop the smallest element from root
                    if len(self.result['etime_duration']) > self.sizelimit:
                        heapq.heappop(self.result['etime_duration'])

                    # Longest wtime, push current wtime onto the heap
                    heapq.heappush(self.result['wtime_duration'], float(wtime))

                    # If the heap exceeds sizelimit, pop the smallest element from root
                    if len(self.result['wtime_duration']) > self.sizelimit:
                        heapq.heappop(self.result['wtime_duration'])

                    # Longest optime, push current optime onto the heap
                    heapq.heappush(self.result['optime_duration'], float(optime))

                    # If the heap exceeds sizelimit, pop the smallest element from root
                    if len(self.result['optime_duration']) > self.sizelimit:
                        heapq.heappop(self.result['optime_duration'])

                    # Manage total times
                    self.result['total_etime'] = self.result.get('total_etime', 0) + float(etime)
                    self.result['total_wtime'] = self.result.get('total_wtime', 0) + float(wtime)
                    self.result['total_optime'] = self.result.get('total_optime', 0) + float(optime)

                    # Statistic reporting
                    self.result['etime_stat'] = round(self.result['etime_stat'] + float(etime), 8)

                    if self.verbose:
                        # Capture error code
                        self.result['error_freq'][err] = self.result['error_freq'].get(err, 0) + 1

                    # Process result notes if present
                    notes = match.group('notes')
                    if notes is not None:
                        # match.group('notes') can be A|U|F
                        self.result[f'notes{notes}_ctr'] = self.result.get(f'notes{notes}_ctr', 0) + 1
                        if self.verbose:
                            # Track result times using server restart count, conn id and op_id as key
                            self.result[f'notes{notes}_map'][restart_conn_op_key] = restart_conn_op_key

                            # Construct the notes dict
                            note_dict = getattr(self, f'notes{notes}')
                            
                            # Exclude VLV 
                            if restart_conn_op_key not in self.vlv['vlv_map_rco']:
                                # Remove microseconds from timestamp
                                tidy_time = timestamp.split('.')[0]
                                if restart_conn_op_key in note_dict:
                                    note_dict[restart_conn_op_key]['time'] = tidy_time
                                else:
                                    # First time round
                                    note_dict[restart_conn_op_key] = {'time': tidy_time}

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
                                    # We dont need this anymore
                                    del self.search['scope_map_rco'][restart_conn_op_key]

                                if restart_conn_op_key in self.search['filter_map_rco']:
                                    note_dict[restart_conn_op_key]['filter'] = self.search['filter_map_rco'][restart_conn_op_key]
                                    # We dont need this anymore
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

                    # Is this a bind response
                    if tag == '97':
                        # Invalid credentials|Entry does not exist
                        if err == '49':
                            if self.verbose:
                                bad_pwd_dn = self.bind['dn_map_rc'].get(restart_conn_key, None)
                                bad_pwd_ip = self.connection['restart_conn_ip_map'].get(restart_conn_key, None)
                                self.result['bad_pwd_map'][(bad_pwd_dn, bad_pwd_ip)] = (
                                    self.result['bad_pwd_map'].get((bad_pwd_dn, bad_pwd_ip), 0) + 1
                                )
                                # Trim items to sizelimit
                                if len(self.result['bad_pwd_map']) > self.sizelimit:
                                    within_size_limit = dict(sorted(self.result['bad_pwd_map'].items(),
                                                        key=lambda item: item[1],
                                                        reverse=True
                                                        )[:self.sizelimit])
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
                            result_dn = match.group('dn')
                            if result_dn:
                                if result_dn != "":
                                    # If this is a result of a sasl bind, grab the dn
                                    if conn_op_key in self.bind['sasl_map_co']:
                                        if result_dn == self.rootDN:
                                            self.bind['rootdn_bind_ctr'] = (
                                            self.bind.get('rootdn_bind_ctr', 0) + 1
                                        )
                                        if self.verbose:
                                            result_dn = result_dn.lower()
                                            if result_dn is not None:
                                                self.bind['dn_map_rc'][restart_conn_key] = result_dn
                                                self.bind['dn_freq'][result_dn] = (
                                                    self.bind['dn_freq'].get(result_dn, 0) + 1
                                                )
                    # Handle other tag values
                    elif tag in ['100', '101', '111', '115']:

                        # Largest nentry, push current nentry onto the heap, no duplicates
                        if int(nentries) not in self.result['nentries_set']:
                            heapq.heappush(self.result['nentries_num'], int(nentries))
                            self.result['nentries_set'].add(int(nentries))

                        # If the heap exceeds sizelimit, pop the smallest element from root
                        if len(self.result['nentries_num']) > self.sizelimit:
                            removed = heapq.heappop(self.result['nentries_num'])
                            self.result['nentries_set'].remove(removed)

                # SEARCH match - 
                elif key == 'SEARCH_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'op_id', 'search_base', 'search_scope', 'search_filter', 'search_attrs']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        op_id = match.group('op_id')
                        search_base = match.group('search_base')
                        search_scope = match.group('search_scope')
                        search_filter = match.group('search_filter').lower()
                        search_attrs = match.group('search_attrs')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()
                    
                    # Create a tracking keys for this entry
                    restart_conn_op_key = (self.server.get('restart_ctr', 0), conn_id, op_id)
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    # Bump search and global op count
                    self.search['search_ctr'] = self.search.get('search_ctr', 0) + 1
                    self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

                    # Is this an internal operation
                    if 'Internal' in match.string:
                        self.server['internal_op_ctr']  = self.server.get('internal_op_ctr', 0) + 1

                    # Search attributes
                    if search_attrs is not None:
                        if search_attrs == 'ALL':
                            self.search['attr_dict']['All Attributes'] += 1
                        else:
                            for attr in search_attrs.split():
                                attr = attr.strip('"')
                                self.search['attr_dict'][attr] += 1

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
                            self.search['scope_map_rco'][restart_conn_op_key] = scope_txt[int(search_scope)]

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
                                if len(self.search['filter_list']) < self.sizelimit:
                                    heapq.heappush(self.search['filter_list'], (1, search_filter))
                                else:
                                    heapq.heappushpop(self.search['filter_list'], (self.search['filter_dict'][search_filter], search_filter))

                    # Check for an entire base search
                    if "objectclass=*" in search_filter or "objectclass=top" in search_filter:
                        if search_scope == '2':
                            self.search['base_search_ctr'] = self.search.get('base_search_ctr', 0) + 1

                    # Persistent search
                    if match.group('options') is not None:
                        options = match.group('options')
                        if options == 'persistent':
                            self.search['persistent_ctr'] = self.search.get('persistent_ctr', 0) + 1

                    # Authorization identity
                    if match.group('authzid_dn') is not None:
                        authzid = match.group('authzid_dn')
                        self.search['authzid'] = self.search.get('authzid', 0) + 1

                # BIND match - 
                elif key == 'BIND_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'op_id', 'bind_dn', 'bind_method', 'bind_version']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        op_id = match.group('op_id')
                        bind_dn = match.group('bind_dn')
                        bind_method = match.group('bind_method')
                        bind_version = match.group('bind_version')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()
                    
                    # Create a tracking keys for this entry
                    restart_conn_op_key = (self.server.get('restart_ctr', 0), conn_id, op_id)
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)
                    conn_op_key = (conn_id, op_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    # Bump bind and global op count
                    self.bind['bind_ctr'] = self.bind.get('bind_ctr', 0) + 1
                    self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

                    # Update bind version count
                    self.bind['version'][bind_version] = self.bind['version'].get(bind_version, 0) + 1

                    # sasl or simple bind
                    if bind_method == 'sasl':
                        self.bind['sasl_bind_ctr'] = self.bind.get('sasl_bind_ctr', 0) + 1
                        sasl_mech = match.group('sasl_mech')
                        if sasl_mech is not None:
                            # Bump sasl mechanism count
                            self.bind['sasl_mech_freq'][sasl_mech] = self.bind['sasl_mech_freq'].get(sasl_mech, 0) + 1
                            
                            # Keep track of bind key to handle sasl result later
                            self.bind['sasl_map_co'][conn_op_key] = sasl_mech

                        if bind_dn == "":
                            if bind_dn == self.rootDN:
                                self.bind['rootdn_bind_ctr'] = self.bind.get('rootdn_bind_ctr', 0) + 1
                        else:
                            if self.verbose:
                                    bind_dn = bind_dn.lower()
                                    self.bind['dn_freq'][bind_dn] = self.bind['dn_freq'].get(bind_dn, 0) + 1
                                    self.bind['dn_map_rc'][restart_conn_key] = bind_dn
                    else:
                        if bind_dn == "":
                            self.bind['anon_bind_ctr'] = self.bind.get('anon_bind_ctr', 0) + 1
                            self.bind['dn_freq']['Anonymous'] = self.bind['dn_freq'].get('Anonymous', 0) + 1
                            self.bind['dn_map_rc'][restart_conn_key] = ""
                        else:
                            if bind_dn == self.rootDN:
                                self.bind['rootdn_bind_ctr'] = self.bind.get('rootdn_bind_ctr', 0) + 1
                            if self.verbose:
                                bind_dn = bind_dn.lower()
                                self.bind['dn_freq'][bind_dn] = self.bind['dn_freq'].get(bind_dn, 0) + 1
                                self.bind['dn_map_rc'][restart_conn_key] = bind_dn

                # UNBIND match - 
                elif key == 'UNBIND_REGEX':
                    # Check required groups are present and not None
                    groups = match.groupdict()
                    required_groups = ['conn_id', 'op_id']
                    if any(group not in groups or groups[group] is None for group in required_groups):
                        print(f"match_line - key {key} - Required group missing")
                        return None
                    else:
                        conn_id = groups.get('conn_id')
                        op_id = match.group('op_id')
                    
                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    # Bump unbind count and map 
                    self.bind['unbind_ctr'] = self.bind.get('unbind_ctr', 0) + 1
                 
                # CONNECTION match - 
                elif key == 'CONNECT_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['src_ip', 'conn_id', 'timestamp', 'slot', 'fd']
                    if self.validate_match_groups(key, match, required_groups):
                        src_ip = match.group('src_ip')
                        conn_id = match.group('conn_id')
                        timestamp = match.group('timestamp')
                        fd = match.group('fd')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()
                    
                    # datetime limitation
                    match_time = timestamp[:26] + timestamp[29:]

                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # To exclude this IP from all procesing, we need to track it
                    if self.excludeIP and src_ip in self.excludeIP:
                            self.connection['exclude_ip_map'][restart_conn_key] = src_ip
                            return None

                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    if self.verbose:
                        # Update open connection count
                        self.connection['open_conns'][src_ip] = self.connection['open_conns'].get(src_ip, 0) + 1

                    if self.verbose:
                        # Grab the start time for latency report
                        self.connection['start_time'][conn_id] = match_time

                    # Update general connection counters
                    for key in ['conn_ctr', 'sim_conn_ctr']:
                        self.connection[key] = self.connection.get(key, 0) + 1

                    # Update max simultaneous connection count
                    self.connection['max_sim_conn_ctr'] = max(
                        self.connection.get('max_sim_conn_ctr', 0), 
                        self.connection['sim_conn_ctr']
                    )

                    # Update protocol counters
                    src_ip_tmp = 'local' if src_ip == 'local' else 'ldap'
                    ssl = match.group('ssl') 
                    if ssl is not None:
                        stat_count_key = 'ldaps_ctr'
                    else: 
                        stat_count_key = 'ldapi_ctr' if src_ip_tmp == 'local' else 'ldap_ctr'
                    self.connection[stat_count_key] = self.connection.get(stat_count_key, 0) + 1
                
                    # Track file descriptor counters
                    self.connection['fd_max_ctr']  = (
                        max(self.connection.get('fd_max_ctr', 0), int(fd))
                    )
                    self.connection['fd_taken_ctr']  = (
                        self.connection.get('fd_taken_ctr', 0) + 1
                    )

                    # Server restart
                    if conn_id == '1':
                        self.server['restart_ctr']  = self.server.get('restart_ctr', 0) + 1
                        if self.verbose:
                            self.connection['open_conns'] = {}

                    # Track source IP
                    self.connection['restart_conn_ip_map'][restart_conn_key] = src_ip
                    self.connection['ip_map'][src_ip] = self.connection['ip_map'].get(src_ip, 0) + 1

                # UNBIND match - 
                elif key == 'AUTH_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'auth_protocol']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        auth_protocol = match.group('auth_protocol')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()
                    
                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    if auth_protocol is not None:
                        if match:
                            self.auth['cipher_ctr'] = self.auth.get('cipher_ctr', 0) + 1        
                            self.auth['auth_protocol'][auth_protocol] = self.auth['auth_protocol'].get(auth_protocol, 0) + 1        

                    auth_message = match.groups('auth_message')
                    if auth_message is not None:
                        auth_message = match.groups('auth_message')
                        if auth_message == 'client bound as':
                            self.auth['ssl_client_bind_ctr'] = self.auth.get('ssl_client_bind_ctr', 0) + 1 
                        elif auth_message == 'failed to map client certificate to LDAP DN':
                            self.auth['ssl_client_bind_failed_ctr'] = self.auth.get('ssl_client_bind_failed_ctr', 0) + 1 

                elif key == 'VLV_REGEX':
                    # Check required groups are present and not None
                    groups = match.groupdict()
                    required_groups = ['conn_id', 'op_id']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        op_id = match.group('op_id')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()
                        
                    # Create tracking keys
                    restart_conn_op_key = (self.server.get('restart_ctr', 0), conn_id, op_id)
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    # Bump some stats
                    self.vlv['vlv_ctr'] = self.vlv.get('vlv_ctr', 0) + 1
                    self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

                    # Key and value are the same, makes set operations easier later on
                    self.vlv['vlv_map_rco'][restart_conn_op_key] = restart_conn_op_key

                elif key == 'ABANDON_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'op_id', 'targetop', 'msgid']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        op_id = match.group('op_id')
                        targetop = match.group('targetop')
                        msgid = match.group('msgid')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()
                        
                    # Create a tracking keys
                    restart_conn_op_key = (self.server.get('restart_ctr', 0), conn_id, op_id)
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    # Bump some stats
                    self.result['result_ctr'] = self.result.get('result_ctr', 0) + 1
                    self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1
                    self.operation['abandon_op_ctr'] = self.operation.get('abandon_op_ctr', 0) + 1

                    # Track for abandon op processing
                    self.operation['abandoned_map_rco'][restart_conn_op_key] = (conn_id, op_id, targetop, msgid)
                    
                elif key == 'SORT_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'op_id']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        op_id = match.group('op_id')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()
                    
                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    self.operation['sort_op_ctr'] = self.operation.get('sort_op_ctr', 0) + 1

                elif key == 'EXTEND_OP_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['oid', 'conn_id', 'op_id']
                    if self.validate_match_groups(key, match, required_groups):
                        oid = match.group('oid')
                        conn_id = match.group('conn_id')
                        op_id = match.group('op_id')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()

                    # Create a tracking key for this entry
                    restart_conn_op_key = (self.server.get('restart_ctr', 0), conn_id, op_id)
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    # Bump some stats
                    self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1
                    self.operation['extnd_op_ctr'] = self.operation.get('extnd_op_ctr', 0) + 1

                    # Track for later processing
                    if oid is not None:
                        self.operation['extop_dict'][oid] = self.operation['extop_dict'].get(oid, 0) + 1
                        self.operation['extop_map_rco'][restart_conn_op_key] = self.operation['extop_map_rco'].get(restart_conn_op_key, 0) + 1

                elif key == 'AUTOBIND_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'bind_dn']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        bind_dn = match.group('bind_dn')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()
                    
                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    # Bump stats
                    self.bind['bind_ctr'] = self.bind.get('bind_ctr', 0) + 1
                    self.bind['autobind_ctr'] = self.bind.get('autobind_ctr', 0) + 1
                    self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

                    # Handle an anonymous autobind
                    if bind_dn == "":
                        self.bind['anon_bind_ctr'] = self.bind.get('anon_bind_ctr', 0) + 1
                    else:
                        if bind_dn == self.rootDN:
                            self.bind['rootdn_bind_ctr'] = self.bind.get('rootdn_bind_ctr', 0) + 1
                        bind_dn = bind_dn.lower()
                        self.bind['dn_freq'][bind_dn] = self.bind['dn_freq'].get(bind_dn, 0) + 1

                elif key == 'DISCONNECT_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['timestamp', 'conn_id']
                    if self.validate_match_groups(key, match, required_groups):
                        timestamp = match.group('timestamp')
                        conn_id = match.group('conn_id')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()

                    # Prepare for datetime comparison in get_elapsed_times
                    match_time = timestamp[:26] + timestamp[29:]

                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)
                    
                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    if self.verbose:
                        # Get the IP for this entry if we have it
                        if restart_conn_key in self.connection['restart_conn_ip_map']:
                            src_ip = self.connection['restart_conn_ip_map'][restart_conn_key]
                            # Is the IP associated with an open connection
                            if src_ip in self.connection['open_conns']:
                                if self.connection['open_conns'][src_ip] > 1:
                                    self.connection['open_conns'][src_ip] = self.connection['open_conns'].get(src_ip, 0) - 1
                                else:
                                    del self.connection['open_conns'][src_ip]

                    # Grab the disconnect time for latency report, only if 
                    # a start time for this conn_id has been captured
                    if self.verbose:
                        start = self.connection['start_time'].get(conn_id, None)
                        end = match_time
                        if start and end:
                            latency = self.get_elapsed_time(start, end, "seconds")
                            bucket = self.group_latencies(latency)
                            latency_groups[bucket] += 1
                            self.connection['start_time'][conn_id] = None
                            self.connection['end_time'][conn_id] = None

                    # Update some stat counters
                    self.connection['sim_conn_ctr'] = self.connection.get('sim_conn_ctr', 0) - 1
                    self.connection['fd_returned_ctr']  = (
                        self.connection.get('fd_returned_ctr', 0) + 1
                    )

                    # Track error (1.4.3) and disconnect codes
                    error_code = match.group('error_code')
                    if error_code is not None:
                        disconnect_code = match.group('disconnect_code')
                        if disconnect_code is not None:
                            self.connection[disconect_errors.get(error_code, 'unknown')][disconnect_code] = (
                                self.connection[disconect_errors.get(error_code, 'unknown')].get(disconnect_code, 0) + 1
                            )
                    disconnect_code = match.group('disconnect_code')
                    if disconnect_code is not None:
                        self.connection['disconnect_code'][disconnect_code] = (
                            self.connection['disconnect_code'].get(disconnect_code, 0) + 1
                        )
                        self.connection['disconnect_code_map'][restart_conn_key] = disconnect_code
                # The regex match supports ADD, CMP, MOD, DEL, MODRDN operations
                elif key == 'CRUD_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'op_type']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        op_type = match.group('op_type')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()

                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    self.operation['all_op_ctr'] = self.operation.get('all_op_ctr', 0) + 1

                    # Bump internal op count
                    if 'Internal' in match.string:
                        self.server['internal_op_ctr']  = self.server.get('internal_op_ctr', 0) + 1

                    # Use operation type as key for stats
                    if op_type is not None:
                        op_key = op_type.lower()
                        self.operation[f"{op_key}_op_ctr"] = self.operation.get(f"{op_key}_op_ctr", 0) + 1
                        self.operation[f"{op_key}_map_rco"][restart_conn_key] = (
                            self.operation[f"{op_key}_map_rco"].get(restart_conn_key, 0) + 1
                        )
                    
                    # Authorization identity
                    if match.group('authzid_dn') is not None:
                        authzid = match.group('authzid_dn')
                        self.operation['authzid'] = self.operation.get('authzid', 0) + 1

                elif key == 'ENTRY_REFERRAL_REGEX':
                    # Check required groups are present and not None
                    required_groups = ['conn_id', 'op_type']
                    if self.validate_match_groups(key, match, required_groups):
                        conn_id = match.group('conn_id')
                        op_type = match.group('op_type')
                    else:
                        print(f"match_line - key {key} - Required group missing")
                        sys.exit()

                    # Create a tracking key for this entry
                    restart_conn_key = (self.server.get('restart_ctr', 0), conn_id)

                    # Should we ignore this operation
                    if restart_conn_key in self.connection['exclude_ip_map']:
                        return None
                    
                    if op_type is not None:
                        if op_type == 'ENTRY':
                            self.result['entry_count'] = self.result.get('entry_count', 0) + 1
                        elif op_type == 'REFERRAL':
                            self.result['referral_count'] = self.result.get('referral_count', 0) + 1

                # Performance stats
                if self.csv_writer is not None:                    
                    # Stats to be reported on
                    stats = {
                        'result_ctr': self.result,
                        'search_ctr': self.search,
                        'add_op_ctr': self.operation,
                        'mod_op_ctr': self.operation,
                        'modrdn_op_ctr': self.operation,
                        'cmp_op_ctr': self.operation,
                        'del_ctr': self.operation,
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

                    # Create a stat block for the curent match
                    curr_stat_block =[]
                    # Create a stat block for output
                    out_stat_block =[]
                    # We need a timestamp for this match to compare later
                    curr_stat_block.insert(0, norm_timestamp)
                    # Gather the stat values and add them to the current stat block 
                    curr_stat_block.extend([refdict[key] for key, refdict in stats.items()])
                    
                    curr_time = datetime.strptime(curr_stat_block[0], self.timeformat)
                    if self.prevstats is not None:
                        prev_stat_block = self.prevstats
                        prev_time = datetime.strptime(prev_stat_block[0], self.timeformat)

                        out_stat_block.append(prev_stat_block[0])
                        out_stat_block.append(int(prev_time.timestamp()))

                        diff = curr_time - prev_time
                        if diff.total_seconds() >= self.stats_interval:
                            # Compare stat int values
                            diff_temp = [
                                c - p if isinstance(p, int) else c
                                for c, p in zip(curr_stat_block[1:], prev_stat_block[1:])
                            ]
                            self.prevstats = curr_stat_block
                            out_stat_block.extend(diff_temp)
                            self.csv_writer.writerow(out_stat_block)
                            # Updated every result match so reset it after we write a block
                            self.result['etime_stat'] = 0.0
                            
                    # This is the first run, add the csv header for each column
                    else:
                        stats_header = [
                        'Time', 'time_t', 'Results', 'Search', 'Add', 'Mod', 'Modrdn', 'Compare',
                        'Delete', 'Abandon', 'Connections', 'SSL Conns', 'Bind', 'Anon Bind', 'Unbind', 
                        'Unindexed search', 'Unindexed component', 'Invalid filter', 'ElapsedTime']
                        self.csv_writer.writerow([stat for stat in stats_header])
                        self.prevstats = curr_stat_block

                    # Shorter than interval and end of file
                    if self.prevstats is not None:
                        if bytes_read >= self.file_size:
                            prev_stat_block = self.prevstats
                            diff_temp = [
                                c - p if isinstance(p, int) else c
                                for c, p in zip(curr_stat_block[1:], prev_stat_block[1:])
                            ]
                            out_stat_block.extend(diff_temp)
                            self.csv_writer.writerow(out_stat_block)
                            self.result['etime_stat'] = 0.0


    # Utility methods

    # This is used for debug, will be removed post code review
    def get_dict_size(self, d):
        size = sys.getsizeof(d)  # Size of the dictionary itself
        for key, value in d.items():
            size += sys.getsizeof(key)  # Size of each key
            size += sys.getsizeof(value)  # Size of each value
        return size

    # Check list of requried match groups are present and not None
    def validate_match_groups(self, id, match, required_groups):
        groups = match.groupdict()
        if any(group not in groups or groups[group] is None for group in required_groups):
            print(f"ID:{id} - Required match group missing")
            return False
        else:
            return True
    
    # Group connection latencies into 6 time ranges
    def group_latencies(self, latency_seconds):
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
    
    # Check log file mime type
    def is_file_compressed(self, filepath):
        logging.debug("is_file_compressed - filepath:{}".format(filepath))
        mime = magic.Magic(mime=True)
        if os.path.exists(filepath):
            filetype = mime.from_file(filepath)
            compressed_mime_types = [
                'application/gzip',             # gz, tar.gz, tgz
                'application/x-gzip',           # gz, tgz
            ]
            if filetype in compressed_mime_types:
                return filetype
            else:
                return False
        else:
            return None
        
    # Validate start and stop times if defined
    def set_parse_times(self, start_time, stop_time):

        norm_start_time = (start_time[:26] + start_time[29:]).strip("[]")
        norm_stop_time = (stop_time[:26] + stop_time[29:]).strip("[]")
        try:
            start = datetime.strptime(norm_start_time, self.timeformat)
            end = datetime.strptime(norm_stop_time, self.timeformat)
        except ValueError as e:
            print("Invalid time format. Expected format: DD/Mon/YYYY:HH:MM:SS TZ. Exiting !")
            sys.exit()

        if end < start:
            print(f"End time is before start time. Exiting !")
            sys.exit()

        # Store the parse times as formatted strings
        self.server['parse_start_time'] = start.strftime(self.timeformat)
        self.server['parse_stop_time'] = end.strftime(self.timeformat)

    # Main log file processing loop
    def process_file(self, filepath):
        file_size = 0
        curr_position = 0
        block_count = 0
        lines_read = 0

        try:
            comptype = self.is_file_compressed(filepath)
            if (comptype):
                if comptype == 'application/gzip':
                    logging.debug("open_and - gzip")
                    filehandle = gzip.open(filepath, 'rb')
                    
            else:
                filehandle = open(filepath, 'rb')
        except Exception as e:
                print(f"Error opening:{filepath}: {e}")

        # Seek to the end to get file size 
        filehandle.seek(0, os.SEEK_END)
        file_size = filehandle.tell()
        self.file_size = file_size
        print(f"{filehandle.name} size (bytes): {file_size}")

        # Back to the start
        filehandle.seek(0) 
        for line in filehandle:
            line_content = line.decode('utf-8').strip()
            if line_content.startswith('['):
                self.match_line(line_content, filehandle.tell())
                block_count += 1
                lines_read += 1

            if block_count >= 25000:
                curr_position = filehandle.tell()
                percent = curr_position/file_size * 100.0
                print(f"{lines_read:10d} Lines Processed     {curr_position:12d} of {file_size:12d} bytes ({percent:.3f}%)")
                block_count = 0

        if filehandle and not filehandle.closed:
            filehandle.close            
        

    # Calcluate elapsed time from start and finish timestamps, output format in seconds or HMS
    def get_elapsed_time(self, start, finish, time_format="seconds"):
        format = "%d/%b/%Y:%H:%M:%S.%f %z"

        if start == None or finish == None:
            if time_format == "seconds":
                return (0)
            elif time_format == "hms":
                return (0, 0, 0)
            
        time1 = datetime.strptime(finish, format)
        time2 = datetime.strptime(start, format)
        elapsed_time = time1 - time2

        if time_format == "seconds":
            return elapsed_time.total_seconds()
        elif time_format == "hms":
            secs = elapsed_time.total_seconds()
            hours, remainder = divmod(secs, 3600)
            mins, secs = divmod(remainder, 60)
            return (hours, mins, secs)
        else:
            raise ValueError("Invalid format. Use 'seconds' or 'hms'.")
        
    # Calculate performance based on results and operations
    def get_overall_perf(self, num_results, num_ops):
        if num_ops == 0:
            perf = 0.0
        else:
            perf = min((num_results / num_ops) * 100, 100.0)
        return f"{perf:.1f}"
    
def main():
    parser = argparse.ArgumentParser(description="Analyze one or more server access logs")
    parser.add_argument('logs', type=str, nargs='+', help='Single or multiple (*) access logs')
    parser.add_argument('-d', '--rootDN', type=str, default="cn=directory manager", help='Directory Managers DN, default is "cn=directory manager"')
    parser.add_argument('-X', '--excludeIP', action='append', help='IP address to exclude from connection stats')
    parser.add_argument('-s', '--sizeLimit', type=int, default=10, help='Number of results to return per catagory, default is 10')
    parser.add_argument('-S', '--startTime', action='store', help='Time to begin analyzing logfile from')
    parser.add_argument('-E', '--endTime', action='store', help='Time to stop analyzing logfile')
    parser.add_argument("-m", '--reportFileSecs', metavar="STATS FILENAME", type=str, help="Enable writing perf stats to specified CSV file at specified intervals")
    parser.add_argument("-M", '--reportFileMins', metavar="STATS FILENAME", type=str, help="Enable writing perf stats to specified CSV file at specified intervals")
    parser.add_argument('-v', '--verbose', action='store_true', help='Enable verbose mode')
    

    args = parser.parse_args()

    db = logAnalyzer(verbose=args.verbose, sizelimit=args.sizeLimit, 
                     rootdn=args.rootDN, excludeip=args.excludeIP, 
                     reportStatsSec=args.reportFileSecs, reportStatsMin=args.reportFileMins)

    if args.startTime and args.endTime:
        db.set_parse_times(args.startTime, args.endTime)
    
    print(f"Access Log Analyzer {db.version}")
    print(f"Command: {' '.join(sys.argv)}")

    # Sanitise list of log files, and sort by creation time
    logs = [file for file in args.logs if not re.search(r'access\.rotationinfo', file)]
    logs.sort(key=lambda x: os.path.getctime(x))
    # We shoud never reach here, if we do put access and the end of the log file list
    if 'access'  in logs:
        logs.append(logs.pop(logs.index('access')))

    print(f"Processing {len(logs)} Access Log(s)")
    print()

    # Main file handling
    for log in logs:
        accesslogs = glob.glob(log)
        logging.debug("main - acceslogs:{}".format(accesslogs))
        if not accesslogs:
            print(f"No access log found !: {args.logs}")
        else:
            for accesslog in accesslogs:
                if os.path.isfile(accesslog):
                    db.process_file(accesslog)
                else:
                    print(f"Invalid file: {accesslog}")


    # Prep for display
    hrs, mins, secs = db.get_elapsed_time(db.server['first_time'], db.server['last_time'], "hms")
    elapsed_secs = db.get_elapsed_time(db.server['first_time'], db.server['last_time'], "seconds")
    num_ops = db.operation.get('all_op_ctr', 0)
    num_results = db.result.get('result_ctr', 0)
    num_conns = db.connection.get('conn_ctr', 0)
    num_ldap = db.connection.get('ldap_ctr', 0)
    num_ldapi = db.connection.get('ldapi_ctr', 0)
    num_ldaps = db.connection.get('ldaps_ctr', 0)
    stlsoid = '1.3.6.1.4.1.1466.20037'
    num_startls = db.operation['extop_dict'].get(stlsoid, 0)
    num_search = db.search.get('search_ctr', 0)
    num_mod = db.operation.get('mod_op_ctr', 0)
    num_add = db.operation.get('add_op_ctr', 0)
    num_del = db.operation.get('del_ctr', 0)
    num_modrdn = db.operation.get('modrdn_op_ctr', 0)
    num_cmp = db.operation.get('cmp_op_ctr', 0)
    num_bind = db.bind.get('bind_ctr', 0)
    num_proxyd_auths = db.operation.get('authzid', 0) + db.search.get('authzid', 0)
    num_time_count = db.result.get('timestamp_ctr')

    print()
    print(f"Total Log Lines Analysed:{db.server['lines_parsed']}")
    print()
    print("\n----------- Access Log Output ------------\n")
    print(f"Start of Logs:                  {db.server['first_time']}")
    print(f"End of Logs:                    {db.server['last_time']}")
    print()
    print(f"Processed Log Time:             {hrs:.0f} Hours, {mins:.0f} Minutes, {secs:.0f} Seconds")
    print()
    if db.stats_interval is not None:
        sys.exit()
    print(f"Restarts:                       {db.server.get('restart_ctr', 0)}")    
    if db.auth.get('cipher_ctr', 0) > 0:
        print(f"Secure Protocol Versions:")
        protocols = db.auth['auth_protocol']
        for protocol in sorted(protocols.keys(), key=lambda k: protocols[k], reverse=True):
            print(f"   - {protocol:<4}: {protocols[protocol]:>6} connections")
    print()
    print(f"Peak Concurrent connections:    {db.connection.get('max_sim_conn_ctr', 0)}")
    print(f"Total Operations:               {num_ops}")
    print(f"Total Results:                  {num_results}")
    print(f"Overall Performance:            {db.get_overall_perf(num_results, num_ops)}%")
    print()
    print(f"Total connections:              {num_conns:<10}{num_conns/elapsed_secs:>10.2f}/sec {(num_conns/elapsed_secs) * 60:>10.2f}/min")
    print(f"- LDAP connections:             {num_ldap:<10}{num_ldap/elapsed_secs:>10.2f}/sec {(num_ldap/elapsed_secs) * 60:>10.2f}/min")
    print(f"- LDAPI connections:            {num_ldapi:<10}{num_ldapi/elapsed_secs:>10.2f}/sec {(num_ldapi/elapsed_secs) * 60:>10.2f}/min")
    print(f"- LDAPS connections:            {num_ldaps:<10}{num_ldaps/elapsed_secs:>10.2f}/sec {(num_ldaps/elapsed_secs) * 60:>10.2f}/min")
    print(f"- StartTLS Extended Ops         {num_startls:<10}{num_startls/elapsed_secs:>10.2f}/sec {(num_startls/elapsed_secs) * 60:>10.2f}/min")
    print()
    print(f"Searches:                       {num_search:<10}{num_search/elapsed_secs:>10.2f}/sec {(num_search/elapsed_secs) * 60:>10.2f}/min")
    print(f"Modifications:                  {num_mod:<10}{num_mod/elapsed_secs:>10.2f}/sec {(num_mod/elapsed_secs) * 60:>10.2f}/min")
    print(f"Adds:                           {num_add:<10}{num_add/elapsed_secs:>10.2f}/sec {(num_add/elapsed_secs) * 60:>10.2f}/min")
    print(f"Deletes:                        {num_del:<10}{num_del/elapsed_secs:>10.2f}/sec {(num_del/elapsed_secs) * 60:>10.2f}/min")
    print(f"Mod RDNs:                       {num_modrdn:<10}{num_modrdn/elapsed_secs:>10.2f}/sec {(num_modrdn/elapsed_secs) * 60:>10.2f}/min")
    print(f"Compares:                       {num_cmp:<10}{num_cmp/elapsed_secs:>10.2f}/sec {(num_cmp/elapsed_secs) * 60:>10.2f}/min")
    print(f"Binds:                          {num_bind:<10}{num_bind/elapsed_secs:>10.2f}/sec {(num_bind/elapsed_secs) * 60:>10.2f}/min")
    print()
    print(f"Average wtime (wait time):      {round(db.result.get('total_wtime', 0)/num_time_count, 9)}")
    print(f"Average optime (op time):       {round(db.result.get('total_optime', 0)/num_time_count, 9)}")
    print(f"Average etime (elapsed time):   {round(db.result.get('total_etime', 0)/num_time_count, 9)}")
    print()
    print(f"Multi-factor Authentications:   {db.result.get('notesM_ctr', 0)}")
    print(f"Proxied Auth Operations:        {num_proxyd_auths}")
    print(f"Persistent Searches:            {db.search.get('persistent_ctr', 0)}")
    print(f"Internal Operations:            {db.server.get('internal_op_ctr', 0)}")
    print(f"Entry Operations:               {db.result.get('entry_count', 0)}")
    print(f"Extended Operations:            {db.operation.get('extnd_op_ctr', 0)}") 
    print(f"Abandoned Requests:             {db.operation.get('abandon_op_ctr', 0)}")
    print(f"Smart Referrals Received:       {db.result.get('referral_count', 0)}") 
    print()
    print(f"VLV Operations:                 {db.vlv.get('vlv_ctr', 0)}")
    print(f"VLV Unindexed Searches:         {len([key for key, value in db.vlv['vlv_map_rco'].items() if value == 'A'])}")
    print(f"VLV Unindexed Components:       {len([key for key, value in db.vlv['vlv_map_rco'].items() if value == 'U'])}")
    print(f"SORT Operations:                {db.operation.get('sort_op_ctr', 0)}")
    print()
    print(f"Entire Search Base Queries:     {db.search.get('base_search_ctr', 0)}")
    print(f"Paged Searches:                 {db.result.get('notesP_ctr', 0)}")
    num_unindexed_search = len(db.notesA.keys())
    print(f"Unindexed Searches:             {num_unindexed_search}")
    if db.verbose:
        if num_unindexed_search > 0:
            for num, key in enumerate(db.notesU, start = 1):
                src, conn, op = key
                restart_conn_op_key = (src, conn, op)
                print(f"Unindexed Search #{num} (notes=A)")
                print(f"  -  Date/Time:             {db.notesA[restart_conn_op_key]['time']}")
                print(f"  -  Connection Number:     {conn}")
                print(f"  -  Operation Number:      {op}")
                print(f"  -  Etime:                 {db.notesA[restart_conn_op_key]['etime']}")
                print(f"  -  Nentries:              {db.notesA[restart_conn_op_key]['nentries']}")
                print(f"  -  IP Address:            {db.notesA[restart_conn_op_key]['ip']}")
                print(f"  -  Search Base:           {db.notesA[restart_conn_op_key]['base']}")
                print(f"  -  Search Scope:          {db.notesA[restart_conn_op_key]['scope']}")
                print(f"  -  Search Filter:         {db.notesA[restart_conn_op_key]['filter']}")
                print(f"  -  Bind DN:               {db.notesA[restart_conn_op_key]['bind_dn']}")
                print()
    num_unindexed_component = len(db.notesU.keys())
    print(f"Unindexed Components:           {num_unindexed_component}")

    if db.verbose:
        if num_unindexed_component > 0:
            for num, key in enumerate(db.notesU, start = 1):
                src, conn, op = key
                restart_conn_op_key = (src, conn, op)
                print()
                print(f"Unindexed Component #{num} (notes=U)")
                print(f"  -  Date/Time:             {db.notesU[restart_conn_op_key]['time']}")
                print(f"  -  Connection Number:     {conn}")
                print(f"  -  Operation Number:      {op}")
                print(f"  -  Etime:                 {db.notesU[restart_conn_op_key]['etime']}")
                print(f"  -  Nentries:              {db.notesU[restart_conn_op_key]['nentries']}")
                print(f"  -  IP Address:            {db.notesU[restart_conn_op_key]['ip']}")
                print(f"  -  Search Base:           {db.notesU[restart_conn_op_key]['base']}")
                print(f"  -  Search Scope:          {db.notesU[restart_conn_op_key]['scope']}")
                print(f"  -  Search Filter:         {db.notesU[restart_conn_op_key]['filter']}")
                print(f"  -  Bind DN:               {db.notesU[restart_conn_op_key]['bind_dn']}")
                print()
    num_invalid_filter = len(db.notesF.keys())
    print(f"Invalid Attribute Filters:      {num_invalid_filter}")
    if db.verbose:
        if num_invalid_filter > 0:
            for num, key in enumerate(db.notesF, start = 1):
                src, conn, op = key
                restart_conn_op_key = (src, conn, op)
                print()
                print(f"Invalid Attribute Filter #{num} (notes=F)")
                print(f"  -  Date/Time:             {db.notesF[restart_conn_op_key]['time']}")
                print(f"  -  Connection Number:     {conn}")
                print(f"  -  Operation Number:      {op}")
                print(f"  -  Etime:                 {db.notesF[restart_conn_op_key]['etime']}")
                print(f"  -  Nentries:              {db.notesF[restart_conn_op_key]['nentries']}")
                print(f"  -  IP Address:            {db.notesF[restart_conn_op_key]['ip']}")
                print(f"  -  Search Filter:         {db.notesF[restart_conn_op_key]['filter']}")
                print(f"  -  Bind DN:               {db.notesF[restart_conn_op_key]['bind_dn']}")
                print()
    print(f"FDs Taken:                      {db.connection.get('fd_taken_ctr', 0)}")
    print(f"FDs Returned:                   {db.connection.get('fd_returned_ctr', 0)}")
    print(f"Highest FD Taken:               {db.connection.get('fd_max_ctr', 0)}")
    print()
    num_broken_pipe = len(db.connection['broken_pipe'])
    print(f"Broken Pipe:                    {num_broken_pipe}")
    if num_broken_pipe > 0:
        for code, count in db.connection['broken_pipe'].items():
            print(f"    - {count} ({code}) {disconnect_msg.get(code, 'unknown')}")
        print()

    num_reset_peer = len(db.connection['connection_reset'])
    print(f"Connection Reset By Peer:       {num_reset_peer}")
    if num_reset_peer > 0:
        for code, count in db.connection['connection_reset'].items():
            print(f"    - {count} ({code}) {disconnect_msg.get(code, 'unknown')}")
        print()

    num_resource_unavail = len(db.connection['resource_unavail'])
    print(f"Resource Unavailable:           {num_resource_unavail}")
    if num_resource_unavail > 0:
        for code, count in db.connection['resource_unavail'].items():
            print(f"    - {count} ({code}) {disconnect_msg.get(code, 'unknown')}")
        print()

    print(f"Max BER Size Exceeded:          {db.connection['disconnect_code'].get('B2', 0)}")
    print()
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
    print(f"- Directory Manager Binds:      {db.bind.get('rootdn_bind_ctr', 0)}")
    print(f"- Anonymous Binds:              {db.bind.get('anon_bind_ctr', 0)}")
    print()
    if db.verbose:
        # Connection Latency
        print()
        print(f" ----- Connection Latency Details -----")
        print()
        print(f" (in seconds){' ' * 10}{'<=1':^7}{'2':^7}{'3':^7}{'4-5':^7}{'6-10':^7}{'11-15':^7}{'>15':^7}")
        print('-' * 72)
        print(f"{' (# of connections)    ':<17}"
            f"{latency_groups['<= 1']:^7}"
            f"{latency_groups['== 2']:^7}"
            f"{latency_groups['== 3']:^7}"
            f"{latency_groups['4-5']:^7}"
            f"{latency_groups['6-10']:^7}"
            f"{latency_groups['11-15']:^7}"
            f"{latency_groups['> 15']:^7}")
        print()

        # Open Connections
        open_conns = db.connection['open_conns']
        if len(open_conns) > 0:
            print(f" ----- Current Open Connection IDs -----\n")
            for conn in sorted(open_conns.keys(), key=lambda k: open_conns[k], reverse=True):
                print(f"{conn:<16} {open_conns[conn]:>10}")    
        print()

        # Error Codes
        print(f" ----- Errors -----\n")
        error_freq = db.result['error_freq']
        for err in sorted(error_freq.keys(), key=lambda k: error_freq[k], reverse=True):
            print(f"err={err:<2} {error_freq[err]:>10}  {ldap_error_codes[err]:<30}")
        
        # Failed Logins
        bad_pwd_map = db.result['bad_pwd_map']
        if len(bad_pwd_map) > 0:
            print()
            print(f"----- Top {db.sizelimit} Failed Logins ------\n")
            for num, (dn, ip) in enumerate(bad_pwd_map):
                if num > db.sizelimit:
                    break
                count = bad_pwd_map.get((dn, ip))
                print(f"{count:<10} {dn}")
            print()
            print(f"From the IP address(s) :")
            print()
            for num, (dn, ip) in enumerate(bad_pwd_map):
                if num > db.sizelimit:
                    break
                count = bad_pwd_map.get((dn, ip))
                print(f"{count:<10} {ip}")
        
        # Connection Codes
        disconnect_codes = db.connection['disconnect_code']
        if len(disconnect_codes) > 0:
            print()
            print(f"----- Total Connection Codes ----")
            print()
            for code in disconnect_codes:
                print(f"{code:<2} {disconnect_codes[code]:>10}  {disconnect_msg.get(code, 'unknown'):<30}")
        
        # Unique IPs
        restart_conn_ip_map = db.connection['restart_conn_ip_map']
        ip_map = db.connection['ip_map']
        if len(ip_map) > 0:
            print()
            print(f"----- Top {db.sizelimit} Clients -----")
            print()
            print(f"Number of Clients:  {len(ip_map)}")
            print()
            for num, (outer_ip, count) in enumerate(ip_map.items(), start = 1):
                temp = {}
                print(f"[{num}] Client: {outer_ip}")
                print(f"    {count} - Connections")
                for id, inner_ip in restart_conn_ip_map.items():
                    (src, conn) = id
                    if outer_ip == inner_ip:
                        code = db.connection['disconnect_code_map'].get((src, conn), 0)
                        if code:
                                temp[code] = temp.get(code, 0) + 1
                for code, count in temp.items():
                    print(f"    {count} - {code} ({disconnect_msg.get(code, 'unknown')})")
                print()
                if num > db.sizelimit - 1:
                    break

        # Unique Bind DN's
        binds = db.bind.get('dn_freq', 0)
        binds_len = len(binds)
        if binds_len > 0:
            print(f"----- Top {db.sizelimit} Bind DN's ----")
            print()
            print(f"Number of Unique Bind DN's: {binds_len}")
            print()
            for num, bind in enumerate(sorted(binds.keys(), key=lambda k: binds[k], reverse=True)):
                if num >= db.sizelimit:
                    break
                print(f"{db.bind['dn_freq'][bind]:<10}  {bind:<30}")
        
        # Unique search bases
        bases = db.search['base_map']
        num_bases = len(bases)
        if num_bases > 0:
            print()
            print(f"----- Top {db.sizelimit} Search Bases ----")
            print()
            print(f"Number of Unique Search Bases: {num_bases}")
            print()
            for num, base in enumerate(sorted(bases.keys(), key=lambda k: bases[k], reverse=True)):
                if num >= db.sizelimit:
                    break
                print(f"{db.search['base_map'][base]:<10}  {base}")

        # Unique search filters
        filters = sorted(db.search['filter_list'], reverse=True)
        num_filters = len(filters)
        if num_filters > 0:
            print()
            print(f"----- Top {db.sizelimit} Search Filters ----")
            print()
            for num, (count, filter) in enumerate(filters):
                if num >= db.sizelimit:
                    break
                print(f"{count:<10} {filter}")

        # Longest elapsed times
        etimes = sorted(db.result['etime_duration'], reverse=True)
        num_etimes = len(etimes)
        if num_etimes > 0:
            print()
            print(f"----- Top {db.sizelimit} Longest etimes (elapsed times) ----")
            print()
            for num, etime in enumerate(etimes):
                if num >= db.sizelimit:
                    break
                print(f"etime={etime:<12}")

        # Longest wait times
        wtimes = sorted(db.result['wtime_duration'], reverse=True)
        num_wtimes = len(wtimes)
        if num_wtimes > 0:
            print()
            print(f"----- Top {db.sizelimit} Longest wtimes (wait times) ----")
            print()
            for num, wtime in enumerate(wtimes):
                if num >= db.sizelimit:
                    break
                print(f"wtime={wtime:<12}")

        # Longest operation times
        optimes = sorted(db.result['optime_duration'], reverse=True)
        num_optimes = len(optimes)
        if num_optimes > 0:
            print()
            print(f"----- Top {db.sizelimit} Longest optimes (actual operation times) ----")
            print()
            for num, optime in enumerate(optimes):
                if num >= db.sizelimit:
                    break
                print(f"optime={optime:<12}")

        # Largest nentries returned
        nentries = sorted(db.result['nentries_num'], reverse=True)
        num_nentries = len(nentries)
        if num_nentries > 0:
            print()
            print(f"----- Top {db.sizelimit} Largest nentries ----")
            print()
            for num, nentry in enumerate(nentries):
                if num >= db.sizelimit:
                    break
                print(f"nentries={nentry:<10}")
            print()

        # Extended operations
        oids = db.operation['extop_dict']
        num_oids = len(oids)
        if num_oids > 0:
            print()
            print(f"----- Top {db.sizelimit} Extended Operations ----")
            print()
            for num, oid in enumerate(sorted(oids, key=lambda k: oids[k], reverse=True)):
                if num >= db.sizelimit:
                    break
                print(f"{oids[oid]:<12} {oid:<30} {oid_messages.get(oid, "Other"):<60}")

        # Commonly requested attributes
        attrs = db.search['attr_dict']
        num_nattrs = len(attrs)
        if num_nattrs > 0:
            print()
            print(f"----- Top {db.sizelimit} Most Requested Attributes ----")
            print()
            for num, attr in enumerate(sorted(attrs, key=lambda k: attrs[k], reverse=True)):
                if num >= db.sizelimit:
                    break
                print(f"{attrs[attr]:<11} {attr:<10}")
            print()

        abandoned = db.operation['abandoned_map_rco']
        num_abandoned = len(abandoned)
        if num_abandoned > 0:
            print()
            print(f"----- Abandon Request Stats -----")
            print()
            for num, abandon in enumerate(abandoned, start = 1):
                (restart, conn, op) = abandon
                conn, op, target_op, msgid = db.operation['abandoned_map_rco'][(restart, conn, op)]
                print(f"{num:<6} conn={conn} op={op} msgid={msgid} target_op:{target_op} client={db.connection['restart_conn_ip_map'].get((restart, conn), '"Unknown"')}")
            print()

    # Used for measuring memory consumption, will be removed post code review
    process = psutil.Process(os.getpid())
    print(f"Memory usage: {process.memory_info().rss / 1024 ** 2} MB")

            
if __name__ == "__main__":
    # Used for profiling, will be removed post code review
    #cProfile.run('main()', 'main.prof', sort='cumtime') 
    main()
    