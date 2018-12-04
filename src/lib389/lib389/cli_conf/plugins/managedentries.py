# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import ManagedEntriesPlugin
from lib389.cli_conf import add_generic_plugin_parsers


def create_parser(subparsers):
    managedentries_parser = subparsers.add_parser('managedentries', help='Manage and configure Managed Entries plugin')
    subcommands = managedentries_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, ManagedEntriesPlugin)
