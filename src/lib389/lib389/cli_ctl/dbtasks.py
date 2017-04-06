# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

def dbtasks_db2index(inst, log, args):
    # inst.db2index(suffixes=[args.suffix,])
    inst.db2index(bename=args.backend)

def create_parser(subcommands):
    db2index_parser = subcommands.add_parser('db2index', help="Initialise a reindex of the server database. The server must be stopped for this to proceed.")
    # db2index_parser.add_argument('suffix', help="The suffix to reindex. IE dc=example,dc=com.")
    db2index_parser.add_argument('backend', help="The backend to reindex. IE userRoot")
    db2index_parser.set_defaults(func=dbtasks_db2index)

