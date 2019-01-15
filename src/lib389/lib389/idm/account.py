# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389._mapped_object import DSLdapObject, DSLdapObjects, _gen_or, _gen_filter, _term_gen
from lib389._constants import SER_ROOT_DN, SER_ROOT_PW

import os
import subprocess

class Account(DSLdapObject):
    """A single instance of Account entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def is_locked(self):
        """Check if nsAccountLock is set

        :returns: True if account is locked
        """

        return self.present('nsAccountLock')

    def lock(self):
        """Set nsAccountLock to 'true'"""

        self.replace('nsAccountLock', 'true')

    def unlock(self):
        """Unset nsAccountLock"""

        self.ensure_removed('nsAccountLock', None)

    # If the account can be bound to, this will attempt to do so. We don't check
    # for exceptions, just pass them back!
    def bind(self, password=None, *args, **kwargs):
        """Open a new connection and bind with the entry.
        You can pass arguments that will be passed to openConnection.

        :param password: An entry password
        :type password: str
        :returns: Connection with a binding as the entry
        """

        inst_clone = self._instance.clone({SER_ROOT_DN: self.dn, SER_ROOT_PW: password})
        inst_clone.open(*args, **kwargs)
        return inst_clone

    def rebind(self, password):
        """Rebind on the same connection
        :param password: An entry password
        :type password: str
        """
        self._instance.simple_bind_s(self.dn, password)

    def sasl_bind(self, *args, **kwargs):
        """Open a new connection and bind with the entry via SASL.
        You can pass arguments that will be pass to clone.

        :return: Connection with a sasl binding to the entry.
        """
        inst_clone = self._instance.clone({SER_ROOT_DN: self.dn})
        inst_clone.open(*args, **kwargs)
        return inst_clone

    def create_keytab(self):
        """
        Create a keytab for this account valid to bind with.
        """
        assert self._instance.realm is not None

        myuid = self.get_attr_val_utf8('uid')
        self._instance.realm.create_principal(myuid)
        self._instance.realm.create_keytab(myuid, "/tmp/%s.keytab" % myuid)

        self._keytab = "/tmp/%s.keytab" % myuid

    def bind_gssapi(self):
        """
        Bind this account with gssapi credntials (if available)
        """
        assert self._instance.realm is not None
        # Kill any local ccache.
        subprocess.call(['/usr/bin/kdestroy', '-A'])

        # This uses an in memory once off ccache.
        os.environ["KRB5_CLIENT_KTNAME"] = self._keytab

        # Because of the way that GSSAPI works, we can't
        # use the normal dirsrv open method.
        inst_clone = self._instance.clone()
        inst_clone.open(saslmethod='gssapi')
        return inst_clone

    def enroll_certificate(self, der_path):
        """Enroll a certificate for certmap verification. Because of the userCertificate
        attribute, we have to do this on userAccount which has support for it.

        :param der_path: the certificate file in DER format to include.
        :type der_path: str
        """
        if ds_is_older('1.4.0'):
            raise Exception("This version of DS does not support nsAccount")
        # Given a cert path, add this to the object as a userCertificate
        crt = None
        with open(der_path, 'rb') as f:
            crt = f.read()
        self.add('usercertificate;binary', crt)

    def reset_password(self, new_password):
        """Set the password of the account: This requires write permission to
        the userPassword attribute, so likely is only possible as an administrator
        of the directory.

        :param new_password: The new password value to set
        :type new_password: str
        """
        self.set('userPassword', new_password)

    def change_password(self, current_password, new_password):
        """Using the accounts current bind password, performan an ldap passwd
        change extended operation. This does not required elevated permissions
        to read/write the userPassword field, so is the way that most accounts
        would change their password. This doesn't work on all classes of objects
        so it could error.

        :param current_password: The existing password value
        :type current_password: str
        :param new_password: The new password value to set
        :type new_password: str
        """
        # Please see _mapped_object.py and DSLdapObject for why this is structured
        # in this way re-controls.
        self._instance.passwd_s(self._dn, current_password, new_password,
            serverctrls=self._server_controls, clientctrls=self._client_controls)

class Accounts(DSLdapObjects):
    """DSLdapObjects that represents Account entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Base DN for all account entries below
    :type basedn: str
    """

    def __init__(self, instance, basedn):
        super(Accounts, self).__init__(instance)
        # These are all the objects capable of holding a password.
        self._objectclasses = [
            'nsAccount',
            'nsPerson',
            'simpleSecurityObject',
            'organization',
            'person',
            'account',
            'organizationalUnit',
            'netscapeServer',
            'domain',
            'posixAccount',
            'shadowAccount',
            'posixGroup',
            'mailRecipient',
        ]
        # MUST BE NONE.
        self._filterattrs = None
        self._childobject = Account
        self._basedn = basedn

    #### This is copied from DSLdapObjects, but change _gen_and to _gen_or!!!

    def _get_objectclass_filter(self):
        return _gen_or(
            _gen_filter(_term_gen('objectclass'), self._objectclasses)
        )

