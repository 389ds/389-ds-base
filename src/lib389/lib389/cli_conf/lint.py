# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import argparse

from lib389.backend import Backend, Backends

LINT_OBJECTS = [
    Backends
]

def _format_lint_output(log, result):
    log.info("==== DS Lint Error: %s ====" % result['dsle'])
    log.info(" Severity: %s " % result['severity'])
    log.info(" Affects:")
    for item in result['items']:
        log.info(" -- %s" % item)
    log.info(" Details:")
    log.info(result['detail'])
    log.info(" Resolution:")
    log.info(result['fix'])

def lint_run(inst, basedn, log, args):
    log.info("Beginning lint report, this could take a while ...")
    report = []
    for lo in LINT_OBJECTS:
        log.info("Checking %s ..." % lo.__name__)
        lo_inst = lo(inst)
        for clo in lo_inst.list():
            result = clo.lint()
            if result is not None:
                report += result
    log.info("Lint complete!")
    for item in report:
        _format_lint_output(log, item)

def create_parser(subparsers):
    lint_parser = subparsers.add_parser('lint', help="Check for configuration issues in your Directory Server instance.")

    subcommands = lint_parser.add_subparsers(help="action")

    run_lint_parser = subcommands.add_parser('run', help="Run a lint report on your Directory Server instance. This is a safe, Read Only operation.")
    run_lint_parser.set_defaults(func=lint_run)

