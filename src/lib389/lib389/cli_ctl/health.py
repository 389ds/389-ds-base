# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import re
from lib389._mapped_object import DSLdapObjects
from lib389._mapped_object_lint import DSLint
from lib389.cli_base import connect_instance, disconnect_instance
from lib389.cli_base.dsrc import dsrc_to_ldap, dsrc_arg_concat
from lib389.backend import Backends
from lib389.config import Encryption, Config
from lib389.monitor import MonitorDiskSpace
from lib389.replica import Replica, Changelog5
from lib389.nss_ssl import NssSsl
from lib389.dseldif import FSChecks, DSEldif
from lib389.dirsrv_log import DirsrvAccessLog
from lib389 import lint
from lib389 import plugins
from lib389._constants import DSRC_HOME
from functools import partial
from typing import Iterable


# These get single instances and check them.
CHECK_OBJECTS = [
    Config,
    Backends,
    Encryption,
    FSChecks,
    plugins.ReferentialIntegrityPlugin,
    MonitorDiskSpace,
    Replica,
    Changelog5,
    DSEldif,
    NssSsl,
    DirsrvAccessLog,
]


def _format_check_output(log, result, idx):
    log.info(f"\n\n[{idx}] DS Lint Error: {result['dsle']}")
    log.info("-" * 80)
    log.info(f"Severity: {result['severity']}")
    if 'check' in result:
        log.info(f"Check: {result['check']}")
    log.info("Affects:")
    for item in result['items']:
        log.info(f" -- {item}")
    log.info("\nDetails:")
    log.info("-----------")
    log.info(result['detail'])
    log.info("\nResolution:")
    log.info("-----------")
    log.info(result['fix'])


def _list_targets(inst):
    for c in CHECK_OBJECTS:
        o = c(inst)
        yield o.lint_uid(), o


def _list_errors(log):
    for r in map(partial(getattr, lint),
                 filter(partial(re.match, r'^DS'),
                        dir(lint))):
        log.info(f"{r['dsle']} :: {r['description']}")


def _list_checks(inst, specs: Iterable[str]):
    o_uids = dict(_list_targets(inst))
    for s in specs:
        wanted, rest = DSLint._dslint_parse_spec(s)
        if wanted == '*':
            raise ValueError('Unexpected spec selector asterisk')

        if wanted in o_uids:
            for l in o_uids[wanted].lint_list(rest):
                yield o_uids[wanted], l
        else:
            raise ValueError('No such object specifier')


def _print_checks(inst, log, specs: Iterable[str]) -> None:
    for o, s in _list_checks(inst, specs):
        print(f'{o.lint_uid()}:{s[0]}')
        log.info(f'{o.lint_uid()}:{s[0]}')

def _run(inst, log, args, checks):
    if not args.json:
        log.info("Beginning lint report, this could take a while ...")

    report = []
    for o, s in checks:
        if not args.json:
            log.info(f"Checking {o.lint_uid()}:{s[0]} ...")
        try:
            report += o.lint(s[0]) or []
        except:
            continue

    if not args.json:
        log.info("Healthcheck complete.")

    count = len(report)
    if count == 0:
        if not args.json:
            log.info("No issues found.")
        else:
            log.info(json.dumps(report, indent=4))
    else:
        plural = ""
        if count > 1:
            plural = "s"
        if not args.json:
            log.info("{} Issue{} found!  Generating report ...".format(count, plural))
            idx = 1
            for item in report:
                _format_check_output(log, item, idx)
                idx += 1
            log.info('\n\n===== End Of Report ({} Issue{} found) ====='.format(count, plural))
        else:
            log.info(json.dumps(report, indent=4))


def health_check_run(inst, log, args):
    """Connect to the local server using LDAPI, and perform various health checks
    """

    if args.list_errors:
        _list_errors(log)
        return

    # update the args for connect_instance()
    args.basedn = None
    args.binddn = None
    args.bindpw = None
    args.starttls = None
    args.pwdfile = None
    args.prompt = False
    dsrc_inst = dsrc_to_ldap(DSRC_HOME, args.instance, log.getChild('dsrc'))
    dsrc_inst = dsrc_arg_concat(args, dsrc_inst)
    try:
        inst = connect_instance(dsrc_inst=dsrc_inst, verbose=args.verbose, args=args)
    except Exception as e:
        raise ValueError('Failed to connect to Directory Server instance: ' + str(e))

    checks = args.check or dict(_list_targets(inst)).keys()

    if args.list_checks or args.dry_run:
        _print_checks(inst, log, checks)
        return

    _run(inst, log, args, _list_checks(inst, checks))

    disconnect_instance(inst)


def create_parser(subparsers):
    run_healthcheck_parser = subparsers.add_parser('healthcheck', help=
        "Run a healthcheck report on a local Directory Server instance. This "
        "is a safe and read-only operation.  Do not attempt to run this on a "
        "remote Directory Server as this tool needs access to local resources, "
        "otherwise the report may be inaccurate.")
    run_healthcheck_parser.set_defaults(func=health_check_run)
    run_healthcheck_parser.add_argument('--list-checks', action='store_true', help='List of known checks')
    run_healthcheck_parser.add_argument('--list-errors', action='store_true', help='List of known error codes')
    run_healthcheck_parser.add_argument('--dry-run', action='store_true', help='Do not execute the actual check, only list what would be done')
    run_healthcheck_parser.add_argument('--check', nargs='+', default=None,
                                        help='Areas to check. These can be obtained by --list-checks. Every element on the left of the colon (:)'
                                             ' may be replaced by an asterisk if multiple options on the right are available.')
