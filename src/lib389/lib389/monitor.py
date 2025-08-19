# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import copy
import psutil
from lib389._constants import *
from lib389._mapped_object import DSLdapObject
from lib389.utils import (ds_is_older)
from lib389.lint import DSDSLE0001
from lib389.backend import DatabaseConfig


class Monitor(DSLdapObject):
    """An object that helps reading of cn=monitor for server statistics.
        :param instance: An instance
        :type instance: lib389.DirSrv
        :param dn: not used
    """
    def __init__(self, instance, dn=None):
        super(Monitor, self).__init__(instance=instance)
        self._dn = DN_MONITOR

    def get_connections(self):
        """Get connection related attribute values for cn=monitor

        :returns: Values of connection, currentconnections,
                  totalconnections attributes of cn=monitor
        """
        connection = self.get_attr_vals_utf8('connection')
        currentconnections = self.get_attr_vals_utf8('currentconnections')
        totalconnections = self.get_attr_vals_utf8('totalconnections')
        return (connection, currentconnections, totalconnections)

    def get_version(self):
        """Get version attribute value for cn=monitor

        :returns: Value of version attribute of cn=monitor
        """
        version = self.get_attr_vals_utf8('connection')
        return version

    def get_threads(self):
        """Get thread related attributes value for cn=monitor

        :returns: Values of threads, currentconnectionsatmaxthreads, and
                  maxthreadsperconnhits attributes of cn=monitor
        """
        threads = self.get_attr_vals_utf8('threads')
        currentconnectionsatmaxthreads = self.get_attr_vals_utf8('currentconnectionsatmaxthreads')
        maxthreadsperconnhits = self.get_attr_vals_utf8('maxthreadsperconnhits')
        return (threads, currentconnectionsatmaxthreads, maxthreadsperconnhits)

    def get_backends(self):
        """Get backends related attributes value for cn=monitor

        :returns: Values of nbackends and backendmonitordn attributes of cn=monitor
        """
        nbackends = self.get_attr_vals_utf8('nbackends')
        backendmonitordn = self.get_attr_vals_utf8('backendmonitordn')
        return (nbackends, backendmonitordn)

    def get_operations(self):
        """Get operations related attributes value for cn=monitor

        :returns: Values of opsinitiated and opscompleted attributes of cn=monitor
        """
        opsinitiated = self.get_attr_vals_utf8('opsinitiated')
        opscompleted = self.get_attr_vals_utf8('opsinitiated')
        return (opsinitiated, opscompleted)

    def get_statistics(self):
        """Get statistics attributes value for cn=monitor

        :returns: Values of dtablesize, readwaiters, entriessent,
                  bytessent, currenttime, starttime attributes of cn=monitor
        """
        dtablesize = self.get_attr_vals_utf8('dtablesize')
        readwaiters = self.get_attr_vals_utf8('readwaiters')
        entriessent = self.get_attr_vals_utf8('entriessent')
        bytessent = self.get_attr_vals_utf8('bytessent')
        currenttime = self.get_attr_vals_utf8('currenttime')
        starttime = self.get_attr_vals_utf8('starttime')
        return (dtablesize, readwaiters, entriessent, bytessent, currenttime, starttime)

    def get_resource_stats(self):
        """
        Get CPU and memory stats
        """
        stats = {}
        try:
            pid = self._instance.get_pid()
        except Exception:
            pid = None
        total_mem = psutil.virtual_memory()[0]

        # Always include total system memory
        stats['total_mem'] = [str(total_mem)]

        # Process-specific stats - only if process is running (pid is not None)
        if pid is not None:
            try:
                p = psutil.Process(pid)
                memory_stats = p.memory_full_info()

                # Get memory & CPU stats
                stats['rss'] = [str(memory_stats[0])]
                stats['vms'] = [str(memory_stats[1])]
                stats['swap'] = [str(memory_stats[9])]
                stats['mem_rss_percent'] = [str(round(p.memory_percent("rss")))]
                stats['mem_vms_percent'] = [str(round(p.memory_percent("vms")))]
                stats['mem_swap_percent'] = [str(round(p.memory_percent("swap")))]
                stats['total_threads'] = [str(p.num_threads())]
                stats['cpu_usage'] = [str(round(p.cpu_percent(interval=0.1)))]
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                # Process exists in PID file but is not accessible or doesn't exist
                pid = None

        # If no valid PID, provide zero values for process stats
        if pid is None:
            stats['rss'] = ['0']
            stats['vms'] = ['0']
            stats['swap'] = ['0']
            stats['mem_rss_percent'] = ['0']
            stats['mem_vms_percent'] = ['0']
            stats['mem_swap_percent'] = ['0']
            stats['total_threads'] = ['0']
            stats['cpu_usage'] = ['0']
            stats['server_status'] = ['PID unavailable']
        else:
            stats['server_status'] = ['Server running']

        # Connections to DS
        if self._instance.port == "0":
            port = "ignore"
        else:
            port = str(self._instance.port)
        if self._instance.sslport == "0":
            sslport = "ignore"
        else:
            sslport = str(self._instance.sslport)

        conn_count = 0
        conn_established_count = 0
        conn_close_wait_count = 0
        conn_time_wait_count = 0
        conns = psutil.net_connections()
        for conn in conns:
            if len(conn[4]) > 0:
                conn_port = str(conn[4][1])
                if conn_port in (port, sslport):
                    if conn[5] == 'TIME_WAIT':
                        conn_time_wait_count += 1
                    if conn[5] == 'CLOSE_WAIT':
                        conn_close_wait_count += 1
                    if conn[5] == 'ESTABLISHED':
                        conn_established_count += 1
                    conn_count += 1

        stats['connection_count'] = [str(conn_count)]
        stats['connection_established_count'] = [str(conn_established_count)]
        stats['connection_close_wait_count'] = [str(conn_close_wait_count)]
        stats['connection_time_wait_count'] = [str(conn_time_wait_count)]

        return stats

    def get_status(self, just_resources=False, use_json=False, ):
        stats = self.get_resource_stats()
        if just_resources:
            return stats

        status = self.get_attrs_vals_utf8([
            'version',
            'threads',
            'connection',
            'currentconnections',
            'totalconnections',
            'currentconnectionsatmaxthreads',
            'maxthreadsperconnhits',
            'dtablesize',
            'readwaiters',
            'opsinitiated',
            'opscompleted',
            'entriessent',
            'bytessent',
            'currenttime',
            'starttime',
            'nbackends',
        ])
        status.update(stats)

        return status


class MonitorLDBM(DSLdapObject):
    """An object that helps reading the global database statistics.
        :param instance: An instance
        :type instance: lib389.DirSrv
        :param dn: not used
    """
    DB_KEYS = {
        DB_IMPL_BDB: [
            'dbcachehits', 'dbcachetries', 'dbcachehitratio',
            'dbcachepagein', 'dbcachepageout', 'dbcacheroevict',
            'dbcacherwevict'
        ],
        DB_IMPL_MDB: [
            'normalizeddncachetries', 'normalizeddncachehits',
            'normalizeddncachemisses', 'normalizeddncachehitratio',
            'normalizeddncacheevictions', 'currentnormalizeddncachesize',
            'maxnormalizeddncachesize', 'currentnormalizeddncachecount',
            'normalizeddncachethreadsize', 'normalizeddncachethreadslots'
        ]
    }
    DB_MONITOR_KEYS = {
        DB_IMPL_BDB: [
            'nsslapd-db-abort-rate', 'nsslapd-db-active-txns', 'nsslapd-db-cache-hit',
            'nsslapd-db-cache-try', 'nsslapd-db-cache-region-wait-rate',
            'nsslapd-db-cache-size-bytes', 'nsslapd-db-clean-pages', 'nsslapd-db-commit-rate',
            'nsslapd-db-deadlock-rate', 'nsslapd-db-dirty-pages', 'nsslapd-db-hash-buckets',
            'nsslapd-db-hash-elements-examine-rate', 'nsslapd-db-hash-search-rate',
            'nsslapd-db-lock-conflicts', 'nsslapd-db-lock-region-wait-rate',
            'nsslapd-db-lock-request-rate', 'nsslapd-db-lockers', 'nsslapd-db-configured-locks',
            'nsslapd-db-current-locks', 'nsslapd-db-max-locks', 'nsslapd-db-current-lock-objects',
            'nsslapd-db-max-lock-objects', 'nsslapd-db-log-bytes-since-checkpoint',
            'nsslapd-db-log-region-wait-rate', 'nsslapd-db-log-write-rate',
            'nsslapd-db-longest-chain-length', 'nsslapd-db-page-create-rate',
            'nsslapd-db-page-read-rate', 'nsslapd-db-page-ro-evict-rate',
            'nsslapd-db-page-rw-evict-rate', 'nsslapd-db-page-trickle-rate',
            'nsslapd-db-page-write-rate', 'nsslapd-db-pages-in-use',
            'nsslapd-db-txn-region-wait-rate', 'nsslapd-db-mp-pagesize'
        ],
        DB_IMPL_MDB: [
            'dbenvmapmaxsize', 'dbenvmapsize', 'dbenvlastpageno',
            'dbenvlasttxnid', 'dbenvmaxreaders', 'dbenvnumreaders',
            'dbenvnumdbis', 'waitingrwtxn', 'activerwtxn',
            'abortrwtxn', 'commitrwtxn', 'granttimerwtxn',
            'lifetimerwtxn', 'waitingrotxn', 'activerotxn',
            'abortrotxn', 'commitrotxn', 'granttimerotxn',
            'lifetimerotxn'
        ]
    }

    def __init__(self, instance, dn=None):
        super(MonitorLDBM, self).__init__(instance=instance)
        self._dn = DN_MONITOR_LDBM
        self._db_mon = MonitorDatabase(instance)
        self.inst_db_impl = self._instance.get_db_lib()
        self._backend_keys = list(self.DB_KEYS.get(self.inst_db_impl, []))
        self._db_mon_keys = list(self.DB_MONITOR_KEYS.get(self.inst_db_impl, []))

        if self.inst_db_impl == DB_IMPL_BDB and not ds_is_older("1.4.0", instance=instance):
            self._backend_keys.extend([
                'normalizeddncachetries', 'normalizeddncachehits',
                'normalizeddncachemisses', 'normalizeddncachehitratio',
                'normalizeddncacheevictions', 'currentnormalizeddncachesize',
                'maxnormalizeddncachesize', 'currentnormalizeddncachecount',
                'normalizeddncachethreadsize', 'normalizeddncachethreadslots'
            ])

    def get_status(self, use_json=False):
        ldbm_dict = self.get_attrs_vals_utf8(self._backend_keys)
        db_dict = self._db_mon.get_attrs_vals_utf8(self._db_mon_keys)
        return {**ldbm_dict, **db_dict}


class MonitorDatabase(DSLdapObject):
    """An object that helps reading the global libdb(bdb) or libdb(mdb) statistics.
        :param instance: An instance
        :type instance: lib389.DirSrv
        :param dn: not used
    """
    def __init__(self, instance, dn=None):
        super(MonitorDatabase, self).__init__(instance=instance)
        self._dn = DN_MONITOR_DATABASE
        self._backend_keys = None

    def __init2(self):
        # Determine the key when really accessing the object with get_status
        # because config attrbute and connectio are not yet set in DirSrv
        # when __init is called
        db_lib = self._instance.get_db_lib()
        if db_lib == "bdb":
            self._backend_keys = [
                'nsslapd-db-abort-rate',
                'nsslapd-db-active-txns',
                'nsslapd-db-cache-hit',
                'nsslapd-db-cache-try',
                'nsslapd-db-cache-region-wait-rate',
                'nsslapd-db-cache-size-bytes',
                'nsslapd-db-clean-pages',
                'nsslapd-db-commit-rate',
                'nsslapd-db-deadlock-rate',
                'nsslapd-db-dirty-pages',
                'nsslapd-db-hash-buckets',
                'nsslapd-db-hash-elements-examine-rate',
                'nsslapd-db-hash-search-rate',
                'nsslapd-db-lock-conflicts',
                'nsslapd-db-lock-region-wait-rate',
                'nsslapd-db-lock-request-rate',
                'nsslapd-db-lockers',
                'nsslapd-db-configured-locks',
                'nsslapd-db-current-locks',
                'nsslapd-db-max-locks',
                'nsslapd-db-current-lock-objects',
                'nsslapd-db-max-lock-objects',
                'nsslapd-db-log-bytes-since-checkpoint',
                'nsslapd-db-log-region-wait-rate',
                'nsslapd-db-log-write-rate',
                'nsslapd-db-longest-chain-length',
                'nsslapd-db-page-create-rate',
                'nsslapd-db-page-read-rate',
                'nsslapd-db-page-ro-evict-rate',
                'nsslapd-db-page-rw-evict-rate',
                'nsslapd-db-page-trickle-rate',
                'nsslapd-db-page-write-rate',
                'nsslapd-db-pages-in-use',
                'nsslapd-db-txn-region-wait-rate',
           ]
        if db_lib == "mdb":
            self._backend_keys = [
                'dbenvmapmaxsize',
                'dbenvmapsize',
                'dbenvlastpageno',
                'dbenvlasttxnid',
                'dbenvmaxreaders',
                'dbenvnumreaders',
                'dbenvnumdbis',
                'waitingrwtxn',
                'activerwtxn',
                'abortrwtxn',
                'commitrwtxn',
                'granttimerwtxn',
                'lifetimerwtxn',
                'waitingrotxn',
                'activerotxn',
                'abortrotxn',
                'commitrotxn',
                'granttimerotxn',
                'lifetimerotxn',
           ]


    def get_status(self, use_json=False):
        if not self._backend_keys:
            self.__init2()
        return self.get_attrs_vals_utf8(self._backend_keys)


class MonitorBackend(DSLdapObject):
    """
    This is initialised from Backend in backend.py to get the right basedn.
    """

    def __init__(self, instance, dn=None):
        super(MonitorBackend, self).__init__(instance=instance, dn=dn)
        self._backend_keys = None

    def __init2(self):
        # Determine the key when really accessing the object with get_status
        # because config attrbute and connectio are not yet set in DirSrv
        # when __init is called
        db_lib = self._instance.get_db_lib()
        if db_lib == "bdb":
            self._backend_keys = [
                'readonly',
                'entrycachehits',
                'entrycachetries',
                'entrycachehitratio',
                'currententrycachesize',
                'maxentrycachesize',
                'currententrycachecount',
                'maxentrycachecount',
                'dncachehits',
                'dncachetries',
                'dncachehitratio',
                'currentdncachesize',
                'maxdncachesize',
                'currentdncachecount',
                'maxdncachecount',
            ]
            if ds_is_older("1.4.0", instance=self._instance):
                self._backend_keys.extend([
                    'normalizeddncachetries',
                    'normalizeddncachehits',
                    'normalizeddncachemisses',
                    'normalizeddncachehitratio',
                    'currentnormalizeddncachesize',
                    'maxnormalizeddncachesize',
                    'currentnormalizeddncachecount'
                ])
        if db_lib == "mdb":
            self._backend_keys = [
                'readonly',
                'entrycachehits',
                'entrycachetries',
                'entrycachehitratio',
                'currententrycachesize',
                'maxentrycachesize',
                'currententrycachecount',
                'maxentrycachecount',
            ]


    def get_status(self, use_json=False):
        if not self._backend_keys:
            self.__init2()
        result = {}
        all_attrs = self.get_all_attrs_utf8()
        for attr in self._backend_keys:
            result[attr] = all_attrs[attr]

        # Now gather all the dbfile* attributes
        for attr, val in all_attrs.items():
            # For bdb
            if attr.startswith('dbfile'):
                result[attr] = val
            # For lmdb
            if attr.startswith('dbi'):
                result[attr] = val

        return result


class MonitorChaining(DSLdapObject):
    """
    """
    def __init__(self, instance, dn=None):
        super(MonitorChaining, self).__init__(instance=instance, dn=dn)
        self._chaining_keys = [
            'nsaddcount',
            'nsdeletecount',
            'nsmodifycount',
            'nsrenamecount',
            'nssearchbasecount',
            'nssearchonelevelcount',
            'nssearchsubtreecount',
            'nsabandoncount',
            'nsbindcount',
            'nsunbindcount',
            'nscomparecount',
            'nsopenopconnectioncount',
            'nsopenbindconnectioncount'
        ]
        self._protected = False

    def get_status(self, use_json=False):
        return self.get_attrs_vals_utf8(self._chaining_keys)


class MonitorSNMP(DSLdapObject):
    """
    """
    def __init__(self, instance, dn=None):
        super(MonitorSNMP, self).__init__(instance=instance, dn=dn)
        self._dn = DN_MONITOR_SNMP
        self._snmp_keys = [
            'anonymousbinds',
            'unauthbinds',
            'simpleauthbinds',
            'strongauthbinds',
            'bindsecurityerrors',
            'inops',
            'readops',
            'compareops',
            'addentryops',
            'removeentryops',
            'modifyentryops',
            'modifyrdnops',
            'listops',
            'searchops',
            'onelevelsearchops',
            'wholesubtreesearchops',
            'referrals',
            'chainings',
            'securityerrors',
            'errors',
            'connections',
            'connectionseq',
            'connectionsinmaxthreads',
            'connectionsmaxthreadscount',
            'bytesrecv',
            'bytessent',
            'entriesreturned',
            'referralsreturned',
            'copyentries',
            'cacheentries',
            'cachehits',
        ]

    def get_status(self, use_json=False):
        return self.get_attrs_vals_utf8(self._snmp_keys)


class MonitorDiskSpace(DSLdapObject):
    """A class for representing "cn=disk space,cn=monitor" entry"""

    def __init__(self, instance, dn=None):
        super(MonitorDiskSpace, self).__init__(instance=instance, dn=dn)
        self._dn = "cn=disk space,cn=monitor"

    @classmethod
    def lint_uid(cls):
        return 'monitor-disk-space'

    def _lint_disk_space(self):
        partitions = self.get_attr_vals_utf8_l("dsDisk")
        for partition in partitions:
            parts = partition.split()
            percent = parts[4].split('=')[1].strip('"')
            if int(percent) >= 90:
                # this partition is over 90% full, not good
                report = copy.deepcopy(DSDSLE0001)
                report['detail'] = report['detail'].replace('PARTITION', parts[0].split('=')[1].strip('"'))
                report['fix'] = report['fix'].replace('YOUR_INSTANCE', self._instance.serverid)
                report['check'] = f'monitor-disk-space:disk_space'
                yield report

    def get_disks(self):
        """Get an information about partitions which contains a Directory Server data"""

        return self.get_attr_vals_utf8_l("dsDisk")
