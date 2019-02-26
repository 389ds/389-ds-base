# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import json
import time
import sys
from getpass import getpass
from lib389 import DirSrv
from lib389.tools import DirSrvTools
from lib389.instance.setup import SetupDs
from lib389.utils import get_instance_list
from lib389.instance.remove import remove_ds_instance
from lib389._constants import CONF_SERVER_ID

from lib389.instance.options import General2Base, Slapd2Base, Backend2Base


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


def instance_restart(inst, log, args):
    inst.restart(post_open=False)
    log.info('Instance "{}" has been restarted'.format(inst.serverid))


def instance_start(inst, log, args):
    inst.start(post_open=False)
    log.info('Instance "{}" has been started'.format(inst.serverid))


def instance_stop(inst, log, args):
    inst.stop()
    log.info('Instance "{}" has been stopped'.format(inst.serverid))


def instance_status(inst, log, args):
    if args.json:
        print(json.dumps({"type": "result", "running": inst.status()}))
        return

    if inst.status() is True:
        log.info('Instance "{}" is running'.format(inst.serverid))
    else:
        log.info('Instance "{}" is not running'.format(inst.serverid))


def instance_create_interactive(inst, log, args):
    sd = SetupDs(args.verbose, False, log, False)
    return sd.create_from_cli()


def instance_create(inst, log, args):
    sd = SetupDs(args.verbose, args.dryrun, log)
    if sd.create_from_inf(args.file):
        # print("Successfully created instance")
        return True
    else:
        # print("Failed to create instance")
        return False


def instance_example(inst, log, args):
    header = """
; 
; This is a version 2 ds setup inf file.
; It is used by the python versions of setup-ds-*
; Most options map 1 to 1 to the original .inf file.
; However, there are some differences that I envision
; For example, note the split backend section.
; You should be able to create, one, many or no backends in an install
;
; The special value {instance_name} is substituted at installation time.
;
; By default, all configuration parameters in this file are commented out.
; To use an INF file with dscreate, you must at least set the parameters
; flagged with [REQUIRED].

"""

    g2b = General2Base(log)
    s2b = Slapd2Base(log)
    b2b = Backend2Base(log, "backend-userroot")

    if args.template_file:
        try:
            # Create file and set permissions
            template_file = open(args.template_file, 'w')
            template_file.close()
            os.chmod(args.template_file, 0o600)

            # Open file and populate it
            template_file = open(args.template_file, 'w')
            template_file.write(header)
            template_file.write(g2b.collect_help())
            template_file.write(s2b.collect_help())
            template_file.write(b2b.collect_help())
            template_file.close()
        except OSError as e:
            log.error("Failed trying to create template file ({}), error: {}".format(args.template_file, str(e)))
            return False
    else:
        print(header)
        print(g2b.collect_help())
        print(s2b.collect_help())
        print(b2b.collect_help())
    return True


def instance_remove_all(log, args):
    """Remove all instances - clean sweep!
    """

    inst_names = get_instance_list(args.remove_all)
    if len(inst_names) > 0:
        answer = input("Are you sure you want to remove all the Directory Server instances?  Enter \"Yes\" to continue: ")
        if answer != 'Yes':
            print("Aborted removal of all instances")
            return

        # Do it!
        list_inst = DirSrv(verbose=args.verbose)
        insts = list_inst.list(all=True, serverid=inst_names[0])
        for inst in insts:
            remove_inst = DirSrv(verbose=args.verbose)
            remove_inst.allocate(inst)
            try:
                log.info("Removing instance: slapd-" + str(remove_inst.serverid))
                remove_ds_instance(remove_inst)
            except Exception as e:
                log.fatal('Failed to remove all instances: ' + str(e))
                sys.exit(1)
        log.info('All instances have been successfully removed')
    else:
        print("No instances to remove")


def instance_remove(inst, log, args):
    if not args.ack:
        # Some day do some type of dry-run validation?
        log.info("""Not removing: if you are sure, add --do-it""")
        return True
    else:
        log.info("""
About to remove instance (%s)!
If this is not what you want, press ctrl-c now ...
        """ % inst.serverid)
        for i in range(1, 6):
            log.info('%s ...' % (6 - int(i)))
            time.sleep(1)
        log.info('Removing instance ...')
        try:
            remove_ds_instance(inst)
            log.info('Completed instance removal')
        except:
            log.fatal('Instance removal failed')
            return False


def create_parser(subcommands):
    # list_parser = subcommands.add_parser('list', help="List installed instances of Directory Server")
    # list_parser.set_defaults(func=instance_list)
    # list_parser.set_defaults(noinst=True)

    restart_parser = subcommands.add_parser('restart', help="Restart an instance of Directory Server, if it is running: else start it.")
    restart_parser.set_defaults(func=instance_restart)

    start_parser = subcommands.add_parser('start', help="Start an instance of Directory Server, if it is not currently running")
    start_parser.set_defaults(func=instance_start)

    stop_parser = subcommands.add_parser('stop', help="Stop an instance of Directory Server, if it is currently running")
    stop_parser.set_defaults(func=instance_stop)

    status_parser = subcommands.add_parser('status', help="Check running status of an instance of Directory Server")
    status_parser.set_defaults(func=instance_status)

    remove_parser = subcommands.add_parser('remove', help="Destroy an instance of Directory Server, and remove all data.")
    remove_parser.set_defaults(func=instance_remove)
    remove_parser.add_argument('--do-it', dest="ack", help="By default we do a dry run. This actually initiates the removal of the instance.",
                               action='store_true', default=False)
