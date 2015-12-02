#!/usr/bin/env python

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
        self.ds = DirSrv()
        if args is not None:
            self.args = args

    def populate_instance_dict(self, instance):
        insts = self.ds.list(serverid=instance)
        if len(insts) != 1:
            # Raise an exception here?
            self.inst = None
        else:
            self.inst = insts[0]

    def get_rootdn_pass(self):
        # There is a dict get key thing somewhere ...
        if self.inst.get(SER_ROOT_PW, None) is None:
            prompt_txt = ('Enter password for %s on instance %s: ' %
                          (self.inst[SER_ROOT_DN],
                           self.inst[SER_SERVERID_PROP]))
            self.inst[SER_ROOT_PW] = getpass(prompt_txt)
        return

    def connect(self):
        self.get_rootdn_pass()
        self.ds.allocate(self.inst)
        self.ds.open()

    def disconnect(self):
        # Is there a ds unbind / disconnect?
        self.ds.close()


def _clitools_parser():
    parser = ArgumentParser(add_help=False)
    parser.add_argument('--instance',
                        '-i',
                        help='The name of the DS instance to connect to and ' +
                             'work upon.')
    return parser

clitools_parser = _clitools_parser()
