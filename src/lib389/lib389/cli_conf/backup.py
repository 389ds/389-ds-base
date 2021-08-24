# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


def backup_create(inst, basedn, log, args):
    log = log.getChild('backup_create')

    task = inst.backup_online(archive=args.archive, db_type=args.db_type)
    task.wait()
    result = task.get_exit_code()

    if task.is_complete() and result == 0:
        log.info("The backup create task has finished successfully")
    else:
        raise ValueError("The backup create task has failed with the error code: ({})".format(result))


def backup_restore(inst, basedn, log, args):
    log = log.getChild('backup_restore')

    task = inst.restore_online(archive=args.archive, db_type=args.db_type)
    task.wait()
    result = task.get_exit_code()
    task_log = task.get_task_log()

    if task.is_complete() and result == 0:
        log.info("The backup restore task has finished successfully")
    else:
        raise ValueError("The backup restore task has failed with the error code: {}\n{}".format(result, task_log))


def create_parser(subparsers):
    backup_parser = subparsers.add_parser('backup', help="Manage online backups")

    subcommands = backup_parser.add_subparsers(help="action")

    create_parser = subcommands.add_parser('create', help="Creates a backup of the database")
    create_parser.set_defaults(func=backup_create)
    create_parser.add_argument('archive', nargs='?', default=None,
                               help="Sets the directory where to store the backup files. "
                                    "Format: instance_name-year_month_date_hour_minutes_seconds. "
                                    "Default: /var/lib/dirsrv/slapd-instance/bak/ ")
    create_parser.add_argument('-t', '--db-type', default="ldbm database",
                               help="Sets the database type. Default: ldbm database")

    restore_parser = subcommands.add_parser('restore', help="Restores a database from a backup")
    restore_parser.set_defaults(func=backup_restore)
    restore_parser.add_argument('archive', help="Set the directory that contains the backup files")
    restore_parser.add_argument('-t', '--db-type', default="ldbm database",
                                help="Sets the database type. Default: ldbm database")
