#!/usr/bin/python

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._constants import *
from clitools import CliTool


class ListTool(CliTool):
    def list_instances(self):
        # Remember, the prefix can be set with the os environment
        try:
            instances = self.ds.list(all=True)
            print('Instances on this system:')
            for instance in instances:
                print(instance[CONF_SERVER_ID])
        except IOError as e:
            print(e)
            print("Perhaps you need to be a different user?")

if __name__ == '__main__':
    listtool = ListTool()
    listtool.list_instances()
