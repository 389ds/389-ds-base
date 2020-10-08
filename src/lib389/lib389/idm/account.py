# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# Copyright (C) 2017, William Brown <william at blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import time
import subprocess
from enum import Enum
import ldap
from lib389._mapped_object import DSLdapObject, DSLdapObjects, _gen_or, _gen_filter, _term_gen
from lib389._constants import SER_ROOT_DN, SER_ROOT_PW
from lib389.utils import gentime_to_posix_time, gentime_to_datetime
from lib389.plugins import AccountPolicyPlugin, AccountPolicyConfig, AccountPolicyEntry
from lib389.cos import CosTemplates
from lib389.mappingTree import MappingTrees
from lib389.idm.role import Roles


class AccountState(Enum):
    ACTIVATED = "activated"
    DIRECTLY_LOCKED = "directly locked through nsAccountLock"
    INDIRECTLY_LOCKED = "indirectly locked through a Role"
    INACTIVITY_LIMIT_EXCEEDED = "inactivity limit exceeded"

    def describe(self, role_dn=None):
        if self.name == "INDIRECTLY_LOCKED" and role_dn is not None:
            return f'{self.value} - {role_dn}'
        else:
            return f'{self.value}'


class Account(DSLdapObject):
    """A single instance of Account entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def _format_status_message(self, message, create_time, modify_time, last_login_time, limit, role_dn=None):
        params = {}
        now = time.mktime(time.gmtime())
        params["Creation Date"] = gentime_to_datetime(create_time)
        params["Modification Date"] = gentime_to_datetime(modify_time)
        params["Last Login Date"] = None
        params["Time Until Inactive"] = None
        params["Time Since Inactive"] = None
        if last_login_time:
            params["Last Login Date"] = gentime_to_datetime(last_login_time)
            if limit:
                remaining_time = float(limit) + gentime_to_posix_time(last_login_time) - now
                if remaining_time <= 0:
                    if message == AccountState.INACTIVITY_LIMIT_EXCEEDED:
                        params["Time Since Inactive"] = remaining_time
                else:
                    params["Time Until Inactive"] = remaining_time
        result = {"state": message, "params": params, "calc_time": now, "role_dn": None}
        if role_dn is not None:
            result["role_dn"] = role_dn
        return result

    def _dict_get_with_ignore_indexerror(self, dict, attr):
        try:
            return dict[attr][0]
        except IndexError:
            return ""

    def status(self):
        """Check if account is locked by Account Policy plugin or
        nsAccountLock (directly or indirectly)

        :returns: a dict in a format -
                  {"status": status, "params": activity_data, "calc_time": epoch_time}
        """

        inst = self._instance

        # Fetch Account Policy data if its enabled
        plugin = AccountPolicyPlugin(inst)
        state_attr = ""
        alt_state_attr = ""
        limit = ""
        spec_attr = ""
        limit_attr = ""
        process_account_policy = False
        try:
            process_account_policy = plugin.status()
        except IndexError:
            self._log.debug("The bound user doesn't have rights to access Account Policy settings. Not checking.")

        if process_account_policy:
            config_dn = plugin.get_attr_val_utf8("nsslapd-pluginarg0")
            config = AccountPolicyConfig(inst, config_dn)
            config_settings = config.get_attrs_vals_utf8(["stateattrname", "altstateattrname",
                                                          "specattrname", "limitattrname"])
            state_attr = self._dict_get_with_ignore_indexerror(config_settings, "stateattrname")
            alt_state_attr = self._dict_get_with_ignore_indexerror(config_settings, "altstateattrname")
            spec_attr = self._dict_get_with_ignore_indexerror(config_settings, "specattrname")
            limit_attr = self._dict_get_with_ignore_indexerror(config_settings, "limitattrname")

            mapping_trees = MappingTrees(inst)
            root_suffix = mapping_trees.get_root_suffix_by_entry(self.dn)
            cos_entries = CosTemplates(inst, root_suffix)
            accpol_entry_dn = ""
            for cos in cos_entries.list():
                if cos.present(spec_attr):
                    accpol_entry_dn = cos.get_attr_val_utf8_l(spec_attr)
            if accpol_entry_dn:
                accpol_entry = AccountPolicyEntry(inst, accpol_entry_dn)
            else:
                accpol_entry = config
            limit = accpol_entry.get_attr_val_utf8_l(limit_attr)

        # Fetch account data
        account_data = self.get_attrs_vals_utf8(["createTimestamp", "modifyTimeStamp",
                                                 "nsAccountLock", state_attr])

        last_login_time = self._dict_get_with_ignore_indexerror(account_data, state_attr)
        if not last_login_time:
            last_login_time = self._dict_get_with_ignore_indexerror(account_data, alt_state_attr)

        create_time = self._dict_get_with_ignore_indexerror(account_data, "createTimestamp")
        modify_time = self._dict_get_with_ignore_indexerror(account_data, "modifyTimeStamp")

        acct_roles = self.get_attr_vals_utf8_l("nsRole")
        mapping_trees = MappingTrees(inst)
        root_suffix = ""
        try:
            root_suffix = mapping_trees.get_root_suffix_by_entry(self.dn)
        except ldap.NO_SUCH_OBJECT:
            self._log.debug("The bound user doesn't have rights to access disabled roles settings. Not checking.")
        if root_suffix:
            roles = Roles(inst, root_suffix)
            try:
                disabled_roles = roles.get_disabled_roles()

                # Locked indirectly through a role
                locked_indirectly_role_dn = ""
                for role in acct_roles:
                    if str.lower(role) in [str.lower(role.dn) for role in disabled_roles.keys()]:
                        locked_indirectly_role_dn = role
                if locked_indirectly_role_dn:
                    return self._format_status_message(AccountState.INDIRECTLY_LOCKED, create_time, modify_time,
                                                       last_login_time, limit, locked_indirectly_role_dn)
            except ldap.NO_SUCH_OBJECT:
                pass

        # Locked directly
        if self._dict_get_with_ignore_indexerror(account_data, "nsAccountLock") == "true":
            return self._format_status_message(AccountState.DIRECTLY_LOCKED,
                                               create_time, modify_time, last_login_time, limit)

        # Locked indirectly through Account Policy plugin
        if process_account_policy and last_login_time:
            # Now check the Account Policy Plugin inactivity limits
            remaining_time = float(limit) - (time.mktime(time.gmtime()) - gentime_to_posix_time(last_login_time))
            if remaining_time <= 0:
                return self._format_status_message(AccountState.INACTIVITY_LIMIT_EXCEEDED,
                                                   create_time, modify_time, last_login_time, limit)
        # All checks are passed - we are active
        return self._format_status_message(AccountState.ACTIVATED, create_time, modify_time, last_login_time, limit)

    def ensure_lock(self):
        """Ensure nsAccountLock is set to 'true'"""

        self.replace('nsAccountLock', 'true')

    def ensure_unlock(self):
        """Unset nsAccountLock if it's set"""

        self.ensure_removed('nsAccountLock', None)

    def lock(self):
        """Set nsAccountLock to 'true'"""

        current_status = self.status()
        if current_status["state"] == AccountState.DIRECTLY_LOCKED:
            raise ValueError("Account is already active")
        self.replace('nsAccountLock', 'true')

    def unlock(self):
        """Unset nsAccountLock"""

        current_status = self.status()
        if current_status["state"] == AccountState.ACTIVATED:
            raise ValueError("Account is already active")
        self.remove('nsAccountLock', None)

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
        self._instance.simple_bind_s(self.dn, password, escapehatch='i am sure')

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
        # Kill any local kerberos ccache.
        subprocess.call(['kdestroy', '-A'])

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
            serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')


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
        # MUST BE NONE. For more, see _gen_filter in _mapped_object.py.
        self._filterattrs = None
        self._childobject = Account
        self._basedn = basedn

    #### This is copied from DSLdapObjects, but change _gen_and to _gen_or!!!

    def _get_objectclass_filter(self):
        return _gen_or(
            _gen_filter(_term_gen('objectclass'), self._objectclasses)
        )


class Anonymous(DSLdapObject):
    """A single instance of Anonymous bind

    :param instance: An instance
    :type instance: lib389.DirSrv
    """
    def __init__(self, instance):
        super(Anonymous, self).__init__(instance, dn=None)

    def bind(self, *args, **kwargs):
        """Open a new connection and Anonymous bind .
        You can pass arguments that will be passed to openConnection.
        :returns: Connection with a binding as the Anonymous
        """
        inst_clone = self._instance.clone({SER_ROOT_DN: '', SER_ROOT_PW: ''})
        inst_clone.open(*args, **kwargs)
        return inst_clone
