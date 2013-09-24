# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

def dbtasks_db2index(inst, log, args):
    inst.db2index(bename=args.backend)

def dbtasks_db2bak(inst, log, args):
    # Needs an output name?
    inst.db2bak(args.archive)
    log.info("db2bak successful")

def dbtasks_bak2db(inst, log, args):
    # Needs the archive to restore.
    inst.bak2db(args.archive)
    log.info("bak2db successful")

def dbtasks_db2ldif(inst, log, args):
    inst.db2ldif(bename=args.backend, encrypt=args.encrypted, repl_data=args.replication, outputfile=args.ldif, suffixes=None, excludeSuffixes=None)
    log.info("db2ldif successful")

def dbtasks_ldif2db(inst, log, args):
    inst.ldif2db(bename=args.backend, encrypt=args.encrypted, import_file=args.ldif, suffixes=None, excludeSuffixes=None)
    log.info("ldif2db successful")

def create_parser(subcommands):
    db2index_parser = subcommands.add_parser('db2index', help="Initialise a reindex of the server database. The server must be stopped for this to proceed.")
    # db2index_parser.add_argument('suffix', help="The suffix to reindex. IE dc=example,dc=com.")
    db2index_parser.add_argument('backend', help="The backend to reindex. IE userRoot")
    db2index_parser.set_defaults(func=dbtasks_db2index)

    db2bak_parser = subcommands.add_parser('db2bak', help="Initialise a BDB backup of the database. The server must be stopped for this to proceed.")
    db2bak_parser.add_argument('archive', help="The destination for the archive. This will be created during the db2bak process.")
    db2bak_parser.set_defaults(func=dbtasks_db2bak)

    db2ldif_parser = subcommands.add_parser('db2ldif', help="Initialise an LDIF dump of the database. The server must be stopped for this to proceed.")
    db2ldif_parser.add_argument('backend', help="The backend to output as an LDIF. IE userRoot")
    db2ldif_parser.add_argument('ldif', help="The path to the ldif output location.")
    db2ldif_parser.add_argument('--replication', help="Export replication information, suitable for importing on a new consumer or backups.", default=False, action='store_true')
    db2ldif_parser.add_argument('--encrypted', help="Export encrypted attributes", default=False, action='store_true')
    db2ldif_parser.set_defaults(func=dbtasks_db2ldif)

    bak2db_parser = subcommands.add_parser('bak2db', help="Restore a BDB backup of the database. The server must be stopped for this to proceed.")
    bak2db_parser.add_argument('archive', help="The archive to restore. This will erase all current server databases.")
    bak2db_parser.set_defaults(func=dbtasks_bak2db)

    ldif2db_parser = subcommands.add_parser('ldif2db', help="Restore an LDIF dump of the database. The server must be stopped for this to proceed.")
    ldif2db_parser.add_argument('backend', help="The backend to restore from an LDIF. IE userRoot")
    ldif2db_parser.add_argument('ldif', help="The path to the ldif to import")
    ldif2db_parser.add_argument('--encrypted', help="Import encrypted attributes", default=False, action='store_true')
    ldif2db_parser.set_defaults(func=dbtasks_ldif2db)

