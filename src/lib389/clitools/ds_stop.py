#!/usr/bin/python

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# from clitools import clitools_parser, get_instance_dict, get_rootdn_pass
from clitools import CliTool, clitools_parser
# from lib389 import DirSrv
from lib389._constants import *
from lib389.tools import DirSrvTools


class StartTool(CliTool):
    def start(self):
        try:
            self.populate_instance_dict(self.args.instance)
            self.ds.allocate(self.inst)

            DirSrvTools.serverCmd(self.ds, "stop", True)
        finally:
            pass
            # self.disconnect()

if __name__ == '__main__':
    # Do some arg parse stuff
    # You can always add a child parser here too ...
    args = clitools_parser.parse_args()
    tool = StartTool(args)
    tool.start()
