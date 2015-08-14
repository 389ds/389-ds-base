#!/usr/bin/python

#from clitools import clitools_parser, get_instance_dict, get_rootdn_pass
from clitools import CliTool, clitools_parser
#from lib389 import DirSrv
from lib389._constants import *
import ldap

class SchemaTool(CliTool):
    def schema_attributetype_list(self):
        try:
            self.populate_instance_dict(self.args.instance)
            self.connect()
            for attributetype in self.ds.schema.get_attributetypes():
                print(attributetype)
        finally:
            self.disconnect()

if __name__ == '__main__':
    # Do some arg parse stuff
    ## You can always add a child parser here too ...
    args = clitools_parser.parse_args()
    schematool = SchemaTool(args)
    schematool.schema_attributetype_list()
