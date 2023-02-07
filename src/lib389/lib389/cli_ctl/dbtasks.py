# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
from lib389._constants import TaskWarning
from pathlib import Path


def dbtasks_db2index(inst, log, args):
    rtn = False
    if not args.backend:
        if not inst.db2index():
            rtn = False
        else:
            rtn = True
    elif args.backend and not args.attr:
        if not inst.db2index(bename=args.backend):
            rtn = False
        else:
            rtn = True
    else:
        if not inst.db2index(bename=args.backend, attrs=args.attr):
            rtn = False
        else:
            rtn = True
    if rtn:
        log.info("db2index successful")
        return rtn
    else:
        log.fatal("db2index failed")
        return rtn


def dbtasks_db2bak(inst, log, args):
    # Needs an output name?
    if not inst.db2bak(args.archive):
        log.fatal("db2bak failed")
        return False
    else:
        log.info("db2bak successful")


def dbtasks_bak2db(inst, log, args):
    # Needs the archive to restore.
    if not inst.bak2db(args.archive):
        log.fatal("bak2db failed")
        return False
    else:
        log.info("bak2db successful")


def dbtasks_db2ldif(inst, log, args):
    # Check if file path exists
    path = Path(args.ldif)
    parent = path.parent.absolute()
    if not parent.exists():
        raise ValueError("The LDIF file location does not exist: " + args.ldif)

    # Export backend
    if not inst.db2ldif(bename=args.backend, encrypt=args.encrypted, repl_data=args.replication,
                        outputfile=args.ldif, suffixes=None, excludeSuffixes=None, export_cl=False):
        log.fatal("db2ldif failed")
        return False
    else:
        log.info("db2ldif successful")


def dbtasks_ldif2db(inst, log, args):
    # Check if ldif file exists
    if not os.path.exists(args.ldif):
        raise ValueError("The LDIF file does not exist: " + args.ldif)

    ret = inst.ldif2db(bename=args.backend, encrypt=args.encrypted, import_file=args.ldif,
                        suffixes=None, excludeSuffixes=None, import_cl=False)
    if not ret:
        log.fatal("ldif2db failed")
        return False
    elif ret == TaskWarning.WARN_SKIPPED_IMPORT_ENTRY:
        log.warn("ldif2db successful with skipped entries")
    else:
        log.info("ldif2db successful")


def dbtasks_backups(inst, log, args):
    if args.delete:
        # Delete backup
        inst.del_backup(args.delete[0])
    else:
        # list backups
        if not inst.backups(args.json):
            log.fatal("Failed to get list of backups")
            return False
        else:
            if args.json is None:
                log.info("backups successful")


def dbtasks_ldifs(inst, log, args):
    if args.delete:
        # Delete LDIF file
        inst.del_ldif(args.delete[0])
    else:
        # list LDIF files
        if not inst.ldifs(args.json):
            log.fatal("Failed to get list of LDIF files")
            return False
        else:
            if args.json is None:
                log.info("backups successful")


def dbtasks_verify(inst, log, args):
    if not inst.dbverify(bename=args.backend):
        log.fatal("dbverify failed")
        return False
    else:
        log.info("dbverify successful")


def create_parser(subcommands):
    db2index_parser = subcommands.add_parser('db2index', help="Initialise a reindex of the server database. The server must be stopped for this to proceed.")
    # db2index_parser.add_argument('suffix', help="The suffix to reindex. IE dc=example,dc=com.")
    db2index_parser.add_argument('backend', nargs="?", help="The backend to reindex. IE userRoot", default=False)
    db2index_parser.add_argument('--attr', nargs="*", help="The attribute's to reindex. IE --attr aci cn givenname", default=False)
    db2index_parser.set_defaults(func=dbtasks_db2index)

    db2bak_parser = subcommands.add_parser('db2bak', help="Initialise a BDB backup of the database. The server must be stopped for this to proceed.")
    db2bak_parser.add_argument('archive', help="The destination for the archive. This will be created during the db2bak process.",
                               nargs='?', default=None)
    db2bak_parser.set_defaults(func=dbtasks_db2bak)

    db2ldif_parser = subcommands.add_parser('db2ldif', help="Initialise an LDIF dump of the database. The server must be stopped for this to proceed.")
    db2ldif_parser.add_argument('backend', help="The backend to output as an LDIF. IE userRoot")
    db2ldif_parser.add_argument('ldif', help="The path to the ldif output location.", nargs='?', default=None)
    db2ldif_parser.add_argument('--replication', help="Export replication information, suitable for importing on a new consumer or backups.",
                                default=False, action='store_true')
    # db2ldif_parser.add_argument('--include-changelog', help="Include the changelog as a separate LDIF file which will be named:  <ldif_file_name>_cl.ldif.  "
    #                                                         "This option also implies the '--replication' option is set.",
    #                             default=False, action='store_true')
    db2ldif_parser.add_argument('--encrypted', help="Export encrypted attributes", default=False, action='store_true')
    db2ldif_parser.set_defaults(func=dbtasks_db2ldif)

    dbverify_parser = subcommands.add_parser('dbverify', help="Perform a db verification. You should only do this at direction of support")
    dbverify_parser.add_argument('backend', help="The backend to verify. IE userRoot")
    dbverify_parser.set_defaults(func=dbtasks_verify)

    bak2db_parser = subcommands.add_parser('bak2db', help="Restore a BDB backup of the database. The server must be stopped for this to proceed.")
    bak2db_parser.add_argument('archive', help="The archive to restore. This will erase all current server databases.")
    bak2db_parser.set_defaults(func=dbtasks_bak2db)

    ldif2db_parser = subcommands.add_parser('ldif2db', help="Restore an LDIF dump of the database. The server must be stopped for this to proceed.")
    ldif2db_parser.add_argument('backend', help="The backend to restore from an LDIF. IE userRoot")
    ldif2db_parser.add_argument('ldif', help="The path to the ldif to import")
    ldif2db_parser.add_argument('--encrypted', help="Import encrypted attributes", default=False, action='store_true')
    # ldif2db_parser.add_argument('--include-changelog', help="Include a replication changelog LDIF file if present.  It must be named like this in order for the import to include it:  <ldif_file_name>_cl.ldif.",
    #                            default=False, action='store_true')
    ldif2db_parser.set_defaults(func=dbtasks_ldif2db)

    backups_parser = subcommands.add_parser('backups', help="List backup's found in the server's default backup directory")
    backups_parser.add_argument('--delete', nargs=1, help="Delete backup directory")
    backups_parser.set_defaults(func=dbtasks_backups)

    ldifs_parser = subcommands.add_parser('ldifs', help="List all the LDIF files located in the server's LDIF directory")
    ldifs_parser.add_argument('--delete', nargs=1, help="Delete LDIF file")
    ldifs_parser.set_defaults(func=dbtasks_ldifs)
