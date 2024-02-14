# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import subprocess
from lib389.cli_base import CustomHelpFormatter


def enable_cockpit(inst, log, args):
    """
    Enable Cockpit socket
    """

    ENABLE_CMD = ['sudo', 'systemctl', 'enable', '--now', 'cockpit.socket']
    try:
        subprocess.run(ENABLE_CMD)
    except subprocess.CalledProcessError as e:
        raise ValueError(f"Failed to enable cockpit socket.  Error {str(e)}")


def open_firewall(inst, log, args):
    """
    Open the firewall for Cockpit service
    """
    if not cockpit_present():
        raise ValueError("The 'cockpit' package is not installed on this system")

    OPEN_CMD = ['sudo', 'firewall-cmd', '--add-service=cockpit', '--permanent']
    if args.zone is not None:
        OPEN_CMD.append(f' --zone={args.zone}')
    try:
        subprocess.run(OPEN_CMD)
    except subprocess.CalledProcessError as e:
        raise ValueError(f'Failed to open firewall for service "cockpit"  Error {str(e)}')


def disable_cockpit(inst, log, args):
    """
    Disable Cockpit socket
    """
    if not cockpit_present():
        raise ValueError("The 'cockpit' package is not installed on this system")

    DISABLE_CMD = ['sudo', 'systemctl', 'disable', '--now', 'cockpit.socket']
    try:
        subprocess.run(DISABLE_CMD)
    except subprocess.CalledProcessError as e:
        raise ValueError(f"Failed to disable cockpit socket.  Error {str(e)}")


def close_firewall(inst, log, args):
    """
    Close firewall for Cockpit service
    """
    if not cockpit_present():
        raise ValueError("The 'cockpit' package is not installed on this system")

    CLOSE_CMD = ['sudo', 'firewall-cmd', '--remove-service=cockpit', '--permanent']
    try:
        subprocess.run(CLOSE_CMD)
    except subprocess.CalledProcessError as e:
        raise ValueError(f'Failed to remove "cockpit" service from firewall.  Error {str(e)}')


def create_parser(subparsers):
    cockpit_parser = subparsers.add_parser('cockpit', help="Enable the Cockpit interface/UI", formatter_class=CustomHelpFormatter)
    subcommands = cockpit_parser.add_subparsers(help="action")

    # Enable socket
    enable_parser = subcommands.add_parser('enable', help='Enable the Cockpit socket', formatter_class=CustomHelpFormatter)
    enable_parser.set_defaults(func=enable_cockpit)

    # Open firewall
    open_parser = subcommands.add_parser('open-firewall', help='Open the firewall for the "cockpit" service', formatter_class=CustomHelpFormatter)
    open_parser.add_argument('--zone', help="The firewall zone")
    open_parser.set_defaults(func=open_firewall)

    # Disable socket
    disable_parser = subcommands.add_parser('disable', help='Disable the Cockpit socket', formatter_class=CustomHelpFormatter)
    disable_parser.set_defaults(func=disable_cockpit)

    # Close firewall
    close_parser = subcommands.add_parser('close-firewall', help='Remove the "cockpit" service from the firewall settings', formatter_class=CustomHelpFormatter)
    close_parser.set_defaults(func=close_firewall)
