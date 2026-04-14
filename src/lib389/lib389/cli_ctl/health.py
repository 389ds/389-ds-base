# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import json
import re
from lib389._mapped_object_lint import DSLint
from lib389.cli_base import connect_instance, disconnect_instance
from lib389.cli_base.dsrc import dsrc_to_ldap, dsrc_arg_concat
from lib389.backend import Backends
from lib389.config import Encryption, Config
from lib389.monitor import MonitorDiskSpace
from lib389.replica import Replica
from lib389.nss_ssl import NssSsl
from lib389.dseldif import FSChecks, DSEldif
from lib389.dirsrv_log import DirsrvAccessLog
from lib389.tunables import Tunables
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
    plugins.MemberOfPlugin,
    MonitorDiskSpace,
    Replica,
    DSEldif,
    NssSsl,
    DirsrvAccessLog,
    Tunables,
]

# Checks that need a running server or LDAP search of database content (see healthcheck.md).
_OFFLINE_SKIP_SPECS_EXACT = frozenset({
    'replication:agmts_status',
    'replication:conflicts',
    'replication:no_ruv',
})


def _offline_skip_spec(spec: str) -> bool:
    if spec in _OFFLINE_SKIP_SPECS_EXACT:
        return True
    if spec.endswith(':search'):
        return True
    return False


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
    if specs is None:
        yield []
        return
    o_uids = dict(_list_targets(inst))
    for s in specs:
        wanted, rest = DSLint._dslint_parse_spec(s)
        if wanted == '*':
            raise ValueError('Unexpected spec selector asterisk')

        if wanted in o_uids:
            for l in o_uids[wanted].lint_list(rest):
                yield o_uids[wanted], l
        else:
            raise ValueError('No such object specifier: ' + wanted)


def _print_checks(inst, log, specs: Iterable[str]) -> None:
    for o, s in _list_checks(inst, specs):
        log.info(f'{o.lint_uid()}:{s[0]}')


def _run(inst, log, args, checks, exclude_checks, offline=False):
    if not args.json:
        log.info("Beginning lint report, this could take a while ...")

    report = []
    skipped = []
    excludes = []
    for _, skip in exclude_checks:
        excludes.append(skip[0])

    for o, s in checks:
        if s[0] in excludes:
            continue

        if not args.json:
            log.info(f"Checking {o.lint_uid()}:{s[0]} ...")

        if offline and _offline_skip_spec(s[0]):
            reason = 'requires running directory server or LDAP-accessible database content'
            skipped.append({'check': s[0], 'reason': reason})
            if not args.json:
                log.info(f" - skipped {s[0]} - requires running server: {reason}")
            continue

        try:
            report += o.lint(s[0]) or []
        except ValueError as e:
            if offline and 'not ONLINE' in str(e):
                reason = 'instance is not connected (offline); check needs LDAP'
                skipped.append({'check': s[0], 'reason': reason})
                if not args.json:
                    log.info(f" - skipped {s[0]} - requires running server: {reason}")
            else:
                raise
        except Exception:
            continue

    if not args.json:
        log.info("Healthcheck complete.")

    count = len(report)
    if count == 0:
        if not args.json:
            log.info("No issues found.")
        else:
            _emit_json(log, report, skipped, [], args)
    else:
        plural = ""
        if count > 1:
            plural = "s"
        if not args.json:
            log.info(f"{count} Issue{plural} found!  Generating report ...")
            idx = 1
            for item in report:
                _format_check_output(log, item, idx)
                idx += 1
            log.info(f'\n\n===== End Of Report ({count} Issue{plural} found) =====')
        else:
            _emit_json(log, report, skipped, [], args)


def _emit_json(log, report, skipped, host_errors, args):
    """Emit JSON: list-only for backward compatibility, or wrapped object if extra sections."""
    if skipped or host_errors:
        payload = {'issues': report}
        if skipped:
            payload['skipped_requires_server'] = skipped
        if host_errors:
            payload['host_errors'] = host_errors
        log.info(json.dumps(payload, indent=4))
    else:
        log.info(json.dumps(report, indent=4))


def health_check_run(inst, log, args):
    """Connect to the local server using LDAPI, and perform various health checks.

    If the server is stopped, ``connect_instance`` fails with ``SERVER_DOWN``; we keep
    the pre-allocated ``inst`` from ``dsctl`` and run checks that do not require LDAP,
    skipping others with explicit messages. Mode is fixed for the duration of the run.
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
    offline = False
    try:
        inst = connect_instance(dsrc_inst=dsrc_inst, verbose=args.verbose,
                                args=args)
    except ldap.SERVER_DOWN:
        log.info('Directory Server instance is down — running offline-capable checks only.')
        offline = True
    except Exception as e:
        raise ValueError('Failed to connect to Directory Server instance: ' +
                         str(e)) from e

    checks = args.check or dict(_list_targets(inst)).keys()
    exclude_checks = args.exclude_check
    if args.list_checks or args.dry_run:
        _print_checks(inst, log, checks)
        return

    _run(inst, log, args, _list_checks(inst, checks),
         _list_checks(inst, exclude_checks), offline=offline)

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
    run_healthcheck_parser.add_argument('--exclude-check', nargs='+', default=[],
                                        help='Areas to skip. These can be obtained by --list-checks. Every element on the left of the colon (:)'
                                             ' may be replaced by an asterisk if multiple options on the right are available.')
