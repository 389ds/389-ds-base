# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016-2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import WhoamiPlugin
from lib389.cli_conf.plugin import add_generic_plugin_parsers


def create_parser(subparsers):
    whoami_parser = subparsers.add_parser('whoami', help='Manage and configure whoami plugin')
    subcommands = whoami_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, WhoamiPlugin)
