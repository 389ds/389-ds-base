# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
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
import os
import signal
import distro
from lib389._constants import *
from socket import getfqdn
from lib389.utils import getdomainname
from lib389.tools import DirSrvTools
from lib389.passwd import password_generate


class MitKrb5(object):
    # Get the realm information
    def __init__(self, realm, warnings=False, debug=False):
        self.warnings = warnings
        self.realm = realm.upper()
        self.krb_prefix = ""
        sep = os.path.sep
        # For the future if we have a non-os krb install.
        if 'suse' in distro.like():
            self.kadmin = os.path.join(sep, self.krb_prefix, "usr/lib/mit/sbin/kadmin.local")
            self.kdb5_util = os.path.join(sep, self.krb_prefix, "usr/lib/mit/sbin/kdb5_util")
            self.krb5kdc = os.path.join(sep, self.krb_prefix, "usr/lib/mit/sbin/krb5kdc")
            self.kdcconf = os.path.join(sep, self.krb_prefix, "var/lib/kerberos/krb5kdc/kdc.conf")
            self.kadm5acl = os.path.join(sep, self.krb_prefix, "var/lib/kerberos/krb5kdc/kadm5.acl")
            self.kadm5keytab = os.path.join(sep, self.krb_prefix, "var/lib/kerberos/krb5kdc/kadm5.keytab")
            self.kdcpid = os.path.join(sep, self.krb_prefix, "var/run/krb5kdc.pid")
            self.krb5conf = os.path.join(sep, self.krb_prefix, "etc/krb5.conf")
            self.krb5confrealm = os.path.join(sep, self.krb_prefix, "etc/krb5.conf.d",
                                              self.realm.lower().replace('.', '-'))
        else:
            self.kadmin = os.path.join(sep, self.krb_prefix, "usr/sbin/kadmin.local")
            self.kdb5_util = os.path.join(sep, self.krb_prefix, "usr/sbin/kdb5_util")
            self.krb5kdc = os.path.join(sep, self.krb_prefix, "usr/sbin/krb5kdc")
            self.kdcconf = os.path.join(sep, self.krb_prefix, "var/kerberos/krb5kdc/kdc.conf")
            self.kadm5acl = os.path.join(sep, self.krb_prefix, "var/kerberos/krb5kdc/kadm5.acl")
            self.kadm5keytab = os.path.join(sep, self.krb_prefix, "var/kerberos/krb5kdc/kadm5.keytab")
            self.kdcpid = os.path.join(sep, self.krb_prefix, "var/run/krb5kdc.pid")
            self.krb5conf = os.path.join(sep, self.krb_prefix, "etc/krb5.conf")
            self.krb5confrealm = os.path.join(sep, self.krb_prefix, "etc/krb5.conf.d",
                                              self.realm.lower().replace('.', '-'))

        self.krb_primary_password = password_generate()

        # Should we write this to a file?

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
            raw_input("Ctrl-C to exit, or press ENTER to continue.")
        print("Kerberos primary password: %s" % self.krb_primary_password)

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
[libdefaults]
 default_realm = {REALM}

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
  acl_file = {KADM5ACL}
  dict_file = /usr/share/dict/words
  admin_keytab = {KADM5KEYTAB}
  # Just use strong enctypes
  # supported_enctypes = aes256-cts:normal aes128-cts:normal
 }}

""".format(REALM=self.realm, PREFIX=self.krb_prefix, KADM5ACL=self.kadm5acl, KADM5KEYTAB=self.kadm5keytab))
        # Invoke kdb5_util
        # Can this use -P
        p = Popen([self.kdb5_util, 'create', '-r', self.realm, '-s', '-P',
                   self.krb_primary_password], env=self.krb_env)
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
        p.communicate(b"yes\n")
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

