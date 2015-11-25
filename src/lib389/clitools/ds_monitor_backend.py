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
from argparse import ArgumentParser


class MonitorTool(CliTool):
    def monitor_backend_list(self):
        try:
            self.populate_instance_dict(self.args.instance)
            self.connect()
            # This is pretty rough, it just dumps the objects
            for monitor in self.ds.monitor.backend(self.args.backend):
                print(monitor)
        finally:
            self.disconnect()

if __name__ == '__main__':
    # Do some arg parse stuff
    # You can always add a child parser here too ...
    parser = ArgumentParser(parents=[clitools_parser])
    parser.add_argument('--backend', '-b', help='The name of the backend to ' +
                        ' retrieve monitoring information for.', required=True)
    args = parser.parse_args()
    monitortool = MonitorTool(args)
    monitortool.monitor_backend_list()
