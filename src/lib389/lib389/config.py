# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Brooker classes to organize ldap methods.
   Stuff is split in classes, like:
   * Replica
   * Backend
   * Suffix

   You will access this from:
   DirSrv.backend.methodName()
"""

import ldap
from lib389._constants import *
from lib389 import Entry
from lib389._mapped_object import DSLdapObject
from lib389.utils import ensure_bytes, ensure_str

from lib389.lint import DSCLE0001, DSCLE0002, DSELE0001

class Config(DSLdapObject):
    """
        Manage "cn=config" tree, including:
        - enable SSL
        - set access and error logging
        - get and set "cn=config" attributes
    """
    def __init__(self, conn, batch=False):
        """@param conn - a DirSrv instance """
        super(Config, self).__init__(instance=conn, batch=batch)
        self._dn = DN_CONFIG
        # self._instance = conn
        # self.log = conn.log
        config_compare_exclude = [
            'nsslapd-ldapifilepath', 
            'nsslapd-accesslog', 
            'nsslapd-auditfaillog', 
            'nsslapd-ldifdir',
            'nsslapd-errorlog', 
            'nsslapd-instancedir', 
            'nsslapd-lockdir', 
            'nsslapd-bakdir',
            'nsslapd-schemadir', 
            'nsslapd-auditlog',
            'nsslapd-rootpw', 
            'nsslapd-workingdir',
            'nsslapd-certdir'
        ]
        self._compare_exclude  = self._compare_exclude + config_compare_exclude
        self._rdn_attribute = 'cn'
        self._lint_functions = [self._lint_hr_timestamp, self._lint_passwordscheme]

    @property
    def dn(self):
        return DN_CONFIG

    @property
    def rdn(self):
        return DN_CONFIG

    def _alter_log_enabled(self, service, state):
        if service not in ('access', 'error', 'audit'):
            self._log.error('Attempted to enable invalid log service "%s"' % service)
        service = 'nsslapd-%slog-logging-enabled' % service
        self._log.debug('Setting log %s to %s' % (service, state))
        self.set(service, state)

    def enable_log(self, service):
        """Enable a logging service in the 389ds instance.
        @param service - The logging service to enable. Can be one of 'access',
                         'error' or 'audit'.

        ex. enable_log('audit')
        """
        self._alter_log_enabled(service, 'on')

    def disable_log(self, service):
        """Disable a logging service in the 389ds instance.
        @param service - The logging service to Disable. Can be one of 'access'
                         , 'error' or 'audit'.

        ex. disable_log('audit')
        """
        self._alter_log_enabled(service, 'off')

    def loglevel(self, vals=(LOG_DEFAULT,), service='error', update=False):
        """Set the access or error log level.
        @param vals - a list of log level codes (eg. lib389.LOG_*)
                      defaults to LOG_DEFAULT
        @param service - 'access' or 'error'. There is no 'audit' log level.
                         use enable_log or disable_log.
        @param update  - False for replace (default), True for update

        ex. loglevel([lib389.LOG_DEFAULT, lib389.LOG_ENTRY_PARSER])
        """
        if service not in ('access', 'error'):
            self._log.error('Attempted to set level on invalid log service "%s"' % service)
        service = 'nsslapd-%slog-level' % service
        assert len(vals) > 0, "set at least one log level"
        tot = 0
        for v in vals:
            tot |= v

        if update:
            old = int(self.get(service))
            tot |= old
            self._log.debug("Update %s value: %r -> %r" % (service, old, tot))
        else:
            self._log.debug("Replace %s with value: %r" % (service, tot))

        self.set(service, str(tot))
        return tot

    def logbuffering(self, state=True):
        if state:
            value = 'on'
        else:
            value = 'off'

        self.set('nsslapd-accesslog-logbuffering', value)

    def reset(self, key):
        self.set(key, None, action=ldap.MOD_DELETE)

    # THIS WILL BE SPLIT OUT TO ITS OWN MODULE
    def enable_ssl(self, secport=636, secargs=None):
        """Configure SSL support into cn=encryption,cn=config.

            secargs is a dict like {
                'nsSSLPersonalitySSL': 'Server-Cert'
            }
        """
        self._log.debug("config.enable_ssl is deprecated! Use RSA, Encryption instead!")
        self._log.debug("configuring SSL with secargs:%r" % secargs)
        secargs = secargs or {}

        dn_enc = 'cn=encryption,cn=config'
        ciphers = ('-rsa_null_md5,+rsa_rc4_128_md5,+rsa_rc4_40_md5,'
                   '+rsa_rc2_40_md5,+rsa_des_sha,+rsa_fips_des_sha,'
                   '+rsa_3des_sha,+rsa_fips_3des_sha,+tls_rsa_export1024'
                   '_with_rc4_56_sha,+tls_rsa_export1024_with_des_cbc_sha')
        mod = [(ldap.MOD_REPLACE, 'nsSSL3', secargs.get('nsSSL3', 'on')),
               (ldap.MOD_REPLACE, 'nsSSLClientAuth',
                secargs.get('nsSSLClientAuth', 'allowed')),
               (ldap.MOD_REPLACE, 'nsSSL3Ciphers', secargs.get('nsSSL3Ciphers',
                ciphers))]
        self._instance.modify_s(dn_enc, mod)

        dn_rsa = 'cn=RSA,cn=encryption,cn=config'
        e_rsa = Entry(dn_rsa)
        e_rsa.update({
            'objectclass': ['top', 'nsEncryptionModule'],
            'nsSSLPersonalitySSL': secargs.get('nsSSLPersonalitySSL',
                                               'Server-Cert'),
            'nsSSLToken': secargs.get('nsSSLToken', 'internal (software)'),
            'nsSSLActivation': secargs.get('nsSSLActivation', 'on')
        })
        try:
            self._instance.add_s(e_rsa)
        except ldap.ALREADY_EXISTS:
            pass

        mod = [
            (ldap.MOD_REPLACE,
             'nsslapd-security',
             secargs.get('nsslapd-security', 'on')),
            (ldap.MOD_REPLACE,
             'nsslapd-ssl-check-hostname',
             secargs.get('nsslapd-ssl-check-hostname', 'off')),
            (ldap.MOD_REPLACE,
             'nsslapd-secureport',
             str(secport))
        ]
        self._log.debug("trying to modify %r with %r" % (DN_CONFIG, mod))
        self._instance.modify_s(DN_CONFIG, mod)

        fields = 'nsslapd-security nsslapd-ssl-check-hostname'.split()
        return self._instance.getEntry(DN_CONFIG, attrlist=fields)

    def _lint_hr_timestamp(self):
        hr_timestamp = self.get_attr_val('nsslapd-logging-hr-timestamps-enabled')
        if ensure_bytes('on') != hr_timestamp:
            return DSCLE0001
        pass # nsslapd-logging-hr-timestamps-enabled

    def _lint_passwordscheme(self):
        allowed_schemes = ['SSHA512', 'PBKDF2_SHA256']
        u_password_scheme = self.get_attr_val_utf8('passwordStorageScheme')
        u_root_scheme = self.get_attr_val_utf8('nsslapd-rootpwstoragescheme')
        if u_root_scheme not in allowed_schemes or u_password_scheme not in allowed_schemes:
            return DSCLE0002
        return None

class Encryption(DSLdapObject):
    """
        Manage "cn=encryption,cn=config" tree, including:
        - ssl ciphers
        - ssl / tls levels
    """
    def __init__(self, conn, batch=False):
        """@param conn - a DirSrv instance """
        super(Encryption, self).__init__(instance=conn, batch=batch)
        self._dn = 'cn=encryption,%s' % DN_CONFIG
        self._create_objectclasses = ['top', 'nsEncryptionConfig']
        # Once created, don't allow it's removal
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._protected = True
        self._lint_functions = [self._lint_check_tls_version]

    def create(self, rdn=None, properties={'cn': 'encryption', 'nsSSLClientAuth': 'allowed'}):
        if rdn is not None:
            self._log.debug("dn on cn=encryption is not None. This is a mistake.")
        super(Encryption, self).create(properties=properties)

    def _lint_check_tls_version(self):
        tls_min = self.get_attr_val('sslVersionMin');
        if tls_min < ensure_bytes('TLS1.1'):
            return DSELE0001
        return None

class RSA(DSLdapObject):
    """
        Manage the "cn=RSA,cn=encryption,cn=config" object
        - Set the certificate name
        - Database path
        - ssl token name
    """
    def __init__(self, conn, batch=False):
        """@param conn - a DirSrv instance """
        super(RSA, self).__init__(instance=conn, batch=batch)
        self._dn = 'cn=RSA,cn=encryption,%s' % DN_CONFIG
        self._create_objectclasses = ['top', 'nsEncryptionModule']
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        # Once we create it, don't remove it
        self._protected = True

    def _validate(self, rdn, properties, basedn):
        (dn, valid_props) = super(RSA, self)._validate(rdn, properties, basedn)
        # Ensure that dn matches self._dn
        assert(self._dn == dn)
        return (dn, valid_props)

    def create(self, rdn=None, properties={'cn': 'RSA', 'nsSSLPersonalitySSL': 'Server-Cert', 'nsSSLActivation': 'on', 'nsSSLToken': 'internal (software)'}):
        # Is this the best way to for the dn?
        if rdn is not None:
            self._log.debug("dn on cn=Rsa create request is not None. This is a mistake.")
        # Our self._dn is already set, no need for rdn.
        super(RSA, self).create(properties=properties)

class CertmapLegacy(object):
    """
    Manage certificate mappings in Directory Server

    This is based on the old certmap fileformat. As a result, the interface is rather
    crude, until we re-write to use dynamic mappings.
    """
    def __init__(self, conn):
        self._instance = conn
        pass

    def reset(self):
        """
        Reset the certmap to empty.
        """
        certmap = os.path.join(self._instance.get_config_dir(), 'certmap.conf')
        with open(certmap, 'w') as f:
            f.write('# LDAP Certificate mappings \n')

    def _parse_maps(self, maps):
        certmaps = {}
        cur_map = None
        for l in maps:
            if l.startswith('certmap'):
                # Line matches format of: certmap name issuer
                (_, name, issuer) = l.split(None, 2)
                certmaps[name] = {
                    'DNComps': None,
                    'FilterComps': None,
                    'VerifyCert': None,
                    'CmapLdapAttr': None,
                    'Library': None,
                    'InitFn': None,
                }
                certmaps[name]['issuer'] = issuer
            else:
                # The line likely is:
                # name:property [value]
                (name, pav) = l.split(':')
                pavs = pav.split(None, 1)
                if len(pavs) == 1:
                    # We have an empty property
                    certmaps[name][pav] = ''
                else:
                    # We clearly have a value.
                    if pavs[0] == 'DNComps' or pavs[0] == 'FilterComps':
                        # These two are comma sep lists
                        values = [w.split for w in pavs[1].split(',')]
                        certmaps[name][pavs[0]] = values
                    else:
                        certmaps[name][pavs[0]] = pavs[1]
        return certmaps

    def list(self):
        """
        Parse and list current certmaps.
        """
        certmap = os.path.join(self._instance.get_config_dir(), 'certmap.conf')
        maps = []
        with open(certmap, 'r') as f:
            for line in f.readlines():
                s_line = line.strip()
                if not s_line.startswith('#'):
                    content = s_line.split('#')[0]
                    if content != '':
                        maps.append(content)
        certmaps = self._parse_maps(maps)
        return certmaps

    def set(self, certmaps):
        """
        Take a dict of certmaps and write them out.
        """
        output = ""
        for name in certmaps:
            certmap = certmaps[name]
            output += "certmap %s %s\n" % (name, certmap['issuer'])
            for v in ['DNComps', 'FilterComps']:
                if certmap[v] == None:
                    output += "# %s:%s\n" % (name, v)
                elif certmap[v] == '':
                    output += "%s:%s\n" % (name, v)
                else:
                    output += "%s:%s %s\n" % (name, v, ', '.join(certmap[v]))
            for v in ['VerifyCert', 'CmapLdapAttr', 'Library', 'InitFn']:
                if certmap[v] == None:
                    output += "# %s:%s\n" % (name, v)
                else:
                    output += "%s:%s %s\n" % (name, v, certmap[v])
        # Now write it out
        certmap = os.path.join(self._instance.get_config_dir(), 'certmap.conf')
        with open(certmap, 'w') as f:
            f.write(output)



