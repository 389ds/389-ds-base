# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Helpers for managing NSS databases in Directory Server
"""

import os
import sys
import random
import string
import re
# from nss import nss
from subprocess import check_call, check_output
from lib389.passwd import password_generate

KEYBITS = 4096
CA_NAME = 'Self-Signed-CA'
CERT_NAME = 'Server-Cert'
PIN_TXT = 'pin.txt'
PWD_TXT = 'pwdfile.txt'
ISSUER = 'CN=ca.unknown.example.com,O=testing,L=unknown,ST=Queensland,C=AU'
SELF_ISSUER = 'CN={HOSTNAME},O=testing,L=unknown,ST=Queensland,C=AU'
VALID = 2


class NssSsl(object):
    def __init__(self, dirsrv, dbpassword=None):
        self.dirsrv = dirsrv
        self.log = self.dirsrv.log
        if dbpassword is None:
            self.dbpassword = password_generate()
        else:
            self.dbpassword = dbpassword

    @property
    def _certdb(self):
        # return "sql:%s" % self.dirsrv.confdir
        return self.dirsrv.confdir

    def _generate_noise(self, fpath):
        noise = password_generate(256)
        with open(fpath, 'w') as f:
            f.write(noise)


    def reinit(self):
        """
        Re-init (create) the nss db.
        """
        # 48886: The DB that DS ships with is .... well, broken. Purge it!
        for f in ('key3.db', 'cert8.db', 'key4.db', 'cert9.db', 'secmod.db', 'pkcs11.txt'):
            try:
                # Perhaps we should be backing these up instead ...
                os.remove("%s/%s" % (self.dirsrv.confdir, f ))
            except:
                pass

        # In the future we may add the needed option to avoid writing the pin
        # files.
        # Write the pin.txt, and the pwdfile.txt
        if not os.path.exists('%s/%s' % (self.dirsrv.confdir, PIN_TXT)):
            with open('%s/%s' % (self.dirsrv.confdir, PIN_TXT), 'w') as f:
                f.write('Internal (Software) Token:%s' % self.dbpassword)
        if not os.path.exists('%s/%s' % (self.dirsrv.confdir, PWD_TXT)):
            with open('%s/%s' % (self.dirsrv.confdir, PWD_TXT), 'w') as f:
                f.write('%s' % self.dbpassword)

        # Init the db.
        # 48886; This needs to be sql format ...
        cmd = ['/usr/bin/certutil', '-N', '-d', self._certdb, '-f', '%s/%s' % (self.dirsrv.confdir, PWD_TXT)]
        self.dirsrv.log.debug("nss cmd: %s" % cmd)
        result = check_output(cmd)
        self.dirsrv.log.debug("nss output: %s" % result)
        return True

    def _db_exists(self):
        """
        Check that a nss db exists at the certpath
        """
        key3 = os.path.exists("%s/key3.db" % (self.dirsrv.confdir))
        cert8 = os.path.exists("%s/cert8.db" % (self.dirsrv.confdir))
        key4 = os.path.exists("%s/key4.db" % (self.dirsrv.confdir))
        cert9 = os.path.exists("%s/cert9.db" % (self.dirsrv.confdir))
        secmod = os.path.exists("%s/secmod.db" % (self.dirsrv.confdir))
        pkcs11 = os.path.exists("%s/pkcs11.txt" % (self.dirsrv.confdir))

        if ((key3 and cert8 and secmod) or (key4 and cert9 and pkcs11)):
            return True
        return False

    def create_rsa_ca(self):
        """
        Create a self signed CA.
        """

        # Create noise.
        self._generate_noise('%s/noise.txt' % self.dirsrv.confdir)
        # Now run the command. Can we do this with NSS native?
        cmd = [
            '/usr/bin/certutil',
            '-S',
            '-n',
            CA_NAME,
            '-s',
            ISSUER,
            '-x',
            '-g',
            '%s' % KEYBITS,
            '-t',
            'CT,,',
            '-v',
            '%s' % VALID,
            '-d',
            self._certdb,
            '-z',
            '%s/noise.txt' % self.dirsrv.confdir,
            '-f',
            '%s/%s' % (self.dirsrv.confdir, PWD_TXT),
        ]
        result = check_output(cmd)
        self.dirsrv.log.debug("nss output: %s" % result)
        # Now extract the CAcert to a well know place.
        cmd = [
            '/usr/bin/certutil',
            '-L',
            '-n',
            CA_NAME,
            '-d',
            self._certdb,
            '-a',
        ]
        certdetails = check_output(cmd)
        with open('%s/ca.crt' % self.dirsrv.confdir, 'w') as f:
            f.write(certdetails)
        if os.path.isfile('/usr/sbin/cacertdir_rehash'):
            check_output(['/usr/sbin/cacertdir_rehash', self.dirsrv.confdir])
        return True

    def _rsa_cert_list(self):
        cmd = [
            '/usr/bin/certutil',
            '-L',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self.dirsrv.confdir, PWD_TXT),
        ]
        result = check_output(cmd)

        # We can skip the first few lines. They are junk
        # IE ['', 
        #     'Certificate Nickname                                         Trust Attributes', 
        #     '                                                             SSL,S/MIME,JAR/XPI', 
        #     '', 
        #     'Self-Signed-CA                                               CTu,u,u', 
        #     '']
        lines = result.split('\n')[4:-1]
        # Now make the lines usable
        cert_values = []
        for line in lines:
            data = line.split()
            cert_values.append((data[0], data[1]))
        return cert_values

    def _rsa_cert_key_exists(self, cert_tuple):
        name = cert_tuple[0]
        cmd = [
            '/usr/bin/certutil',
            '-K',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self.dirsrv.confdir, PWD_TXT),
        ]
        result = check_output(cmd)

        lines = result.split('\n')[1:-1]
        key_list = []
        for line in lines:
            m = re.match('\<(?P<id>.*)\> (?P<type>\w+)\s+(?P<hash>\w+).*:(?P<name>.+)', line)
            if name == m.group('name'):
                return True
        return False

    def _rsa_cert_is_catrust(self, cert_tuple):
        trust_flags = cert_tuple[1]
        (ssl_flag, mime_flag, jar_flag) = trust_flags.split(',')
        return 'C' in ssl_flag

    def _rsa_ca_exists(self):
        """
        Detect if a self-signed ca exists
        """
        have_ca = False
        cert_list = self._rsa_cert_list()
        for cert in cert_list:
            if self._rsa_cert_key_exists(cert) and self._rsa_cert_is_catrust(cert):
                have_ca = True
        return have_ca

    def _rsa_key_and_cert_exists(self):
        """
        Check if a valid server key and cert pain exist.
        """
        have_cert = False
        cert_list = self._rsa_cert_list()
        for cert in cert_list:
            # This could do a better check for !ca, and server attrs
            if self._rsa_cert_key_exists(cert) and not self._rsa_cert_is_catrust(cert):
                have_cert = True
        return have_cert

    def create_rsa_key_and_cert(self, alt_names=[]):
        """
        Create a key and a cert that is signed by the self signed ca

        This will use the hostname from the DS instance, and takes a list of
        extra names to take.
        """

        # Create noise.
        self._generate_noise('%s/noise.txt' % self.dirsrv.confdir)
        # Now run the command. Can we do this with NSS native?
        cmd = [
            '/usr/bin/certutil',
            '-S',
            '-n',
            CERT_NAME,
            '-s',
            SELF_ISSUER.format(HOSTNAME=self.dirsrv.host),
            '-c',
            CA_NAME,
            '-g',
            '%s' % KEYBITS,
            '-t',
            ',,',
            '-v',
            '%s' % VALID,
            '-d',
            self._certdb,
            '-z',
            '%s/noise.txt' % self.dirsrv.confdir,
            '-f',
            '%s/%s' % (self.dirsrv.confdir, PWD_TXT),
        ]

        result = check_output(cmd)
        self.dirsrv.log.debug("nss output: %s" % result)
        return True
