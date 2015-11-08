#!/usr/bin/python

#from clitools import clitools_parser, get_instance_dict, get_rootdn_pass
from clitools import CliTool, clitools_parser
#from lib389 import DirSrv
from lib389._constants import *
from lib389.tools import DirSrvTools
import ldap

class StartTool(CliTool):
    def start(self):
        try:
            self.populate_instance_dict(self.args.instance)
            self.ds.allocate(self.inst)

            DirSrvTools.serverCmd(self.ds, "start", True)
        finally:
            pass
            #self.disconnect()

if __name__ == '__main__':
    # Do some arg parse stuff
    ## You can always add a child parser here too ...
    args = clitools_parser.parse_args()
    tool = StartTool(args)
    tool.start()
