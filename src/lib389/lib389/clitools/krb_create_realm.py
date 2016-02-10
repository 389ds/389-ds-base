#!/usr/bin/python

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.clitools import CliTool, clitools_parser
from lib389._constants import *
from lib389.mit_krb5 import MitKrb5
from argparse import ArgumentParser


class MitKrb5Tool(CliTool):
    def mit_krb5_realm_create(self):
        try:
            krb = MitKrb5(realm=args.realm, warnings=True)
            krb.create_realm()
        finally:
            pass

if __name__ == '__main__':
    # Do some arg parse stuff
    # You can always add a child parser here too ...
    parser = clitools_parser.add_argument_group('krb', 'kerberos options')
    parser.add_argument('--realm', '-r',
                        help='The name of the realm to create',
                        required=True)
    args = clitools_parser.parse_args()
    mittool = MitKrb5Tool(args)
    mittool.mit_krb5_realm_create()
