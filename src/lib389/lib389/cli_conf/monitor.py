# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

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
            # Inejct a new line for now ... see https://pagure.io/389-ds-base/issue/50189
            log.info("")


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


def create_parser(subparsers):
    monitor_parser = subparsers.add_parser('monitor', help="Monitor the state of the instance")
    subcommands = monitor_parser.add_subparsers(help='action')

    server_parser = subcommands.add_parser('server', help="Monitor the server statistics, connections and operations")
    server_parser.set_defaults(func=monitor)

    ldbm_parser = subcommands.add_parser('ldbm', help="Monitor the ldbm statistics, such as dbcache")
    ldbm_parser.set_defaults(func=ldbm_monitor)

    backend_parser = subcommands.add_parser('backend', help="Monitor the behaviour of a backend database")
    backend_parser.add_argument('backend', nargs='?', help="Optional name of the backend to monitor")
    backend_parser.set_defaults(func=backend_monitor)

    snmp_parser = subcommands.add_parser('snmp', help="Monitor the SNMP statistics")
    snmp_parser.set_defaults(func=snmp_monitor)

    chaining_parser = subcommands.add_parser('chaining', help="Monitor database chaining statistics")
    chaining_parser.add_argument('backend', nargs='?', help="Optional name of the chaining backend to monitor")
    chaining_parser.set_defaults(func=chaining_monitor)

    disk_parser = subcommands.add_parser('disk', help="Disk space statistics.  All values are in bytes")
    disk_parser.set_defaults(func=disk_monitor)
