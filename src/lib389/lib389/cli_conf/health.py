# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import argparse

from lib389.backend import Backend, Backends
from lib389.config import Encryption, Config
from lib389 import plugins

# These get all instances, then check them all.
CHECK_MANY_OBJECTS = [
    Backends,
]

# These get single instances and check them.
CHECK_OBJECTS = [
    Config,
    Encryption,
    plugins.ReferentialIntegrityPlugin
]

def _format_check_output(log, result):
    log.info("==== DS Lint Error: %s ====" % result['dsle'])
    log.info(" Severity: %s " % result['severity'])
    log.info(" Affects:")
    for item in result['items']:
        log.info(" -- %s" % item)
    log.info(" Details:")
    log.info(result['detail'])
    log.info(" Resolution:")
    log.info(result['fix'])

def health_check_run(inst, basedn, log, args):
    log.info("Beginning lint report, this could take a while ...")
    report = []
    for lo in CHECK_MANY_OBJECTS:
        log.info("Checking %s ..." % lo.__name__)
        lo_inst = lo(inst)
        for clo in lo_inst.list():
            result = clo.lint()
            if result is not None:
                report += result
    for lo in CHECK_OBJECTS:
        log.info("Checking %s ..." % lo.__name__)
        lo_inst = lo(inst)
        result = lo_inst.lint()
        if result is not None:
            report += result
    log.info("Healthcheck complete!")
    for item in report:
        _format_check_output(log, item)

def create_parser(subparsers):
    run_healthcheck_parser = subparsers.add_parser('healthcheck', help="Run a healthcheck report on your Directory Server instance. This is a safe, read only operation.")
    run_healthcheck_parser.set_defaults(func=health_check_run)

