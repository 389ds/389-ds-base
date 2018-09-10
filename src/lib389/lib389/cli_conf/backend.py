# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.backend import Backend, Backends
from lib389.utils import ensure_str
from lib389.cli_base import (
    populate_attr_arguments,
    _generic_list,
    _generic_get,
    _generic_get_dn,
    _generic_create,
    _generic_delete,
    _get_arg,
    _get_args,
    _get_attributes,
    _warn,
    )
import json

SINGULAR = Backend
MANY = Backends
RDN = 'cn'


def _search_backend_dn(inst, be_name):
    found = False
    be_insts = MANY(inst).list()
    for be in be_insts:
        cn = ensure_str(be.get_attr_val('cn')).lower()
        suffix = ensure_str(be.get_attr_val('nsslapd-suffix')).lower()
        del_be_name = be_name.lower()
        if cn == del_be_name or suffix == del_be_name:
            dn = be.dn
            found = True
            break
    if found:
        return dn


def backend_list(inst, basedn, log, args):
    if 'suffix' in args:
        result = {"type": "list", "items": []}
        be_insts = MANY(inst).list()
        for be in be_insts:
            if args.json:
                result['items'].append(ensure_str(be.get_attr_val_utf8_l('nsslapd-suffix')).lower())
            else:
                print(ensure_str(be.get_attr_val_utf8_l('nsslapd-suffix')).lower())
        if args.json:
            print(json.dumps(result))

    else:
        _generic_list(inst, basedn, log.getChild('backend_list'), MANY, args)


def backend_get(inst, basedn, log, args):
    rdn = _get_arg(args.selector, msg="Enter %s to retrieve" % RDN)
    _generic_get(inst, basedn, log.getChild('backend_get'), MANY, rdn, args)


def backend_get_dn(inst, basedn, log, args):
    dn = _get_arg(args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('backend_get_dn'), MANY, dn, args)


def backend_create(inst, basedn, log, args):
    kwargs = _get_attributes(args, SINGULAR._must_attributes)
    _generic_create(inst, basedn, log.getChild('backend_create'), MANY, kwargs, args)


def backend_delete(inst, basedn, log, args, warn=True):
    dn = _search_backend_dn(inst, args.be_name)
    if dn is None:
        raise ValueError("Unable to find a backend with the name: ({})".format(args.be_name))

    if warn and args.json is False:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('backend_delete'), SINGULAR, dn, args)


def backend_import(inst, basedn, log, args):
    log = log.getChild('backend_import')
    dn = _search_backend_dn(inst, args.be_name)
    if dn is None:
        raise ValueError("Unable to find a backend with the name: ({})".format(args.be_name))

    mc = SINGULAR(inst, dn)
    task = mc.import_ldif(ldifs=args.ldifs, chunk_size=args.chunks_size, encrypted=args.encrypted,
                          gen_uniq_id=args.gen_uniq_id, only_core=args.only_core, include_suffixes=args.include_suffixes,
                          exclude_suffixes=args.exclude_suffixes)
    task.wait()
    result = task.get_exit_code()

    if task.is_complete() and result == 0:
        log.info("The import task has finished successfully")
    else:
        raise ValueError("The import task has failed with the error code: ({})".format(result))


def backend_export(inst, basedn, log, args):
    log = log.getChild('backend_export')

    # If the user gave a root suffix we need to get the backend CN
    be_cn_names = []
    if not isinstance(args.be_names, str):
        for be_name in args.be_names:
            dn = _search_backend_dn(inst, be_name)
            if dn is not None:
                mc = SINGULAR(inst, dn)
                be_cn_names.append(mc.rdn)
            else:
                raise ValueError("Unable to find a backend with the name: ({})".format(args.be_names))

    mc = MANY(inst)
    task = mc.export_ldif(be_names=be_cn_names, ldif=args.ldif, use_id2entry=args.use_id2entry,
                          encrypted=args.encrypted, min_base64=args.min_base64, no_dump_uniq_id=args.no_dump_uniq_id,
                          replication=args.replication, not_folded=args.not_folded, no_seq_num=args.no_seq_num,
                          include_suffixes=args.include_suffixes, exclude_suffixes=args.exclude_suffixes)
    task.wait()
    result = task.get_exit_code()

    if task.is_complete() and result == 0:
        log.info("The export task has finished successfully")
    else:
        raise ValueError("The export task has failed with the error code: ({})".format(result))


def create_parser(subparsers):
    backend_parser = subparsers.add_parser('backend', help="Manage database suffixes and backends")

    subcommands = backend_parser.add_subparsers(help="action")

    list_parser = subcommands.add_parser('list', help="List current active backends and suffixes")
    list_parser.set_defaults(func=backend_list)
    list_parser.add_argument('--suffix', action='store_true', help='Display the suffix for each backend')

    get_parser = subcommands.add_parser('get', help='get')
    get_parser.set_defaults(func=backend_get)
    get_parser.add_argument('selector', nargs='?', help='The backend to search for')

    get_dn_parser = subcommands.add_parser('get_dn', help='get_dn')
    get_dn_parser.set_defaults(func=backend_get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The backend dn to get')

    create_parser = subcommands.add_parser('create', help='create')
    create_parser.set_defaults(func=backend_create)
    populate_attr_arguments(create_parser, SINGULAR._must_attributes)

    delete_parser = subcommands.add_parser('delete', help='deletes the object')
    delete_parser.set_defaults(func=backend_delete)
    delete_parser.add_argument('be_name', help='The backend name or suffix to delete')

    import_parser = subcommands.add_parser('import', help="do an online import of the suffix")
    import_parser.set_defaults(func=backend_import)
    import_parser.add_argument('be_name', nargs='?',
                               help='The backend name or the root suffix where to import')
    import_parser.add_argument('ldifs', nargs='*',
                               help="Specifies the filename of the input LDIF files."
                                    "When multiple files are imported, they are imported in the order"
                                    "they are specified on the command line.")
    import_parser.add_argument('-c', '--chunks-size', type=int,
                               help="The number of chunks to have during the import operation.")
    import_parser.add_argument('-E', '--encrypted', action='store_true',
                               help="Decrypts encrypted data during export. This option is used only"
                                    "if database encryption is enabled.")
    import_parser.add_argument('-g', '--gen-uniq-id',
                               help="Generate a unique id. Type none for no unique ID to be generated"
                                    "and deterministic for the generated unique ID to be name-based."
                                    "By default, a time-based unique ID is generated."
                                    "When using the deterministic generation to have a name-based unique ID,"
                                    "it is also possible to specify the namespace for the server to use."
                                    "namespaceId is a string of characters"
                                    "in the format 00-xxxxxxxx-xxxxxxxx-xxxxxxxx-xxxxxxxx.")
    import_parser.add_argument('-O', '--only-core', action='store_true',
                               help="Requests that only the core database is created without attribute indexes.")
    import_parser.add_argument('-s', '--include-suffixes', nargs='+',
                               help="Specifies the suffixes or the subtrees to be included.")
    import_parser.add_argument('-x', '--exclude-suffixes', nargs='+',
                               help="Specifies the suffixes to be excluded.")

    export_parser = subcommands.add_parser('export', help='do an online export of the suffix')
    export_parser.set_defaults(func=backend_export)
    export_parser.add_argument('be_names', nargs='+',
                               help="The backend names or the root suffixes from where to export.")
    export_parser.add_argument('-l', '--ldif',
                               help="Gives the filename of the output LDIF file."
                                    "If more than one are specified, use a space as a separator")
    export_parser.add_argument('-C', '--use-id2entry', action='store_true', help="Uses only the main database file.")
    export_parser.add_argument('-E', '--encrypted', action='store_true',
                               help="""Decrypts encrypted data during export. This option is used only
                                       if database encryption is enabled.""")
    export_parser.add_argument('-m', '--min-base64', action='store_true',
                               help="Sets minimal base-64 encoding.")
    export_parser.add_argument('-N', '--no-seq-num', action='store_true',
                               help="Enables you to suppress printing the sequence number.")
    export_parser.add_argument('-r', '--replication', action='store_true',
                               help="Exports the information required to initialize a replica when the LDIF is imported")
    export_parser.add_argument('-u', '--no-dump-uniq-id', action='store_true',
                               help="Requests that the unique ID is not exported.")
    export_parser.add_argument('-U', '--not-folded', action='store_true',
                               help="Requests that the output LDIF is not folded.")
    export_parser.add_argument('-s', '--include-suffixes', nargs='+',
                               help="Specifies the suffixes or the subtrees to be included.")
    export_parser.add_argument('-x', '--exclude-suffixes', nargs='+',
                               help="Specifies the suffixes to be excluded.")
