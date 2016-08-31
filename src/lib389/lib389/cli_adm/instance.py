# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._constants import *

from lib389.tools import DirSrvTools
from lib389.instance.setup import SetupDs
from getpass import getpass
import os
import time
import sys

from lib389.instance.options import General2Base, Slapd2Base

def instance_list(inst, log, args):
    instances = inst.list(all=True)

    try:
        if len(instances) > 0:
            for instance in instances:
                log.info("instance: %s" % instance[CONF_SERVER_ID])
        else:
            log.info("No instances of Directory Server")
    except IOError as e:
        log.info(e)
        log.info("Perhaps you need to be a different user?")

def instance_start(inst, log, args):
    if inst.status() is False:
        inst.start()

def instance_stop(inst, log, args):
    if inst.status() is True:
        inst.stop()

def instance_status(inst, log, args):
    if inst.status() is True:
        log.info("Instance is running")
    else:
        log.info("Instance is not running")

def instance_create(inst, log, args):
    if not args.ack:
        sys.exit(0)
    else:
        log.info("""
 _________________________________________
/ This is not what you want! Press ctrl-c \\
\ now ...                                 /
 -----------------------------------------
      \\                   / \\  //\\
       \\    |\\___/|      /   \\//  \\\\
            /0  0  \\__  /    //  | \\ \\
           /     /  \\/_/    //   |  \\  \\
           @_^_@'/   \\/_   //    |   \\   \\
           //_^_/     \\/_ //     |    \\    \\
        ( //) |        \\///      |     \\     \\
      ( / /) _|_ /   )  //       |      \\     _\\
    ( // /) '/,_ _ _/  ( ; -.    |    _ _\\.-~        .-~~~^-.
  (( / / )) ,-{        _      `-.|.-~-.           .~         `.
 (( // / ))  '/\\      /                 ~-. _ .-~      .-~^-.  \\
 (( /// ))      `.   {            }                   /      \\  \\
  (( / ))     .----~-.\\        \\-'                 .~         \\  `. \\^-.
             ///.----..>        \\             _ -~             `.  ^-`  ^-_
               ///-._ _ _ _ _ _ _}^ - - - - ~                     ~-- ,.-~
                                                                  /.-~
        """)
    for i in range(1,6):
        log.info('%s ...' % (5 - int(i)))
        time.sleep(1)
    log.info('Launching ...')
    sd = SetupDs(args.verbose, args.dryrun, log)
    ### If args.file is not set, we need to interactively get answers!
    if sd.create_from_inf(args.file):
        # print("Sucessfully created instance")
        return True
    else:
        # print("Failed to create instance")
        return False

def instance_example(inst, log, args):
    print("""
; --- BEGIN COPYRIGHT BLOCK ---
; Copyright (C) 2015 Red Hat, Inc.
; All rights reserved.
;
; License: GPL (version 3 or any later version).
; See LICENSE for details.
; --- END COPYRIGHT BLOCK ---

; Author: firstyear at redhat.com

; This is a version 2 ds setup inf file.
; It is used by the python versions of setup-ds-*
; Most options map 1 to 1 to the original .inf file.
; However, there are some differences that I envision
; For example, note the split backend section.
; You should be able to create, one, many or no backends in an install

    """)
    g2b = General2Base(log)
    s2b = Slapd2Base(log)
    print(g2b.collect_help())
    print(s2b.collect_help())

def create_parser(subparsers):
    instance_parser = subparsers.add_parser('instance', help="Manager instances of Directory Server")

    subcommands = instance_parser.add_subparsers(help="action")

    list_parser = subcommands.add_parser('list', help="List installed instances of Directory Server")
    list_parser.set_defaults(func=instance_list)

    start_parser = subcommands.add_parser('start', help="Start an instance of Directory Server, if it is not currently running")
    # start_parser.add_argument('instance', nargs=1, help="The name of the instance to start.")
    start_parser.set_defaults(func=instance_start)

    stop_parser = subcommands.add_parser('stop', help="Stop an instance of Directory Server, if it is currently running")
    # stop_parser.add_argument('instance', nargs=1, help="The name of the instance to stop.")
    stop_parser.set_defaults(func=instance_stop)

    status_parser = subcommands.add_parser('status', help="Check running status of an instance of Directory Server")
    # status_parser.add_argument('instance', nargs=1, help="The name of the instance to check.")
    status_parser.set_defaults(func=instance_status)

    create_parser = subcommands.add_parser('create', help="Create an instance of Directory Server. Can be interactive or silent with an inf answer file")
    create_parser.add_argument('-n', '--dryrun', help="Validate system and configurations only. Do not alter the system.", action='store_true', default=False)
    create_parser.add_argument('-f', '--file', help="Inf file to use with prepared answers")
    create_parser.add_argument('--IsolemnlyswearthatIamuptonogood', dest="ack",
                        help="""You are here likely here by mistake! You want setup-ds.pl!
By setting this value you acknowledge and take responsibility for the fact this command is UNTESTED and NOT READY. You are ON YOUR OWN!
""",
                        action='store_true', default=False)
    create_parser.set_defaults(func=instance_create)
    create_parser.set_defaults(noinst=True)

    example_parser = subcommands.add_parser('example', help="Display an example ini answer file, with comments")
    example_parser.set_defaults(func=instance_example)
    create_parser.set_defaults(noinst=True)

