#!/usr/bin/python

#from clitools import clitools_parser, get_instance_dict, get_rootdn_pass
from clitools import CliTool, clitools_parser
#from lib389 import DirSrv
from lib389._constants import *
import ldap

class MonitorTool(CliTool):
    def monitor_server_list(self):
        try:
            self.populate_instance_dict(self.args.instance)
            self.connect()
            # This is pretty rough, it just dumps the objects
            for monitor in self.ds.monitor.server():
                print(monitor)
        finally:
            self.disconnect()

if __name__ == '__main__':
    # Do some arg parse stuff
    ## You can always add a child parser here too ...
    args = clitools_parser.parse_args()
    monitortool = MonitorTool(args)
    monitortool.monitor_server_list()
