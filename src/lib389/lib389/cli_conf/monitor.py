# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.monitor import Monitor, MonitorLDBM
from lib389.backend import Backends

from lib389.utils import ensure_str

def _format_status(log, mtype, json=False):
    if json:
        print(mtype.get_status_json())
    else:
        status_dict = mtype.get_status()
        log.info('dn: ' + mtype._dn)
        for k, v in list(status_dict.items()):
            # For each value in the multivalue attr
            for vi in v:
                log.info('{}: {}'.format(k, vi))


def monitor(inst, basedn, log, args):
    monitor = Monitor(inst)
    _format_status(log, monitor, args.json)


def backend_monitor(inst, basedn, log, args):
    bes = Backends(inst)
    if args.backend:
        be = bes.get(args.backend)
        be_monitor = be.get_monitor()
        be_monitor.get_status()
    else:
        for be in bes.list():
            be_monitor = be.get_monitor()
            be_monitor.get_status()
            # Inejct a new line for now ... see https://pagure.io/389-ds-base/issue/50189
            print("")


def ldbm_monitor(inst, basedn, log, args):
    ldbm_monitor = MonitorLDBM(inst)
    ldbm_monitor.get_status()


def create_parser(subparsers):
    monitor_parser = subparsers.add_parser('monitor', help="Monitor the state of the instance")

    subcommands = monitor_parser.add_subparsers(help='action')

    server_parser = subcommands.add_parser('server', help="Monitor the server statistics, connectinos and operations")
    server_parser.set_defaults(func=monitor)

    ldbm_parser = subcommands.add_parser('ldbm', help="Monitor the ldbm statistics, such as dbcache")
    ldbm_parser.set_defaults(func=ldbm_monitor)

    backend_parser = subcommands.add_parser('backend', help="Monitor the behaviour of a backend database")
    backend_parser.add_argument('backend', nargs='?', help="Optional name of the backend to monitor")
    backend_parser.set_defaults(func=backend_monitor)

