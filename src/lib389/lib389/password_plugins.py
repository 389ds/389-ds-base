# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 William Brown <william@blackhats.net.au>
# Copyright (C) 2023 Red Hat, Inc.
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

class PBKDF2Plugin(PasswordPlugin):
    def __init__(self, instance, dn="cn=PBKDF2-SHA256,cn=Password Storage Schemes,cn=plugins,cn=config"):
        super(PBKDF2Plugin, self).__init__(instance, dn)


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


class PasswordPlugins(Plugins):
    def __init__(self, instance):
        super(PasswordPlugins, self).__init__(instance=instance)
        self._objectclasses = ['nsSlapdPlugin']
        self._filterattrs = ['cn']
        self._childobject = PasswordPlugin
        self._basedn = DN_PWDSTORAGE_SCHEMES
