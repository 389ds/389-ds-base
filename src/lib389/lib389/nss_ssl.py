# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Helpers for managing NSS databases in Directory Server
"""

import copy
import os
import re
import socket
import time
import shutil
import logging
# from nss import nss
import subprocess
from datetime import datetime, timedelta
from subprocess import check_output, run, PIPE
from lib389.passwd import password_generate
from lib389.lint import DSCERTLE0001, DSCERTLE0002
from lib389.utils import ensure_str, format_cmd_list
import uuid

KEYBITS = 4096
CA_NAME = 'Self-Signed-CA'
CERT_NAME = 'Server-Cert'
USER_PREFIX = 'user-'
PIN_TXT = 'pin.txt'
PWD_TXT = 'pwdfile.txt'
CERT_SUFFIX = 'O=testing,L=389ds,ST=Queensland,C=AU'
ISSUER = 'CN=ssca.389ds.example.com,%s' % CERT_SUFFIX
SELF_ISSUER = 'CN={HOSTNAME},givenName={GIVENNAME},%s' % CERT_SUFFIX
USER_ISSUER = 'CN={HOSTNAME},%s' % CERT_SUFFIX
VALID = 24
VALID_MIN = 61  # Days

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

        self.db_files = {"dbm_backend": ["%s/%s" % (self._certdb, f) for f in ("key3.db", "cert8.db", "secmod.db")],
                         "sql_backend": ["%s/%s" % (self._certdb, f) for f in ("key4.db", "cert9.db", "pkcs11.txt")],
                         "support": ["%s/%s" % (self._certdb, f) for f in ("noise.txt", PIN_TXT, PWD_TXT)]}
        self._lint_functions = [self._lint_certificate_expiration,]

    def lint(self):
        results = []
        for fn in self._lint_functions:
            for result in fn():
                if result is not None:
                    results.append(result)
        return results

    def _lint_certificate_expiration(self):
        """Check all the certificates in the db if they will expire within 30 days
        or have already expired.
        """
        cert_list = []
        all_certs = self._rsa_cert_list()
        for cert in all_certs:
            cert_list.append(self.get_cert_details(cert[0]))

        for cert in cert_list:
            cert_date = cert[3].split()[0]
            diff_date = datetime.strptime(cert_date, '%Y-%m-%d').date() - datetime.today().date()
            if diff_date < timedelta(days=0):
                # Expired
                report = copy.deepcopy(DSCERTLE0002)
                report['detail'] = report['detail'].replace('CERT', cert[0])
                yield report
            elif diff_date < timedelta(days=30):
                # Expiring within 30 days
                report = copy.deepcopy(DSCERTLE0001)
                report['detail'] = report['detail'].replace('CERT', cert[0])
                yield report

    def detect_alt_names(self, alt_names=[]):
        """Attempt to determine appropriate subject alternate names for a host.
        Returns the list of names we derive.

        :param alt_names: A list of alternate names.
        :type alt_names: list[str]
        :returns: list[str]
        """
        if self.dirsrv and self.dirsrv.host not in alt_names:
            alt_names.append(self.dirsrv.host)
        if len(alt_names) == 0:
            alt_names.append(socket.gethostname())
        return alt_names

    def generate_cert_subject(self, alt_names=[]):
        """Return the cert subject we would generate for this host
        from the lib389 self signed process. This is *not* the subject
        of the actual cert, which could be different.

        :param alt_names: Alternative names you want to configure.
        :type alt_names: [str, ]
        :returns: String of the subject DN.
        """

        if self.dirsrv and len(alt_names) > 0:
            return SELF_ISSUER.format(GIVENNAME=self.dirsrv.get_uuid(), HOSTNAME=alt_names[0])
        elif len(alt_names) > 0:
            return SELF_ISSUER.format(GIVENNAME=uuid.uuid4(), HOSTNAME=alt_names[0])
        else:
            return SELF_ISSUER.format(GIVENNAME=uuid.uuid4(), HOSTNAME='lib389host.localdomain')

    def get_server_cert_subject(self, alt_names=[]):
        """Get the server db subject. For now, this uses generate, but later
        we can make this determined from other factors like x509 parsing.

        :returns: str
        """
        alt_names = self.detect_alt_names(alt_names)
        return self.generate_cert_subject(alt_names)

    def _generate_noise(self, fpath):
        noise = password_generate(256)
        prv_mask = os.umask(0o177)
        try:
            with open(fpath, 'w') as f:
                f.write(noise)
        finally:
            prv_mask = os.umask(prv_mask)

    def reinit(self):
        """
        Re-init (create) the nss db.
        """
        # 48886: The DB that DS ships with is .... well, broken. Purge it!
        assert self.remove_db()

        try:
            os.makedirs(self._certdb)
        except FileExistsError:
            pass

        if self.dirsrv is None:
            # Write a README to let people know what this is
            readme_file = '%s/%s' % (self._certdb, 'README.txt')
            if not os.path.exists(readme_file):
                with open(readme_file, 'w') as f:
                    f.write("""
SSCA - Simple Self-Signed Certificate Authority

This is part of the 389 Directory Server project's lib389 toolkit. It
creates a simple, standalone certificate authority for testing and
development purposes. It's suitable for evaluation and testing purposes
only.
                    """)

        # In the future we may add the needed option to avoid writing the pin
        # files.
        # Write the pin.txt, and the pwdfile.txt
        prv_mask = os.umask(0o177)
        try:
            pin_file = '%s/%s' % (self._certdb, PIN_TXT)
            if not os.path.exists(pin_file):
                with open(pin_file, 'w') as f:
                    f.write('Internal (Software) Token:%s' % self.dbpassword)

            pwd_text_file = '%s/%s' % (self._certdb, PWD_TXT)
            if not os.path.exists(pwd_text_file):
                with open(pwd_text_file, 'w') as f:
                    f.write('%s' % self.dbpassword)
        finally:
            prv_mask = os.umask(prv_mask)

        # Init the db.
        # 48886; This needs to be sql format ...
        cmd = ['/usr/bin/certutil', '-N', '-d', self._certdb, '-f', '%s/%s' % (self._certdb, PWD_TXT)]
        self._generate_noise('%s/noise.txt' % self._certdb)
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        self.log.debug("nss output: %s", result)
        return True

    def _db_exists(self, even_partial=False):
        """Check that a nss db exists at the certpath"""

        fn = any if even_partial else all
        if fn(map(os.path.exists, self.db_files["dbm_backend"])) or \
           fn(map(os.path.exists, self.db_files["sql_backend"])):
            return True
        return False

    def remove_db(self):
        """Remove nss db files at the certpath"""

        files = self.db_files["dbm_backend"] + \
                self.db_files["sql_backend"] + \
                self.db_files["support"]

        for file in files:
            try:
                os.remove(file)
            except FileNotFoundError:
                pass

        assert not self._db_exists()
        return True

    def create_rsa_ca(self, months=VALID):
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
            '%s' % months,
            '-2',
            '--keyUsage',
            'certSigning',
            '-d',
            self._certdb,
            '-z',
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        cmd_input = b'y\n\n'  # responses to certutil questions
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        result = ensure_str(run(cmd, check=True, stderr=PIPE, stdout=PIPE, input=cmd_input).stdout)
        self.log.debug("nss output: %s", result)
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
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        certdetails = check_output(cmd, stderr=subprocess.STDOUT)
        with open('%s/ca.crt' % self._certdb, 'w') as f:
            f.write(ensure_str(certdetails))
        cmd = ['/usr/bin/c_rehash', self._certdb]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)
        return True

    def rsa_ca_needs_renew(self):
        """Check is our self signed CA is expired or
        will expire less than a minimum period of time (VALID_MIN)
        """

        cmd = [
            '/usr/bin/certutil',
            '-L',
            '-n',
            CA_NAME,
            '-d',
            self._certdb,
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        certdetails = check_output(cmd, stderr=subprocess.STDOUT, encoding='utf-8')
        end_date_str = certdetails.split("Not After : ")[1].split("\n")[0]
        date_format = '%a %b %d %H:%M:%S %Y'
        end_date = datetime.strptime(end_date_str, date_format)

        if end_date - datetime.now() < timedelta(days=VALID_MIN):
            return True
        else:
            return False

    def renew_rsa_ca(self, months=VALID):
        """Renew the self signed CA."""

        csr_path = os.path.join(self._certdb, 'CA_renew.csr')
        crt_path = '%s/ca.crt' % self._certdb

        # Create noise.
        self._generate_noise('%s/noise.txt' % self._certdb)

        # Generate a CSR for a new CA cert
        cmd = [
            '/usr/bin/certutil',
            '-R',
            '-s',
            ISSUER,
            '-g',
            '%s' % KEYBITS,
            '-k',
            'NSS Certificate DB:%s' % CA_NAME,
            '-d',
            self._certdb,
            '-z',
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
            '-a',
            '-o', csr_path,
            ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)

        # Sign the CSR with our old CA
        cmd = [
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
            '--keyUsage',
            'certSigning',
            '-t',
            'CT,,',
            '-v',
            '%s' % months,
            ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)

        cmd = ['/usr/bin/c_rehash', self._certdb]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)

        # Import the new CA to our DB instead of the old CA
        cmd = [
            '/usr/bin/certutil',
            '-A',
            '-n', CA_NAME,
            '-t', "CT,,",
            '-a',
            '-i', crt_path,
            '-d', self._certdb,
            '-f', '%s/%s' % (self._certdb, PWD_TXT),
            ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)

        return crt_path

    def _rsa_cert_list(self):
        cmd = [
            '/usr/bin/certutil',
            '-L',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))

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
            if line == '':
                continue
            if line == 'Database needs user init':
                # There are no certs, abort...
                return []
            cert_values.append(re.match(r'^(.+[^\s])[\s]+([^\s]+)$', line.rstrip()).groups())
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
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))

        lines = result.split('\n')[1:-1]
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

    def create_rsa_key_and_cert(self, alt_names=[], months=VALID):
        """
        Create a key and a cert that is signed by the self signed ca

        This will use the hostname from the DS instance, and takes a list of
        extra names to take.
        """

        alt_names = self.detect_alt_names(alt_names)
        subject = self.generate_cert_subject(alt_names)

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
            subject,
            # We MUST issue with SANs else ldap wont verify the name.
            '-8', ','.join(alt_names),
            '-c',
            CA_NAME,
            '-g',
            '%s' % KEYBITS,
            '-t',
            ',,',
            '-v',
            '%s' % months,
            '-d',
            self._certdb,
            '-z',
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        self.log.debug("nss output: %s", result)
        return True

    def create_rsa_key_and_csr(self, alt_names=[]):
        """Create a new RSA key and the certificate signing request. This
        request can be submitted to a CA for signing. The returned certifcate
        can be added with import_rsa_crt.
        """
        csr_path = os.path.join(self._certdb, '%s.csr' % CERT_NAME)

        alt_names = self.detect_alt_names(alt_names)
        subject = self.generate_cert_subject(alt_names)

        # Wait a second to avoid an NSS bug with serial ids based on time.
        time.sleep(1)
        # Create noise.
        self._generate_noise('%s/noise.txt' % self._certdb)

        cmd = [
            '/usr/bin/certutil',
            '-R',
            # We want a dual purposes client and server cert
            '--keyUsage',
            'digitalSignature,nonRepudiation,keyEncipherment,dataEncipherment',
            '--nsCertType',
            'sslClient,sslServer',
            '--extKeyUsage',
            'clientAuth,serverAuth',
            '-s',
            subject,
            # We MUST issue with SANs else ldap wont verify the name.
            '-8', ','.join(alt_names),
            '-g',
            '%s' % KEYBITS,
            '-d',
            self._certdb,
            '-z',
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
            '-a',
            '-o', csr_path,
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)

        return csr_path

    def rsa_ca_sign_csr(self, csr_path, months=VALID):
        """ Given a CSR, sign it with our CA certificate (if present). This
        emits a signed certificate which can be imported with import_rsa_crt.
        """
        crt_path = 'crt'.join(csr_path.rsplit('csr', 1))
        ca_path = '%s/ca.crt' % self._certdb

        cmd = [
            '/usr/bin/certutil',
            '-C',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
            '-v',
            '%s' % months,
            '-a',
            '-i', csr_path,
            '-o', crt_path,
            '-c', CA_NAME,
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)

        return (ca_path, crt_path)

    def import_rsa_crt(self, ca=None, crt=None):
        """Given a signed certificate from a ca, import the CA and certificate
        to our database.


        """

        assert ca is not None or crt is not None, "At least one parameter should be specified (ca or crt)"

        if ca is not None:
            shutil.copyfile(ca, '%s/ca.crt' % self._certdb)
            cmd = ['/usr/bin/c_rehash', self._certdb]
            self.log.debug("nss cmd: %s", format_cmd_list(cmd))
            check_output(cmd, stderr=subprocess.STDOUT)
            cmd = [
                '/usr/bin/certutil',
                '-A',
                '-n', CA_NAME,
                '-t', "CT,,",
                '-a',
                '-i', '%s/ca.crt' % self._certdb,
                '-d', self._certdb,
                '-f',
                '%s/%s' % (self._certdb, PWD_TXT),
            ]
            self.log.debug("nss cmd: %s", format_cmd_list(cmd))
            check_output(cmd, stderr=subprocess.STDOUT)

        if crt is not None:
            cmd = [
                '/usr/bin/certutil',
                '-A',
                '-n', CERT_NAME,
                '-t', ",,",
                '-a',
                '-i', crt,
                '-d', self._certdb,
                '-f',
                '%s/%s' % (self._certdb, PWD_TXT),
            ]
            self.log.debug("nss cmd: %s", format_cmd_list(cmd))
            check_output(cmd, stderr=subprocess.STDOUT)
            cmd = [
                '/usr/bin/certutil',
                '-V',
                '-d', self._certdb,
                '-n', CERT_NAME,
                '-u', 'YCV'
            ]
            self.log.debug("nss cmd: %s", format_cmd_list(cmd))
            check_output(cmd, stderr=subprocess.STDOUT)

    def create_rsa_user(self, name, months=VALID):
        """
        Create a key and cert for a user to authenticate to the directory.

        Name is the uid of the account, and will become the CN of the cert.
        """
        subject = USER_ISSUER.format(HOSTNAME=name)
        if self._rsa_user_exists(name):
            return subject

        # Wait a second to avoid an NSS bug with serial ids based on time.
        time.sleep(1)
        cmd = [
            '/usr/bin/certutil',
            '-S',
            '-n',
            '%s%s' % (USER_PREFIX, name),
            '-s',
            subject,
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
            '%s' % months,
            '-d',
            self._certdb,
            '-z',
            '%s/noise.txt' % self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))

        result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        self.log.debug("nss output: %s", result)
        # Now extract this into PEM files that we can use.
        # pk12util -o user-william.p12 -d . -k pwdfile.txt -n user-william -W ''
        cmd = [
            'pk12util',
            '-d', self._certdb,
            '-o', '%s/%s%s.p12' % (self._certdb, USER_PREFIX, name),
            '-k', '%s/%s' % (self._certdb, PWD_TXT),
            '-n', '%s%s' % (USER_PREFIX, name),
            '-W', '""'
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)
        # openssl pkcs12 -in user-william.p12 -passin pass:'' -out file.pem -nocerts -nodes
        # Extract the key
        cmd = [
            'openssl',
            'pkcs12',
            '-in', '%s/%s%s.p12' % (self._certdb, USER_PREFIX, name),
            '-passin', 'pass:""',
            '-out', '%s/%s%s.key' % (self._certdb, USER_PREFIX, name),
            '-nocerts',
            '-nodes'
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)
        # Extract the cert
        cmd = [
            'openssl',
            'pkcs12',
            '-in', '%s/%s%s.p12' % (self._certdb, USER_PREFIX, name),
            '-passin', 'pass:""',
            '-out', '%s/%s%s.crt' % (self._certdb, USER_PREFIX, name),
            '-nokeys',
            '-clcerts',
            '-nodes'
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)
        # Convert the cert for userCertificate attr
        cmd = [
            'openssl',
            'x509',
            '-inform', 'PEM',
            '-outform', 'DER',
            '-in', '%s/%s%s.crt' % (self._certdb, USER_PREFIX, name),
            '-out', '%s/%s%s.der' % (self._certdb, USER_PREFIX, name),
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)

        return subject

    def get_rsa_user(self, name):
        """
        Return a dict of information for ca, key and cert paths for the user id
        """
        ca_path = '%s/ca.crt' % self._certdb
        key_path = '%s/%s%s.key' % (self._certdb, USER_PREFIX, name)
        crt_path = '%s/%s%s.crt' % (self._certdb, USER_PREFIX, name)
        crt_der_path = '%s/%s%s.der' % (self._certdb, USER_PREFIX, name)
        return {'ca': ca_path, 'key': key_path, 'crt': crt_path, 'crt_der_path': crt_der_path}

    # Certificate helper functions
    def del_cert(self,  nickname):
        """Delete this certificate
        """
        cmd = [
                '/usr/bin/certutil',
                '-D',
                '-d', self._certdb,
                '-n', nickname,
                '-f',
                '%s/%s' % (self._certdb, PWD_TXT),
            ]
        self.log.debug("del_cert cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)

    def edit_cert_trust(self, nickname,  trust_flags):
        """Edit trust flags
        """

        # validate trust flags
        flag_sections = trust_flags.split(',')
        if len(flag_sections) != 3:
            raise ValueError("Invalid trust flag format")

        for section in flag_sections:
            if len(section) > 6:
                raise ValueError("Invalid trust flag format, too many flags in a section")

        for c in trust_flags:
            if c not in ['p', 'P', 'c',  'C', 'T', 'u', ',']:
                raise ValueError("Invalid trust flag {}".format(c))

        # Modify certificate flags
        cmd = [
            '/usr/bin/certutil',
            '-M',
            '-d', self._certdb,
            '-n', nickname,
            '-t',  trust_flags,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        self.log.debug("edit_cert_trust cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)


    def get_cert_details(self, nickname):
        """Get the trust flags, subject DN, issuer, and expiration date

        return a list:
            0 - nickname
            1 - subject
            2 - issuer
            3 - expire date
            4 - trust_flags
        """
        all_certs = self._rsa_cert_list()
        for cert in all_certs:
            if cert[0] == nickname:
                trust_flags = cert[1]
                cmd = [
                    '/usr/bin/certutil',
                    '-d', self._certdb,
                    '-n', nickname,
                    '-L',
                    '-f',
                    '%s/%s' % (self._certdb, PWD_TXT),
                ]
                self.log.debug("get_cert_details cmd: %s", format_cmd_list(cmd))

                # Expiration date
                certdetails = check_output(cmd, stderr=subprocess.STDOUT, encoding='utf-8')
                end_date_str = certdetails.split("Not After : ")[1].split("\n")[0]
                date_format = '%a %b %d %H:%M:%S %Y'
                end_date = datetime.strptime(end_date_str, date_format)

                # Subject DN
                subject = ""
                for line in certdetails.splitlines():
                    line = line.lstrip()
                    if line.startswith("Subject: "):
                        subject = line.split("Subject: ")[1].split("\n")[0]
                    elif subject != "":
                        if not line.startswith("Subject Public Key Info:"):
                            subject += line
                        else:
                            # Done, strip off quotes
                            subject = subject[1:-1]
                            break

                # Issuer
                issuer = ""
                for line in certdetails.splitlines():
                    line = line.lstrip()
                    if line.startswith("Issuer: "):
                        issuer = line.split("Issuer: ")[1].split("\n")[0]
                    elif issuer != "":
                        if not line.startswith("Validity:"):
                            issuer += line
                        else:
                            issuer = issuer[1:-1]
                            break

                return ([nickname,  subject, issuer, str(end_date), trust_flags])

        # Did not find cert with that name
        raise ValueError("Certificate '{}' not found in NSS database".format(nickname))


    def list_certs(self, ca=False):
        all_certs = self._rsa_cert_list()
        certs = []
        for cert in all_certs:
            trust_flags = cert[1]
            if (ca and "CT" in trust_flags) or (not ca and "CT" not in trust_flags):
                certs.append(self.get_cert_details(cert[0]))
        return certs


    def add_cert(self, nickname, input_file, ca=False):
        """Add server or CA cert
        """

        # Verify input_file exists
        if not os.path.exists(input_file):
            raise ValueError("The certificate file ({}) does not exist".format(input_file))

        if ca:
            trust_flags = "CT,,"
        else:
            trust_flags = ",,"

        cmd = [
            '/usr/bin/certutil',
            '-A',
            '-d', self._certdb,
            '-n', nickname,
            '-t', trust_flags,
            '-i', input_file,
            '-a',
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        self.log.debug("add_cert cmd: %s", format_cmd_list(cmd))
        check_output(cmd, stderr=subprocess.STDOUT)
