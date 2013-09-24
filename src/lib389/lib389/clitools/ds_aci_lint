#!/usr/bin/python

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# from clitools import clitools_parser, get_instance_dict, get_rootdn_pass
from lib389.clitools import CliTool, clitools_parser
# from lib389 import DirSrv
from lib389._constants import *
from argparse import ArgumentParser


class AciLintTool(CliTool):
    def aci_lint(self):
        try:
            self.populate_instance_dict(self.args.instance)
            self.connect()
            # This is pretty rough, it just dumps the objects
            (result, details) = status = self.ds.aci.lint(self.args.suffix)
            print(self.ds.aci.format_lint(details))
            if result:
                print("PASS")
            else:
                print("FAIL")
        finally:
            self.disconnect()

if __name__ == '__main__':
    # Do some arg parse stuff
    # You can always add a child parser here too ...
    parser = clitools_parser.add_argument_group('aci', 'aci linting options')
    parser.add_argument('-b', '--suffix', help='The name of the suffix to ' +
                        ' validate acis.', required=True)
    args = clitools_parser.parse_args()
    acilinttool = AciLintTool(args)
    acilinttool.aci_lint()
