# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 William Brown <william@blackhats.net.au>
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.plugins import Plugin, Plugins
from lib389._constants import DN_PWDSTORAGE_SCHEMES


class PasswordPlugin(Plugin):
    _plugin_properties = {
        'nsslapd-pluginpath': 'libpwdstorage-plugin',
        'nsslapd-plugintype': 'pwdstoragescheme',
        'nsslapd-pluginEnabled' : 'on'
    }

    def __init__(self, instance, dn=None):
        super(PasswordPlugin, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = [
            'nsslapd-pluginEnabled',
            'nsslapd-pluginPath',
            'nsslapd-pluginInitfunc',
            'nsslapd-pluginType',
            ]
        self._create_objectclasses = ['top', 'nsslapdplugin']
        # We'll mark this protected, and people can just disable the plugins.
        self._protected = True


class SSHA512Plugin(PasswordPlugin):
    def __init__(self, instance, dn=f'cn=SSHA512,{DN_PWDSTORAGE_SCHEMES}'):
        super(SSHA512Plugin, self).__init__(instance, dn)


class SHAPlugin(PasswordPlugin):
    def __init__(self, instance, dn=f'cn=SHA,{DN_PWDSTORAGE_SCHEMES}'):
        super(SHAPlugin, self).__init__(instance, dn)


class CRYPTPlugin(PasswordPlugin):
    def __init__(self, instance, dn=f'cn=CRYPT,{DN_PWDSTORAGE_SCHEMES}'):
        super(CRYPTPlugin, self).__init__(instance, dn)


class SSHAPlugin(PasswordPlugin):
    def __init__(self, instance, dn=f'cn=SSHA,{DN_PWDSTORAGE_SCHEMES}'):
        super(SSHAPlugin, self).__init__(instance, dn)


class PBKDF2BasePlugin(PasswordPlugin):
    """Base class for all PBKDF2 variants"""
    DEFAULT_ROUNDS = 100000

    def __init__(self, instance, dn):
        super(PBKDF2BasePlugin, self).__init__(instance, dn)
        self._create_objectclasses.append('pwdPBKDF2PluginConfig')
        
    def set_rounds(self, rounds):
        """Set the number of rounds for PBKDF2 hashing (requires restart)
        
        :param rounds: Number of rounds
        :type rounds: int
        """
        # Ensure the pwdPBKDF2PluginConfig objectClass is present
        self.ensure_present('objectClass', 'pwdPBKDF2PluginConfig')
        
        rounds = int(rounds)
        if rounds < 10000 or rounds > 10000000:
            raise ValueError("PBKDF2 rounds must be between 10,000 and 10,000,000")
        self.replace('nsslapd-pwdPBKDF2NumIterations', str(rounds))
        
    def get_rounds(self):
        """Get the current number of rounds
        
        :param use_json: Whether to return JSON formatted output
        :type use_json: bool
        :returns: Current rounds setting or JSON string
        :rtype: int
        """
        rounds = self.get_attr_val_int('nsslapd-pwdPBKDF2NumIterations')
        if rounds:
            return rounds
        return self.DEFAULT_ROUNDS


class PBKDF2Plugin(PBKDF2BasePlugin):
    """PBKDF2 password storage scheme"""

    def __init__(self, instance, dn=f'cn=PBKDF2,{DN_PWDSTORAGE_SCHEMES}'):
        super(PBKDF2Plugin, self).__init__(instance, dn)


class PBKDF2SHA1Plugin(PBKDF2BasePlugin):
    """PBKDF2-SHA1 password storage scheme"""

    def __init__(self, instance, dn=f'cn=PBKDF2-SHA1,{DN_PWDSTORAGE_SCHEMES}'):
        super(PBKDF2SHA1Plugin, self).__init__(instance, dn)


class PBKDF2SHA256Plugin(PBKDF2BasePlugin):
    """PBKDF2-SHA256 password storage scheme"""

    def __init__(self, instance, dn=f'cn=PBKDF2-SHA256,{DN_PWDSTORAGE_SCHEMES}'):
        super(PBKDF2SHA256Plugin, self).__init__(instance, dn)


class PBKDF2SHA512Plugin(PBKDF2BasePlugin):
    """PBKDF2-SHA512 password storage scheme"""

    def __init__(self, instance, dn=f'cn=PBKDF2-SHA512,{DN_PWDSTORAGE_SCHEMES}'):
        super(PBKDF2SHA512Plugin, self).__init__(instance, dn)


class PasswordPlugins(Plugins):
    def __init__(self, instance):
        super(PasswordPlugins, self).__init__(instance=instance)
        self._objectclasses = ['nsSlapdPlugin']
        self._filterattrs = ['cn']
        self._childobject = PasswordPlugin
        self._basedn = DN_PWDSTORAGE_SCHEMES
