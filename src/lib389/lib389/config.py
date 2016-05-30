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
        #self.conn = conn
        #self.log = conn.log

    def _alter_log_enabled(self, service, state):
        if service not in ('access', 'error', 'audit'):
            self._log.error('Attempted to enable invalid log service "%s"' %
                           service)
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
            self._log.error('Attempted to set level on invalid log service "%s"'
                           % service)
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

    #### THIS WILL BE SPLIT OUT TO ITS OWN MODULE
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
        self.conn.modify_s(dn_enc, mod)

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
            self.conn.add_s(e_rsa)
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
        self.log.debug("trying to modify %r with %r" % (DN_CONFIG, mod))
        self.conn.modify_s(DN_CONFIG, mod)

        fields = 'nsslapd-security nsslapd-ssl-check-hostname'.split()
        return self.conn.getEntry(DN_CONFIG, attrlist=fields)


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
        # Once created, don't allow it's removal
        self._protected = True


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

    def _validate(self, tdn, properties):
        (dn, valid_props) = super(RSA, self)._validate(tdn, properties)
        # Ensure that dn matches self._dn
        assert(self._dn == dn)
        return (dn, valid_props)

    def create(self, dn=None, properties={'cn': 'RSA'}):
        # Is this the best way to for the dn?
        if dn is not None:
            self._log.debug("dn on cn=Rsa create request is not None. This is a mistake.")
        super(RSA, self).create(dn=self._dn, properties=properties)


