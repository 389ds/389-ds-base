# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import ContentSyncPlugin
from lib389.cli_conf import add_generic_plugin_parsers

def create_parser(subparsers):
    contentsync_parser = subparsers.add_parser('contentsync', help='Manage and configure Content Sync Plugin (aka syncrepl)')
    subcommands = contentsync_parser.add_subparsers(help='action')
    add_generic_plugin_parsers(subcommands, ContentSyncPlugin)
