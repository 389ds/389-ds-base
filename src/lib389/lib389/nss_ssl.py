# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
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
import subprocess
import uuid
from datetime import datetime, timedelta
from subprocess import check_output, run, PIPE
from lib389.passwd import password_generate
from lib389._mapped_object_lint import DSLint
from lib389.lint import DSCERTLE0001, DSCERTLE0002
from lib389.utils import ensure_str, format_cmd_list, cert_is_ca


# Setuptools ships with 'packaging' module, let's use it from there
try:
    from pkg_resources.extern.packaging.version import LegacyVersion
# Fallback to a normal 'packaging' module in case 'setuptools' is stripped
except:
    from packaging.version import LegacyVersion

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
VALID = 24 # Months
VALID_MIN = 61  # Days

# My logger
log = logging.getLogger(__name__)


class NssSsl(DSLint):
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

        self.db_files = {group: [f"{self._certdb}/{f}" for f in files]
                         for group, files in {"dbm_backend": ("key3.db", "cert8.db", "secmod.db"),
                                              "sql_backend": ("key4.db", "cert9.db", "pkcs11.txt"),
                                              "support": ("noise.txt", PIN_TXT, PWD_TXT)}.items()}

    @classmethod
    def lint_uid(cls):
        return 'tls'

    def _assert_not_chain(self, pemfile):
        # To work this out, we open the file and count how many
        # begin key and begin cert lines there are. Any more than 1 is bad.
        count = 0
        with open(pemfile, 'r') as f:
            for line in f:
                if line.startswith('-----BEGIN PRIVATE KEY-----') or line.startswith('-----BEGIN CERTIFICATE-----'):
                    count = count + 1
        if count > 1:
            raise ValueError(f"The file {pemfile} may be a chain file. This is not supported. Break out each certificate and key into unique files, and import them individually.")

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
                report['check'] = f'tls:certificate_expiration'
                yield report
            elif diff_date < timedelta(days=30):
                # Expiring within 30 days
                report = copy.deepcopy(DSCERTLE0001)
                report['detail'] = report['detail'].replace('CERT', cert[0])
                report['check'] = f'tls:certificate_expiration'
                yield report

    def detect_alt_names(self, alt_names=[]):
        """Attempt to determine appropriate subject alternate names for a host.
        Returns the list of names we derive.

        :param alt_names: A list of alternate names.
        :type alt_names: list[str]
        :returns: list[str]
        """
        if self.dirsrv and self.dirsrv.host not in alt_names:
            alt_names.append(ensure_str(self.dirsrv.host))
        if len(alt_names) == 0:
            alt_names.append(ensure_str(socket.gethostname()))
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
        cmd = ['/usr/bin/certutil', '-N', '-d', self._certdb, '-f', '%s/%s' % (self._certdb, PWD_TXT),  '-@', '%s/%s' % (self._certdb, PWD_TXT)]
        self._generate_noise('%s/noise.txt' % self._certdb)
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
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

    def openssl_rehash(self, certdir):
        """
        Compatibly run c_rehash (on old openssl versions) or openssl rehash (on
        new ones). Prefers openssl rehash, because openssl on versions where
        the rehash command doesn't exist, also doesn't correctly set the return
        code. Instead, we parse the output of `openssl version` and try to
        figure out if we have a new enough version to unconditionally run rehash.
        """
        try:
            openssl_version = check_output(['/usr/bin/openssl', 'version']).decode('utf-8').strip()
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
        rehash_available = LegacyVersion(openssl_version.split(' ')[1]) >= LegacyVersion('1.1.0')

        if rehash_available:
            cmd = ['/usr/bin/openssl', 'rehash', certdir]
        else:
            cmd = ['/usr/bin/c_rehash', certdir]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

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
        try:
            certdetails = check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
        with open('%s/ca.crt' % self._certdb, 'w') as f:
            f.write(ensure_str(certdetails))
        self.openssl_rehash(self._certdb)
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
        try:
            certdetails = check_output(cmd, stderr=subprocess.STDOUT, encoding='utf-8')
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

        self.openssl_rehash(self._certdb)

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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

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
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

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

    def _openssl_get_csr_subject(self, csr_dir, csr_name):
        cmd = [
            '/usr/bin/openssl',
            'req',
            '-subject',
            '-noout',
            '-in',
            '%s/%s'% (csr_dir, csr_name),
        ]
        self.log.debug("cmd: %s", format_cmd_list(cmd))
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

        # Parse the subject string from openssl output
        result = result.replace("subject=", "")
        result = result.replace(" ", "").strip()
        result = result.split(',')
        result = result[slice(None, None, -1)]
        result = ','.join([str(elem) for elem in result])
        return result

    def _openssl_get_csr_sub_alt_names(self, csr_dir, csr_name):
        cmd = [
            '/usr/bin/openssl',
            'req',
            '-noout',
            '-text',
            '-in',
            '%s/%s'% (csr_dir, csr_name),
        ]
        self.log.debug("cmd: %s", format_cmd_list(cmd))
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

        subaltnames = []
        lines = result.split('\n')
        for line in lines:
            if 'DNS:' in line:
                names = line.split(',')
                for altname in names:
                    altname = altname.replace("DNS:", "")
                    subaltnames.append(altname.replace(",:", "").strip())

        return subaltnames

    def _csr_show(self, name):
        csr_dir = self.dirsrv.get_cert_dir()
        result = ""
        # Display PEM contents of a CSR file
        if name and csr_dir:
            if os.path.exists(csr_dir + "/" + name + ".csr"):
                cmd = [
                    "/usr/bin/sed",
                    "-n",
                    '/BEGIN NEW/,/END NEW/p',
                    csr_dir + "/" + name + ".csr"
                ]
                self.log.debug("cmd: %s", format_cmd_list(cmd))
                try:
                    result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
                except subprocess.CalledProcessError as e:
                    raise ValueError(e.output.decode('utf-8').rstrip())

        return result

    def _csr_list(self, csr_dir=None):
        csr_list = []
        csr_dir = self.dirsrv.get_cert_dir()
        # Search for .csr file extensions in instance config dir
        cmd = [
            '/usr/bin/find',
            csr_dir,
            '-type',
            'f',
            '-name',
            '*.csr',
            '-printf',
            '%f\\n',
        ]
        self.log.debug("cmd: %s", format_cmd_list(cmd))
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

        # Bail out if we cant find any .csr files
        if len(result) == 0:
            return []

        # For each .csr file, get last modified time and subject DN
        for csr_file in result.splitlines():
            csr = []
            # Get last modified time stamp
            cmd = [
                '/usr/bin/date',
                '-r',
                '%s/%s'% (csr_dir, csr_file),
                '+%Y-%m-%d %H:%M:%S',
            ]
            try:
                result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
            except subprocess.CalledProcessError as e:
                raise ValueError(e.output.decode('utf-8').rstrip())

            # Add csr modified timestamp
            csr.append(result.strip())
            # Use openssl to get the csr subject DN
            csr.append(self._openssl_get_csr_subject(csr_dir, csr_file))
            # Use openssl to get the csr subject alt host names
            csr.append(self._openssl_get_csr_sub_alt_names(csr_dir, csr_file))
            # Add csr name, without extension
            csr.append(csr_file.rsplit('.', 1)[0])
            csr_list.append(csr)

        return csr_list

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
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
        lines = result.split('\n')[1:-1]
        for line in lines:
            m = re.match(r'\<(?P<id>.*)\> (?P<type>\w+)\s+(?P<hash>\w+).*:(?P<name>.+)', line)
            if name == m.group('name'):
                return True
        return False

    def _rsa_cert_is_catrust(self, cert_tuple):
        trust_flags = cert_tuple[1]
        (ssl_flag, mime_flag, jar_flag) = trust_flags.split(',')
        return 'C' in ssl_flag

    def _rsa_cert_is_caclienttrust(self, cert_tuple):
        trust_flags = cert_tuple[1]
        (ssl_flag, mime_flag, jar_flag) = trust_flags.split(',')
        return 'T' in ssl_flag

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
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
        self.log.debug("nss output: %s", result)
        return True

    def create_rsa_key_and_csr(self, alt_names=[], subject=None, name=None):
        """Create a new RSA key and the certificate signing request. This
        request can be submitted to a CA for signing. The returned certificate
        can be added with import_rsa_crt.
        """
        if name is None:
            csr_path = os.path.join(self._certdb, '%s.csr' % CERT_NAME)
        else:
            csr_path = os.path.join(self._certdb, '%s.csr' % name)

        if len(alt_names) == 0:
            alt_names = self.detect_alt_names(alt_names)
        if subject is None:
            subject = self.generate_cert_subject(alt_names)

        self.log.debug(f"CSR name -> {name}")
        self.log.debug(f"CSR subject -> {subject}")
        self.log.debug(f"CSR alt_names -> {alt_names}")

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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

        return (ca_path, crt_path)

    def import_rsa_crt(self, ca=None, crt=None):
        """Given a signed certificate from a ca, import the CA and certificate
        to our database.


        """

        assert ca is not None or crt is not None, "At least one parameter should be specified (ca or crt)"

        if ca is not None:
            if not os.path.exists(ca):
                raise ValueError("The certificate file ({}) does not exist".format(ca))
            self._assert_not_chain(ca)

            shutil.copyfile(ca, '%s/ca.crt' % self._certdb)
            self.openssl_rehash(self._certdb)
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
            try:
                check_output(cmd, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                raise ValueError(e.output.decode('utf-8').rstrip())

        if crt is not None:
            if not os.path.exists(crt):
                raise ValueError("The certificate file ({}) does not exist".format(crt))
            self._assert_not_chain(crt)
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
            try:
                check_output(cmd, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                raise ValueError(e.output.decode('utf-8').rstrip())
            cmd = [
                '/usr/bin/certutil',
                '-V',
                '-d', self._certdb,
                '-n', CERT_NAME,
                '-u', 'YCV'
            ]
            self.log.debug("nss cmd: %s", format_cmd_list(cmd))
            try:
                check_output(cmd, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                raise ValueError(e.output.decode('utf-8').rstrip())

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
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

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

    def list_keys(self, orphan=None):
        key_list = []
        cmd = [
            '/usr/bin/certutil',
            '-K',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

        # Ignore the first line of certutil output
        for line in result.splitlines()[1:]:
            # Normalise the output of certutil
            line = re.sub(r"\<[^>]*\>","", line)
            key = re.split(r'\s{2,}', line)
            if orphan:
                if 'orphan' in line:
                    key_list.append(key)
            else:
                key_list.append(key)

        return key_list

    def del_key(self, keyid):
        cmd = [
            '/usr/bin/certutil',
            '-F',
            '-d',
            self._certdb,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
            '-k',
            keyid,
        ]
        try:
            result = ensure_str(check_output(cmd, stderr=subprocess.STDOUT))
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

        return result

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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

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
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())


    def display_cert_details(self, nickname):
        cmd = [
            '/usr/bin/certutil',
            '-d', self._certdb,
            '-n', nickname,
            '-L',
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        self.log.debug("display_cert_details cmd: %s", format_cmd_list(cmd))
        try:
            result = check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

        return ensure_str(result)


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
                certdetails = self.display_cert_details(nickname)
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

    def list_ca_certs(self):
        return [
            cert
            for cert in self._rsa_cert_list()
            if self._rsa_cert_is_catrust(cert)
        ]

    def list_client_ca_certs(self):
        return [
            cert
            for cert in self._rsa_cert_list()
            if self._rsa_cert_is_caclienttrust(cert)
        ]

    def add_cert(self, nickname, input_file, ca=False):
        """Add server or CA cert
        """

        # Verify input_file exists
        if not os.path.exists(input_file):
            raise ValueError("The certificate file ({}) does not exist".format(input_file))

        pem_file = True
        if not input_file.lower().endswith(".pem"):
            pem_file = False
        else:
            self._assert_not_chain(input_file)

        if ca:
            # Verify this is a CA cert
            if not cert_is_ca(input_file):
                raise ValueError(f"Certificate ({nickname}) is not a CA certificate")
            trust_flags = "CT,,"
        else:
            # Verify this is a server cert
            if cert_is_ca(input_file):
                raise ValueError(f"Certificate ({nickname}) is not a server certificate")
            trust_flags = ",,"

        cmd = [
            '/usr/bin/certutil',
            '-A',
            '-d', self._certdb,
            '-n', nickname,
            '-t', trust_flags,
            '-i', input_file,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]
        if pem_file:
            cmd.append('-a')

        self.log.debug("add_cert cmd: %s", format_cmd_list(cmd))
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())

    def add_server_key_and_cert(self, input_key, input_cert):
        if not os.path.exists(input_key):
            raise ValueError("The key file ({}) does not exist".format(input_key))
        if not os.path.exists(input_cert):
            raise ValueError("The cert file ({}) does not exist".format(input_cert))

        self.log.debug(f"Importing key and cert -> {input_key}, {input_cert}")

        p12_bundle = "%s/temp_server_key_cert.p12" % self._certdb

        # Remove the p12 if it exists
        if os.path.exists(p12_bundle):
            os.remove(p12_bundle)

        # Transform to p12
        cmd = [
            'openssl',
            'pkcs12',
            '-export',
            '-in', input_cert,
            '-inkey', input_key,
            '-out', p12_bundle,
            '-name', CERT_NAME,
            '-passout', 'pass:',
            '-aes128'
        ]
        self.log.debug("nss cmd: %s", format_cmd_list(cmd))
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
        # Remove the server-cert if it exists, because else the import name fails.
        try:
            self.del_cert(CERT_NAME)
        except:
            pass
        try:
            # Import it
            cmd = [
                'pk12util',
                '-v',
                '-i', p12_bundle,
                '-d', self._certdb,
                '-k', '%s/%s' % (self._certdb, PWD_TXT),
                '-W', "",
            ]
            self.log.debug("nss cmd: %s", format_cmd_list(cmd))
            try:
                check_output(cmd, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                raise ValueError(e.output.decode('utf-8').rstrip())
        finally:
            # Remove the p12
            if os.path.exists(p12_bundle):
                os.remove(p12_bundle)

    def add_ca_cert_bundle(self, cert_file, nicknames):
        """
        Add a PEM file that could be a bundle of CA certs

        :param nicknames: list of names of each CA certificate
        :param cert_file: path to certificate PEM file
        :raises:
        """

        # Verify input_file exists
        if not os.path.exists(cert_file):
            raise ValueError("The certificate file ({}) does not exist".format(cert_file))

        if not cert_file.lower().endswith(".pem"):
            # Binary cert, this can not be a bundle
            self.add_cert(nicknames[0], cert_file, ca=True)
            log.info(f"Successfully added CA certificate ({nicknames[0]})")
            return

        try:
            with open(cert_file, 'r') as f:
                ca_count = 0
                ca_files_to_cleanup = []
                writing_ca = False
                try:
                    for line in f:
                        if line.startswith('-----BEGIN CERTIFICATE-----'):
                            # create tmp cert file
                            writing_ca = True
                            ca_file_name = cert_file + "-" + str(ca_count)
                            ca_file = open(ca_file_name, 'w')
                            ca_files_to_cleanup.append(ca_file_name)
                            ca_file.write(line)
                        elif writing_ca:
                            ca_file.write(line)
                            if line.startswith('-----END CERTIFICATE-----'):
                                writing_ca = False
                                ca_file.close()

                                # Generate CA certificate nickname
                                names_len = len(nicknames)
                                if names_len > ca_count:
                                    if nicknames[ca_count].lower() == CERT_NAME.lower() or nicknames[ca_count].lower() == CA_NAME.lower():
                                        # cleanup
                                        try:
                                            for tmp_file in ca_files_to_cleanup:
                                                os.remove(tmp_file)
                                        except IOError as e:
                                            # failed to remove tmp cert
                                            log.debug("Failed to remove tmp cert file: " + str(e))
                                        raise ValueError(f"You may not import a CA with the nickname {CERT_NAME} or {CA_NAME}")
                                    ca_cert_name = nicknames[ca_count]
                                else:
                                    # A name was not provided for this cert, create one
                                    ca_cert_name = nicknames[names_len - 1] + str(ca_count)

                                # Check if certificate nickname exists
                                name_exists = False
                                try:
                                    self.get_cert_details(ca_cert_name)
                                    name_exists = True
                                except ValueError:
                                    pass

                                if name_exists:
                                    # Not good, cleanup and raise error
                                    try:
                                        for tmp_file in ca_files_to_cleanup:
                                            os.remove(tmp_file)
                                    except IOError as e:
                                        # failed to remove tmp cert
                                        log.debug("Failed to remove tmp cert file: " + str(e))
                                    raise ValueError(f"Certificate already exists with the same name ({ca_cert_name})")

                                # Verify this is a CA cert
                                if not cert_is_ca(ca_file_name):
                                    raise ValueError(f"Certificate ({ca_cert_name}) is not a CA certificate")

                                # Add this CA certificate
                                self.add_cert(ca_cert_name, ca_file_name, ca=True)
                                ca_count += 1
                                log.info(f"Successfully added CA certificate ({ca_cert_name})")

                    # All done, cleanup
                    try:
                        for tmp_file in ca_files_to_cleanup:
                            os.remove(tmp_file)
                    except IOError as e:
                        # failed to remove tmp cert
                        log.debug("Failed to remove tmp cert file: " + str(e))
                except IOError as e:
                    # Some failure, remove all the tmp files
                    ca_file.close()
                    try:
                        for tmp_file in ca_files_to_cleanup:
                            os.remove(tmp_file)
                    except IOError as e:
                        # failed to remove tmp cert
                        log.debug("Failed to remove tmp cert file: " + str(e))
                    raise e
        except EnvironmentError as e:
            raise e

    def export_cert(self, nickname, output_file=None, der_format=False):
        """
        :param nickname: name of certificate
        :param output_file: name for exported certificate
        :param der_format: export certificate in DER/binary format
        :raise ValueError: error
        """
        cmd = [
            '/usr/bin/certutil',
            '-L',
            '-d', self._certdb,
            '-n', nickname,
            '-f',
            '%s/%s' % (self._certdb, PWD_TXT),
        ]

        # Handle args for PEM vs DER options
        if der_format:
            cmd.append('-r')
            if output_file is None:
                output_file = nickname + ".crt"
        else:
            cmd.append('-a')
            if output_file is None:
                output_file = nickname + ".pem"

        # Set output file name
        if not output_file.startswith("/"):
            # Must be full path, otherwise but it in the cert dir
            output_file = f"{self._certdb}/{output_file}"
        cmd.append('-o')
        cmd.append(output_file)

        self.log.debug("export_cert cmd: %s", format_cmd_list(cmd))
        try:
            check_output(cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise ValueError(e.output.decode('utf-8').rstrip())
