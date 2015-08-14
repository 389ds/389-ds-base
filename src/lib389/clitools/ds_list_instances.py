#!/usr/bin/python

from lib389._constants import *
from clitools import CliTool

class ListTool(CliTool):
    def list_instances(self):
        # Remember, the prefix can be set with the os environment
        instances = self.ds.list(all=True)
        print('Instances on this system:')
        for instance in instances:
            print(instance[CONF_SERVER_ID])

if __name__ == '__main__':
    listtool = ListTool()
    listtool.list_instances()


