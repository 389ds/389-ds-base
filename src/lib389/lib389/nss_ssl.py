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
import time
import shutil
import logging
# from nss import nss
from subprocess import check_call, check_output
from lib389.passwd import password_generate

from lib389.utils import ensure_str, ensure_bytes

KEYBITS = 4096
CA_NAME = 'Self-Signed-CA'
CERT_NAME = 'Server-Cert'
USER_PREFIX = 'user-'
PIN_TXT = 'pin.txt'
PWD_TXT = 'pwdfile.txt'
CERT_SUFFIX = 'O=testing,L=389ds,ST=Queensland,C=AU'
ISSUER = 'CN=ssca.389ds.example.com,%s' % CERT_SUFFIX
SELF_ISSUER = 'CN={HOSTNAME},%s' % CERT_SUFFIX
VALID = 2

# My logger
log = logging.getLogger(__name__)

class NssSsl(object):
    def __init__(self, dirsrv=None, dbpassword=None, dbpath=None):
        self.dirsrv = dirsrv
        self._certdb = dbpath
        if self._certdb is None:
            self._certdb = self.dirsrv.get_cert_dir()
        self.log = log
        if self.dirsrv is not None:
            self.log = self.dirsrv.log
        if dbpassword is None:
            self.dbpassword = password_generate()
        else:
            self.dbpassword = dbpassword

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
                os.remove("%s/%s" % (self._certdb, f ))
            except:
                pass

        try:
            os.makedirs(self._certdb)
        except FileExistsError:
            pass

        # In the future we may add the needed option to avoid writing the pin
        # files.
        # Write the pin.txt, and the pwdfile.txt
        if not os.path.exists('%s/%s' % (self._certdb, PIN_TXT)):
            with open('%s/%s' % (self._certdb, PIN_TXT), 'w') as f:
                f.write('Internal (Software) Token:%s' % self.dbpassword)
        if not os.path.exists('%s/%s' % (self._certdb, PWD_TXT)):
            with open('%s/%s' % (self._certdb, PWD_TXT), 'w') as f:
                f.write('%s' % self.dbpassword)

        # Init the db.
        # 48886; This needs to be sql format ...
        cmd = ['/usr/bin/certutil', '-N', '-d', self._certdb, '-f', '%s/%s' % (self._certdb, PWD_TXT)]
        self._generate_noise('%s/noise.txt' % self._certdb)
        self.log.debug("nss cmd: %s" % cmd)
        result = ensure_str(check_output(cmd))
        self.log.debug("nss output: %s" % result)
        return True

    def _db_exists(self):
        """
        Check that a nss db exists at the certpath
        """
        key3 = os.path.exists("%s/key3.db" % (self._certdb))
        cert8 = os.path.exists("%s/cert8.db" % (self._certdb))
        key4 = os.path.exists("%s/key4.db" % (self._certdb))
        cert9 = os.path.exists("%s/cert9.db" % (self._certdb))
        secmod = os.path.exists("%s/secmod.db" % (self._certdb))
        pkcs11 = os.path.exists("%s/pkcs11.txt" % (self._certdb))

        if ((key3 and cert8 and secmod) or (key4 and cert9 and pkcs11)):
            return True
        return False

    def create_rsa_ca(self):
        """
        Create a self signed CA.
        """

        # Wait a second to avoid an NSS bug with serial ids based on time.
        time.sleep(1)
        # Create noise.
        self._generate_noise('%s/noise.txt' % self._certdb)
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
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        result = ensure_str(check_output(cmd))
        self.log.debug("nss output: %s" % result)
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
        with open('%s/ca.crt' % self._certdb, 'w') as f:
            f.write(ensure_str(certdetails))
        check_output(['/usr/sbin/cacertdir_rehash', self._certdb])
        return True

    def _rsa_cert_list(self):
        cmd = [
            '/usr/bin/certutil',
            '-L',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        result = ensure_str(check_output(cmd))

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
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        result = ensure_str(check_output(cmd))

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
        if self.dirsrv and self.dirsrv.host not in alt_names:
            alt_names.append(self.dirsrv.host)

        # Wait a second to avoid an NSS bug with serial ids based on time.
        time.sleep(1)
        # Create noise.
        self._generate_noise('%s/noise.txt' % self._certdb)
        cmd = [
            '/usr/bin/certutil',
            '-S',
            '-n',
            CERT_NAME,
            '-s',
            SELF_ISSUER.format(HOSTNAME=alt_names[0]),
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
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]

        result = ensure_str(check_output(cmd))
        self.log.debug("nss output: %s" % result)
        return True

    def create_rsa_key_and_csr(self, alt_names=[]):
        """Create a new RSA key and the certificate signing request. This
        request can be submitted to a CA for signing. The returned certifcate
        can be added with import_rsa_crt.
        """
        csr_path = os.path.join(self._certdb, '%s.csr' % CERT_NAME)

        if len(alt_names) == 0:
            alt_names.append(socket.gethostname())
        if self.dirsrv and self.dirsrv.host not in alt_names:
            alt_names.append(self.dirsrv.host)

        # Wait a second to avoid an NSS bug with serial ids based on time.
        time.sleep(1)
        # Create noise.
        self._generate_noise('%s/noise.txt' % self._certdb)

        check_call([
            '/usr/bin/certutil',
            '-R',
            '-s',
            SELF_ISSUER.format(HOSTNAME=alt_names[0]),
            # We MUST issue with SANs else ldap wont verify the name.
            '-8', ','.join(alt_names),
            '-g',
            '%s' % KEYBITS,
            '-v',
            '%s' % VALID,
            '-d',
            self._certdb,
            '-z',
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
            '-a',
            '-o', csr_path,
        ])
        return csr_path

    def rsa_ca_sign_csr(self, csr_path):
        """ Given a CSR, sign it with our CA certificate (if present). This
        emits a signed certificate which can be imported with import_rsa_crt.
        """
        crt_path = 'crt'.join(csr_path.rsplit('csr', 1))
        ca_path = '%s/ca.crt' % self._certdb

        check_call([
            '/usr/bin/certutil',
            '-C',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
            '-a',
            '-i', csr_path,
            '-o', crt_path,
            '-c', CA_NAME,
        ])

        return (ca_path, crt_path)

    def import_rsa_crt(self, ca, crt):
        """Given a signed certificate from a ca, import the CA and certificate
        to our database.
        """
        shutil.copyfile(ca, '%s/ca.crt' % self._certdb)
        check_output(['/usr/sbin/cacertdir_rehash', self._certdb])
        check_call([
            '/usr/bin/certutil',
            '-A',
            '-n', CA_NAME,
            '-t', "CT,,",
            '-a',
            '-i', '%s/ca.crt' % self._certdb,
            '-d', self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ])
        check_call([
            '/usr/bin/certutil',
            '-A',
            '-n', CERT_NAME,
            '-t', ",,",
            '-a',
            '-i', crt,
            '-d', self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ])
        check_call([
            '/usr/bin/certutil',
            '-V',
            '-d', self._certdb,
            '-n', CERT_NAME,
            '-u', 'V'
        ])

    def create_rsa_user(self, name):
        """
        Create a key and cert for a user to authenticate to the directory.

        Name is the uid of the account, and will become the CN of the cert.
        """
        if self._rsa_user_exists(name):
            return True

        # Wait a second to avoid an NSS bug with serial ids based on time.
        time.sleep(1)
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
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]

        result = ensure_str(check_output(cmd))
        self.log.debug("nss output: %s" % result)
        # Now extract this into PEM files that we can use.
        # pk12util -o user-william.p12 -d . -k pwdfile.txt -n user-william -W ''
        check_call([
            'pk12util',
            '-d', self._certdb,
            '-o', '%s/%s%s.p12' % (self._certdb, USER_PREFIX, name),
            '-k', '%s/%s' % (self._certdb, PWD_TXT),
            '-n', '%s%s' % (USER_PREFIX, name),
            '-W', '""'
        ])
        # openssl pkcs12 -in user-william.p12 -passin pass:'' -out file.pem -nocerts -nodes
        # Extract the key
        check_call([
            'openssl',
            'pkcs12',
            '-in', '%s/%s%s.p12' % (self._certdb, USER_PREFIX, name),
            '-passin', 'pass:""',
            '-out', '%s/%s%s.key' % (self._certdb, USER_PREFIX, name),
            '-nocerts',
            '-nodes'
        ])
        # Extract the cert
        check_call([
            'openssl',
            'pkcs12',
            '-in', '%s/%s%s.p12' % (self._certdb, USER_PREFIX, name),
            '-passin', 'pass:""',
            '-out', '%s/%s%s.crt' % (self._certdb, USER_PREFIX, name),
            '-nokeys',
            '-clcerts',
            '-nodes'
        ])
        # Convert the cert for userCertificate attr
        check_call([
            'openssl',
            'x509',
            '-inform', 'PEM',
            '-outform', 'DER',
            '-in', '%s/%s%s.crt' % (self._certdb, USER_PREFIX, name),
            '-out', '%s/%s%s.der' % (self._certdb, USER_PREFIX, name),
        ])

        return True

    def get_rsa_user(self, name):
        """
        Return a dict of information for ca, key and cert paths for the user id
        """
        ca_path = '%s/ca.crt' % self._certdb
        key_path = '%s/%s%s.key' % (self._certdb, USER_PREFIX, name)
        crt_path = '%s/%s%s.crt' % (self._certdb, USER_PREFIX, name)
        crt_der_path = '%s/%s%s.der' % (self._certdb, USER_PREFIX, name)
        return {'ca': ca_path, 'key': key_path, 'crt': crt_path, 'crt_der_path': crt_der_path}

