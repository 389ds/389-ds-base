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
import ldap
from argparse import ArgumentParser


class SchemaTool(CliTool):
    def schema_attributetype_query(self):
        try:
            self.populate_instance_dict(self.args.instance)
            self.connect()
            attributetype, must, may = \
                self.ds.schema.query_attributetype(self.args.attributetype)
            print(attributetype)
            print("")
            print('MUST')
            for objectclass in must:
                print(objectclass)
            print("")
            print('MAY')
            for objectclass in may:
                print(objectclass)
        finally:
            self.disconnect()

if __name__ == '__main__':
    # Do some arg parse stuff
    # You can always add a child parser here too ...
    parser = clitools_parser.add_argument_group('schema', 'schema options')
    parser.add_argument('--attributetype',
                        '-a',
                        help='The name of the attribute type to query',
                        required=True)
    args = clitools_parser.parse_args()
    schematool = SchemaTool(args)
    schematool.schema_attributetype_query()
