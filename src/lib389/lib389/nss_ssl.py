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

from nss import nss
from nss import error as nss_error
from subprocess import check_call

from lib389.passwd import password_generate

KEYBITS=4096
CA_NAME='Self-Signed-CA'
CERT_NAME='Server-Cert'
PIN_TXT='pin.txt'
PWD_TXT='pwdfile.txt'
ISSUER='CN=ca.unknown.example.com,O=testing,L=unknown,ST=Queensland,C=AU'
SELF_ISSUER='CN={HOSTNAME},O=testing,L=unknown,ST=Queensland,C=AU'
VALID=2

dbpassword = None

def nss_ssl_pw_cb(slot, retry, optional=[]):
    global dbpassword
    return dbpassword

class NssSsl(object):
    def __init__(self, dirsrv, dbpassword=None):
        self.dirsrv = dirsrv
        self.log = self.dirsrv.log
        if dbpassword is None:
            self.dbpassword = password_generate()
        else:
            self.dbpassword = dbpassword

    def _generate_noise(self, fpath):
        noise = password_generate(256)
        with open(fpath, 'w') as f:
            f.write(noise)



    def open(self):
        """
        Opens the certdb.
        """
        global dbpassword
        # Read in the password
        ## How! Do I make the password CB work here?
        if os.path.exists('%s/%s' % (self.dirsrv.confdir, PWD_TXT)):
            with open('%s/%s' % (self.dirsrv.confdir, PWD_TXT), 'r') as f:
                self.dbpassword = f.readline()
            # This is to make the password CB work.
            dbpassword = self.dbpassword
        else:
            Exception('No pwdfile.txt!')

        if not nss.nss_is_initialized():
            nss.nss_init(self.dirsrv.confdir)
        nss.set_password_callback(nss_ssl_pw_cb)

    # How do we automatically close it? Decorator?

    def close(self):
        """
        Close the db, and returns IF it was opened (IE we had to close it)
        """
        r = nss.nss_is_initialized()
        if r:
            # This is impossible to clean out to make shutdown work ...
            self.log.info(nss.dump_certificate_cache_info())
            #nss.nss_shutdown()
        return r

    def reinit(self):
        """
        Re-init (create) the nss db.
        """
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
        cmd = ['/usr/bin/certutil', '-N', '-d', self.dirsrv.confdir, '-f',
            '%s/%s' % (self.dirsrv.confdir, PWD_TXT)]
        result = check_call(cmd)
        if result != 0:
            return False
        else:
            return True

    def _db_exists(self):
        """
        Check that a nss db exists at the certpath
        """
        try:
            self.open()
        except nss_error.NSPRError:
            return False
        return self.close()

    def create_rsa_ca(self):
        """
        Create a self signed CA.
        """
        self.open()

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
            self.dirsrv.confdir,
            '-z',
            '%s/noise.txt' % self.dirsrv.confdir,
            '-f',
            '%s/%s' % (self.dirsrv.confdir, PWD_TXT),
        ]

        result = check_call(cmd)

        self.close()

        if result != 0:
            return False
        else:
            return True

    def _rsa_ca_exists(self):
        """
        Detect if a self-signed ca exists
        """
        self.open()
        have_ca = False
        ca_certs = nss.list_certs(nss.PK11CertListCA)
        # I'll let the bad password exception go up here.
        for ca_cert in ca_certs:
            pk = nss.find_key_by_any_cert(ca_cert)
            if pk is not None:
                #### STILL NEED TO CHECK the nickname.
                have_ca = True
                del(pk)
            del(ca_cert)
        self.close()
        return have_ca

    def _rsa_key_and_cert_exists(self):
        """
        Check if a valid server key and cert pain exist.
        """
        self.open()
        have_cert = False
        # I'll let the bad password exception go up here.
        try:
            cert = nss.find_cert_from_nickname(CERT_NAME)
            pk = nss.find_key_by_any_cert(cert)
            if pk is not None:
                ### STILL NEED tO CHECK THE NICKNAME
                have_cert = True
                del(pk)
            del(cert)
        except:
            # Means no cert. Need to expand this to work with no password either.
            pass
        self.close()
        return have_cert

    def create_rsa_key_and_cert(self, alt_names=[]):
        """
        Create a key and a cert that is signed by the self signed ca

        This will use the hostname from the DS instance, and takes a list of
        extra names to take.
        """
        self.open()

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
            self.dirsrv.confdir,
            '-z',
            '%s/noise.txt' % self.dirsrv.confdir,
            '-f',
            '%s/%s' % (self.dirsrv.confdir, PWD_TXT),
        ]

        result = check_call(cmd)

        self.close()

        if result != 0:
            return False
        else:
            return True

