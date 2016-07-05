# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389 import DirSrv, InvalidArgumentError
from lib389.properties import *
from lib389._constants import *

from lib389._mapped_object import DSLdapObjects, DSLdapObject

class Plugin(DSLdapObject):
    def __init__(self, instance, dn=None, batch=False):
        super(Plugin, self).__init__(instance, dn, batch)
        self._rdn_attribute = 'cn'
        self._must_attributes = []
        self._create_objectclasses = ['top', 'nsslapdplugin']
        # We'll mark this protected, and people can just disable the plugins.
        self._protected = True

    def enable(self):
        self.set('nsslapd-pluginEnabled', 'on')

    def disable(self):
        self.set('nsslapd-pluginEnabled', 'off')

    def status(self):
        if self.get_attr_val('nsslapd-pluginEnabled') == 'on':
            return True
        return False

class AttributeUniquenessPlugin(Plugin):
    def __init__(self, instance, dn="cn=attribute uniqueness,cn=plugins,cn=config", batch=False):
        super(AttributeUniquenessPlugin, self).__init__(instance, dn, batch)

    ## These are some wrappers to the important attributes
    # This plugin will be "tricky" in that it can have "many" instance
    # of the plugin, rather than many configs.

    def add_unique_attribute(self, attr):
        self.add('uniqueness-attribute-name', attr)

    def remove_unique_attribute(self, attr):
        self.remove('uniqueness-attribute-name', attr)

    def add_unique_subtree(self, basedn):
        self.add('uniqueness-subtrees', basedn)

    def remove_unique_subtree(self, basedn):
        self.remove('uniqueness-subtrees', basedn)

    def enable_all_subtrees(self):
        self.set('uniqueness-across-all-subtrees', 'on')

    def disable_all_subtrees(self):
        self.set('uniqueness-across-all-subtrees', 'off')

class ManagedEntriesPlugin(Plugin):
    def __init__(self, instance, dn="cn=managed entries,cn=plugins,cn=config", batch=False):
        super(ManagedEntriesPlugin, self).__init__(instance, dn, batch)

    # This will likely need to be a bit like both the DSLdapObjects AND the object.
    # Because there are potentially many MEP configs.

class ReferentialIntegrityPlugin(Plugin):
    def __init__(self, instance, dn="cn=referential integrity postoperation,cn=plugins,cn=config", batch=False):
        super(ReferentialIntegrityPlugin, self).__init__(instance, dn, batch)

    # referint-update-delay: 0
    # referint-logfile: /opt/dirsrv/var/log/dirsrv/slapd-standalone_2/referint
    # referint-logchanges: 0
    # referint-membership-attr: member
    # referint-membership-attr: uniquemember
    # referint-membership-attr: owner
    # referint-membership-attr: seeAlso



class Plugins(DSLdapObjects):

    # This is a map of plugin to type, so when we
    # do a get / list / create etc, we can map to the correct
    # instance.
    def __init__(self, instance, batch=False):
        super(Plugins, self).__init__(instance=instance, batch=batch)
        self._objectclasses = ['top', 'nsslapdplugin']
        self._filterattrs = ['cn', 'nsslapd-pluginPath']
        self._childobject = Plugin
        self._basedn = 'cn=plugins,cn=config'
        # This is used to allow entry to instance to work
        self._list_attrlist = ['dn', 'nsslapd-pluginPath']
        # This may not work for attr unique which can have many instance ....
        # Should we be doing this from the .so name?
        self._pluginmap = {
            'libattr-unique-plugin' : AttributeUniquenessPlugin,
            'libmanagedentries-plugin' : ManagedEntriesPlugin,
            'libreferint-plugin' : ReferentialIntegrityPlugin,
        }

    def _entry_to_instance(self, dn=None, entry=None):
        # If dn in self._pluginmap
        if entry['nsslapd-pluginPath'] in self._pluginmap:
            return self._pluginmap[entry['nsslapd-pluginPath']](self._instance, dn=dn, batch=self._batch)
        else:
            return super(Plugins, self)._entry_to_instance(dn)


    # To maintain compat with pluginslegacy, here are some helpers.

    def enable(self, name=None, plugin_dn=None):
        if plugin_dn is not None:
            raise ValueError('You should swap to the new Plugin API!')
        if name is None:
            raise ldap.NO_SUCH_OBJECT('Must provide a selector for name')
        plugin = self.get(selector=name)
        plugin.enable()

    def disable(self, name=None, plugin_dn=None):
        if plugin_dn is not None:
            raise ValueError('You should swap to the new Plugin API!')
        if name is None:
            raise ldap.NO_SUCH_OBJECT('Must provide a selector for name')
        plugin = self.get(selector=name)
        plugin.disable()


class PluginsLegacy(object):

    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Plugins.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def list(self, name=None):
        '''
            Returns a search result of the plugins entries with all their
            attributes

            If 'name' is not specified, it returns all the plugins, else it
            returns the plugin matching ('cn=<name>, DN_PLUGIN')

            @param name - name of the plugin

            @return plugin entries

            @raise None

        '''

        if name:
            filt = "(objectclass=%s)" % PLUGINS_OBJECTCLASS_VALUE
            base = "cn=%s,%s" % (name, DN_PLUGIN)
            scope = ldap.SCOPE_BASE
        else:
            filt = "(objectclass=%s)" % PLUGINS_OBJECTCLASS_VALUE
            base = DN_PLUGIN
            scope = ldap.SCOPE_ONELEVEL

        ents = self.conn.search_s(base, scope, filt)
        return ents

    def enable(self, name=None, plugin_dn=None):
        '''
            Enable a plugin

            If 'plugin_dn' and 'name' are provided, plugin_dn is used and
            'name' is not considered

            @param name - name of the plugin
            @param plugin_dn - DN of the plugin

            @return None

            @raise ValueError - if 'name' or 'plugin_dn' lead to unknown plugin
                   InvalidArgumentError - if 'name' and 'plugin_dn' are missing
        '''

        dn = plugin_dn or "cn=%s,%s" % (name, DN_PLUGIN)
        filt = "(objectclass=%s)" % PLUGINS_OBJECTCLASS_VALUE

        if not dn:
            raise InvalidArgumentError("'name' and 'plugin_dn' are missing")

        ents = self.conn.search_s(dn, ldap.SCOPE_BASE, filt)
        if len(ents) != 1:
            raise ValueError("%s is unknown")

        self.conn.modify_s(dn, [(ldap.MOD_REPLACE,
                                 PLUGIN_PROPNAME_TO_ATTRNAME[PLUGIN_ENABLE],
                                 PLUGINS_ENABLE_ON_VALUE)])

    def disable(self, name=None, plugin_dn=None):
        '''
            Disable a plugin

            If 'plugin_dn' and 'name' are provided, plugin_dn is used and
            'name' is not considered

            @param name - name of the plugin
            @param plugin_dn - DN of the plugin

            @return None

            @raise ValueError - if 'name' or 'plugin_dn' lead to unknown plugin
                InvalidArgumentError - if 'name' and 'plugin_dn' are missing
        '''

        dn = plugin_dn or "cn=%s,%s" % (name, DN_PLUGIN)
        filt = "(objectclass=%s)" % PLUGINS_OBJECTCLASS_VALUE

        if not dn:
            raise InvalidArgumentError("'name' and 'plugin_dn' are missing")

        ents = self.conn.search_s(dn, ldap.SCOPE_BASE, filt)
        if len(ents) != 1:
            raise ValueError("%s is unknown")

        self.conn.modify_s(dn, [(ldap.MOD_REPLACE,
                                 PLUGIN_PROPNAME_TO_ATTRNAME[PLUGIN_ENABLE],
                                 PLUGINS_ENABLE_OFF_VALUE)])
