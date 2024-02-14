# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from lib389.conflicts import (ConflictEntries, ConflictEntry, GlueEntries, GlueEntry)
from lib389.cli_base import CustomHelpFormatter

conflict_attrs = ['nsds5replconflict', '*']


def list_conflicts(inst, basedn, log, args):
    conflicts = ConflictEntries(inst, args.suffix).list()
    if args.json:
        results = []
        for conflict in conflicts:
            results.append(json.loads(conflict.get_all_attrs_json()))
        log.info(json.dumps({'type': 'list', 'items': results}, indent=4))
    else:
        if len(conflicts) > 0:
            for conflict in conflicts:
                log.info(conflict.display(conflict_attrs))
        else:
            log.info("There were no conflict entries found under the suffix")


def cmp_conflict(inst, basedn, log, args):
    conflict = ConflictEntry(inst, args.DN)
    valid_entry = conflict.get_valid_entry()

    if args.json:
        results = []
        results.append(json.loads(conflict.get_all_attrs_json()))
        results.append(json.loads(valid_entry.get_all_attrs_json()))
        log.info(json.dumps({'type': 'list', 'items': results}, indent=4))
    else:
        log.info("Conflict Entry:\n")
        log.info(conflict.display(conflict_attrs))
        log.info("Valid Entry:\n")
        log.info(valid_entry.display(conflict_attrs))


def del_conflict(inst, basedn, log, args):
    conflict = ConflictEntry(inst, args.DN)
    conflict.delete()


def swap_conflict(inst, basedn, log, args):
    conflict = ConflictEntry(inst, args.DN)
    conflict.swap()


def convert_conflict(inst, basedn, log, args):
    conflict = ConflictEntry(inst, args.DN)
    conflict.convert(args.new_rdn)


def list_glue(inst, basedn, log, args):
    glues = GlueEntries(inst, args.suffix).list()
    if args.json:
        results = []
        for glue in glues:
            results.append(json.loads(glue.get_all_attrs_json()))
        log.info(json.dumps({'type': 'list', 'items': results}, indent=4))
    else:
        if len(glues) > 0:
            for glue in glues:
                log.info(glue.display(conflict_attrs))
        else:
            log.info("There were no glue entries found under the suffix")


def del_glue(inst, basedn, log, args):
    glue = GlueEntry(inst, args.DN)
    glue.delete_all()


def convert_glue(inst, basedn, log, args):
    glue = GlueEntry(inst, args.DN)
    glue.convert()


def create_parser(subparsers):
    conflict_parser = subparsers.add_parser('repl-conflict', help="Manage replication conflicts", formatter_class=CustomHelpFormatter)
    subcommands = conflict_parser.add_subparsers(help='action')

    # coinflict entry arguments
    list_parser = subcommands.add_parser('list', help="List conflict entries", formatter_class=CustomHelpFormatter)
    list_parser.add_argument('suffix', help='Sets the backend name, or suffix, to look for conflict entries')
    list_parser.set_defaults(func=list_conflicts)

    cmp_parser = subcommands.add_parser('compare', help="Compare the conflict entry with its valid counterpart", formatter_class=CustomHelpFormatter)
    cmp_parser.add_argument('DN', help='The DN of the conflict entry')
    cmp_parser.set_defaults(func=cmp_conflict)

    del_parser = subcommands.add_parser('delete', help="Delete a conflict entry", formatter_class=CustomHelpFormatter)
    del_parser.add_argument('DN', help='The DN of the conflict entry')
    del_parser.set_defaults(func=del_conflict)

    replace_parser = subcommands.add_parser('swap', help="Replace the valid entry with the conflict entry", formatter_class=CustomHelpFormatter)
    replace_parser.add_argument('DN', help='The DN of the conflict entry')
    replace_parser.set_defaults(func=swap_conflict)

    replace_parser = subcommands.add_parser('convert', help="Convert the conflict entry to a valid entry, "
                                                            "while keeping the original valid entry counterpart.  "
                                                            "This requires that the converted conflict entry have "
                                                            "a new RDN value.  For example: \"cn=my_new_rdn_value\".")
    replace_parser.add_argument('DN', help='The DN of the conflict entry')
    replace_parser.add_argument('--new-rdn', required=True, help="Sets the new RDN for the converted conflict entry.  "
                                                                 "For example: \"cn=my_new_rdn_value\"")
    replace_parser.set_defaults(func=convert_conflict)

    # Glue entry arguments
    list_glue_parser = subcommands.add_parser('list-glue', help="List replication glue entries", formatter_class=CustomHelpFormatter)
    list_glue_parser.add_argument('suffix', help='The backend name, or suffix, to look for glue entries')
    list_glue_parser.set_defaults(func=list_glue)

    del_glue_parser = subcommands.add_parser('delete-glue', help="Delete the glue entry and its child entries", formatter_class=CustomHelpFormatter)
    del_glue_parser.add_argument('DN', help='The DN of the glue entry')
    del_glue_parser.set_defaults(func=del_glue)

    convert_glue_parser = subcommands.add_parser('convert-glue', help="Convert the glue entry into a regular entry", formatter_class=CustomHelpFormatter)
    convert_glue_parser.add_argument('DN', help='The DN of the glue entry')
    convert_glue_parser.set_defaults(func=convert_glue)
