# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import AccountPolicyPlugin
from lib389.cli_conf import add_generic_plugin_parsers


def create_parser(subparsers):
    accountpolicy_parser = subparsers.add_parser('accountpolicy', help='Manage and configure Account Policy plugin')
    subcommands = accountpolicy_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, AccountPolicyPlugin)
