# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Mit_krb5 kdc setup routine.

Not ideal for use for a PRD krb system, used internally for testing GSSAPI
integration with 389ds.

"""
# In the future we might add support for an ldap-backed krb realm
from subprocess import Popen, PIPE
import krbV
import os
import signal

from lib389._constants import *
from socket import getfqdn
from lib389.utils import getdomainname
from lib389.tools import DirSrvTools


class MitKrb5(object):
    # Get the realm information
    def __init__(self, realm, warnings=False, debug=False):
        self.warnings = warnings
        self.realm = realm.upper()
        # For the future if we have a non-os krb install.
        self.krb_prefix = ""
        # Probably should be using os.path.join ...
        self.kadmin = "%s/usr/sbin/kadmin.local" % self.krb_prefix
        self.kdb5_util = "%s/usr/sbin/kdb5_util" % self.krb_prefix
        self.krb5kdc = "%s/usr/sbin/krb5kdc" % self.krb_prefix
        self.kdcconf = "%s/var/kerberos/krb5kdc/kdc.conf" % self.krb_prefix
        self.kdcpid = "%s/var/run/krb5kdc.pid" % self.krb_prefix
        self.krb5conf = "%s/etc/krb5.conf" % (self.krb_prefix)
        self.krb5confrealm = ("%s/etc/krb5.conf.d/%s" %
                              (self.krb_prefix,
                               self.realm.lower().replace('.', '-')))

        # THIS IS NOT SECURE
        # We should probably randomise this per install
        # We should write it to a file too ...
        self.krb_master_password = 'si7athohyiezah9riz6Aayaiphoo1ii0uashail5'

        self.krb_env = {}
        if debug is True:
            self.krb_env['KRB5_TRACE'] = '/tmp/krb_lib389.trace'

    # Validate our hostname is in /etc/hosts, and is fqdn
    def validate_hostname(self):
        if getdomainname() == "":
            # Should we return a message?
            raise AssertionError("Host does not have a domain name.")
        DirSrvTools.searchHostsFile(getfqdn())

    # Check if a realm exists or not.
    # Should we check globally? Or just on this local host.
    def check_realm(self):
        p = Popen([self.kadmin, '-r', self.realm, '-q', 'list_principals'],
                  env=self.krb_env, stdout=PIPE, stderr=PIPE)
        returncode = p.wait()
        if returncode == 0:
            return True
        return False

    # Be able to setup the real
    def create_realm(self, ignore=False):
        self.validate_hostname()
        exists = self.check_realm()
        if exists is True and ignore is False:
            raise AssertionError("Realm already exists!")
        elif exists is True and ignore is True:
            # Realm exists, continue
            return
        # Raise a scary warning about eating your krb settings
        if self.warnings:
            print("This will alter / erase your krb5 and kdc settings.")
            print("THIS IS NOT A SECURE KRB5 INSTALL, DON'T USE IN PRODUCTION")
            raw_input("Ctrl-C to exit, or press ENTER to continue.")

        # If we don't have the directories for this, create them.
        # but if we create them there is no guarantee this will work ...
        if not os.path.exists(os.path.dirname(self.krb5confrealm)):
            os.makedirs(os.path.dirname(self.krb5confrealm))
        # Put the includedir statement into /etc/krb5.conf
        include = True
        with open(self.krb5conf, 'r') as kfile:
            for line in kfile.readlines():
                if 'includedir %s/' % os.path.dirname(self.krb5confrealm) \
                   in line:
                    include = False
        if include is True:
            with open(self.krb5conf, 'a') as kfile:
                kfile.write('\nincludedir %s/\n' %
                            os.path.dirname(self.krb5confrealm))

        # Write to  /etc/krb5.conf.d/example.com
        with open(self.krb5confrealm, 'w') as cfile:
            domainname = getdomainname()
            fqdn = getfqdn()
            realm = self.realm
            lrealm = self.realm.lower()
            cfile.write("""
[realms]
{REALM} = {{
 kdc = {HOST}
 admin_server = {HOST}
}}

[domain_realm]
.{LREALM} = {REALM}
{LREALM} = {REALM}
.{DOMAIN} = {REALM}
{DOMAIN} = {REALM}
""".format(HOST=fqdn, REALM=realm, LREALM=lrealm, DOMAIN=domainname))

        # Do we need to edit /var/kerberos/krb5kdc/kdc.conf ?
        with open(self.kdcconf, 'w') as kfile:
            kfile.write("""
[kdcdefaults]
 kdc_ports = 88
 kdc_tcp_ports = 88

[realms]
 {REALM} = {{
  #master_key_type = aes256-cts
  acl_file = {PREFIX}/var/kerberos/krb5kdc/kadm5.acl
  dict_file = /usr/share/dict/words
  admin_keytab = {PREFIX}/var/kerberos/krb5kdc/kadm5.keytab
  # Just use strong enctypes
  supported_enctypes = aes256-cts:normal aes128-cts:normal
 }}

""".format(REALM=self.realm, PREFIX=self.krb_prefix))
        # Invoke kdb5_util
        # Can this use -P
        p = Popen([self.kdb5_util, 'create', '-r', self.realm, '-s', '-P',
                   self.krb_master_password], env=self.krb_env)
        assert(p.wait() == 0)
        # Start the kdc
        p = Popen([self.krb5kdc, '-P', self.kdcpid, '-r', self.realm],
                  env=self.krb_env)
        assert(p.wait() == 0)
        # ???
        # PROFIT
        return

    # Destroy the realm
    def destroy_realm(self):
        assert(self.check_realm())
        if self.warnings:
            print("This will ERASE your kdc settings.")
            raw_input("Ctrl-C to exit, or press ENTER to continue.")
        # If the pid exissts, try to kill it.
        if os.path.isfile(self.kdcpid):
            with open(self.kdcpid, 'r') as pfile:
                pid = int(pfile.readline().strip())
                try:
                    os.kill(pid, signal.SIGTERM)
                except OSError:
                    pass
            os.remove(self.kdcpid)

        p = Popen([self.kdb5_util, 'destroy', '-r', self.realm],
                  env=self.krb_env, stdin=PIPE)
        p.communicate("yes\n")
        p.wait()
        assert(p.returncode == 0)
        # Should we clean up the configurations we made too?
        # ???
        # PROFIT
        return

    def list_principals(self):
        assert(self.check_realm())
        p = Popen([self.kadmin, '-r', self.realm, '-q', 'list_principals'],
                  env=self.krb_env)
        p.wait()

    # Create princs for services
    def create_principal(self, principal, password=None):
        assert(self.check_realm())
        p = None
        if password:
            p = Popen([self.kadmin, '-r', self.realm, '-q',
                       'add_principal -pw \'%s\' %s@%s' % (password, principal,
                                                           self.realm)],
                      env=self.krb_env)
        else:
            p = Popen([self.kadmin, '-r', self.realm, '-q',
                       'add_principal -randkey %s@%s' % (principal,
                                                         self.realm)],
                      env=self.krb_env)
        assert(p.wait() == 0)
        self.list_principals()

    # Extract KTs for services
    def create_keytab(self, principal, keytab):
        assert(self.check_realm())
        # Remove the old keytab
        try:
            os.remove(keytab)
        except:
            pass
        p = Popen([self.kadmin, '-r', self.realm, '-q', 'ktadd -k %s %s@%s' %
                  (keytab, principal, self.realm)])
        assert(p.wait() == 0)


class KrbClient(object):
    def __init__(self, principal, keytab, cache_file=None):
        self.context = krbV.default_context()
        self.principal = principal
        self.keytab = keytab
        self._keytab = krbV.Keytab(name=self.keytab, context=self.context)
        self._principal = krbV.Principal(name=self.principal,
                                         context=self.context)
        if cache_file:
            self.ccache = krbV.CCache(name="FILE:" + cache_file,
                                      context=self.context,
                                      primary_principal=self._principal)
        else:
            self.ccache = self.context.default_ccache(
                primary_principal=self._principal)
        if self._keytab:
            self.reinit()

    def reinit(self):
        assert self._keytab
        assert self._principal
        self.ccache.init(self._principal)
        self.ccache.init_creds_keytab(keytab=self._keytab,
                                      principal=self._principal)
