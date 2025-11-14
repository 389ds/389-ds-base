# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from collections import defaultdict
from datetime import datetime, timezone, tzinfo, timedelta
from typing import Dict, List, Optional, Tuple, Generator, Any, NamedTuple, Union
import os
import re
import json
import csv
import logging
import ldap
import subprocess
from lib389._constants import *
from lib389.properties import *
from lib389.utils import normalizeDN

try:
    import plotly.graph_objs as go
    import plotly.io as pio
    from plotly.subplots import make_subplots
    PLOTLY_AVAILABLE = True
except ImportError:
    PLOTLY_AVAILABLE = False

try:
    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates
    MATPLOTLIB_AVAILABLE = True
except ImportError:
    MATPLOTLIB_AVAILABLE = False

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


# Helper functions
def _alphanum_key(s):
    """Turn the string into a list of string and number parts"""

    return [int(c) if c.isdigit() else c for c in
            re.split('([0-9]+)', s)]


def smart_sort(str_list):
    """Sort the given list in the way that humans expect.

    :param str_list: A list of strings to sort
    :type str_list: list
    """

    str_list.sort(key=_alphanum_key)


def _getCSNTime(inst, csn):
    """Take a CSN and get the access log timestamp in seconds

    :param inst: An instance to check access log
    :type inst: lib389.DirSrv
    :param csn: A "csn" string that is used to find when the csn was logged in
                the access log, and what time in seconds it was logged.
    :type csn: str

    :returns: The time is seconds that the operation was logged
    """

    op_line = inst.ds_access_log.match('.*csn=%s' % csn)
    if op_line:
        #vals = inst.ds_access_log.parse_line(op_line[0])
        return inst.ds_access_log.get_time_in_secs(op_line[0])
    else:
        return None


def _getCSNandTime(inst, line):
    """Take the line and find the CSN from the inst's access logs

    :param inst: An instance to check access log
    :type inst: lib389.DirSrv
    :param line: A "RESULT" line from the access log that contains a "csn"
    :type line: str

    :returns: A tuple containing the "csn" value and the time in seconds when
              it was logged.
    """

    op_line = inst.ds_access_log.match('.*%s.*' % line)
    if op_line:
        vals = inst.ds_access_log.parse_line(op_line[0])
        op = vals['op']
        conn = vals['conn']

        # Now find the result line and CSN
        result_line = inst.ds_access_log.match_archive(
            '.*conn=%s op=%s RESULT.*' % (conn, op))

        if result_line:
            vals = inst.ds_access_log.parse_line(result_line[0])
            if 'csn' in vals:
                ts = inst.ds_access_log.get_time_in_secs(result_line[0])
                return (vals['csn'], ts)

    return (None, None)


class ReplTools(object):
    """Replication tools"""

    @staticmethod
    def checkCSNs(dirsrv_replicas, ignoreCSNs=None):
        """Gather all the CSN strings from the access and verify all of those
        CSNs exist on all the other replicas.

        :param dirsrv_replicas: A list of DirSrv objects. The list must begin
                                with supplier replicas
        :type dirsrv_replicas: list of lib389.DirSrv
        :param ignoreCSNs: An optional string of csns to be ignored if
                           the caller knows that some csns can differ eg.:
                           '57e39e72000000020000|vucsn-57e39e76000000030000'
        :type ignoreCSNs: str

        :returns: True if all the CSNs are present, otherwise False
        """

        csn_logs = []
        csn_log_count = 0

        for replica in dirsrv_replicas:
            logdir = '%s*' % replica.ds_access_log._get_log_path()
            outfile = '/tmp/csn' + str(csn_log_count)
            csn_logs.append(outfile)
            csn_log_count += 1
            if ignoreCSNs:
                cmd = ("grep csn= " + logdir +
                       " | awk '{print $10}' | egrep -v '" + ignoreCSNs + "' | sort -u > " + outfile)
            else:
                cmd = ("grep csn= " + logdir +
                       " | awk '{print $10}' | sort -u > " + outfile)
            os.system(cmd)

        # Set a side the first supplier log - we use this for our "diffing"
        main_log = csn_logs[0]
        csn_logs.pop(0)

        # Now process the remaining csn logs
        for csnlog in csn_logs:
            cmd = 'diff %s %s' % (main_log, csnlog)
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True)
            line = proc.stdout.readline()
            if line != "" and line != "\n":
                if not line.startswith("\\"):
                    log.fatal("We have a CSN mismatch between (%s vs %s): %s" %
                              (main_log, csnlog, line))
                    return False

        return True

    @staticmethod
    def replConvReport(suffix, ops, replica, all_replicas):
        """Find and measure the convergence of entries from a replica, and
        print a report on how fast all the "ops" replicated to the other
        replicas.

        :param suffix: Replicated suffix
        :type suffix: str
        :param ops:  a list of "operations" to search for in the access logs
        :type ops: list
        :param replica: Instance where the entries originated
        :type replica: lib389.DirSrv
        :param all_replicas: Suppliers, hubs, consumers
        :type all_replicas: list of lib389.DirSrv

        :returns: The longest time in seconds for an operation to fully converge
        """
        highest_time = 0
        total_time = 0

        print('Convergence Report for replica: %s (%s)' %
              (replica.serverid, suffix))
        print('-' * 80)

        # Loop through each operation checking all the access logs
        for op in ops:
            csnstr, csntime = _getCSNandTime(replica, op)
            if csnstr is None and csntime is None:
                # Didn't find a csn, move on
                continue

            conv_time = []
            longest_time = 0
            for inst in all_replicas:
                replObj = inst.replicas.get(suffix)
                if replObj is None:
                    inst.log.warning('(%s) not setup for replication of (%s)' %
                                   (inst.serverid, suffix))
                    continue
                ctime = _getCSNTime(inst, csnstr)
                if ctime:
                    role = replObj.get_role()
                    if role == ReplicaRole.SUPPLIER:
                        txt = ' Supplier (%s)' % (inst.serverid)
                    elif role == ReplicaRole.HUB:
                        txt = ' Hub (%s)' % (inst.serverid)
                    elif role == ReplicaRole.CONSUMER:
                        txt = ' Consumer (%s)' % (inst.serverid)
                    else:
                        txt = '?'
                    ctime = ctime - csntime
                    conv_time.append(str(ctime) + txt)
                    if ctime > longest_time:
                        longest_time = ctime

            smart_sort(conv_time)
            print('\n    Operation: %s\n    %s' % (op, '-' * 40))
            print('\n      Convergence times:')
            for line in conv_time:
                parts = line.split(' ', 1)
                print('        %8s secs - %s' % (parts[0], parts[1]))
            print('\n      Longest Convergence Time: ' +
                  str(longest_time))
            if longest_time > highest_time:
                highest_time = longest_time
            total_time += longest_time

        print('\n    Summary for "{}"'.format(replica.serverid))
        print('    ----------------------------------------')
        print('      Highest convergence time: {} seconds'.format(highest_time))
        print('      Average longest convergence time: {} seconds\n'.format(int(total_time / len(ops))))

        return highest_time

    @staticmethod
    def replIdle(replicas, suffix=DEFAULT_SUFFIX):
        """Take a list of DirSrv Objects and check to see if all of the present
        replication agreements are idle for a particular backend

        :param replicas: Suppliers, hubs, consumers
        :type replicas: list of lib389.DirSrv
        :param suffix: Replicated suffix
        :type suffix: str

        :raises: LDAPError: if unable to search for the replication agreements
        :returns: True if all the agreements are idle, otherwise False
        """

        IDLE_MSG = ('Replica acquired successfully: Incremental ' +
                    'update succeeded')
        STATUS_ATTR = 'nsds5replicaLastUpdateStatus'
        FILTER = ('(&(nsDS5ReplicaRoot=' + suffix +
                  ')(objectclass=nsds5replicationAgreement))')
        repl_idle = True

        for inst in replicas:
            try:
                entries = inst.search_s("cn=config",
                    ldap.SCOPE_SUBTREE, FILTER, [STATUS_ATTR])
                if entries:
                    for entry in entries:
                        if IDLE_MSG not in entry.getValue(STATUS_ATTR):
                            repl_idle = False
                            break

                if not repl_idle:
                    break

            except ldap.LDAPError as e:
                log.fatal('Failed to search the repl agmts on ' +
                          '%s - Error: %s' % (inst.serverid, str(e)))
                assert False
        return repl_idle

    @staticmethod
    def createReplManager(server, repl_manager_dn=None, repl_manager_pw=None):
        """Create an entry that will be used to bind as replication manager.

        :param server: An instance to connect to
        :type server: lib389.DirSrv
        :param repl_manager_dn: DN of the bind entry. If not provided use
                                the default one
        :type repl_manager_dn: str
        :param repl_manager_pw: Password of the entry. If not provide use
                                the default one
        :type repl_manager_pw: str

        :returns: None
        :raises: - KeyError - if can not find valid values of Bind DN and Pwd
                 - LDAPError - if we fail to add the replication manager
        """

        # check the DN and PW
        try:
            repl_manager_dn = repl_manager_dn or \
                defaultProperties[REPLICATION_BIND_DN]
            repl_manager_pw = repl_manager_pw or \
                defaultProperties[REPLICATION_BIND_PW]
            if not repl_manager_dn or not repl_manager_pw:
                raise KeyError
        except KeyError:
            if not repl_manager_pw:
                server.log.warning("replica_createReplMgr: bind DN password " +
                                   "not specified")
            if not repl_manager_dn:
                server.log.warning("replica_createReplMgr: bind DN not " +
                                   "specified")
            raise

        # If the replication manager entry already exists, just return
        try:
            entries = server.search_s(repl_manager_dn, ldap.SCOPE_BASE,
                                              "objectclass=*")
            if entries:
                # it already exist, fine
                return
        except ldap.NO_SUCH_OBJECT:
            pass

        # ok it does not exist, create it
        attrs = {'nsIdleTimeout': '0',
                 'passwordExpirationTime': '20381010000000Z'}
        server.setupBindDN(repl_manager_dn, repl_manager_pw, attrs)


class DSLogParser:
    """Base parser for Directory Server logs, focusing on replication events."""

    REGEX_TIMESTAMP = re.compile(
        r'\[(?P<day>\d*)\/(?P<month>\w*)\/(?P<year>\d*):(?P<hour>\d*):(?P<minute>\d*):(?P<second>\d*)(\.(?P<nanosecond>\d*))+\s(?P<tz>[\+\-]\d{2})(?P<tz_minute>\d{2})'
    )
    REGEX_LINE = re.compile(
        r'\s(?P<quoted>[^= ]+="[^"]*")|(?P<var>[^= ]+=[^\s]+)|(?P<keyword>[^\s]+)'
    )
    MONTH_LOOKUP = {
        'Jan': "01", 'Feb': "02", 'Mar': "03", 'Apr': "04",
        'May': "05", 'Jun': "06", 'Jul': "07", 'Aug': "08",
        'Sep': "09", 'Oct': "10", 'Nov': "11", 'Dec': "12"
    }

    class ParserResult:
        """Container for parsed log line results."""
        def __init__(self):
            self.keywords: List[str] = []
            self.vars: Dict[str, str] = {}
            self.raw: Any = None
            self.timestamp: Optional[str] = None
            self.line: Optional[str] = None

    def __init__(self, logname: str, suffixes: List[str],
                tz: tzinfo = timezone.utc,
                start_time: Optional[datetime] = None,
                end_time: Optional[datetime] = None,
                batch_size: int = 1000):
        """Initialize the parser with time range filtering.

        :param logname: Path to the log file
        :param suffixes: Suffixes that should be tracked
        :param tz: Timezone to interpret log timestamps
        :param start_time: Optional start time filter
        :param end_time: Optional end time filter
        :param batch_size: Batch size for memory-efficient processing
        """
        self.logname = logname
        self.lineno = 0
        self.line: Optional[str] = None
        self.tz = tz
        self._suffixes = self._normalize_suffixes(suffixes)

        # Ensure start_time and end_time are timezone-aware
        self.start_time = self._ensure_timezone_aware(start_time) if start_time else None
        self.end_time = self._ensure_timezone_aware(end_time) if end_time else None

        self.batch_size = batch_size
        self.pending_ops: Dict[Tuple[str, str], Dict[str, Any]] = {}
        self._logger = logging.getLogger(__name__)
        self._current_batch: List[Dict[str, Any]] = []

    def _ensure_timezone_aware(self, dt: datetime) -> datetime:
        """Ensure datetime is timezone-aware using configured timezone."""
        if dt.tzinfo is None:
            return dt.replace(tzinfo=self.tz)
        return dt.astimezone(self.tz)

    @staticmethod
    def parse_timestamp(ts: Union[str, datetime]) -> datetime:
        """Parse a timestamp into a datetime object."""
        if isinstance(ts, datetime):
            return ts

        match = DSLogParser.REGEX_TIMESTAMP.match(ts)
        if not match:
            raise ValueError(f"Invalid timestamp format: {ts}")

        parsed = match.groupdict()
        iso_ts = '{YEAR}-{MONTH}-{DAY}T{HOUR}:{MINUTE}:{SECOND}{TZH}:{TZM}'.format(
            YEAR=parsed['year'],
            MONTH=DSLogParser.MONTH_LOOKUP[parsed['month']],
            DAY=parsed['day'],
            HOUR=parsed['hour'],
            MINUTE=parsed['minute'],
            SECOND=parsed['second'],
            TZH=parsed['tz'],
            TZM=parsed['tz_minute']
        )

        # Create timezone-aware datetime
        dt = datetime.fromisoformat(iso_ts)

        # Handle nanoseconds if present
        if parsed['nanosecond']:
            dt = dt.replace(microsecond=int(parsed['nanosecond']) // 1000)

        return dt

    def _is_in_time_range(self, timestamp: datetime) -> bool:
        """Check if timestamp is within configured time range."""
        # Ensure timestamp is timezone-aware and in the same timezone
        aware_timestamp = self._ensure_timezone_aware(timestamp)

        if self.start_time and aware_timestamp < self.start_time:
            return False
        if self.end_time and aware_timestamp > self.end_time:
            return False
        return True

    def _cleanup_resources(self):
        """Clean up any remaining resources."""
        self.pending_ops.clear()
        self._current_batch.clear()

    def _process_operation(self, result: 'DSLogParser.ParserResult') -> Optional[Dict[str, Any]]:
        """Process operation with memory optimization."""
        conn = result.vars.get('conn')
        op = result.vars.get('op')

        if not conn or not op:
            return None

        conn_op = (conn, op)

        # Handle completion keywords
        if any(kw in result.keywords for kw in ['RESULT', 'ABANDON', 'DISCONNECT']):
            if conn_op in self.pending_ops:
                op_data = self.pending_ops.pop(conn_op)
                return self._create_record(result, op_data)
            return None

        # Manage pending operations
        if conn_op not in self.pending_ops:
            self.pending_ops[conn_op] = {
                'start_time': result.timestamp,
                'last_time': result.timestamp,
                'conn': conn,
                'op': op,
                'suffix': None,
                'target_dn': None
            }
        else:
            # Update last seen time
            self.pending_ops[conn_op]['last_time'] = result.timestamp

        # Check for DN and suffix
        if 'dn' in result.vars:
            matched_suffix = self._match_suffix(result.vars['dn'])
            if matched_suffix:
                self.pending_ops[conn_op]['suffix'] = matched_suffix
                self.pending_ops[conn_op]['target_dn'] = result.vars['dn']

        # Check for CSN
        if 'csn' in result.vars:
            self.pending_ops[conn_op]['csn'] = result.vars['csn']

        return None

    def parse_file(self) -> Generator[Dict[str, Any], None, None]:
        """Parse log file with memory-efficient batch processing."""
        try:
            with open(self.logname, 'r', encoding='utf-8') as f:
                for self.line in f:
                    self.lineno += 1
                    try:
                        result = self.parse_line()
                        if result:
                            # Record is returned if operation is complete
                            record = self._process_operation(result)
                            if record:
                                self._current_batch.append(record)

                                # Yield batch if full
                                if len(self._current_batch) >= self.batch_size:
                                    yield from self._process_batch()

                    except Exception as e:
                        self._logger.warning(
                            f"Error parsing line {self.lineno} in {self.logname}: {e}"
                        )
                        continue

                # Process any remaining operations in the final batch
                if self._current_batch:
                    yield from self._process_batch()

                # Handle any remaining pending operations
                yield from self._process_remaining_ops()

        except (OSError, IOError) as e:
            raise IOError(f"Failed to open or read log file {self.logname}: {e}")
        finally:
            self._cleanup_resources()

    def parse_line(self) -> Optional['DSLogParser.ParserResult']:
        """Parse a single line, returning a ParserResult object if recognized."""
        line = self.line
        if not line:
            return None

        # Extract timestamp
        timestamp_match = self.REGEX_TIMESTAMP.match(line)
        if not timestamp_match:
            return None

        result = DSLogParser.ParserResult()
        result.raw = line
        result.timestamp = timestamp_match.group(0)

        # Remove the timestamp portion from the line for parsing
        after_ts = line[timestamp_match.end():].strip()
        # Use REGEX_LINE to parse remaining content
        for match in self.REGEX_LINE.finditer(after_ts):
            if match.group('keyword'):
                # Something that is not in key=value format
                result.keywords.append(match.group('keyword'))
            elif match.group('var'):
                # key=value
                var = match.group('var')
                k, v = var.split('=', 1)
                result.vars[k] = v.strip()
            elif match.group('quoted'):
                # key="value"
                kv = match.group('quoted')
                k, v = kv.split('=', 1)
                result.vars[k] = v.strip('"')

        return result

    def _normalize_suffixes(self, suffixes: List[str]) -> List[str]:
        """Normalize suffixes for matching (lowercase, remove spaces)."""
        normalized = [normalizeDN(s) for s in suffixes if s]
        # Sort by length descending so we match the longest suffix first
        return sorted(normalized, key=len, reverse=True)

    def _match_suffix(self, dn: str) -> Optional[str]:
        """Return a matched suffix if dn ends with one of our suffixes."""
        if not dn:
            return None
        dn_clean = normalizeDN(dn)
        for sfx in self._suffixes:
            if dn_clean.endswith(sfx):
                return sfx
        return None

    def _process_batch(self) -> Generator[Dict[str, Any], None, None]:
        """Process and yield a batch of operations."""
        for record in self._current_batch:
            try:
                # Handle timestamp regardless of type
                if isinstance(record['timestamp'], str):
                    timestamp = self.parse_timestamp(record['timestamp'])
                else:
                    timestamp = record['timestamp']

                if not self._is_in_time_range(timestamp):
                    continue

                record['timestamp'] = timestamp
                yield record

            except ValueError as e:
                self._logger.warning(
                    f"Error processing timestamp in batch: {e}"
                )
        self._current_batch.clear()

    def _create_record(self, result: Optional['DSLogParser.ParserResult'] = None,
                    op_data: Dict[str, Any] = None) -> Optional[Dict[str, Any]]:
        """Create a standardized record from either a parser result or operation data."""
        try:
            # Determine source of data
            if result and op_data:
                # Active operation
                timestamp = self.parse_timestamp(result.timestamp)
                conn = result.vars.get('conn')
                op = result.vars.get('op')
                csn = result.vars.get('csn')
                etime = result.vars.get('etime')
                duration = self._calculate_duration(op_data['start_time'], result.timestamp)
            elif op_data:
                # Remaining operation
                timestamp = op_data.get('last_time', op_data['start_time'])
                if isinstance(timestamp, str):
                    timestamp = self.parse_timestamp(timestamp)
                conn = op_data.get('conn')
                op = op_data.get('op')
                csn = op_data.get('csn')
                etime = None
                duration = self._calculate_duration(
                    op_data['start_time'],
                    timestamp
                )
            else:
                self._logger.warning("Invalid record creation attempt: no data provided")
                return None

            # Validate required fields
            if not all([timestamp, conn, op]):
                self._logger.debug(
                    f"Missing required fields: timestamp={timestamp}, conn={conn}, op={op}"
                )
                return None

            # Create standardized record
            record = {
                'timestamp': timestamp,
                'conn': conn,
                'op': op,
                'csn': csn,
                'suffix': op_data.get('suffix'),
                'target_dn': op_data.get('target_dn'),
                'duration': duration,
                'etime': etime
            }

            # Verify time range
            if not self._is_in_time_range(timestamp):
                return None

            return record

        except Exception as e:
            self._logger.warning(f"Error creating record: {e}")
            return None

    def _process_remaining_ops(self) -> Generator[Dict[str, Any], None, None]:
        """Process any remaining pending operations."""
        for (conn, op), op_data in list(self.pending_ops.items()):
            try:
                if 'csn' in op_data and 'suffix' in op_data:
                    record = self._create_record(op_data=op_data)
                    if record:
                        yield record
            except Exception as e:
                self._logger.warning(
                    f"Error processing remaining operation {conn}-{op}: {e}"
                )
            finally:
                self.pending_ops.pop((conn, op), None)

    def _calculate_duration(self, start: Union[str, datetime],
                        end: Union[str, datetime]) -> float:
        """Compute duration between two timestamps. """
        try:
            if isinstance(start, str):
                st = self.parse_timestamp(start)
            else:
                st = start

            if isinstance(end, str):
                et = self.parse_timestamp(end)
            else:
                et = end

            return (et - st).total_seconds()
        except (ValueError, TypeError):
            return 0.0


class ChartData(NamedTuple):
    """Container for chart data series."""
    times: List[datetime]
    lags: List[float]
    durations: List[float]
    hover: List[str]

class VisualizationHelper:
    """Helper class for visualization-related functionality."""

    @staticmethod
    def generate_color_palette(num_colors: int) -> List[str]:
        """Generate a visually pleasing color palette.

        :param num_colors: Number of colors needed
        :returns: List of rgba color strings
        """
        colors = []
        for i in range(num_colors):
            hue = i / num_colors
            saturation = 0.7
            value = 0.9

            # Convert HSV to RGB
            c = value * saturation
            x = c * (1 - abs((hue * 6) % 2 - 1))
            m = value - c

            h_sector = int(hue * 6)
            if h_sector == 0:
                r, g, b = c, x, 0
            elif h_sector == 1:
                r, g, b = x, c, 0
            elif h_sector == 2:
                r, g, b = 0, c, x
            elif h_sector == 3:
                r, g, b = 0, x, c
            elif h_sector == 4:
                r, g, b = x, 0, c
            else:
                r, g, b = c, 0, x

            # Convert to RGB values
            rgb = [int((val + m) * 255) for val in (r, g, b)]
            colors.append(f'rgba({rgb[0]},{rgb[1]},{rgb[2]},0.8)')

        return colors

    @staticmethod
    def prepare_chart_data(csns: Dict[str, Dict[Union[int, str], Dict[str, Any]]],
                          tz: tzinfo = timezone.utc) -> Dict[Tuple[str, str], ChartData]:
        """Prepare data for visualization with timezone-aware timestamps."""
        chart_data = defaultdict(lambda: {
            'times': [], 'lags': [], 'durations': [], 'hover': []
        })

        for csn, server_map in csns.items():
            # Gather only valid records (dict, not '__hop_lags__', must have 'logtime')
            valid_records = [
                rec for key, rec in server_map.items()
                if isinstance(rec, dict)
                   and key != '__hop_lags__'
                   and 'logtime' in rec
            ]
            if not valid_records:
                continue

            # Compute global lag for this CSN (earliest vs. latest among valid records)
            t_list = [rec['logtime'] for rec in valid_records]
            earliest = min(t_list)
            latest = max(t_list)
            lag_val = latest - earliest

            # Populate chart data for each server record
            for rec in valid_records:
                suffix_val = rec.get('suffix', 'unknown')
                server_val = rec.get('server_name', 'unknown')

                # Convert numeric UTC timestamp to timezone-aware datetime
                ts_dt = datetime.fromtimestamp(rec['logtime'], tz=tz)

                # Operation duration, defaulting to 0.0 if missing
                duration_val = float(rec.get('duration', 0.0))

                # Build the ChartData slot
                data_slot = chart_data[(suffix_val, server_val)]
                data_slot['times'].append(ts_dt)
                data_slot['lags'].append(lag_val)  # The same global-lag for all servers
                data_slot['durations'].append(duration_val)
                # Format timestamp for hover display in the specified timezone
                timestamp_str = ts_dt.strftime('%Y-%m-%d %H:%M:%S')
                data_slot['hover'].append(
                    f"Timestamp: {timestamp_str}<br>"
                    f"CSN: {csn}<br>"
                    f"Server: {server_val}<br>"
                    f"Lag Time: {lag_val:.3f}s<br>"
                    f"Duration: {duration_val:.3f}s<br>"
                    f"Suffix: {suffix_val}<br>"
                    f"Entry: {(rec.get('target_dn') or 'unknown')}"
                )

        # Convert the dict-of-lists into your namedtuple-based ChartData
        return {
            key: ChartData(
                times=value['times'],
                lags=value['lags'],
                durations=value['durations'],
                hover=value['hover']
            )
            for key, value in chart_data.items()
        }


class ReplicationLogAnalyzer:
    """This class handles:
    - Collecting log files from multiple directories.
    - Parsing them for replication events (CSN).
    - Filtering by suffix.
    - Storing earliest and latest timestamps for each CSN to compute lag.
    - Generating final dictionaries to be used for CSV, HTML, or JSON reporting.
    """

    # Sampling configuration constants
    AUTO_SAMPLING_THRESHOLD = 4000  # Trigger auto sampling above this many CSN points
    HOP_SERIES_BUDGET_RATIO = 0.25  # Allocate max 25% of chart points to hop lag series
    MIN_POINTS_PER_SERIES = 2       # Minimum points to preserve series shape after sampling

    # Precision preset configurations
    PRECISION_PRESETS = {
        'fast': 2000,      # Fast analysis with more aggressive sampling
        'balanced': 6000,  # Balanced speed and detail (default)
        'full': None       # Full precision, no sampling cap
    }

    def __init__(self, log_dirs: List[str], suffixes: Optional[List[str]] = None,
                anonymous: bool = False, only_fully_replicated: bool = False,
                only_not_replicated: bool = False, lag_time_lowest: Optional[float] = None,
                etime_lowest: Optional[float] = None,
                utc_offset: Optional[int] = None, time_range: Optional[Dict[str, datetime]] = None,
                sampling_mode: str = "auto", max_chart_points: Optional[int] = None,
                analysis_precision: str = "balanced"):
        if not log_dirs:
            raise ValueError("No log directories provided for analysis.")

        self.log_dirs = log_dirs
        self.suffixes = suffixes or []
        self.anonymous = anonymous
        self.only_fully_replicated = only_fully_replicated
        self.only_not_replicated = only_not_replicated
        self.lag_time_lowest = lag_time_lowest
        self.etime_lowest = etime_lowest
        self._active_server_count: int = 0
        self._processed_log_dirs: List[str] = []
        self._skipped_log_dirs: List[str] = []

        # Set timezone
        if utc_offset is not None:
            try:
                self.tz = self._parse_timezone_offset(utc_offset)
            except ValueError as e:
                raise ValueError(f"Invalid UTC offset: {e}")
        else:
            self.tz = timezone.utc

        self.time_range = time_range or {}
        self.csns: Dict[str, Dict[Union[int, str], Dict[str, Any]]] = {}

        # Track earliest and latest timestamps
        self.start_dt: Optional[datetime] = None
        self.start_udt: Optional[float] = None
        self.end_dt: Optional[datetime] = None
        self.end_udt: Optional[float] = None
        self._logger = logging.getLogger(__name__)

        # Sampling / precision controls
        # analysis_precision: fast|balanced|full (string hint)
        self.analysis_precision = (analysis_precision or "balanced").lower()
        self.sampling_mode = (sampling_mode or "auto").lower()  # none|uniform|auto
        # Max total chart points across all series for replicationLags (default by precision)
        if max_chart_points is None:
            self.max_chart_points = self.PRECISION_PRESETS.get(
                self.analysis_precision,
                self.PRECISION_PRESETS['balanced']
            )
        else:
            self.max_chart_points = max_chart_points if max_chart_points > 0 else None

        # Threshold to trigger auto sampling if lots of CSNs
        self._auto_sampling_csn_threshold = self.AUTO_SAMPLING_THRESHOLD

    def _should_include_record(self, csn: str, server_map: Dict[Union[int, str], Dict[str, Any]]) -> bool:
        """Determine if a record should be included based on filtering criteria."""
        total_servers = self._active_server_count or len(self.log_dirs)

        if self.only_fully_replicated and len(server_map) != total_servers:
            return False
        if self.only_not_replicated and len(server_map) == total_servers:
            return False

        # Check lag time threshold
        if self.lag_time_lowest is not None:
            # Only consider dict items, skipping the '__hop_lags__' entry
            t_list = [
                d['logtime']
                for key, d in server_map.items()
                if isinstance(d, dict) and key != '__hop_lags__'
            ]
            if not t_list:
                return False
            lag_time = max(t_list) - min(t_list)
            if lag_time <= self.lag_time_lowest:
                return False

        # Check etime threshold
        if self.etime_lowest is not None:
            for key, record in server_map.items():
                if not isinstance(record, dict) or key == '__hop_lags__':
                    continue
                if float(record.get('etime', 0)) <= self.etime_lowest:
                    return False

        return True

    def _collect_logs(self) -> List[Dict[str, Any]]:
        """For each directory in self.log_dirs, return metadata about discovered log files."""
        data: List[Dict[str, Any]] = []
        processed_dirs: List[str] = []
        for dpath in self.log_dirs:
            if not os.path.isdir(dpath):
                self._logger.warning(f"{dpath} is not a directory or not accessible.")
                continue

            server_name = os.path.basename(dpath.rstrip('/'))
            logfiles = []
            for fname in os.listdir(dpath):
                if fname.startswith('access'):  # Only parse access logs
                    full_path = os.path.join(dpath, fname)
                    if os.path.isfile(full_path) and os.access(full_path, os.R_OK):
                        logfiles.append(full_path)
                    else:
                        self._logger.warning(f"Cannot read file: {full_path}")

            logfiles.sort()
            if logfiles:
                data.append({
                    'server_name': server_name or dpath,
                    'logfiles': logfiles,
                    'source_dir': dpath,
                })
                processed_dirs.append(dpath)
            else:
                self._logger.warning(f"No accessible 'access' logs found in {dpath}")
        self._processed_log_dirs = processed_dirs
        self._skipped_log_dirs = [d for d in self.log_dirs if d not in processed_dirs]
        return data

    @staticmethod
    def _parse_timezone_offset(offset_str: str) -> timezone:
        """Parse timezone offset string in ±HHMM format."""
        if not isinstance(offset_str, str):
            raise ValueError("Timezone offset must be a string in ±HHMM format")

        match = re.match(r'^([+-])(\d{2})(\d{2})$', offset_str)
        if not match:
            raise ValueError("Invalid timezone offset format. Use ±HHMM (e.g., -0400, +0530)")

        sign, hours, minutes = match.groups()
        hours = int(hours)
        minutes = int(minutes)

        if hours > 12 or minutes >= 60:
            raise ValueError("Invalid timezone offset. Hours must be ≤12, minutes <60")

        total_minutes = hours * 60 + minutes
        if sign == '-':
            total_minutes = -total_minutes

        return timezone(timedelta(minutes=total_minutes))

    def _compute_hop_lags(self, server_map: Dict[Union[int, str], Dict[str, Any]]) -> List[Dict[str, Any]]:
        """Compute per-hop replication lags for one CSN across multiple servers."""
        arrivals = []
        for key, data in server_map.items():
            # Skip the special '__hop_lags__' and any non-dict
            if not isinstance(data, dict) or key == '__hop_lags__':
                continue
            arrivals.append({
                'server_name': data.get('server_name', 'unknown'),
                'logtime': data.get('logtime', 0.0),  # numeric UTC timestamp
                'suffix': data.get('suffix'),
                'target_dn': data.get('target_dn'),
            })

        # Sort by ascending logtime
        arrivals.sort(key=lambda x: x['logtime'])

        # Iterate pairs (supplier -> consumer)
        hops = []
        for i in range(1, len(arrivals)):
            supplier = arrivals[i - 1]
            consumer = arrivals[i]
            hop_lag = consumer['logtime'] - supplier['logtime']  # in seconds
            hops.append({
                'supplier': supplier['server_name'],
                'consumer': consumer['server_name'],
                'hop_lag': hop_lag,
                'arrival_consumer': consumer['logtime'],
                'suffix': consumer.get('suffix'),
                'target_dn': consumer.get('target_dn'),
            })

        return hops

    def parse_logs(self) -> None:
        """Parse logs from all directories. Each directory is treated as one server
        unless anonymized, in which case we use 'server_{index}'.
        """
        server_data = self._collect_logs()
        if not server_data:
            raise ValueError("No valid log directories with accessible logs found.")

        self._active_server_count = len(server_data)

        for idx, server_entry in enumerate(server_data):
            server_name = server_entry['server_name']
            logfiles = server_entry['logfiles']

            displayed_name = f"server_{idx}" if self.anonymous else server_name

            # For each log file, parse line by line
            for logfile in logfiles:
                parser = DSLogParser(
                    logname=logfile,
                    suffixes=self.suffixes,
                    tz=self.tz,
                    start_time=self.time_range.get('start'),
                    end_time=self.time_range.get('end')
                )

                for record in parser.parse_file():
                    # If there's no CSN or no suffix, skip
                    if not record.get('csn') or not record.get('suffix'):
                        continue

                    csn = record['csn']
                    ts = record['timestamp']
                    # Convert timestamp to numeric UTC
                    udt = ts.astimezone(timezone.utc).timestamp()

                    # Track earliest global timestamp
                    if self.start_udt is None or udt < self.start_udt:
                        self.start_udt = udt
                        self.start_dt = ts

                    if csn not in self.csns:
                        self.csns[csn] = {}

                    # Build record for this server
                    self.csns[csn][idx] = {
                        'logtime': udt,
                        'etime': record.get('etime'),
                        'server_name': displayed_name,
                        'suffix': record.get('suffix'),
                        'target_dn': record.get('target_dn'),
                        'duration': record.get('duration', 0.0),
                    }

        # Apply filters after collecting all data
        filtered_csns = {}
        earliest_udt: Optional[float] = None
        latest_udt: Optional[float] = None

        for csn, server_map in self.csns.items():
            if self._should_include_record(csn, server_map):
                filtered_csns[csn] = server_map
                # Compute hop-lags and store
                hop_list = self._compute_hop_lags(server_map)
                filtered_csns[csn]['__hop_lags__'] = hop_list

                for key, record in server_map.items():
                    if key == '__hop_lags__' or not isinstance(record, dict):
                        continue
                    logtime = record.get('logtime')
                    if logtime is None:
                        continue
                    if earliest_udt is None or logtime < earliest_udt:
                        earliest_udt = logtime
                    if latest_udt is None or logtime > latest_udt:
                        latest_udt = logtime

        self.csns = filtered_csns

        if earliest_udt is not None:
            earliest_dt = datetime.fromtimestamp(earliest_udt, tz=timezone.utc).astimezone(self.tz)
            self.start_udt = earliest_udt
            self.start_dt = earliest_dt
        else:
            self.start_udt = None
            self.start_dt = None

        if latest_udt is not None:
            latest_dt = datetime.fromtimestamp(latest_udt, tz=timezone.utc).astimezone(self.tz)
            self.end_udt = latest_udt
            self.end_dt = latest_dt
        else:
            self.end_udt = None
            self.end_dt = None

    def build_result(self) -> Dict[str, Any]:
        """Build the final dictionary object with earliest timestamp, UTC offset, and replication data."""
        if not self.start_dt:
            raise ValueError("No valid replication data collected.")

        obj = {
            "start-time": str(self.start_dt),
            "utc-start-time": self.start_udt,
            "utc-offset": self.start_dt.utcoffset().total_seconds() if self.start_dt.utcoffset() else 0,
            "lag": self.csns
        }
        if self.end_dt is not None:
            obj["end-time"] = str(self.end_dt)
            obj["utc-end-time"] = self.end_udt
        # Also record the log-files (anonymous or not)
        if self.anonymous:
            obj['log-files'] = list(range(self._active_server_count))
        else:
            obj['log-files'] = self._processed_log_dirs or self.log_dirs
        return obj

    def generate_report(self, output_dir: str,
                       formats: List[str],
                       report_name: str = "replication_analysis") -> Dict[str, str]:
        """Generate reports in specified formats."""
        if not os.path.exists(output_dir):
            try:
                os.makedirs(output_dir)
            except OSError as e:
                raise OSError(f"Could not create directory {output_dir}: {e}")

        if not self.csns:
            # Provide a more user-friendly error message
            error_msg = "No replication data found matching the specified criteria."
            if self.lag_time_lowest is not None or self.etime_lowest is not None:
                error_msg += " The threshold filters (lag time or etime) may be too restrictive."
            if self.only_fully_replicated or self.only_not_replicated:
                error_msg += " The replication status filter may have excluded all entries."
            error_msg += " Try adjusting the filters or expanding the time range."
            raise ValueError(error_msg)

        results = self.build_result()
        generated_files = {}

        # Always produce JSON summary
        summary_path = os.path.join(output_dir, f"{report_name}_summary.json")
        self._generate_summary_json(results, summary_path)
        generated_files["summary"] = summary_path

        # Generate PatternFly format JSON data if requested
        if 'json' in formats:
            json_path = os.path.join(output_dir, f"{report_name}.json")
            self._generate_patternfly_json(results, json_path)
            generated_files["json"] = json_path

        # Generate requested formats
        for fmt in formats:
            fmt = fmt.lower()
            if fmt == 'json':  # Already handled above
                continue

            outfile = os.path.join(output_dir, f"{report_name}.{fmt}")

            if fmt == 'csv':
                self._generate_csv(results, outfile)
                generated_files["csv"] = outfile

            elif fmt == 'html':
                if not PLOTLY_AVAILABLE:
                    self._logger.warning("Plotly not installed. Skipping HTML report. Please install python3-lib389-repl-reports package to get required dependencies.")
                    continue
                fig = self._create_plotly_figure(results)
                self._generate_html(fig, outfile)
                generated_files["html"] = outfile

            elif fmt == 'png':
                if not MATPLOTLIB_AVAILABLE:
                    self._logger.warning("Matplotlib not installed. Skipping PNG report. Please install python3-lib389-repl-reports package to get required dependencies.")
                    continue
                fig = self._create_plotly_figure(results)
                self._generate_png(fig, outfile)
                generated_files["png"] = outfile

            else:
                self._logger.warning(f"Unknown report format requested: {fmt}")

        return generated_files

    def _create_plotly_figure(self, results: Dict[str, Any]):
        """Create a plotly figure for visualization."""
        if not PLOTLY_AVAILABLE:
            raise ImportError("Plotly is required for figure creation")

        # Create figure with 3 subplots: we still generate all 3 for HTML usage
        fig = make_subplots(
            rows=3, cols=1,
            subplot_titles=(
                "Global Replication Lag Over Time",
                "Operation Duration Over Time",
                "Per-Hop Replication Lags"
            ),
            vertical_spacing=0.10,   # spacing between subplots
            shared_xaxes=True
        )

        # Collect all (suffix, server_name) pairs to color consistently
        server_suffix_pairs = set()
        for csn, server_map in self.csns.items():
            for key, rec in server_map.items():
                if not isinstance(rec, dict) or key == '__hop_lags__':
                    continue

                suffix_val = rec.get('suffix', 'unknown')
                srv_val = rec.get('server_name', 'unknown')
                server_suffix_pairs.add((suffix_val, srv_val))

        # Generate colors
        colors = VisualizationHelper.generate_color_palette(len(server_suffix_pairs))

        # Prepare chart data for the first two subplots
        chart_data = VisualizationHelper.prepare_chart_data(self.csns)

        # Plot Per-Hop Lags in row=3 (for HTML usage)
        for csn, server_map in self.csns.items():
            hop_list = server_map.get('__hop_lags__', [])
            for hop in hop_list:
                consumer_ts = hop.get("arrival_consumer", 0.0)
                consumer_dt = datetime.fromtimestamp(consumer_ts)
                hop_lag = hop.get("hop_lag", 0.0)

                hover_text = (
                    f"Supplier: {hop.get('supplier','unknown')}<br>"
                    f"Consumer: {hop.get('consumer','unknown')}<br>"
                    f"Hop Lag: {hop_lag:.3f}s<br>"
                    f"Arrival Time: {consumer_dt}<br>"
                    f"Suffix: {(hop.get('suffix') or 'unknown')}<br>"
                    f"Entry: {(hop.get('target_dn') or 'unknown')}"
                )

                # showlegend=False means these hop-lag traces won't crowd the legend
                fig.add_trace(
                    go.Scatter(
                        x=[consumer_dt],
                        y=[hop_lag],
                        mode='markers',
                        marker=dict(size=7, symbol='circle'),
                        name=f"{hop.get('supplier','?')}→{hop.get('consumer','?')}",
                        text=[hover_text],
                        hoverinfo='text+x+y',
                        showlegend=False
                    ),
                    row=3, col=1
                )

        # Plot Global Lag (row=1) and Durations (row=2)
        for idx, ((sfx, srv), data) in enumerate(sorted(chart_data.items())):
            color = colors[idx % len(colors)]

            # Row=1: Global Replication Lag
            fig.add_trace(
                go.Scatter(
                    x=data.times,
                    y=data.lags,
                    mode='lines+markers',
                    name=f"{sfx} - {srv}",
                    text=data.hover,
                    hoverinfo='text+x+y',
                    line=dict(color=color, width=2),
                    marker=dict(size=6),
                    showlegend=True
                ),
                row=1, col=1
            )

            # Row=2: Operation Durations
            fig.add_trace(
                go.Scatter(
                    x=data.times,
                    y=data.durations,
                    mode='lines+markers',
                    name=f"{sfx} - {srv}",
                    text=data.hover,
                    hoverinfo='text+x+y',
                    line=dict(color=color, width=2, dash='solid'),
                    marker=dict(size=6),
                    showlegend=False
                ),
                row=2, col=1
            )

        # Figure layout settings
        fig.update_layout(
            title={
                'text': 'Replication Analysis Report',
                'y': 0.96,
                'x': 0.5,
                'xanchor': 'center',
                'yanchor': 'top'
            },
            template='plotly_white',
            hovermode='closest',
            showlegend=True,
            legend=dict(
                title="Suffix / Server",
                yanchor="top",
                y=0.99,
                xanchor="right",
                x=1.15,
                bgcolor='rgba(255, 255, 255, 0.8)'
            ),
            height=900,
            margin=dict(t=100, r=200, l=80)
        )

        # X-axis styling
        fig.update_xaxes(title_text="Time", gridcolor='lightgray', row=1, col=1)
        fig.update_xaxes(
            title_text="Time",
            gridcolor='lightgray',
            rangeslider_visible=True,
            rangeselector=dict(
                buttons=list([
                    dict(count=1, label="1h", step="hour", stepmode="backward"),
                    dict(count=6, label="6h", step="hour", stepmode="backward"),
                    dict(count=1, label="1d", step="day", stepmode="backward"),
                    dict(count=7, label="1w", step="day", stepmode="backward"),
                    dict(step="all")
                ]),
                bgcolor='rgba(255, 255, 255, 0.8)'
            ),
            row=2, col=1
        )
        fig.update_xaxes(title_text="Time", gridcolor='lightgray', row=3, col=1)

        # Y-axis styling
        fig.update_yaxes(title_text="Lag Time (seconds)", gridcolor='lightgray', row=1, col=1)
        fig.update_yaxes(title_text="Duration (seconds)", gridcolor='lightgray', row=2, col=1)
        fig.update_yaxes(title_text="Hop Lag (seconds)", gridcolor='lightgray', row=3, col=1)

        return fig

    def _generate_png(self, fig, outfile: str) -> None:
        """Generate PNG snapshot of the plotly figure using matplotlib.
        For PNG, we deliberately omit the hop-lag (3rd subplot) data.
        """
        try:
            # Create a matplotlib figure with 2 subplots
            plt.figure(figsize=(12, 8))

            # Extract data from the Plotly figure.
            # We'll plot only the first two subplots (y-axis = 'y' or 'y2').
            for trace in fig.data:
                # Check which y-axis the trace belongs to.
                # 'y'  => subplot row=1
                # 'y2' => subplot row=2
                # 'y3' => subplot row=3 (hop-lags) - skip those
                if trace.yaxis == 'y':  # Global Lag subplot
                    plt.subplot(2, 1, 1)
                    plt.plot(trace.x, trace.y, label=trace.name)
                elif trace.yaxis == 'y2':  # Duration subplot
                    plt.subplot(2, 1, 2)
                    plt.plot(trace.x, trace.y, label=trace.name)
                else:
                    # This is likely the hop-lag data on subplot row=3, so skip it
                    continue

            # Format each subplot
            for idx, title in enumerate(['Replication Lag Times', 'Operation Durations']):
                plt.subplot(2, 1, idx + 1)
                plt.title(title)
                plt.xlabel('Time')
                plt.ylabel('Seconds')
                plt.grid(True)
                plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
                # Format x-axis as date/time
                plt.gca().xaxis.set_major_formatter(mdates.DateFormatter('%Y-%m-%d %H:%M'))
                plt.gcf().autofmt_xdate()

            plt.tight_layout()

            # Save with explicit format to ensure proper PNG generation
            plt.savefig(outfile, dpi=300, bbox_inches='tight', format='png')
            plt.close()

            # Verify file was created and is a valid PNG
            if not os.path.exists(outfile) or os.path.getsize(outfile) < 100:
                raise IOError(f"PNG file was not created correctly: {outfile}")

            # Attempt to open and verify the file is a valid PNG
            try:
                with open(outfile, 'rb') as f:
                    header = f.read(8)
                    # Check for PNG signature
                    if header != b'\x89PNG\r\n\x1a\n':
                        raise IOError(f"Generated file does not have a valid PNG signature")
            except Exception as e:
                raise IOError(f"Failed to verify PNG file: {e}")

        except Exception as e:
            # If PNG generation fails, try a direct plotly export as fallback
            try:
                # Try using plotly's built-in image export
                pio.write_image(fig, outfile, format='png', width=1200, height=800, scale=2)

                # Verify the file exists
                if not os.path.exists(outfile):
                    raise IOError("Fallback PNG generation failed")
            except Exception as fallback_err:
                raise IOError(f"Failed to generate PNG report: {e}. Fallback also failed: {fallback_err}")

    def _generate_html(self, fig, outfile: str) -> None:
        """Generate HTML report from the plotly figure."""
        try:
            pio.write_html(
                fig,
                outfile,
                include_plotlyjs='cdn',
                full_html=True,
                include_mathjax='cdn',
                config={
                    'responsive': True,
                    'scrollZoom': True,
                    'modeBarButtonsToAdd': ['drawline', 'drawopenpath', 'eraseshape'],
                    'toImageButtonOptions': {
                        'format': 'png',
                        'filename': 'replication_analysis',
                        'height': 1000,
                        'width': 1500,
                        'scale': 2
                    }
                }
            )
        except Exception as e:
            raise IOError(f"Failed to write HTML report: {e}")

    def _generate_csv(self, results: Dict[str, Any], outfile: str) -> None:
        """Generate a CSV report listing each replication event and its hop-lags."""
        try:
            with open(outfile, 'w', newline='', encoding='utf-8') as csvfile:
                writer = csv.writer(csvfile)

                # Global-lag rows
                writer.writerow([
                    'Timestamp', 'Server', 'CSN', 'Suffix', 'Target DN',
                    'Global Lag (s)', 'Duration (s)', 'Operation Etime'
                ])
                for csn, server_map in self.csns.items():
                    # Compute global-lag for normal dict entries
                    t_list = [
                        d['logtime']
                        for key, d in server_map.items()
                        if isinstance(d, dict) and key != '__hop_lags__'
                    ]
                    if not t_list:
                        continue
                    earliest = min(t_list)
                    latest = max(t_list)
                    global_lag = latest - earliest

                    # Write lines for each normal server record
                    for key, data_map in server_map.items():
                        if not isinstance(data_map, dict) or key == '__hop_lags__':
                            continue
                        ts_dt = datetime.fromtimestamp(data_map['logtime'], tz=self.tz)
                        ts_str = ts_dt.strftime('%Y-%m-%d %H:%M:%S')
                        writer.writerow([
                            ts_str,
                            data_map['server_name'],
                            csn,
                            data_map.get('suffix', 'unknown'),
                            data_map.get('target_dn', ''),
                            f"{global_lag:.3f}",
                            f"{float(data_map.get('duration', 0.0)):.3f}",
                            data_map.get('etime', 'N/A')
                        ])

                # Hop-lag rows
                writer.writerow([])  # blank line
                writer.writerow(["-- Hop-Lag Data --"])
                writer.writerow([
                    'CSN', 'Supplier', 'Consumer', 'Hop Lag (s)', 'Arrival (Consumer)', 'Suffix', 'Target DN'
                ])
                for csn, server_map in self.csns.items():
                    hop_list = server_map.get('__hop_lags__', [])
                    for hop_info in hop_list:
                        hop_lag_str = f"{hop_info['hop_lag']:.3f}"
                        arrival_dt = datetime.fromtimestamp(hop_info['arrival_consumer'], tz=self.tz)
                        arrival_ts = arrival_dt.strftime('%Y-%m-%d %H:%M:%S')
                        writer.writerow([
                            csn,
                            hop_info['supplier'],
                            hop_info['consumer'],
                            hop_lag_str,
                            arrival_ts,
                            hop_info.get('suffix', 'unknown'),
                            hop_info.get('target_dn', '')
                        ])

        except Exception as e:
            raise IOError(f"Failed to write CSV report {outfile}: {e}")

    def _generate_summary_json(self, results: Dict[str, Any], outfile: str) -> None:
        """Create a JSON summary from the final dictionary."""
        global_lag_times = []
        hop_lag_times = []
        suffix_updates = {}

        for csn, server_map in self.csns.items():
            t_list = [
                rec['logtime']
                for key, rec in server_map.items()
                if isinstance(rec, dict) and key != '__hop_lags__' and 'logtime' in rec
            ]
            if not t_list:
                continue

            # Global earliest vs. latest (for "global lag")
            earliest = min(t_list)
            latest = max(t_list)
            global_lag = latest - earliest
            global_lag_times.append(global_lag)

            # Suffix counts
            for key, record in server_map.items():
                # Only process normal server records, skip the special '__hop_lags__'
                if not isinstance(record, dict) or key == '__hop_lags__':
                    continue

                sfx = record.get('suffix', 'unknown')
                suffix_updates[sfx] = suffix_updates.get(sfx, 0) + 1

            # Hop-lag data
            hop_list = server_map.get('__hop_lags__', [])
            for hop_info in hop_list:
                hop_lag_times.append(hop_info['hop_lag'])

        # Compute global-lag stats
        if global_lag_times:
            min_lag = min(global_lag_times)
            max_lag = max(global_lag_times)
            avg_lag = sum(global_lag_times) / len(global_lag_times)
        else:
            min_lag = 0.0
            max_lag = 0.0
            avg_lag = 0.0

        # Compute hop-lag stats
        if hop_lag_times:
            min_hop_lag = min(hop_lag_times)
            max_hop_lag = max(hop_lag_times)
            avg_hop_lag = sum(hop_lag_times) / len(hop_lag_times)
            total_hops = len(hop_lag_times)
        else:
            min_hop_lag = 0.0
            max_hop_lag = 0.0
            avg_hop_lag = 0.0
            total_hops = 0

        # Build analysis summary
        analysis_summary = {
            'total_servers': self._active_server_count or len(self.log_dirs),
            'configured_log_dirs': self.log_dirs,
            'processed_log_dirs': self._processed_log_dirs,
            'skipped_log_dirs': self._skipped_log_dirs,
            'analyzed_logs': len(self.csns),
            'total_updates': sum(suffix_updates.values()),
            'minimum_lag': min_lag,
            'maximum_lag': max_lag,
            'average_lag': avg_lag,
            'minimum_hop_lag': min_hop_lag,
            'maximum_hop_lag': max_hop_lag,
            'average_hop_lag': avg_hop_lag,
            'total_hops': total_hops,
            'updates_by_suffix': suffix_updates,
            'time_range': {
                'start': results['start-time'],
                'end': results.get('end-time', 'current')
            }
        }

        # Wrap it up for writing
        summary = {
            'analysis_summary': analysis_summary
        }

        # Write to JSON
        try:
            with open(outfile, 'w') as f:
                json.dump(summary, f, indent=4, default=str)
        except Exception as e:
            raise IOError(f"Failed to write JSON summary to {outfile}: {e}")

    def _generate_patternfly_json(self, results: Dict[str, Any], outfile: str) -> None:
        """Generate JSON specifically formatted for PatternFly 5 charts."""
        chart_data = VisualizationHelper.prepare_chart_data(self.csns, self.tz)
        ordered_series = sorted(chart_data.keys())
        color_palette = VisualizationHelper.generate_color_palette(len(ordered_series))

        def _uniform_indices(n: int, target: int) -> List[int]:
            if target <= 0 or target >= n:
                return list(range(n))
            step = (n - 1) / float(target - 1)
            return [int(round(i * step)) for i in range(target)]

        sampling_enabled = self.sampling_mode != "none"
        total_points = sum(len(chart_data[key].times) for key in ordered_series)
        if sampling_enabled and self.sampling_mode == "auto" and total_points <= self._auto_sampling_csn_threshold:
            sampling_enabled = False

        ratio = None
        if sampling_enabled and self.max_chart_points and total_points > self.max_chart_points:
            ratio = self.max_chart_points / float(total_points)

        sampling_meta = {
            "applied": False,
            "mode": None,
            "samplingMode": self.sampling_mode,
            "precision": self.analysis_precision,
            "maxChartPoints": self.max_chart_points,
            "originalTotalPoints": total_points,
            "reducedTotalPoints": None
        }

        def _mark_sampling():
            sampling_meta["applied"] = True
            if not sampling_meta["mode"]:
                sampling_meta["mode"] = "uniform-auto" if self.sampling_mode == "auto" else "uniform"

        def _apply_sampling(indices: List[int], target: Optional[int]) -> List[int]:
            if target is None or target >= len(indices):
                return indices
            target = max(self.MIN_POINTS_PER_SERIES, target)
            if target >= len(indices):
                return indices
            _mark_sampling()
            return [indices[j] for j in _uniform_indices(len(indices), target)]

        series_data = []
        for idx, (suffix, server_name) in enumerate(ordered_series):
            data = chart_data[(suffix, server_name)]
            if not data.times:
                continue
            sorted_idx = sorted(range(len(data.times)), key=lambda i: data.times[i])
            target = None
            if ratio is not None:
                target = int(round(len(sorted_idx) * ratio))
            indices = _apply_sampling(sorted_idx, target)
            datapoints = [{
                "name": server_name,
                "x": data.times[i].isoformat(),
                "y": data.lags[i],
                "duration": data.durations[i],
                "hoverInfo": data.hover[i]
            } for i in indices]
            series_data.append({
                "datapoints": datapoints,
                "legendItem": {"name": f"{server_name} ({suffix})"},
                "color": color_palette[idx % len(color_palette)]
            })

        hop_data: Dict[str, Dict[str, List[Any]]] = defaultdict(lambda: {"times": [], "lags": [], "hover": []})
        for csn, server_map in self.csns.items():
            for hop in server_map.get('__hop_lags__', []):
                source = hop.get('supplier', 'unknown')
                target = hop.get('consumer', 'unknown')
                key = f"{source} → {target}"
                entry = hop_data[key]
                ts = datetime.fromtimestamp(hop.get('arrival_consumer', 0.0), tz=self.tz)
                entry["times"].append(ts)
                entry["lags"].append(hop.get('hop_lag', 0.0))
                ts_str = ts.strftime('%Y-%m-%d %H:%M:%S')
                entry["hover"].append(
                    f"Timestamp: {ts_str}<br>"
                    f"CSN: {csn}<br>"
                    f"Source: {source}<br>"
                    f"Target: {target}<br>"
                    f"Hop Lag: {hop.get('hop_lag', 0.0):.3f}s<br>"
                    f"Suffix: {hop.get('suffix','unknown')}<br>"
                    f"Entry: {(hop.get('target_dn') or 'unknown')}"
                )

        total_hop_points = sum(len(entry["times"]) for entry in hop_data.values())
        sampling_meta["originalTotalPoints"] += total_hop_points

        hop_ratio = None
        if sampling_enabled and self.max_chart_points:
            hop_limit = int(self.max_chart_points * self.HOP_SERIES_BUDGET_RATIO)
            if hop_limit > 0 and total_hop_points > hop_limit:
                hop_ratio = hop_limit / float(total_hop_points)

        hop_palette = VisualizationHelper.generate_color_palette(len(hop_data))
        hop_series = []
        for idx, (key, entry) in enumerate(sorted(hop_data.items())):
            if not entry["times"]:
                continue
            sorted_idx = sorted(range(len(entry["times"])), key=lambda i: entry["times"][i])
            target = None
            if hop_ratio is not None:
                target = int(round(len(sorted_idx) * hop_ratio))
            indices = _apply_sampling(sorted_idx, target)
            datapoints = [{
                "name": key,
                "x": entry["times"][i].isoformat(),
                "y": entry["lags"][i],
                "hoverInfo": entry["hover"][i].replace("Suffix: None", "Suffix: unknown").replace("Entry: None", "Entry: unknown")
            } for i in indices]
            hop_series.append({
                "datapoints": datapoints,
                "legendItem": {"name": key},
                "color": hop_palette[idx % len(hop_palette)]
            })

        if sampling_meta["applied"]:
            reduced = sum(len(item["datapoints"]) for item in series_data)
            reduced += sum(len(item["datapoints"]) for item in hop_series)
            sampling_meta["reducedTotalPoints"] = reduced

        pf_data = {
            "replicationLags": {
                "title": "Global Replication Lag Over Time",
                "yAxisLabel": "Lag Time (seconds)",
                "xAxisLabel": "Time",
                "series": series_data
            },
            "hopLags": {
                "title": "Per-Hop Replication Lags",
                "yAxisLabel": "Hop Lag Time (seconds)",
                "xAxisLabel": "Time",
                "series": hop_series
            },
            "metadata": {
                "totalServers": self._active_server_count or len(self.log_dirs),
                "configuredLogDirs": self.log_dirs,
                "processedLogDirs": self._processed_log_dirs,
                "skippedLogDirs": self._skipped_log_dirs,
                "analyzedLogs": len(self.csns),
                "totalUpdates": sum(len([
                    rec for key, rec in server_map.items()
                    if isinstance(rec, dict) and key != '__hop_lags__'
                ]) for server_map in self.csns.values()),
                "timeRange": {
                    "start": results['start-time'],
                    "end": results.get('end-time', 'current')
                },
                "timezone": str(self.tz),
                "sampling": sampling_meta
            }
        }

        # Write to JSON file
        try:
            with open(outfile, 'w') as f:
                json.dump(pf_data, f, indent=4, default=str)
        except Exception as e:
            raise IOError(f"Failed to write PatternFly JSON to {outfile}: {e}")
