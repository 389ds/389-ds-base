# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.idm.directorymanager import DirectoryManager
from lib389.cli_base import _get_arg, CustomHelpFormatter


def password_change(inst, basedn, log, args):
    # Due to an issue, we can't use extended op, so we have to
    # submit the password directly to the field.
    password = _get_arg(args.password, msg="Enter new directory manager password", hidden=True, confirm=True)
    dm = DirectoryManager(inst)
    dm.change_password(password)


def create_parsers(subparsers):
    directory_manager_parser = subparsers.add_parser('directory_manager', help="Manage the Directory Manager account", formatter_class=CustomHelpFormatter)

    subcommands = directory_manager_parser.add_subparsers(help='action')

    password_change_parser = subcommands.add_parser('password_change', help="Changes the password of the Directory Manager account", formatter_class=CustomHelpFormatter)
    password_change_parser.set_defaults(func=password_change)
    # This is to put in a dummy attr that args can work with. We do this
    # because the actual test case will over-ride it, but it prevents
    # a user putting the pw on the cli.
    password_change_parser.set_defaults(password=None)


