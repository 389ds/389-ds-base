# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._constants import INSTALL_LATEST_CONFIG
from lib389.configurations import get_sample_entries
from lib389.cli_base import CustomHelpFormatter

def initialise(inst, basedn, log, args):
    sample_entries = get_sample_entries(args.version)
    assert basedn is not None
    s_ent = sample_entries(inst, basedn)
    s_ent.apply()

def create_parser(subparsers):
    initialise_parser = subparsers.add_parser('initialise', help="Initialise a backend with domain information and sample entries", formatter_class=CustomHelpFormatter)
    initialise_parser.set_defaults(func=initialise)
    initialise_parser.add_argument('--version', help="The version of entries to create.", default=INSTALL_LATEST_CONFIG)

