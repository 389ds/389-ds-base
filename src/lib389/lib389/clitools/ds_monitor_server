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


class MonitorTool(CliTool):
    def monitor_server_list(self):
        try:
            self.populate_instance_dict(self.args.instance)
            self.connect()
            # This is pretty rough, it just dumps the objects
            status = self.ds.monitor.server()
            print ("dn: %s" % status.pop('dn'))
            for monitor in status:
                print("%s: %s" % (monitor, status[monitor][0]))
        finally:
            self.disconnect()

if __name__ == '__main__':
    # Do some arg parse stuff
    # You can always add a child parser here too ...
    args = clitools_parser.parse_args()
    monitortool = MonitorTool(args)
    monitortool.monitor_server_list()
