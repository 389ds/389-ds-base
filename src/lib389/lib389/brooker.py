"""Brooker classes to organize ldap methods.
   Stuff is split in classes, like:
   * Replica
   * Backend
   * Suffix

   You will access this from:
   DirSrv.backend.methodName()
"""
import ldap
import os
import re
import time
import glob


from lib389._constants import *
from lib389 import Entry, DirSrv, InvalidArgumentError
from lib389.utils import normalizeDN, escapeDNValue, suffixfilt
from lib389 import (
    NoSuchEntryError
)


from lib389._replication import RUV
from lib389._entry import FormatDict


class Config(object):
    """
        Manage "cn=config" tree, including:
        - enable SSL
        - set access and error logging
        - get and set "cn=config" attributes
    """
    def __init__(self, conn):
        """@param conn - a DirSrv instance """
        self.conn = conn
        self.log = conn.log
        
    def set(self, key, value):
        """Set a parameter under cn=config
            @param key - the attribute name
            @param value - attribute value as string
            
            eg. set('passwordExp', 'on')
        """
        self.log.debug("set(%r, %r)" % (key, value))
        return self.conn.modify_s(DN_CONFIG,
            [(ldap.MOD_REPLACE, key, value)])
            
    def get(self, key):
        """Get an attribute under cn=config"""
        return self.conn.getEntry(DN_CONFIG).__getattr__(key)

    def loglevel(self, vals=(LOG_DEFAULT,), level='error', update=False):
        """Set the access or error log level.
        @param vals - a list of log level codes (eg. lib389.LOG_*) 
                      defaults to LOG_DEFAULT
        @param level   -   'access' or 'error'
        @param update  - False for replace (default), True for update
        
        ex. loglevel([lib389.LOG_DEFAULT, lib389.LOG_ENTRY_PARSER])
        """
        level = 'nsslapd-%slog-level' % level
        assert len(vals) > 0, "set at least one log level"
        tot = 0
        for v in vals:
            tot |= v

        if update:
            old = int(self.get(level))
            tot |= old
            self.log.debug("Update %s value: %r -> %r" % (level, old, tot))
        else:
            self.log.debug("Replace %s with value: %r" % (level, tot))

        self.set(level, str(tot))
        return tot

    def enable_ssl(self, secport=636, secargs=None):
        """Configure SSL support into cn=encryption,cn=config.

            secargs is a dict like {
                'nsSSLPersonalitySSL': 'Server-Cert'
            }
        """
        self.log.debug("configuring SSL with secargs:%r" % secargs)
        secargs = secargs or {}

        dn_enc = 'cn=encryption,cn=config'
        ciphers = '-rsa_null_md5,+rsa_rc4_128_md5,+rsa_rc4_40_md5,+rsa_rc2_40_md5,+rsa_des_sha,' + \
            '+rsa_fips_des_sha,+rsa_3des_sha,+rsa_fips_3des_sha,' + \
            '+tls_rsa_export1024_with_rc4_56_sha,+tls_rsa_export1024_with_des_cbc_sha'
        mod = [(ldap.MOD_REPLACE, 'nsSSL3', secargs.get('nsSSL3', 'on')),
               (ldap.MOD_REPLACE, 'nsSSLClientAuth',
                secargs.get('nsSSLClientAuth', 'allowed')),
               (ldap.MOD_REPLACE, 'nsSSL3Ciphers', secargs.get('nsSSL3Ciphers', ciphers))]
        self.conn.modify_s(dn_enc, mod)

        dn_rsa = 'cn=RSA,cn=encryption,cn=config'
        e_rsa = Entry(dn_rsa)
        e_rsa.update({
            'objectclass': ['top', 'nsEncryptionModule'],
            'nsSSLPersonalitySSL': secargs.get('nsSSLPersonalitySSL', 'Server-Cert'),
            'nsSSLToken': secargs.get('nsSSLToken', 'internal (software)'),
            'nsSSLActivation': secargs.get('nsSSLActivation', 'on')
        })
        try:
            self.conn.add_s(e_rsa)
        except ldap.ALREADY_EXISTS:
            pass

        mod = [
            (ldap.MOD_REPLACE,
                'nsslapd-security', secargs.get('nsslapd-security', 'on')),
            (ldap.MOD_REPLACE,
                'nsslapd-ssl-check-hostname', secargs.get('nsslapd-ssl-check-hostname', 'off')),
            (ldap.MOD_REPLACE,
                'nsslapd-secureport', str(secport))
        ]
        self.log.debug("trying to modify %r with %r" % (DN_CONFIG, mod))
        self.conn.modify_s(DN_CONFIG, mod)

        fields = 'nsslapd-security nsslapd-ssl-check-hostname'.split()
        return self.conn.getEntry(DN_CONFIG, attrlist=fields)


class Index(object):
    
    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log
    
    def delete_all(self, benamebase):
        dn = "cn=index, cn=" + benamebase + "," + DN_LDBM
        
        # delete each defined index
        ents = self.conn.search_s(dn, ldap.SCOPE_ONELEVEL)
        for ent in ents:
            self.log.debug("Delete index entry %s" % (ent.dn))
            self.conn.delete_s(ent.dn)
            
        # Then delete the top index entry
        self.log.debug("Delete head index entry %s" % (dn))
        self.conn.delete_s(dn)
