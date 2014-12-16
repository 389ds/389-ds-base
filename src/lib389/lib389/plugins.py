'''
Created on Jan 14, 2014

@author: tbordaz
'''

import ldap
from lib389 import DirSrv, InvalidArgumentError
from lib389.properties import *
from lib389._constants import *


class Plugins(object):

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
            Returns a search result of the plugins entries with all their attributes

            If 'name' is not specified, it returns all the plugins, else it returns the plugin
            matching ('cn=<name>, DN_PLUGIN')

            @param name - name of the plugin

            @return plugin entries

            @raise None

        '''
        
        if name:
            filt  = "(objectclass=%s)" % PLUGINS_OBJECTCLASS_VALUE
            base  = "cn=%s,%s" % (name, DN_PLUGIN)
            scope = ldap.SCOPE_BASE
        else:
            filt  = "(objectclass=%s)" % PLUGINS_OBJECTCLASS_VALUE
            base  = DN_PLUGIN
            scope = ldap.SCOPE_ONELEVEL
        
        ents = self.conn.search_s(base, scope, filt)
        return ents
    
    def enable(self, name=None, plugin_dn=None):
        '''
            Enable a plugin
            
            If 'plugin_dn' and 'name' are provided, plugin_dn is used and 'name is not considered
            
            @param name - name of the plugin
            @param plugin_dn - DN of the plugin
            
            @return None
            
            @raise ValueError - if 'name' or 'plugin_dn' lead to unknown plugin
                    InvalidArgumentError - if 'name' and 'plugin_dn' are missing
        '''
        
        dn = plugin_dn or "cn=%s,%s" % (name, DN_PLUGIN) 
        filt  = "(objectclass=%s)" % PLUGINS_OBJECTCLASS_VALUE
        
        if not dn:
            raise InvalidArgumentError("plugin 'name' and 'plugin_dn' are missing")
        
        ents = self.conn.search_s(dn, ldap.SCOPE_BASE, filt)
        if len(ents) != 1:
            raise ValueError("%s is unknown")
        
        self.conn.modify_s(dn, [(ldap.MOD_REPLACE, PLUGIN_PROPNAME_TO_ATTRNAME[PLUGIN_ENABLE], PLUGINS_ENABLE_ON_VALUE)])
        
    def disable(self, name=None, plugin_dn=None):
        '''
            Disable a plugin
            
            If 'plugin_dn' and 'name' are provided, plugin_dn is used and 'name is not considered
            
            @param name - name of the plugin
            @param plugin_dn - DN of the plugin
            
            @return None
            
            @raise ValueError - if 'name' or 'plugin_dn' lead to unknown plugin
                    InvalidArgumentError - if 'name' and 'plugin_dn' are missing
        '''
        
        dn = plugin_dn or "cn=%s,%s" % (name, DN_PLUGIN) 
        filt  = "(objectclass=%s)" % PLUGINS_OBJECTCLASS_VALUE
        
        if not dn:
            raise InvalidArgumentError("plugin 'name' and 'plugin_dn' are missing")
        
        ents = self.conn.search_s(dn, ldap.SCOPE_BASE, filt)
        if len(ents) != 1:
            raise ValueError("%s is unknown")
        
        self.conn.modify_s(dn, [(ldap.MOD_REPLACE, PLUGIN_PROPNAME_TO_ATTRNAME[PLUGIN_ENABLE], PLUGINS_ENABLE_OFF_VALUE)])

