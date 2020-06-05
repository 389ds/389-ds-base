# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from collections import OrderedDict, namedtuple
import json
import os
from lib389.config import Config, Encryption, RSA
from lib389.nss_ssl import NssSsl


Props = namedtuple('Props', ['cls', 'attr', 'help', 'values'])

onoff = ('on', 'off')
protocol_versions = ('SSLv3', 'TLS1.0', 'TLSv1.1', 'TLSv1.2', 'TLSv1.3', '')
SECURITY_ATTRS_MAP = OrderedDict([
    ('security', Props(Config, 'nsslapd-security',
                       'Enable or disable security',
                       onoff)),
    ('listen-host', Props(Config, 'nsslapd-securelistenhost',
                          'Host/address to listen on for LDAPS',
                          str)),
    ('secure-port', Props(Config, 'nsslapd-securePort',
                          'Port for LDAPS to listen on',
                          range(1, 65536))),
    ('tls-client-auth', Props(Encryption, 'nsSSLClientAuth',
                              'Client authentication requirement',
                              ('off', 'allowed', 'required'))),
    ('tls-client-renegotiation', Props(Encryption, 'nsTLSAllowClientRenegotiation',
                                       'Allow client TLS renegotiation',
                                       onoff)),
    ('require-secure-authentication', Props(Config, 'nsslapd-require-secure-binds',
                                            'Require binds over LDAPS, StartTLS, or SASL',
                                            onoff)),
    ('check-hostname', Props(Config, 'nsslapd-ssl-check-hostname',
                             'Check Subject of remote certificate against the hostname',
                             onoff)),
    ('verify-cert-chain-on-startup', Props(Config, 'nsslapd-validate-cert',
                                           'Validate server certificate during startup',
                                           ('warn', *onoff))),
    ('session-timeout', Props(Encryption, 'nsSSLSessionTimeout',
                              'Secure session timeout',
                              int)),
    ('tls-protocol-min', Props(Encryption, 'sslVersionMin',
                               'Secure protocol minimal allowed version',
                               protocol_versions)),
    ('tls-protocol-max', Props(Encryption, 'sslVersionMax',
                               'Secure protocol maximal allowed version',
                               protocol_versions)),
    ('allow-insecure-ciphers', Props(Encryption, 'allowWeakCipher',
                                     'Allow weak ciphers for legacy use',
                                     onoff)),
    ('allow-weak-dh-param', Props(Encryption, 'allowWeakDHParam',
                                  'Allow short DH params for legacy use',
                                  onoff)),
    ('cipher-pref', Props(Encryption, 'nsSSL3Ciphers',
                          'Use this command to directly set nsSSL3Ciphers attribute. It is a comma separated list '
                          'of cipher names (prefixed with + or -), optionally including +all or -all. The attribute '
                          'may optionally be prefixed by keyword default. Please refer to documentation of '
                          'the attribute for a more detailed description.',
                          onoff)),
])

RSA_ATTRS_MAP = OrderedDict([
    ('tls-allow-rsa-certificates', Props(RSA, 'nsSSLActivation',
                                         'Activate use of RSA certificates', onoff)),
    ('nss-cert-name', Props(RSA, 'nsSSLPersonalitySSL',
                            'Server certificate name in NSS DB', str)),
    ('nss-token', Props(RSA, 'nsSSLToken',
                        'Security token name (module of NSS DB)', str))
])


def _security_generic_get(inst, basedn, logs, args, attrs_map):
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


def _security_generic_set(inst, basedn, logs, args, attrs_map):
    for attr, props in attrs_map.items():
        arg = getattr(args, attr.replace('-', '_'))
        if arg is None:
            continue
        dsobj = props.cls(inst)
        if arg != "":
            dsobj.replace(props.attr, arg)
        else:
            dsobj.remove_all(props.attr)


def _security_generic_get_parser(parent, attrs_map, help):
    p = parent.add_parser('get', help=help)
    p.set_defaults(func=lambda *args: _security_generic_get(*args, attrs_map))
    return p


def _security_generic_set_parser(parent, attrs_map, help, description):
    p = parent.add_parser('set', help=help, description=description)
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
        p = parent.add_parser(action.lower(), help=help_pattern.format(action))
        p.set_defaults(func=lambda *args: _security_generic_toggle(*args, cls, attr, value, action))
        return p

    return list(map(add_parser, ('Enable', 'Disable'), ('on', 'off')))

def security_enable(inst, basedn, log, args):
    dbpath = inst.get_cert_dir()
    tlsdb = NssSsl(dbpath=dbpath)
    certs = tlsdb.list_certs()
    if len(certs) == 0:
        raise ValueError('There are no server certificates in the security ' +
                         'database, security can not be enabled.')

    if len(certs) == 1:
        # If there is only cert make sure it is set as the server certificate
        RSA(inst).set('nsSSLPersonalitySSL', certs[0][0])
    elif args.cert_name is not None:
        # A certificate nickname was provided, set it as the server certificate
        RSA(inst).set('nsSSLPersonalitySSL', args.cert_name)

    # it should now be safe to enable security
    Config(inst).set('nsslapd-security', 'on')


def security_disable(inst, basedn, log, args):
    Config(inst).set('nsslapd-security', 'off')


def security_ciphers_enable(inst, basedn, log, args):
    _security_ciphers_change('+', args.cipher, inst, log)


def security_ciphers_disable(inst, basedn, log, args):
    _security_ciphers_change('-', args.cipher, inst, log)


def security_ciphers_set(inst, basedn, log, args):
    enc = Encryption(inst)
    enc.ciphers = args.cipher_string.split(',')


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


def cert_add(inst, basedn, log, args):
    """Add server certificate
    """
    # Verify file and certificate name
    os.path.isfile(args.file)
    tlsdb = NssSsl(dirsrv=inst)
    if not tlsdb._db_exists(even_partial=True):  # we want to be very careful
        log.info('Security database does not exist. Creating a new one in {}.'.format(inst.get_cert_dir()))
        tlsdb.reinit()

    try:
        tlsdb.get_cert_details(args.name)
        raise ValueError("Certificate already exists with the same name")
    except ValueError:
        pass

    if args.primary_cert:
        # This is the server's primary certificate, update RSA entry
        RSA(inst).set('nsSSLPersonalitySSL', args.name)

    # Add the cert
    tlsdb.add_cert(args.name, args.file)


def cacert_add(inst, basedn, log, args):
    """Add CA certificate
    """
    # Verify file and certificate name
    os.path.isfile(args.file)
    tlsdb = NssSsl(dirsrv=inst)
    if not tlsdb._db_exists(even_partial=True):  # we want to be very careful
        log.info('Security database does not exist. Creating a new one in {}.'.format(inst.get_cert_dir()))
        tlsdb.reinit()

    try:
        tlsdb.get_cert_details(args.name)
        raise ValueError("Certificate already exists with the same name")
    except ValueError:
        pass

    # Add the cert
    tlsdb.add_cert(args.name, args.file, ca=True)


def cert_list(inst, basedn, log, args):
    """List all the server certificates
    """
    cert_list = []
    tlsdb = NssSsl(dirsrv=inst)
    certs = tlsdb.list_certs()
    for cert in certs:
        if args.json:
            cert_list.append(
                {
                    "type": "certificate",
                    "attrs": {
                                'nickname': cert[0],
                                'subject': cert[1],
                                'issuer': cert[2],
                                'expires': cert[3],
                                'flags': cert[4],
                            }
                }
            )
        else:
            log.info('Certificate Name: {}'.format(cert[0]))
            log.info('Subject DN: {}'.format(cert[1]))
            log.info('Issuer DN: {}'.format(cert[2]))
            log.info('Expires: {}'.format(cert[3]))
            log.info('Trust Flags: {}\n'.format(cert[4]))
    if args.json:
        log.info(json.dumps(cert_list, indent=4))


def cacert_list(inst, basedn, log, args):
    """List all CA certs
    """
    cert_list = []
    tlsdb = NssSsl(dirsrv=inst)
    certs = tlsdb.list_certs(ca=True)
    for cert in certs:
        if args.json:
            cert_list.append(
                {
                    "type": "certificate",
                    "attrs": {
                                'nickname': cert[0],
                                'subject': cert[1],
                                'issuer': cert[2],
                                'expires': cert[3],
                                'flags': cert[4],
                            }
                }
            )
        else:
            log.info('Certificate Name: {}'.format(cert[0]))
            log.info('Subject DN: {}'.format(cert[1]))
            log.info('Issuer DN: {}'.format(cert[2]))
            log.info('Expires: {}'.format(cert[3]))
            log.info('Trust Flags: {}\n'.format(cert[4]))
    if args.json:
        log.info(json.dumps(cert_list, indent=4))


def cert_get(inst, basedn, log, args):
    """Get the details about a server certificate
    """
    tlsdb = NssSsl(dirsrv=inst)
    details = tlsdb.get_cert_details(args.name)
    if args.json:
        log.info(json.dumps(
                {
                    "type": "certificate",
                    "attrs": {
                                'nickname': details[0],
                                'subject': details[1],
                                'issuer': details[2],
                                'expires': details[3],
                                'flags': details[4],
                            }
                }, indent=4
            )
        )
    else:
        log.info('Certificate Name: {}'.format(details[0]))
        log.info('Subject DN: {}'.format(details[1]))
        log.info('Issuer DN: {}'.format(details[2]))
        log.info('Expires: {}'.format(details[3]))
        log.info('Trust Flags: {}'.format(details[4]))


def cert_edit(inst, basedn, log, args):
    """Edit cert
    """
    tlsdb = NssSsl(dirsrv=inst)
    tlsdb.edit_cert_trust(args.name, args.flags)


def cert_del(inst, basedn, log, args):
    """Delete cert
    """
    tlsdb = NssSsl(dirsrv=inst)
    tlsdb.del_cert(args.name)


def create_parser(subparsers):
    security = subparsers.add_parser('security', help='Query and manipulate security options')
    security_sub = security.add_subparsers(help='security')

    # Core security management
    _security_generic_set_parser(security_sub, SECURITY_ATTRS_MAP, 'Set general security options',
        ('Use this command for setting security related options located in cn=config and cn=encryption,cn=config.'
         '\n\nTo enable/disable security you can use enable and disable commands instead.'))
    _security_generic_get_parser(security_sub, SECURITY_ATTRS_MAP, 'Get general security options')
    security_enable_p = security_sub.add_parser('enable', help='Enable security', description=(
        'If missing, create security database, then turn on security functionality. Please note this is usually not'
        ' enough for TLS connections to work - proper setup of CA and server certificate is necessary.'))
    security_enable_p.add_argument('--cert-name', default=None,
        help='The name of the certificate the server should use')
    security_enable_p.set_defaults(func=security_enable)
    security_disable_p = security_sub.add_parser('disable', help='Disable security', description=(
        'Turn off security functionality. The rest of the configuration will be left untouched.'))
    security_disable_p.set_defaults(func=security_disable)

    # Server certificate management
    certs = security_sub.add_parser('certificate', help='Manage TLS certificates')
    certs_sub = certs.add_subparsers(help='certificate')
    cert_add_parser = certs_sub.add_parser('add', help='Add a server certificate', description=(
        'Add a server certificate to the NSS database'))
    cert_add_parser.add_argument('--file', required=True,
        help='The file name of the certificate')
    cert_add_parser.add_argument('--name', required=True,
        help='The name/nickname of the certificate')
    cert_add_parser.add_argument('--primary-cert', action='store_true',
                                 help="Set this certificate as the server's certificate")
    cert_add_parser.set_defaults(func=cert_add)

    cert_edit_parser = certs_sub.add_parser('set-trust-flags', help='Set the Trust flags',
        description=('Change the trust flags of a server certificate'))
    cert_edit_parser.add_argument('name', help='The name/nickname of the certificate')
    cert_edit_parser.add_argument('--flags', required=True,
        help='The trust flags for the server certificate')
    cert_edit_parser.set_defaults(func=cert_edit)

    cert_del_parser = certs_sub.add_parser('del', help='Delete a certificate',
        description=('Delete a certificate from the NSS database'))
    cert_del_parser.add_argument('name', help='The name/nickname of the certificate')
    cert_del_parser.set_defaults(func=cert_del)

    cert_get_parser = certs_sub.add_parser('get', help="Get a server certificate's information",
        description=('Get detailed information about a certificate, like trust attributes, expiration dates, Subject and Issuer DNs '))
    cert_get_parser.add_argument('name', help='The name/nickname of the certificate')
    cert_get_parser.set_defaults(func=cert_get)

    cert_list_parser = certs_sub.add_parser('list', help='List the server certificates',
        description=('List the server certificates in the NSS database'))
    cert_list_parser.set_defaults(func=cert_list)

    # CA certificate management
    cacerts = security_sub.add_parser('ca-certificate', help='Manage TLS Certificate Authorities')
    cacerts_sub = cacerts.add_subparsers(help='ca-certificate')
    cacert_add_parser = cacerts_sub.add_parser('add', help='Add a Certificate Authority', description=(
        'Add a Certificate Authority to the NSS database'))
    cacert_add_parser.add_argument('--file', required=True,
        help='The file name of the CA certificate')
    cacert_add_parser.add_argument('--name', required=True,
        help='The name/nickname of the CA certificate')
    cacert_add_parser.set_defaults(func=cacert_add)

    cacert_edit_parser = cacerts_sub.add_parser('set-trust-flags', help='Set the Trust flags',
        description=('Change the trust attributes of a CA certificate.  Certificate Authorities typically use "CT,,"'))
    cacert_edit_parser.add_argument('name', help='The name/nickname of the CA certificate')
    cacert_edit_parser.add_argument('--flags', required=True,
        help='The trust flags for the CA certificate')
    cacert_edit_parser.set_defaults(func=cert_edit)

    cacert_del_parser = cacerts_sub.add_parser('del', help='Delete a certificate',
        description=('Delete a CA certificate from the NSS database'))
    cacert_del_parser.add_argument('name', help='The name/nickname of the CA certificate')
    cacert_del_parser.set_defaults(func=cert_del)

    cacert_get_parser = cacerts_sub.add_parser('get', help="Get a Certificate Authority's information",
        description=('Get detailed information about a CA certificate, like trust attributes, expiration dates, Subject and Issuer DN'))
    cacert_get_parser.add_argument('name', help='The name/nickname of the CA certificate')
    cacert_get_parser.set_defaults(func=cert_get)

    cacert_list_parser = cacerts_sub.add_parser('list', help='List the Certificate Authorities',
        description=('List the CA certificates in the NSS database'))
    cacert_list_parser.set_defaults(func=cacert_list)

    # RSA management
    rsa = security_sub.add_parser('rsa', help='Query and manipulate RSA security options')
    rsa_sub = rsa.add_subparsers(help='rsa')
    _security_generic_set_parser(rsa_sub, RSA_ATTRS_MAP, 'Set RSA security options',
        ('Use this command for setting RSA (private key) related options located in cn=RSA,cn=encryption,cn=config.'
         '\n\nTo enable/disable RSA you can use enable and disable commands instead.'))
    _security_generic_get_parser(rsa_sub, RSA_ATTRS_MAP, 'Get RSA security options')
    _security_generic_toggle_parsers(rsa_sub, RSA, 'nsSSLActivation', '{} RSA')

    # Cipher management
    ciphers = security_sub.add_parser('ciphers', help='Manage secure ciphers')
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
        'may optionally be prefixed by keyword default. Please refer to documentation of '
        'the attribute for a more detailed description.'))
    ciphers_set.set_defaults(func=security_ciphers_set)
    ciphers_set.add_argument('cipher_string', metavar='cipher-string')

    ciphers_list = ciphers_sub.add_parser('list', help='List ciphers', description=(
        'List secure ciphers. Without arguments, list ciphers as configured in nsSSL3Ciphers attribute.'))
    ciphers_list.set_defaults(func=security_ciphers_list)
    ciphers_list_group = ciphers_list.add_mutually_exclusive_group()
    ciphers_list_group.add_argument('--enabled', action='store_true',
                                    help='Only enabled ciphers')
    ciphers_list_group.add_argument('--supported', action='store_true',
                                    help='Only supported ciphers')
    ciphers_list_group.add_argument('--disabled', action='store_true',
                                    help='Only supported ciphers without enabled ciphers')
