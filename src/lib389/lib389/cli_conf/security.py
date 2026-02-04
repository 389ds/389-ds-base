# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from collections import OrderedDict, namedtuple
import json
import os
import sys
from lib389.config import Config, Encryption, RSA
from lib389.nss_ssl import NssSsl
from lib389.cert_manager import CertManager
from lib389.cli_base import _warn, CustomHelpFormatter


Props = namedtuple('Props', ['cls', 'attr', 'help', 'values'])

onoff = ('on', 'off')
protocol_versions = ('SSLv3', 'TLS1.0', 'TLSv1.1', 'TLSv1.2', 'TLSv1.3', '')
SECURITY_ATTRS_MAP = OrderedDict([
    ('security', Props(Config, 'nsslapd-security',
                       'Enables or disables security',
                       onoff)),
    ('listen-host', Props(Config, 'nsslapd-securelistenhost',
                          'Sets the host or IP address to listen on for LDAPS',
                          str)),
    ('secure-port', Props(Config, 'nsslapd-securePort',
                          'Sets the port for LDAPS to listen on',
                          range(1, 65536))),
    ('tls-client-auth', Props(Encryption, 'nsSSLClientAuth',
                              'Configures client authentication requirement',
                              ('off', 'allowed', 'required'))),
    ('tls-client-renegotiation', Props(Encryption, 'nsTLSAllowClientRenegotiation',
                                       'Allows client TLS renegotiation',
                                       onoff)),
    ('require-secure-authentication', Props(Config, 'nsslapd-require-secure-binds',
                                            'Configures whether binds over LDAPS, StartTLS, or SASL are required',
                                            onoff)),
    ('check-hostname', Props(Config, 'nsslapd-ssl-check-hostname',
                             'Checks the subject of remote certificate against the hostname',
                             onoff)),
    ('verify-cert-chain-on-startup', Props(Config, 'nsslapd-validate-cert',
                                           'Validates the server certificate during startup',
                                           ('warn', *onoff))),
    ('session-timeout', Props(Encryption, 'nsSSLSessionTimeout',
                              'Sets the secure session timeout',
                              int)),
    ('tls-protocol-min', Props(Encryption, 'sslVersionMin',
                               'Sets the minimal allowed secure protocol version',
                               protocol_versions)),
    ('tls-protocol-max', Props(Encryption, 'sslVersionMax',
                               'Sets the maximal allowed secure protocol version',
                               protocol_versions)),
    ('allow-insecure-ciphers', Props(Encryption, 'allowWeakCipher',
                                     'Allows weak ciphers for legacy use',
                                     onoff)),
    ('allow-weak-dh-param', Props(Encryption, 'allowWeakDHParam',
                                  'Allows short DH params for legacy use',
                                  onoff)),
    ('cipher-pref', Props(Encryption, 'nsSSL3Ciphers',
                          'Directly sets the nsSSL3Ciphers attribute. It is a comma-separated list '
                          'of cipher names (prefixed with + or -), optionally including +all or -all. The attribute '
                          'may optionally be prefixed by keyword "default". Please refer to documentation of '
                          'the attribute for a more detailed description.',
                          onoff)),
])

RSA_ATTRS_MAP = OrderedDict([
    ('tls-allow-rsa-certificates', Props(RSA, 'nsSSLActivation',
                                         'Activates the use of RSA certificates', onoff)),
    ('nss-cert-name', Props(RSA, 'nsSSLPersonalitySSL',
                            'Sets the server certificate name in NSS DB', str)),
    ('nss-token', Props(RSA, 'nsSSLToken',
                        'Sets the security token name (module of NSS DB)', str))
])


def _security_generic_get(inst, basedn, log, args, attrs_map):
    result = {}
    for attr, props in attrs_map.items():
        val = props.cls(inst).get_attr_val_utf8(props.attr)
        if val is None:
            val = ""
        result[props.attr.lower()] = val
    if args.json:
        print(json.dumps({'type': 'list', 'items': result}, indent=4))
    else:
        print('\n'.join([f'{attr}: {value or ""}' for attr, value in result.items()]))


def _security_generic_set(inst, basedn, log, args, attrs_map):
    for attr, props in attrs_map.items():
        arg = getattr(args, attr.replace('-', '_'))
        if arg is None:
            continue
        dsobj = props.cls(inst)
        if arg != "":
            dsobj.replace(props.attr, arg)
        else:
            dsobj.remove_all(props.attr)
    log.info(f"Successfully updated security configuration ({props.attr})")


def _security_generic_get_parser(parent, attrs_map, help):
    p = parent.add_parser('get', help=help, formatter_class=CustomHelpFormatter)
    p.set_defaults(func=lambda *args: _security_generic_get(*args, attrs_map))
    return p


def _security_generic_set_parser(parent, attrs_map, help, description):
    p = parent.add_parser('set', help=help, description=description, formatter_class=CustomHelpFormatter)
    p.set_defaults(func=lambda *args: _security_generic_set(*args, attrs_map))
    for opt, params in attrs_map.items():
        p.add_argument(f'--{opt}', help=f'{params[2]} ({params[1]})')
    return p


def _security_ciphers_change(mode, ciphers, inst, log):
    log = log.getChild('_security_ciphers_change')
    if ('default' in ciphers) or ('all' in ciphers):
        log.error(('Use ciphers\' names only. Keywords "default" and "all" are ignored. '
                   'Please, instead specify them manually using \'set\' command.'))
        return
    enc = Encryption(inst)
    if enc.change_ciphers(mode, ciphers) is False:
        log.error('Setting new ciphers failed.')


def _security_generic_toggle(inst, basedn, log, args, cls, attr, value, thing):
    cls(inst).set(attr, value)


def _security_generic_toggle_parsers(parent, cls, attr, help_pattern):
    def add_parser(action, value):
        p = parent.add_parser(action.lower(), help=help_pattern.format(action), formatter_class=CustomHelpFormatter)
        p.set_defaults(func=lambda *args: _security_generic_toggle(*args, cls, attr, value, action))
        return p

    return list(map(add_parser, ('Enable', 'Disable'), ('on', 'off')))


def _resolve_pkcs12_password(args):
    if args.pkcs12_pin_text:
        return args.pkcs12_pin_text

    if args.pkcs12_pin_stdin:
        return sys.stdin.readline().rstrip("\n")

    if args.pkcs12_pin_path:
        with open(args.pkcs12_pin_path) as f:
            return f.read().rstrip("\n")

    return None

def _dump_cert(cert, json_output: bool = False, log = None):
    """
    Print or return a certificate's details in text or JSON format.

    :param cert: dict describing a certificate
    :param json_output: If True print JSON else print text
    :param log: Optional logger to output text
    """
    if not isinstance(cert, dict):
        raise TypeError(f"Expected dict, got {type(cert)}")

    if json_output:
        return {
            "type": "certificate",
            "attrs": {
                "nickname": cert["cn"],
                "subject": cert["subject"],
                "issuer": cert["issuer"],
                "expires": cert["expires"],
                "flags": cert["trust_flags"],
            }
        }
    else:
        msg = (
            f"Certificate Name: {cert['cn']}\n"
            f"Subject DN: {cert['subject']}\n"
            f"Issuer DN: {cert['issuer']}\n"
            f"Expires: {cert['expires']}\n"
            f"Trust Flags: {cert['trust_flags']}\n"
        )
        if log:
            log.info(msg)
        else:
            print(msg)

def security_enable(inst, basedn, log, args):
    dbpath = inst.get_cert_dir()
    tlsdb = NssSsl(dbpath=dbpath)
    certs = tlsdb.list_certs()
    if len(certs) == 0:
        raise ValueError('There are no server certificates in the security ' +
                         'database, security can not be enabled.')

    if len(certs) == 1:
        # If there is only cert make sure it is set as the server certificate
        RSA(inst).set('nsSSLPersonalitySSL', certs[0]['cn'])
    elif args.cert_name is not None:
        # A certificate nickname was provided, set it as the server certificate
        RSA(inst).set('nsSSLPersonalitySSL', args.cert_name)

    # it should now be safe to enable security
    Config(inst).set('nsslapd-security', 'on')
    log.info("Successfully enabled security")


def security_disable(inst, basedn, log, args):
    Config(inst).set('nsslapd-security', 'off')


def security_ciphers_enable(inst, basedn, log, args):
    _security_ciphers_change('+', args.cipher, inst, log)


def security_ciphers_disable(inst, basedn, log, args):
    _security_ciphers_change('-', args.cipher, inst, log)


def security_ciphers_set(inst, basedn, log, args):
    enc = Encryption(inst)
    enc.ciphers = args.cipher_string.lstrip().split(',')


def security_ciphers_get(inst, basedn, log, args):
    enc = Encryption(inst)
    if args.json:
        print({'type': 'list', 'items': enc.ciphers})
    else:
        val = ','.join(enc.ciphers)
        print(val if val != '' else '<undefined>')


def security_ciphers_list(inst, basedn, log, args):
    enc = Encryption(inst)

    if args.enabled:
        lst = enc.enabled_ciphers
    elif args.supported:
        lst = enc.supported_ciphers
    elif args.disabled:
        lst = set(enc.supported_ciphers) - set(enc.enabled_ciphers)
    else:
        lst = enc.ciphers

    if args.json:
        print(json.dumps({'type': 'list', 'items': lst}, indent=4))
    else:
        if lst == []:
            log.getChild('security').warn('List of ciphers is empty')
        else:
            print(*lst, sep='\n')


def security_disable_plaintext_port(inst, basedn, log, args, warn=True):
    if warn and args.json is False:
        _warn(True, msg="Disabling plaintext ldap port - you must have ldaps configured")
    inst.config.disable_plaintext_port()
    log.info("Plaintext port disabled - please restart your instance to take effect")
    log.info("To undo this change run the subcommand - 'dsconf <instance> config replace nsslapd-port=<port number>'")


def cert_add(inst, basedn, log, args):
    """Add server certificate
    """
    # Verify file and certificate name
    if not os.path.isfile(args.file):
        raise ValueError(f'Certificate file "{args.file}" does not exist')

    pkcs12_password = None
    pkcs12_file = args.file.lower().endswith((".p12", ".pfx"))
    if pkcs12_file:
        pkcs12_password = _resolve_pkcs12_password(args)

    certmgr = CertManager(instance=inst)
    cert = certmgr.get_cert(args.name)
    if cert:
        log.info(f"Certificate '{args.name}' already exists, skipping")
        return

    certmgr.add_cert(
        args.file,
        args.name,
        pkcs12_password=pkcs12_password,
        primary=args.primary_cert,
        ca=False,
        force=args.force
    )
    log.info("Successfully added certificate")

def cacert_add(inst, basedn, log, args):
    """Add CA certificate, or CA certificate bundle
    """
    # Verify file and certificate name
    if not os.path.isfile(args.file):
        raise ValueError(f'Certificate file "{args.file}" does not exist')

    # Does it make sense to add a CA cert from p12 container ?
    if args.file.lower().endswith((".p12", ".pfx")):
        raise ValueError("PKCS#12 CA certificates not supported. Use PEM or DER file")

    certmgr = CertManager(instance=inst)
    certmgr.add_ca_cert(args.file, args.name, force=args.force)
    log.info("Successfully added CA certificate")

def cert_list(inst, basedn, log, args):
    """List all the server certificates
    """
    certmgr = CertManager(instance=inst)
    certs = certmgr.list_certs()
    if not certs:
        log.info("No certificates found.")
        return

    if args.json:
        output = [_dump_cert(cert, json_output=True) for cert in certs]
        print(json.dumps(output, indent=4))
    else:
        for cert in certs:
            _dump_cert(cert, json_output=False, log=log)

def cacert_list(inst, basedn, log, args):
    """List all CA certs
    """
    certmgr = CertManager(instance=inst)
    ca_certs = certmgr.list_ca_certs()
    if not ca_certs:
        log.info("No CA certificates found.")
        return

    if args.json:
        output = [_dump_cert(cert, json_output=True) for cert in ca_certs]
        print(json.dumps(output, indent=4))
    else:
        for cert in ca_certs:
            _dump_cert(cert, json_output=False, log=log)

def cert_get(inst, basedn, log, args):
    """Get the details about a server certificate
    """
    certmgr = CertManager(instance=inst)
    cert = certmgr.get_cert(args.name)
    if not cert:
        log.error(f"Certificate '{args.name}' not found.")
        return

    if "C" in cert.get("trust_flags", ""):
        return

    if args.json:
        output = _dump_cert(cert, json_output=args.json)
        log.info(json.dumps(output, indent=4))
    else:
        _dump_cert(cert, json_output=args.json, log=log)

def cacert_get(inst, basedn, log, args):
    """Get the details about a CA certificate
    """
    certmgr = CertManager(instance=inst)
    cert = certmgr.get_cert(args.name)
    if not cert:
        log.error(f"Certificate '{args.name}' not found.")
        return

    if "C" not in cert.get("trust_flags", ""):
        return

    if args.json:
        output = _dump_cert(cert, json_output=args.json)
        log.info(json.dumps(output, indent=4))
    else:
        _dump_cert(cert, json_output=args.json, log=log)

def csr_list(inst, basedn, log, args):
    """
    List all files with .csr extension in instance config dir
    """
    csr_list = []
    tlsdb = NssSsl(dirsrv=inst)
    details = tlsdb._csr_list(args.path)

    for detail in details:
        if args.json:
            csr_list.append(
                {
                    "type": "csr",
                    "attrs": {
                        'modified': detail[0],
                        'subject': detail[1],
                        'subject_alt_names': detail[2],
                        'name': detail[3],
                    }
                }
            )
        else:
            log.info(f'Modified: {detail[0]}')
            log.info(f'Subject: {detail[1]}')
            log.info(f'Subject Alternative Names: {", ".join(detail[2])}')
            log.info(f'Name: {detail[3]}\n')

    if args.json:
        log.info(json.dumps(csr_list, indent=4))


def csr_get(inst, basedn, log, args):
    """
    Show PEM format of a csr
    """
    tlsdb = NssSsl(dirsrv=inst)
    details = tlsdb._csr_show(args.name)
    log.info(f"{details}")


def csr_gen(inst, basedn, log, args):
    """
    Generate a .csr file in instance config dir
    """
    tlsdb = NssSsl(dirsrv=inst)
    alt_names = args.alt_names
    subject = args.subject
    name = args.name
    out_path = tlsdb.create_rsa_key_and_csr(alt_names, subject, name)
    log.info(out_path)


def csr_del(inst, basedn, log, args):
    """
    Delete a .csr file from instance config dir
    """
    csr_dir = inst.get_cert_dir()
    file_path = os.path.join(csr_dir, args.name + ".csr")
    try:
        os.remove(file_path)
    except FileNotFoundError:
        raise ValueError(file_path + " not found")
    log.info(f"Successfully deleted: " + file_path)


def cert_edit(inst, basedn, log, args):
    """Edit cert
    """
    certmgr = CertManager(instance=inst)
    certmgr.edit_cert_trust(args.name, args.flags)
    log.info("Successfully edited certificate trust flags")


def cert_del(inst, basedn, log, args):
    """Delete cert
    """
    certmgr = CertManager(instance=inst)
    try:
        certmgr.del_cert(args.name)
        log.info(f"Successfully deleted certificate")
    except ValueError as e:
        log.error(f"Failed to delete certificate '{args.name}': {e}")

def key_list(inst, basedn, log, args):
    """
    List keys in the NSS DB
    """
    key_list = []
    tls = NssSsl(dirsrv=inst)
    keys = tls.list_keys(args.orphan)

    for key in keys:
        if args.json:
            key_list.append(
                {
                    "type": "key",
                    "attrs": {
                                'cipher': key[0],
                                'key_id': key[1],
                                'state': key[2],
                            }
                }
            )
        else:
            log.info('Cipher: {}'.format(key[0]))
            log.info('Key Id: {}'.format(key[1]))
            log.info('State: {}\n'.format(key[2]))

    if args.json:
            log.info(json.dumps(key_list, indent=4))


def key_del(inst, basedn, log, args):
    """
    Delete a key from NSS DB
    """
    tls = NssSsl(dirsrv=inst)
    keys = tls.del_key(args.key_id)
    log.info(keys)


def export_cert(inst, basedn, log, args):
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
    security = subparsers.add_parser('security', help='Manage security settings', formatter_class=CustomHelpFormatter)
    security_sub = security.add_subparsers(help='security')

    # Core security management
    _security_generic_set_parser(security_sub, SECURITY_ATTRS_MAP, 'Set general security options',
        ('Use this command for setting security related options located in cn=config and cn=encryption,cn=config.'
         '\n\nTo enable/disable security you can use enable and disable commands instead.'))
    _security_generic_get_parser(security_sub, SECURITY_ATTRS_MAP, 'Display general security options')
    security_enable_p = security_sub.add_parser('enable', help='Enable security', description=(
        'If missing, create security database, then turn on security functionality. Please note this is usually not'
        ' enough for TLS connections to work - proper setup of CA and server certificate is necessary.'))
    security_enable_p.add_argument('--cert-name', default=None,
        help='Sets the name of the certificate the server should use')
    security_enable_p.set_defaults(func=security_enable)
    security_disable_p = security_sub.add_parser('disable', help='Disable security', description=(
        'Turn off security functionality. The rest of the configuration will be left untouched.'))
    security_disable_p.set_defaults(func=security_disable)

    security_disable_plain_parser = security_sub.add_parser('disable_plain_port',
        help="Disables the plain text LDAP port, allowing only LDAPS to function")
    security_disable_plain_parser.set_defaults(func=security_disable_plaintext_port)

    # Server certificate management
    certs = security_sub.add_parser('certificate', help='Manage TLS certificates', formatter_class=CustomHelpFormatter)
    certs_sub = certs.add_subparsers(help='certificate')
    cert_add_parser = certs_sub.add_parser('add', help='Add a server certificate', description=(
        'Add a server certificate to the NSS database'))
    cert_add_parser.add_argument('--file', required=True,
        help='Sets the file name of the certificate')
    cert_add_parser.add_argument('--name', required=True,
        help='Sets the name/nickname of the certificate')
    cert_add_parser.add_argument('--primary-cert', action='store_true',
                                 help="Sets this certificate as the server's certificate")
    cert_add_parser.add_argument('--pkcs12-pin-text', help='The PKCS#12 password as plain text')
    cert_add_parser.add_argument('--pkcs12-pin-stdin', help='Read the PKCS#12 password from stdin', action='store_true')
    cert_add_parser.add_argument('--pkcs12-pin-path',  help='Path to a file containing the PKCS#12 password')
    cert_add_parser.add_argument('--do-it', dest="force", help="Force the addition of a certificate that cannot be verified",action='store_true', default=False)
    cert_add_parser.set_defaults(func=cert_add)

    cert_edit_parser = certs_sub.add_parser('set-trust-flags', help='Set the Trust flags',
        description=('Change the trust flags of a server certificate'))
    cert_edit_parser.add_argument('name', help='The name/nickname of the certificate')
    cert_edit_parser.add_argument('--flags', required=True,
        help='Sets the trust flags for the server certificate')
    cert_edit_parser.set_defaults(func=cert_edit)

    cert_del_parser = certs_sub.add_parser('del', help='Delete a certificate',
        description=('Delete a server certificate from the NSS database or DynamicCerts backend.'))
    cert_del_parser.add_argument('name', help='The name/nickname of the certificate')
    cert_del_parser.set_defaults(func=cert_del)

    cert_get_parser = certs_sub.add_parser('get', help="Display a server certificate's information",
        description=('Displays detailed information about a certificate, such as trust attributes, expiration dates, Subject and Issuer DNs '))
    cert_get_parser.add_argument('name', help='Set the name/nickname of the certificate')
    cert_get_parser.set_defaults(func=cert_get)

    cert_list_parser = certs_sub.add_parser('list', help='List the server certificates',
        description=('List all server certificates in the NSS database or DynamicCerts backend.'))
    cert_list_parser.set_defaults(func=cert_list)

    # CA certificate management
    cacerts = security_sub.add_parser('ca-certificate', help='Manage TLS certificate authorities', formatter_class=CustomHelpFormatter)
    cacerts_sub = cacerts.add_subparsers(help='ca-certificate')
    cacert_add_parser = cacerts_sub.add_parser('add', help='Add a Certificate Authority', description=(
        'Add a CA certificate (PEM or DER only) to the NSS database or DynamicCerts backend.'))
    cacert_add_parser.add_argument('--file', required=True,
        help='Path to the CA certificate file (PEM or DER). If adding a PEM bundle then specify multiple names one for '
             'each certificate, otherwise a number increment will be added to the previous name.')
    cacert_add_parser.add_argument('--name', nargs='+', required=True,
        help='Sets the name/nickname of the CA certificate')
    cacert_add_parser.add_argument('--do-it', dest="force", help="Force the addition of a certificate that cannot be verified",action='store_true', default=False)
    cacert_add_parser.set_defaults(func=cacert_add)

    cacert_edit_parser = cacerts_sub.add_parser('set-trust-flags', help='Set the Trust flags',
        description=('Change the trust attributes of a CA certificate.  Certificate Authorities typically use "CT,,"'))
    cacert_edit_parser.add_argument('name', help='The name/nickname of the CA certificate')
    cacert_edit_parser.add_argument('--flags', required=True,
        help='Sets the trust flags for the CA certificate')
    cacert_edit_parser.set_defaults(func=cert_edit)

    cacert_del_parser = cacerts_sub.add_parser('del', help='Delete a certificate',
        description=('Delete a CA certificate from the NSS database'))
    cacert_del_parser.add_argument('name', help='The name/nickname of the CA certificate')
    cacert_del_parser.set_defaults(func=cert_del)

    cacert_get_parser = cacerts_sub.add_parser('get', help="Displays a Certificate Authority's information",
        description=('Get detailed information about a CA certificate, like trust attributes, expiration dates, Subject and Issuer DN'))
    cacert_get_parser.add_argument('name', help='The name/nickname of the CA certificate')
    cacert_get_parser.set_defaults(func=cacert_get)

    cacert_list_parser = cacerts_sub.add_parser('list', help='List the Certificate Authorities',
        description=('List the CA certificates in the NSS database'))
    cacert_list_parser.set_defaults(func=cacert_list)

    # RSA management
    rsa = security_sub.add_parser('rsa', help='Query and update RSA security options', formatter_class=CustomHelpFormatter)
    rsa_sub = rsa.add_subparsers(help='rsa')
    _security_generic_set_parser(rsa_sub, RSA_ATTRS_MAP, 'Set RSA security options',
        ('Use this command for setting RSA (private key) related options located in cn=RSA,cn=encryption,cn=config.'
         '\n\nTo enable/disable RSA you can use enable and disable commands instead.'))
    _security_generic_get_parser(rsa_sub, RSA_ATTRS_MAP, 'Get RSA security options')
    _security_generic_toggle_parsers(rsa_sub, RSA, 'nsSSLActivation', '{} RSA')

    # Cipher management
    ciphers = security_sub.add_parser('ciphers', help='Manage secure ciphers', formatter_class=CustomHelpFormatter)
    ciphers_sub = ciphers.add_subparsers(help='ciphers')

    ciphers_enable = ciphers_sub.add_parser('enable', help='Enable ciphers', description=(
        'Use this command to enable specific ciphers.'))
    ciphers_enable.set_defaults(func=security_ciphers_enable)
    ciphers_enable.add_argument('cipher', nargs='+')

    ciphers_disable = ciphers_sub.add_parser('disable', help='Disable ciphers', description=(
        'Use this command to disable specific ciphers.'))
    ciphers_disable.set_defaults(func=security_ciphers_disable)
    ciphers_disable.add_argument('cipher', nargs='+')

    ciphers_get = ciphers_sub.add_parser('get', help='Get ciphers attribute', description=(
        'Use this command to get contents of nsSSL3Ciphers attribute.'))
    ciphers_get.set_defaults(func=security_ciphers_get)

    ciphers_set = ciphers_sub.add_parser('set', help='Set ciphers attribute', description=(
        'Use this command to directly set nsSSL3Ciphers attribute. It is a comma separated list '
        'of cipher names (prefixed with + or -), optionally including +all or -all. The attribute '
        'may optionally be set to keyword default. Please refer to documentation of '
        'the attribute for a more detailed description.'))
    ciphers_set.set_defaults(func=security_ciphers_set)
    ciphers_set.add_argument('cipher_string', metavar='cipher-string')

    ciphers_list = ciphers_sub.add_parser('list', help='List ciphers', description=(
        'List secure ciphers. Without arguments, list ciphers as configured in nsSSL3Ciphers attribute.'))
    ciphers_list.set_defaults(func=security_ciphers_list)
    ciphers_list_group = ciphers_list.add_mutually_exclusive_group()
    ciphers_list_group.add_argument('--enabled', action='store_true',
                                    help='Lists only enabled ciphers')
    ciphers_list_group.add_argument('--supported', action='store_true',
                                    help='Lists only supported ciphers')
    ciphers_list_group.add_argument('--disabled', action='store_true',
                                    help='Lists only supported ciphers but without enabled ciphers')

    # Certificate Signing Request Management
    csr = security_sub.add_parser('csr', help='Manage certificate signing requests', formatter_class=CustomHelpFormatter)
    csr_sub = csr.add_subparsers(help='csr')

    csr_list_parser = csr_sub.add_parser('list', help='List CSRs', description=('List all CSR files in instance'
        ' configuration directiory'))
    csr_list_parser.add_argument('--path', '-p', default=None, help="Directory contanining CSR file")
    csr_list_parser.set_defaults(func=csr_list)

    csr_get_parser = csr_sub.add_parser('get', help='Display CSR content', description=('Displays the contents of a CSR, '
        ' which can be used for submittal to CA'))
    csr_get_parser.add_argument('name', help="Name of the CSR file to display")
    csr_get_parser.set_defaults(func=csr_get)

    csr_req_parser = csr_sub.add_parser('req', help='Generate a Certificate Signing Request',
        description=('Generate a CSR that can be submitted to a CA for verification'))
    csr_req_parser.add_argument('--subject', '-s', required=True, help="Subject field")
    csr_req_parser.add_argument('--name', '-n', required=True, help="Name")
    csr_req_parser.add_argument('alt_names', nargs='*',
         help="CSR alternative names. These are auto-detected if not provided")
    csr_req_parser.set_defaults(func=csr_gen)

    csr_delete_parser = csr_sub.add_parser('del', help='Delete a CSR file', description=('Delete a CSR file'), formatter_class=CustomHelpFormatter)
    csr_delete_parser.add_argument('name', help="Name of the CSR file to delete")
    csr_delete_parser.set_defaults(func=csr_del)

    # Key Management
    key = security_sub.add_parser('key', help='Manage keys in NSS DB', formatter_class=CustomHelpFormatter)
    key_sub = key.add_subparsers(help='key')

    key_list_parser = key_sub.add_parser('list', help='List all keys in NSS DB', formatter_class=CustomHelpFormatter)
    key_list_parser.add_argument('--orphan', action='store_true', help='List orphan keys (An orphan key is'
        ' a private key in the NSS DB for which there is NO cert with the corresponding '
        ' public key). An orphan key is created during CSR generation, when the associated certificate is imported'
        ' into the NSS DB, its orphan state will be removed.')
    key_list_parser.set_defaults(func=key_list)

    key_del_parser = key_sub.add_parser('del', help='Delete a key from NSS DB', description=('Remove a'
        ' key from the NSS DB. Make sure the key is not in use before you delete'))
    key_del_parser.add_argument('key_id', help='This is the key ID displayed when listing keys')
    key_del_parser.set_defaults(func=key_del)

    # Export certificate
    export_cert_parser = security_sub.add_parser(
        'export-cert',
        help="Export a certificate to PEM or DER/Binary format.  PEM format is the default"
    )
    export_cert_parser.add_argument('nickname', help="The name of the certificate to export")
    export_cert_parser.add_argument('--binary-format', action='store_true',
                                    help="Export certificate in DER/binary format")
    export_cert_parser.add_argument('--output-file',
                                    help='The name for the exported certificate.  Default name is the certificate '
                                         'nickname with an extension of ".pem" or ".crt"')
    export_cert_parser.set_defaults(func=export_cert)
