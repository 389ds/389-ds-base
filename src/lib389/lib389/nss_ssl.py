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
import socket
# from nss import nss
from subprocess import check_call, check_output
from lib389.passwd import password_generate

KEYBITS = 4096
CA_NAME = 'Self-Signed-CA'
CERT_NAME = 'Server-Cert'
USER_PREFIX = 'user-'
PIN_TXT = 'pin.txt'
PWD_TXT = 'pwdfile.txt'
ISSUER = 'CN=ca.lib389.example.com,O=testing,L=lib389,ST=Queensland,C=AU'
SELF_ISSUER = 'CN={HOSTNAME},O=testing,L=lib389,ST=Queensland,C=AU'
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
        # return "sql:%s" % self.dirsrv.get_cert_dir()
        return self.dirsrv.get_cert_dir()

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
                os.remove("%s/%s" % (self.dirsrv.get_cert_dir(), f ))
            except:
                pass

        # In the future we may add the needed option to avoid writing the pin
        # files.
        # Write the pin.txt, and the pwdfile.txt
        if not os.path.exists('%s/%s' % (self.dirsrv.get_cert_dir(), PIN_TXT)):
            with open('%s/%s' % (self.dirsrv.get_cert_dir(), PIN_TXT), 'w') as f:
                f.write('Internal (Software) Token:%s' % self.dbpassword)
        if not os.path.exists('%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT)):
            with open('%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT), 'w') as f:
                f.write('%s' % self.dbpassword)

        # Init the db.
        # 48886; This needs to be sql format ...
        cmd = ['/usr/bin/certutil', '-N', '-d', self._certdb, '-f', '%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT)]
        self.dirsrv.log.debug("nss cmd: %s" % cmd)
        result = check_output(cmd)
        self.dirsrv.log.debug("nss output: %s" % result)
        return True

    def _db_exists(self):
        """
        Check that a nss db exists at the certpath
        """
        key3 = os.path.exists("%s/key3.db" % (self.dirsrv.get_cert_dir()))
        cert8 = os.path.exists("%s/cert8.db" % (self.dirsrv.get_cert_dir()))
        key4 = os.path.exists("%s/key4.db" % (self.dirsrv.get_cert_dir()))
        cert9 = os.path.exists("%s/cert9.db" % (self.dirsrv.get_cert_dir()))
        secmod = os.path.exists("%s/secmod.db" % (self.dirsrv.get_cert_dir()))
        pkcs11 = os.path.exists("%s/pkcs11.txt" % (self.dirsrv.get_cert_dir()))

        if ((key3 and cert8 and secmod) or (key4 and cert9 and pkcs11)):
            return True
        return False

    def create_rsa_ca(self):
        """
        Create a self signed CA.
        """

        # Create noise.
        self._generate_noise('%s/noise.txt' % self.dirsrv.get_cert_dir())
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
            '%s/noise.txt' % self.dirsrv.get_cert_dir(),
            '-f',
            '%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT),
        ]
        result = check_output(cmd)
        self.dirsrv.log.debug("nss output: %s" % result)
        # Now extract the CAcert to a well know place.
        # This allows us to point the cacert dir here and it "just works"
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
        with open('%s/ca.crt' % self.dirsrv.get_cert_dir(), 'w') as f:
            f.write(certdetails)
        if os.path.isfile('/usr/sbin/cacertdir_rehash'):
            check_output(['/usr/sbin/cacertdir_rehash', self.dirsrv.get_cert_dir()])
        return True

    def _rsa_cert_list(self):
        cmd = [
            '/usr/bin/certutil',
            '-L',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT),
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
            '%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT),
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

    def _rsa_cert_is_user(self, cert_tuple):
        """
        Check an RSA cert is user trust

        Sadly we can't check for ext key usage, because NSS makes this really hard.
        """
        trust_flags = cert_tuple[1]
        (ssl_flag, mime_flag, jar_flag) = trust_flags.split(',')
        return 'u' in ssl_flag

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
        Check if a valid server key and cert pair exist.
        """
        have_cert = False
        cert_list = self._rsa_cert_list()
        for cert in cert_list:
            # This could do a better check for !ca, and server attrs
            if self._rsa_cert_key_exists(cert) and not self._rsa_cert_is_catrust(cert):
                have_cert = True
        return have_cert

    def _rsa_user_exists(self, name):
        """
        Check if a valid server key and cert pair exist for a user.

        we use the format, user-<name>
        """
        have_user = False
        cert_list = self._rsa_cert_list()
        for cert in cert_list:
            if (cert[0] == '%s%s' % (USER_PREFIX, name)):
                if self._rsa_cert_key_exists(cert) and self._rsa_cert_is_user(cert):
                    have_user = True
        return have_user


    def create_rsa_key_and_cert(self, alt_names=[]):
        """
        Create a key and a cert that is signed by the self signed ca

        This will use the hostname from the DS instance, and takes a list of
        extra names to take.
        """

        if len(alt_names) == 0:
            alt_names.append(socket.gethostname())
        if self.dirsrv.host not in alt_names:
            alt_names.append(self.dirsrv.host)

        # Create noise.
        self._generate_noise('%s/noise.txt' % self.dirsrv.get_cert_dir())
        cmd = [
            '/usr/bin/certutil',
            '-S',
            '-n',
            CERT_NAME,
            '-s',
            SELF_ISSUER.format(HOSTNAME=self.dirsrv.host),
            # We MUST issue with SANs else ldap wont verify the name.
            '-8', ','.join(alt_names),
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
            '%s/noise.txt' % self.dirsrv.get_cert_dir(),
            '-f',
            '%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT),
        ]

        result = check_output(cmd)
        self.dirsrv.log.debug("nss output: %s" % result)
        return True

    def create_rsa_user(self, name):
        """
        Create a key and cert for a user to authenticate to the directory.

        Name is the uid of the account, and will become the CN of the cert.
        """
        cmd = [
            '/usr/bin/certutil',
            '-S',
            '-n',
            '%s%s' % (USER_PREFIX, name),
            '-s',
            SELF_ISSUER.format(HOSTNAME=name),
            '--keyUsage',
            'digitalSignature,nonRepudiation,keyEncipherment,dataEncipherment',
            '--nsCertType',
            'sslClient',
            '--extKeyUsage',
            'clientAuth',
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
            '%s/noise.txt' % self.dirsrv.get_cert_dir(),
            '-f',
            '%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT),
        ]

        result = check_output(cmd)
        self.dirsrv.log.debug("nss output: %s" % result)
        # Now extract this into PEM files that we can use.
        # pk12util -o user-william.p12 -d . -k pwdfile.txt -n user-william -W ''
        check_call([
            'pk12util',
            '-d', self._certdb,
            '-o', '%s/%s%s.p12' % (self.dirsrv.get_cert_dir(), USER_PREFIX, name),
            '-k', '%s/%s' % (self.dirsrv.get_cert_dir(), PWD_TXT),
            '-n', '%s%s' % (USER_PREFIX, name),
            '-W', '""'
        ])
        # openssl pkcs12 -in user-william.p12 -passin pass:'' -out file.pem -nocerts -nodes
        # Extract the key
        check_call([
            'openssl',
            'pkcs12',
            '-in', '%s/%s%s.p12' % (self.dirsrv.get_cert_dir(), USER_PREFIX, name),
            '-passin', 'pass:""',
            '-out', '%s/%s%s.key' % (self.dirsrv.get_cert_dir(), USER_PREFIX, name),
            '-nocerts',
            '-nodes'
        ])
        # Extract the cert
        check_call([
            'openssl',
            'pkcs12',
            '-in', '%s/%s%s.p12' % (self.dirsrv.get_cert_dir(), USER_PREFIX, name),
            '-passin', 'pass:""',
            '-out', '%s/%s%s.crt' % (self.dirsrv.get_cert_dir(), USER_PREFIX, name),
            '-nokeys',
            '-clcerts',
            '-nodes'
        ])
        return True

    def get_rsa_user(self, name):
        """
        Return a dict of information for ca, key and cert paths for the user id
        """
        ca_path = '%s/ca.crt' % self.dirsrv.get_cert_dir()
        key_path = '%s/%s%s.key' % (self.dirsrv.get_cert_dir(), USER_PREFIX, name)
        crt_path = '%s/%s%s.crt' % (self.dirsrv.get_cert_dir(), USER_PREFIX, name)
        return {'ca': ca_path, 'key': key_path, 'crt': crt_path}

