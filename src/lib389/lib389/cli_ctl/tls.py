# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
from lib389.nss_ssl import NssSsl, CERT_NAME, CA_NAME
from lib389.cli_base import _warn, CustomHelpFormatter


def show_servercert(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    log.info(tls.display_cert_details(CERT_NAME))


def list_client_cas(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    # This turns an array of [('CA', 'C,,')]
    for c in tls.list_client_ca_certs():
        log.info(c[0])


def list_cas(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    # This turns an array of [('CA', 'C,,')]
    for c in tls.list_ca_certs():
        log.info(c[0])


def show_cert(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    nickname = args.nickname
    log.info(tls.display_cert_details(nickname))


def import_client_ca(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    cert_path = args.cert_path
    nickname = args.nickname
    if nickname.lower() == CERT_NAME.lower() or nickname.lower() == CA_NAME.lower():
        log.error("You may not import a CA with the nickname %s or %s" % (CERT_NAME, CA_NAME))
        return
    tls.add_cert(nickname=nickname, input_file=cert_path)
    tls.edit_cert_trust(nickname, "T,,")


def import_ca(inst, log, args):
    if not os.path.isfile(args.cert_path):
        raise ValueError(f'Certificate file "{args.cert_path}" does not exist')

    tls = NssSsl(dirsrv=inst)
    tls.add_ca_cert_bundle(args.cert_path, args.nickname)


def import_key_cert_pair(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    key_path = args.key_path
    cert_path = args.cert_path
    tls.add_server_key_and_cert(key_path, cert_path)


def generate_key_csr(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    alt_names = args.alt_names
    subject = args.subject
    out_path = tls.create_rsa_key_and_csr(alt_names, subject)
    log.info(out_path)


def import_server_cert(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    cert_path = args.cert_path
    tls.import_rsa_crt(crt=cert_path)


def remove_cert(inst, log, args, warn=True):
    tls = NssSsl(dirsrv=inst)
    nickname = args.nickname
    if warn:
        _warn(nickname, msg="Deleting certificate %s" % nickname)
    tls.del_cert(nickname)


def export_cert(inst, log, args):
    tls = NssSsl(dirsrv=inst)
    nickname = args.nickname
    der_format = False
    output_file = None
    if args.binary_format:
        der_format = True
    if args.output_file is not None:
        output_file = args.output_file
    tls.export_cert(nickname, output_file, der_format)


def create_parser(subparsers):
    tls_parser = subparsers.add_parser('tls', help="Manage TLS certificates", formatter_class=CustomHelpFormatter)

    subcommands = tls_parser.add_subparsers(help='action')

    list_ca_parser = subcommands.add_parser('list-ca', help='list server certificate authorities including intermediates', formatter_class=CustomHelpFormatter)
    list_ca_parser.set_defaults(func=list_cas)

    list_client_ca_parser = subcommands.add_parser('list-client-ca', help='list client certificate authorities including intermediates', formatter_class=CustomHelpFormatter)
    list_client_ca_parser.set_defaults(func=list_client_cas)

    show_servercert_parser = subcommands.add_parser('show-server-cert', help='Show the active server certificate that clients will see and verify', formatter_class=CustomHelpFormatter)
    show_servercert_parser.set_defaults(func=show_servercert)

    show_cert_parser = subcommands.add_parser('show-cert', help='Show a certificate\'s details referenced by it\'s nickname. This is analogous to certutil -L -d <path> -n <nickname>', formatter_class=CustomHelpFormatter)
    show_cert_parser.add_argument('nickname', help="The nickname (friendly name) of the certificate to display")
    show_cert_parser.set_defaults(func=show_cert)

    generate_server_cert_csr_parser = subcommands.add_parser(
        'generate-server-cert-csr',
        help="Generate a Server-Cert certificate signing request - the csr is then submitted to a CA for verification, and when signed you import with import-ca and import-server-cert",
        formatter_class=CustomHelpFormatter
    )
    generate_server_cert_csr_parser.add_argument('--subject', '-s',
        default=None,
        help="Certificate Subject field to use")
    generate_server_cert_csr_parser.add_argument('alt_names', nargs='*',
        help="Certificate requests subject alternative names. These are auto-detected if not provided")
    generate_server_cert_csr_parser.set_defaults(func=generate_key_csr)

    import_client_ca_parser = subcommands.add_parser(
        'import-client-ca',
        help="Import a CA trusted to issue user (client) certificates. This is part of how client certificate authentication functions.",
        formatter_class=CustomHelpFormatter
    )
    import_client_ca_parser.add_argument('cert_path',
        help="The path to the x509 cert to import as a client trust root")
    import_client_ca_parser.add_argument('nickname', help="The name of the certificate once imported")
    import_client_ca_parser.set_defaults(func=import_client_ca)

    import_ca_parser = subcommands.add_parser(
        'import-ca',
        help="Import a CA or intermediate CA for signing this servers certificates (aka Server-Cert). "
             "You should import all the CA's in the chain as required.  PEM bundles are accepted",
        formatter_class=CustomHelpFormatter
    )
    import_ca_parser.add_argument('cert_path',
        help="The path to the x509 cert to import as a server CA")
    import_ca_parser.add_argument('nickname', nargs='+', help="The name of the certificate once imported")
    import_ca_parser.set_defaults(func=import_ca)

    import_server_cert_parser = subcommands.add_parser(
        'import-server-cert',
        help="Import a new Server-Cert after the csr has been signed from a CA.",
        formatter_class=CustomHelpFormatter
    )
    import_server_cert_parser.add_argument('cert_path',
        help="The path to the x509 cert to import as Server-Cert")
    import_server_cert_parser.set_defaults(func=import_server_cert)

    import_server_key_cert_parser = subcommands.add_parser(
        'import-server-key-cert',
        help="Import a new key and Server-Cert after having been signed from a CA. This is used if you have an external csr tool or a service like lets encrypt that generates PEM keys externally.",
        formatter_class=CustomHelpFormatter
    )
    import_server_key_cert_parser.add_argument('cert_path',
        help="The path to the x509 cert to import as Server-Cert")
    import_server_key_cert_parser.add_argument('key_path',
        help="The path to the x509 key to import associated to Server-Cert")
    import_server_key_cert_parser.set_defaults(func=import_key_cert_pair)

    remove_cert_parser = subcommands.add_parser(
        'remove-cert',
        help="Delete a certificate from this database. This will remove it from acting as a CA, a client CA or the Server-Cert role.",
        formatter_class=CustomHelpFormatter
    )
    remove_cert_parser.add_argument('nickname', help="The name of the certificate to delete")
    remove_cert_parser.set_defaults(func=remove_cert)

    export_cert_parser = subcommands.add_parser(
        'export-cert',
        help="Export a certificate to PEM or DER/Binary format.  PEM format is the default",
        formatter_class=CustomHelpFormatter
    )
    export_cert_parser.add_argument('nickname', help="The name of the certificate to export")
    export_cert_parser.add_argument('--binary-format', action='store_true',
                                    help="Export certificate in DER/binary format")
    export_cert_parser.add_argument('--output-file',
                                    help='The name for the exported certificate.  Default name is the certificate '
                                         'nickname with an extension of ".pem" or ".crt"')
    export_cert_parser.set_defaults(func=export_cert)
