# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from lib389.cli_base import connect_instance, disconnect_instance
from lib389.cli_base.dsrc import dsrc_to_ldap, dsrc_arg_concat
from lib389.backend import Backends
from lib389.config import Encryption, Config
from lib389.monitor import MonitorDiskSpace
from lib389.replica import Replica, Changelog5
from lib389.nss_ssl import NssSsl
from lib389.dseldif import FSChecks, DSEldif
from lib389 import plugins
from lib389._constants import DSRC_HOME

# These get all instances, then check them all.
CHECK_MANY_OBJECTS = [
    Backends,
]

# These get single instances and check them.
CHECK_OBJECTS = [
    Config,
    Encryption,
    FSChecks,
    plugins.ReferentialIntegrityPlugin,
    MonitorDiskSpace,
    Replica,
    Changelog5,
    DSEldif,
    NssSsl,
]


def _format_check_output(log, result, idx):
    log.info("\n\n[{}] DS Lint Error: {}".format(idx, result['dsle']))
    log.info("-" * 80)
    log.info("Severity: %s " % result['severity'])
    log.info("Affects:")
    for item in result['items']:
        log.info(" -- %s" % item)
    log.info("\nDetails:")
    log.info('-----------')
    log.info(result['detail'])
    log.info("\nResolution:")
    log.info('-----------')
    log.info(result['fix'])


def health_check_run(inst, log, args):
    """Connect to the local server using LDAPI, and perform various health checks
    """

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

    if not args.json:
        log.info("Beginning lint report, this could take a while ...")
    report = []
    for lo in CHECK_MANY_OBJECTS:
        if not args.json:
            log.info("Checking %s ..." % lo.__name__)
        lo_inst = lo(inst)
        for clo in lo_inst.list():
            result = clo.lint()
            if result is not None:
                report += result
    for lo in CHECK_OBJECTS:
        if not args.json:
            log.info("Checking %s ..." % lo.__name__)
        lo_inst = lo(inst)
        result = lo_inst.lint()
        if result is not None:
            report += result
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

    disconnect_instance(inst)


def create_parser(subparsers):
    run_healthcheck_parser = subparsers.add_parser('healthcheck', help=
        "Run a healthcheck report on a local Directory Server instance. This "
        "is a safe and read-only operation.  Do not attempt to run this on a "
        "remote Directory Server as this tool needs access to local resources, "
        "otherwise the report may be inaccurate.")
    run_healthcheck_parser.set_defaults(func=health_check_run)

