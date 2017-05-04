# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

##################################################
#
# Usage: when using gdb, these commands are automatically loaded with ns-slapd.
#
# else, from inside gdb run "source /path/to/ns-slapd-gdb.py"
#

import itertools
import re

import gdb
from gdb.FrameDecorator import FrameDecorator

class DSAccessLog (gdb.Command):
    """Display the Directory Server access log."""
    def __init__ (self):
        super (DSAccessLog, self).__init__ ("ds-access-log", gdb.COMMAND_DATA)

    def invoke (self, arg, from_tty):
        print('===== BEGIN ACCESS LOG =====')
        gdb.execute('set print elements 0')
        o = gdb.execute('p loginfo.log_access_buffer.top', to_string=True)
        for l in o.split('\\n'):
            print(l)
        print('===== END ACCESS LOG =====')

class DSBacktrace(gdb.Command):
    """Display a filtered backtrace"""
    def __init__ (self):
        super (DSBacktrace, self).__init__ ("ds-backtrace", gdb.COMMAND_DATA)

    def _parse_thread_state(self, lwpid, tid):
        # Stash the BT output
        o = gdb.execute('bt', to_string=True)
        # Get to the top of the frame stack.
        gdb.newest_frame()
        # Now work our way down.
        backtrace = []
        cur_frame = gdb.selected_frame()
        while cur_frame is not None:
            backtrace.append(cur_frame.name())
            cur_frame = cur_frame.older()
        # Dicts can't use lists as keys, so we need to squash this to a string.
        s_backtrace = ' '.join(backtrace)
        # Have we seen this trace before?
        if s_backtrace not in self._stack_maps:
            # Make it!
            self._stack_maps[s_backtrace] = []
        # Add it to the set.
        self._stack_maps[s_backtrace].append({'gtid': tid, 'lwpid': lwpid, 'bt': o}  )

    def invoke(self, arg, from_tty):
        print('===== BEGIN ACTIVE THREADS =====')

        # Reset our thread maps.
        self._stack_maps = {}

        inferiors = gdb.inferiors()
        for inferior in inferiors:
            threads = inferior.threads()
            for thread in threads:
                (tpid, lwpid, tid) = thread.ptid
                gtid = thread.num
                thread.switch()
                self._parse_thread_state(lwpid, gtid)

        # print (self._stack_maps)
        for m in self._stack_maps:
            # Take a copy of the bt
            o = self._stack_maps[m][0]['bt']
            # Print every thread and id.
            for t in self._stack_maps[m]:
                print("Thread %s (LWP %s))" % (t['gtid'], t['lwpid']))
            # Print the trace
            print(o)

        print('===== END ACTIVE THREADS =====')

class DSIdleFilterDecorator(FrameDecorator):
    def __init__(self, fobj):
        super(DSIdleFilterDecorator, self).__init__(fobj)

    def function(self):
        frame = self.inferior_frame()
        name = str(frame.name())

        if name == 'connection_wait_for_new_work' or name == 'work_q_wait':
            name = "[IDLE THREAD] " + name

        return name

class DSIdleFilter():
    def __init__(self):
        self.name = "DSIdleFilter"
        self.priority = 100
        self.enabled = True
        # Register this frame filter with the global frame_filters
        # dictionary.
        gdb.frame_filters[self.name] = self

    def filter(self, frame_iter):
        # Just return the iterator.
        if hasattr(itertools, 'imap'):
            frame_iter = imap(DSIdleFilterDecorator, frame_iter)
        else:
            frame_iter = map(DSIdleFilterDecorator, frame_iter)
        return frame_iter

DSAccessLog()
DSBacktrace()
DSIdleFilter()

