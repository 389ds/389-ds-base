# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# Probably need some helpers for argparse and instance selection

from argparse import ArgumentParser
from getpass import getpass
from lib389 import DirSrv
from lib389._constants import *


class CliTool(object):
    def __init__(self, args=None):
        if args is not None:
            self.args = args
            self.ds = DirSrv(verbose=args.verbose)
        else:
            self.ds = DirSrv()

    def populate_instance_dict(self, instance):
        insts = self.ds.list(serverid=instance)
        if len(insts) != 1:
            # Raise an exception here?
            self.inst = None
            raise ValueError("No such instance %s" % instance)
        else:
            self.inst = insts[0]

    def get_rootdn_pass(self):
        if self.args.binddn is None:
            binddn = self.inst[SER_ROOT_DN]
        else:
            binddn = self.args.binddn
        # There is a dict get key thing somewhere ...
        if self.inst.get(SER_ROOT_PW, None) is None:
            prompt_txt = ('Enter password for %s on instance %s: ' %
                          (binddn,
                           self.inst[SER_SERVERID_PROP]))
            self.inst[SER_ROOT_PW] = getpass(prompt_txt)
            print("")
        return

    def connect(self):
        # Can we attempt the autobind?
        # This should be a bit cleaner perhaps
        # Perhaps an argument to the cli?
        self.ds.allocate(self.inst)
        if not self.ds.can_autobind():
            self.get_rootdn_pass()
            self.ds.allocate(self.inst)
        self.ds.open()

    def disconnect(self):
        # Is there a ds unbind / disconnect?
        self.ds.close()


def _clitools_parser():
    parser = ArgumentParser()
    parser.add_argument('-Z', '--instance',
                        help='The name of the DS instance to connect to and ' +
                             'work upon.', required=True)
    parser.add_argument('-D', '--binddn',
                        help='The bind dn to use for operations. Defaults to ' +
                             'rooddn', default=None)
    parser.add_argument('-v', '--verbose', help="Display verbose debug information", action='store_true', default=False)
    return parser

clitools_parser = _clitools_parser()

if __name__ == '__main__':
    args = clitools_parser.parse_args()
