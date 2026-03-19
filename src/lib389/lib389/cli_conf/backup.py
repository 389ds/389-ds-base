# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
from datetime import datetime
from lib389.cli_base import CustomHelpFormatter
from lib389.tasks import BackupTask, RestoreTask

def backup_create(inst, basedn, log, args):
    log = log.getChild('backup_create')

    backup_task = BackupTask(inst)

    if args.archive is None:
        backup_dir_name = "backup-%s" % datetime.now().strftime("%Y_%m_%d_%H_%M_%S")
        archive = os.path.join(inst.ds_paths.backup_dir, backup_dir_name)
    else:
        archive = args.archive
    task_properties = {'nsArchiveDir': archive}

    if args.db_type is not None:
        task_properties['nsDatabaseType'] = args.db_type

    backup_task.create(properties=task_properties)
    if args.watch:
        log.info("Creating backup: %s", archive)
        backup_task.watch()
    elif args.wait:
        backup_task.wait(timeout=args.timeout)

    result = backup_task.get_exit_code()
    if backup_task.is_complete() and result == 0:
        log.info("The backup create task has finished successfully")
    else:
        if result is None:
            raise ValueError("The backup create task has not completed. Please check server's error log for more information")
        else:
            raise ValueError(f"The backup create task has failed with the error code: ({result})")


def backup_restore(inst, basedn, log, args):
    log = log.getChild('backup_restore')

    restore_task = RestoreTask(inst)
    task_properties = {'nsArchiveDir': args.archive}
    if args.db_type is not None:
        task_properties['nsDatabaseType'] = args.db_type
    restore_task.create(properties=task_properties)
    if args.watch:
        log.info("Restoring backup: %s", args.archive)
        restore_task.watch()
    elif args.wait:
        restore_task.wait(timeout=args.timeout)

    result = restore_task.get_exit_code()
    task_log = restore_task.get_task_log()

    if restore_task.is_complete() and result == 0:
        log.info("The backup restore task has finished successfully")
    else:
        if result is None:
            raise ValueError(f"The backup restore task has not completed. Please check server's error log for more information\n{task_log}")
        else:
            raise ValueError(f"The backup restore task has failed with the error code: {result}\n{task_log}")


def create_parser(subparsers):
    backup_parser = subparsers.add_parser('backup', help="Manage online backups", formatter_class=CustomHelpFormatter)

    subcommands = backup_parser.add_subparsers(help="action")

    create_backup_parser = subcommands.add_parser('create', help="Creates a backup of the database", formatter_class=CustomHelpFormatter)
    create_backup_parser.set_defaults(func=backup_create)
    create_backup_parser.add_argument('archive', nargs='?', default=None,
                                      help="Sets the directory where to store the backup files. "
                                           "Format: instance_name-year_month_date_hour_minutes_seconds. "
                                           "Default: /var/lib/dirsrv/slapd-instance/bak/ ")
    create_backup_parser.add_argument('-t', '--db-type', default="ldbm database",
                                      help="Sets the database type. Default: ldbm database")
    create_backup_parser.add_argument('--timeout', type=int, default=120,
                                      help="Sets the task timeout.  Default is 120 seconds,")
    create_backup_parser.add_argument('--watch', action='store_true', help='Watch the status of the backup task')

    restore_parser = subcommands.add_parser('restore', help="Restores a database from a backup", formatter_class=CustomHelpFormatter)
    restore_parser.set_defaults(func=backup_restore)
    restore_parser.add_argument('archive', help="Set the directory that contains the backup files")
    restore_parser.add_argument('-t', '--db-type', default="ldbm database",
                                help="Sets the database type. Default: ldbm database")
    restore_parser.add_argument('--timeout', type=int, default=120,
                                help="Sets the task timeout.  Default is 120 seconds.")
    restore_parser.add_argument('--watch', action='store_true', help='Watch the status of the restore task')
