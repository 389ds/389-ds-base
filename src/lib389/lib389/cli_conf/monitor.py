# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import datetime
import json
from lib389.monitor import (Monitor, MonitorLDBM, MonitorSNMP, MonitorDiskSpace)
from lib389.chaining import (ChainingLinks)
from lib389.backend import Backends
from lib389.utils import convert_bytes
from lib389.cli_base import _format_status


def monitor(inst, basedn, log, args):
    monitor = Monitor(inst)
    _format_status(log, monitor, args.json)


def backend_monitor(inst, basedn, log, args):
    bes = Backends(inst)
    if args.backend:
        be = bes.get(args.backend)
        be_monitor = be.get_monitor()
        _format_status(log, be_monitor, args.json)
    else:
        for be in bes.list():
            be_monitor = be.get_monitor()
            _format_status(log, be_monitor, args.json)


def ldbm_monitor(inst, basedn, log, args):
    ldbm_monitor = MonitorLDBM(inst)
    _format_status(log, ldbm_monitor, args.json)


def snmp_monitor(inst, basedn, log, args):
    snmp_monitor = MonitorSNMP(inst)
    _format_status(log, snmp_monitor, args.json)


def chaining_monitor(inst, basedn, log, args):
    links = ChainingLinks(inst)
    if args.backend:
        link = links.get(args.backend)
        link_monitor = link.get_monitor()
        _format_status(log, link_monitor, args.json)
    else:
        for link in links.list():
            link_monitor = link.get_monitor()
            _format_status(log, link_monitor, args.json)
            # Inject a new line for now ... see https://pagure.io/389-ds-base/issue/50189
            log.info("")


def disk_monitor(inst, basedn, log, args):
    disk_space_mon = MonitorDiskSpace(inst)
    disks = disk_space_mon.get_disks()
    disk_list = []
    for disk in disks:
        # partition="/" size="52576092160" used="25305038848" available="27271053312" use%="48"
        parts = disk.split()
        mount = parts[0].split('=')[1].strip('"')
        disk_size = convert_bytes(parts[1].split('=')[1].strip('"'))
        used = convert_bytes(parts[2].split('=')[1].strip('"'))
        avail = convert_bytes(parts[3].split('=')[1].strip('"'))
        percent = parts[4].split('=')[1].strip('"')
        if args.json:
            disk_list.append({
                'mount': mount,
                'size': disk_size,
                'used': used,
                'avail': avail,
                'percent': percent
            })
        else:
            log.info("Partition: " + mount)
            log.info("Size: " + disk_size)
            log.info("Used Space: " + used)
            log.info("Available Space: " + avail)
            log.info("Percentage Used: " + percent + "%\n")

    if args.json:
        log.info(json.dumps({"type": "list", "items": disk_list}, indent=4))


def db_monitor(inst, basedn, log, args):
    """Report on all the database statistics
    """
    ldbm_monitor = MonitorLDBM(inst)
    backends_obj = Backends(inst)
    backend_objs = []
    args_backends = None

    # Gather all the backends
    if args.backends is not None:
        # This is a space separated list, it could be backend names or suffixes
        args_backends = args.backends.lower().split()

    for be in backends_obj.list():
        if args_backends is not None:
            for arg_be in args_backends:
                if '=' in arg_be:
                    # We have a suffix
                    if arg_be == be.get_suffix():
                        backend_objs.append(be)
                        break
                else:
                    # We have a backend name
                    if arg_be == be.rdn.lower():
                        backend_objs.append(be)
                        break
        else:
            # Get all the backends
            backend_objs.append(be)

    if args_backends is not None and len(backend_objs) == 0:
        raise ValueError("Could not find any backends from the provided list: {}".format(args.backends))

    # Gather the global DB stats
    report_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    ldbm_mon = ldbm_monitor.get_status()
    dbcachesize = int(ldbm_mon['nsslapd-db-cache-size-bytes'][0])
    if 'nsslapd-db-page-size' in ldbm_mon:
        pagesize = int(ldbm_mon['nsslapd-db-page-size'][0])
    else:
        pagesize = 8 * 1024  # Taken from DBLAYER_PAGESIZE
    dbhitratio = ldbm_mon['dbcachehitratio'][0]
    dbcachepagein = ldbm_mon['dbcachepagein'][0]
    dbcachepageout = ldbm_mon['dbcachepageout'][0]
    dbroevict = ldbm_mon['nsslapd-db-page-ro-evict-rate'][0]
    dbpages = int(ldbm_mon['nsslapd-db-pages-in-use'][0])
    dbcachefree = int(dbcachesize - (pagesize * dbpages))
    dbcachefreeratio = dbcachefree/dbcachesize
    ndnratio = ldbm_mon['normalizeddncachehitratio'][0]
    ndncursize = int(ldbm_mon['currentnormalizeddncachesize'][0])
    ndnmaxsize = int(ldbm_mon['maxnormalizeddncachesize'][0])
    ndncount = ldbm_mon['currentnormalizeddncachecount'][0]
    ndnevictions = ldbm_mon['normalizeddncacheevictions'][0]
    if ndncursize > ndnmaxsize:
        ndnfree = 0
        ndnfreeratio = 0
    else:
        ndnfree = ndnmaxsize - ndncursize
        ndnfreeratio = "{:.1f}".format(ndnfree / ndnmaxsize * 100)

    # Build global cache stats
    result = {
        'date': report_time,
        'dbcache': {
            'hit_ratio': dbhitratio,
            'free': convert_bytes(str(dbcachefree)),
            'free_percentage': "{:.1f}".format(dbcachefreeratio * 100),
            'roevicts': dbroevict,
            'pagein': dbcachepagein,
            'pageout': dbcachepageout
        },
        'ndncache': {
            'hit_ratio': ndnratio,
            'free': convert_bytes(str(ndnfree)),
            'free_percentage': ndnfreeratio,
            'count': ndncount,
            'evictions': ndnevictions
        },
        'backends': {},
    }

    # Build the backend results
    for be in backend_objs:
        be_name = be.rdn
        be_suffix = be.get_suffix()
        monitor = be.get_monitor()
        all_attrs = monitor.get_status()

        # Process entry cache stats
        entcur = int(all_attrs['currententrycachesize'][0])
        entmax = int(all_attrs['maxentrycachesize'][0])
        entcnt = int(all_attrs['currententrycachecount'][0])
        entratio = all_attrs['entrycachehitratio'][0]
        entfree = entmax - entcur
        entfreep = "{:.1f}".format(entfree / entmax * 100)
        if entcnt == 0:
            entsize = 0
        else:
            entsize = int(entcur / entcnt)

        # Process DN cache stats
        dncur = int(all_attrs['currentdncachesize'][0])
        dnmax = int(all_attrs['maxdncachesize'][0])
        dncnt = int(all_attrs['currentdncachecount'][0])
        dnratio = all_attrs['dncachehitratio'][0]
        dnfree = dnmax - dncur
        dnfreep = "{:.1f}".format(dnfree / dnmax * 100)
        if dncnt == 0:
            dnsize = 0
        else:
            dnsize = int(dncur / dncnt)

        # Build the backend result
        result['backends'][be_name] = {
            'suffix': be_suffix,
            'entry_cache_count': all_attrs['currententrycachecount'][0],
            'entry_cache_free': convert_bytes(str(entfree)),
            'entry_cache_free_percentage': entfreep,
            'entry_cache_size': convert_bytes(str(entsize)),
            'entry_cache_hit_ratio': entratio,
            'dn_cache_count': all_attrs['currentdncachecount'][0],
            'dn_cache_free': convert_bytes(str(dnfree)),
            'dn_cache_free_percentage': dnfreep,
            'dn_cache_size': convert_bytes(str(dnsize)),
            'dn_cache_hit_ratio': dnratio,
            'indexes': []
        }

        # Process indexes if requested
        if args.indexes:
            index = {}
            index_name = ''
            for attr, val in all_attrs.items():
                if attr.startswith('dbfile'):
                    if attr.startswith("dbfilename-"):
                        if index_name != '':
                            # Update backend index list
                            result['backends'][be_name]['indexes'].append(index)
                        index_name = val[0].split('/')[1]
                        index = {'name': index_name}
                    elif attr.startswith('dbfilecachehit-'):
                        index['cachehit'] = val[0]
                    elif attr.startswith('dbfilecachemiss-'):
                        index['cachemiss'] = val[0]
                    elif attr.startswith('dbfilepagein-'):
                        index['pagein'] = val[0]
                    elif attr.startswith('dbfilepageout-'):
                        index['pageout'] = val[0]
            if index_name != '':
                # Update backend index list
                result['backends'][be_name]['indexes'].append(index)

    # Return the report
    if args.json:
        log.info(json.dumps(result, indent=4))
    else:
        log.info("DB Monitor Report: " + result['date'])
        log.info("--------------------------------------------------------")
        log.info("Database Cache:")
        log.info(" - Cache Hit Ratio:     {}%".format(result['dbcache']['hit_ratio']))
        log.info(" - Free Space:          {}".format(result['dbcache']['free']))
        log.info(" - Free Percentage:     {}%".format(result['dbcache']['free_percentage']))
        log.info(" - RO Page Drops:       {}".format(result['dbcache']['roevicts']))
        log.info(" - Pages In:            {}".format(result['dbcache']['pagein']))
        log.info(" - Pages Out:           {}".format(result['dbcache']['pageout']))
        log.info("")
        log.info("Normalized DN Cache:")
        log.info(" - Cache Hit Ratio:     {}%".format(result['ndncache']['hit_ratio']))
        log.info(" - Free Space:          {}".format(result['ndncache']['free']))
        log.info(" - Free Percentage:     {}%".format(result['ndncache']['free_percentage']))
        log.info(" - DN Count:            {}".format(result['ndncache']['count']))
        log.info(" - Evictions:           {}".format(result['ndncache']['evictions']))
        log.info("")
        log.info("Backends:")
        for be_name, attr_dict in result['backends'].items():
            log.info(f"  - {attr_dict['suffix']} ({be_name}):")
            log.info("    - Entry Cache Hit Ratio:        {}%".format(attr_dict['entry_cache_hit_ratio']))
            log.info("    - Entry Cache Count:            {}".format(attr_dict['entry_cache_count']))
            log.info("    - Entry Cache Free Space:       {}".format(attr_dict['entry_cache_free']))
            log.info("    - Entry Cache Free Percentage:  {}%".format(attr_dict['entry_cache_free_percentage']))
            log.info("    - Entry Cache Average Size:     {}".format(attr_dict['entry_cache_size']))
            log.info("    - DN Cache Hit Ratio:           {}%".format(attr_dict['dn_cache_hit_ratio']))
            log.info("    - DN Cache Count:               {}".format(attr_dict['dn_cache_count']))
            log.info("    - DN Cache Free Space:          {}".format(attr_dict['dn_cache_free']))
            log.info("    - DN Cache Free Percentage:     {}%".format(attr_dict['dn_cache_free_percentage']))
            log.info("    - DN Cache Average Size:        {}".format(attr_dict['dn_cache_size']))
            if len(result['backends'][be_name]['indexes']) > 0:
                log.info("    - Indexes:")
                for index in result['backends'][be_name]['indexes']:
                    log.info("      - Index:      {}".format(index['name']))
                    log.info("      - Cache Hit:  {}".format(index['cachehit']))
                    log.info("      - Cache Miss: {}".format(index['cachemiss']))
                    log.info("      - Page In:    {}".format(index['pagein']))
                    log.info("      - Page Out:   {}".format(index['pageout']))
                    log.info("")
            log.info("")


def create_parser(subparsers):
    monitor_parser = subparsers.add_parser('monitor', help="Monitor the state of the instance")
    subcommands = monitor_parser.add_subparsers(help='action')

    server_parser = subcommands.add_parser('server', help="Monitor the server statistics, connections and operations")
    server_parser.set_defaults(func=monitor)

    dbmon_parser = subcommands.add_parser('dbmon', help="Monitor the all the database statistics in a single report")
    dbmon_parser.set_defaults(func=db_monitor)
    dbmon_parser.add_argument('-b', '--backends', help="List of space separated backends to monitor.  Default is all backends.")
    dbmon_parser.add_argument('-x', '--indexes', action='store_true', default=False, help="Show index stats for each backend")

    ldbm_parser = subcommands.add_parser('ldbm', help="Monitor the ldbm statistics, such as dbcache")
    ldbm_parser.set_defaults(func=ldbm_monitor)

    backend_parser = subcommands.add_parser('backend', help="Monitor the behavior of a backend database")
    backend_parser.add_argument('backend', nargs='?', help="Optional name of the backend to monitor")
    backend_parser.set_defaults(func=backend_monitor)

    snmp_parser = subcommands.add_parser('snmp', help="Monitor the SNMP statistics")
    snmp_parser.set_defaults(func=snmp_monitor)

    chaining_parser = subcommands.add_parser('chaining', help="Monitor database chaining statistics")
    chaining_parser.add_argument('backend', nargs='?', help="Optional name of the chaining backend to monitor")
    chaining_parser.set_defaults(func=chaining_monitor)

    disk_parser = subcommands.add_parser('disk', help="Disk space statistics.  All values are in bytes")
    disk_parser.set_defaults(func=disk_monitor)
