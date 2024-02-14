# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import pwd
import grp
import json
import time
from shutil import copytree, copyfile
from lib389 import DirSrv
from lib389.cli_base import CustomHelpFormatter
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
        print(json.dumps({"type": "result", "running": inst.status()}, indent=4))
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
            template_file.write(g2b.collect_help(advanced=args.advanced))
            template_file.write(s2b.collect_help(advanced=args.advanced))
            template_file.write(b2b.collect_help(advanced=args.advanced))
            template_file.close()
        except OSError as e:
            log.error("Failed trying to create template file ({}), error: {}".format(args.template_file, str(e)))
            return False
    else:
        print(header)
        print(g2b.collect_help(advanced=args.advanced))
        print(s2b.collect_help(advanced=args.advanced))
        print(b2b.collect_help(advanced=args.advanced))
    return True


def prepare_ds_root(inst, log, args):
    def get_dest(path):
        # Compute the destination path from original path
        if path.startswith('/usr/'):
            return f'{args.root_dir}/{path[5:]}'
        return f'{args.root_dir}/{path}'

    def copy_and_substitute(file, sub_list):
        # Copy .inf file and apply all sub_list modifiers to each line
        #  Supported modifier are:
        #    (WORD, pattern, value) ==> Replace first occurance of pattern by value
        #    (LINE, pattern, value) ==> Replace line statring with pattern
        #                                 by: pattern = value
        log.debug(f'Update {get_dest(file)} from {file}')
        with open(file, 'rt') as fin:
            with open(get_dest(file), 'wt') as fout:
                for line in fin:
                    for k, p, v in sub_list:
                        if k == 'WORD':
                            line = line.replace(p, v, 1)
                        if k == 'LINE':
                            if line.startswith(p):
                                line = f'{p} = {v}\n'
                    fout.write(line)

    def copy_and_skip_entry(file, pattern_list):
        # copy ldif file skipping entries containing a pattern from pattern_list
        class Entry:
            # Helper class to handle the ldif entries
            def __init__(self):
                self.skip = False
                self.entry = ""

            def add_line(self, line):
                self.entry += line
                for p in pattern_list:
                    if p in line:
                        self.skip = True

            def write(self, fout):
                if not self.skip:
                    fout.write(self.entry)

        log.debug(f'Update {get_dest(file)} from {file}')
        with open(file, 'rt') as fin:
            with open(get_dest(file), 'wt') as fout:
                entry = Entry()
                for line in fin:
                    entry.add_line(line)
                    if line == "\n":
                        entry.write(fout)
                        entry = Entry()
                entry.write(fout)

    uid = os.getuid()
    if uid == 0:
        raise ValueError("ds-root subcommand should not be run by root user.")
    user = pwd.getpwuid(uid).pw_name
    group = grp.getgrgid(os.getgid()).gr_name

    # Perform consistency checks then create the wrappers
    if args.bin_dir:
        found = False
        for path in os.environ['PATH'].split(':'):
            if path.startswith('.'):
                continue
            if path == args.bin_dir:
                found = True
                break
            if os.path.exists(f'{path}/dsconf'):
                log.error(f'bin_dir argument should be before {path} in PATH')
                return False
        if not found:
            log.error(f'bin_dir argument should be in PATH')
            return False
        os.makedirs(args.bin_dir, 0o755, True)
        for wrapper in ['dsconf',  'dscreate',  'dsctl',  'dsidm']:
            log.debug(f'Creating {args.bin_dir}/{wrapper} wrapper')
            with open(f'{args.bin_dir}/{wrapper}', 'wt') as f:
                f.write('#!/bin/sh\n')
                f.write(f'export PATH="{args.root_dir}:$PATH"\n')
                f.write(f'export PREFIX="{args.root_dir}"\n')
                f.write(f'export INSTALL_PREFIX="{args.root_dir}"\n')
                p = "${@}"
                f.write(f'exec /usr/sbin/{wrapper} "{p}"\n')
            os.chmod(f'{args.bin_dir}/{wrapper}', 0o755)
    os.makedirs(args.root_dir, 0o700, True)
    # Copy subtrees
    for dir in ['/usr/share/dirsrv/', '/etc/dirsrv/config',  '/etc/dirsrv/schema', ]:
        destdir = get_dest(dir)
        log.debug(f'Copying {dir} into {destdir}')
        os.makedirs(destdir, 0o755, True)
        copytree(dir, destdir, dirs_exist_ok=True)
    # Create empty directories
    for dir in ['/tmp', ]:
        destdir = get_dest(dir)
        log.debug(f'Creating directory {destdir}')
        os.makedirs(destdir, 0o755, True)
    # Copy binaries
    for bin in ['/usr/sbin/ns-slapd']:
        destbin = get_dest(bin)
        log.debug(f'Copying {bin} into {destbin}')
        os.makedirs(os.path.dirname(destbin), 0o755, True)
        copyfile(bin, destbin)
    # And finally, update the template files
    for dse in ('/usr/share/dirsrv/data/template-dse-minimal.ldif', '/usr/share/dirsrv/data/template-dse.ldif'):
        copy_and_skip_entry(dse, ('libpwdchan-plugin',))
    # Use PO and PF to escape { } in formatted strings
    PO = '{'
    PF = '}'
    copy_and_substitute('/usr/share/dirsrv/inf/defaults.inf', (
            ('WORD', ' /', f' {os.path.abspath(args.root_dir)}/'),
            ('LINE', 'with_selinux', 'no'),
            ('LINE', 'with_systemd', '0'),
            ('LINE', 'user', user),
            ('LINE', 'group', group),
            ('LINE', 'prefix', args.root_dir),
            ('LINE', 'bin_dir', '/usr/bin'),
            ('LINE', 'sbin_dir', '/usr/sbin'),
            ('LINE', 'lib_dir', '/usr/lib64'),
            ('LINE', 'data_dir', f'{args.root_dir}/share'),
            ('LINE', 'inst_dir', f'{args.root_dir}/lib64/slapd-{PO}instance_name{PF}'),
            ('LINE', 'plugin_dir', '/usr/lib64/dirsrv/plugins'),
            ('LINE', 'system_schema_dir', f'{args.root_dir}/share/dirsrv/schema'),
       ))

    return True


def instance_remove_all(log, args):
    """Remove all instances - clean sweep!
    """

    inst_names = get_instance_list()
    if len(inst_names) > 0:
        log.info("""
About to remove all Directory Server instances!
If this is not what you want, press ctrl-c now ...
        """)
        for i in range(1, 6):
            log.info('%s ...' % (6 - int(i)))
            time.sleep(1)

        # Do it!
        list_inst = DirSrv(verbose=args.verbose)
        insts = list_inst.list(all=True, serverid=inst_names[0])
        no_problems = True
        for inst in insts:
            remove_inst = DirSrv(verbose=args.verbose)
            remove_inst.allocate(inst)
            try:
                log.info("Removing instance: slapd-" + str(remove_inst.serverid))
                remove_ds_instance(remove_inst)
            except Exception as e:
                log.error(f'Failed to remove slapd-{remove_inst.serverid} - Error: {str(e)}')
                no_problems = False
        if no_problems:
            log.info('All instances have been successfully removed')
        else:
            log.info('Some instances were not removed')
    else:
        print("No instances to remove")


def instance_remove(inst, log, args):
    if not args.ack:
        # Some day do some type of dry-run validation?
        log.info("""Not removing: if you are sure, add --do-it""")
        return True
    else:
        log.info('Removing instance ...')
        try:
            remove_ds_instance(inst)
            log.info('Completed instance removal')
        except:
            log.fatal('Instance removal failed')
            return False


def create_parser(subcommands):
    # list_parser = subcommands.add_parser('list', help="List installed instances of Directory Server", formatter_class=CustomHelpFormatter)
    # list_parser.set_defaults(func=instance_list)
    # list_parser.set_defaults(noinst=True)

    restart_parser = subcommands.add_parser('restart', help="Restart an instance of Directory Server, if it is running: else start it.", formatter_class=CustomHelpFormatter)
    restart_parser.set_defaults(func=instance_restart)

    start_parser = subcommands.add_parser('start', help="Start an instance of Directory Server, if it is not currently running", formatter_class=CustomHelpFormatter)
    start_parser.set_defaults(func=instance_start)

    stop_parser = subcommands.add_parser('stop', help="Stop an instance of Directory Server, if it is currently running", formatter_class=CustomHelpFormatter)
    stop_parser.set_defaults(func=instance_stop)

    status_parser = subcommands.add_parser('status', help="Check running status of an instance of Directory Server", formatter_class=CustomHelpFormatter)
    status_parser.set_defaults(func=instance_status)

    remove_parser = subcommands.add_parser('remove', help="Destroy an instance of Directory Server, and remove all data.", formatter_class=CustomHelpFormatter)
    remove_parser.set_defaults(func=instance_remove)
    remove_parser.add_argument('--do-it', dest="ack", help="By default we do a dry run. This actually initiates the removal of the instance.",
                               action='store_true', default=False)
