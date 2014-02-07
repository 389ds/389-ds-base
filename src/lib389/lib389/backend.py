'''
Created on Dec 13, 2013

@author: tbordaz
'''

import ldap
from lib389 import DirSrv, Entry, NoSuchEntryError, InvalidArgumentError
from lib389._constants import *
from lib389.utils import (
    normalizeDN, 
    suffixfilt
    )
from compiler.ast import Not
from lib389.properties import *
from __init__ import UnwillingToPerformError

class Backend(object):
    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Backend.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def list(self, suffix=None, backend_dn=None, bename=None):
        """
            Returns a search result of the backend(s) entries with all their attributes

            If 'suffix'/'backend_dn'/'benamebase' are specified. It uses 'backend_dn' first, then 'suffix', then 'benamebase'.

            If neither 'suffix', 'backend_dn' and 'benamebase' are specified, it returns all the backend entries 
            
            Get backends by name or suffix
            
            @param suffix - suffix of the backend
            @param backend_dn - DN of the backend entry
            @param bename - 'commonname'/'cn' of the backend (e.g. 'userRoot')
            
            @return backend entries
            
            @raise None
        """
        
        filt = "(objectclass=%s)" % BACKEND_OBJECTCLASS_VALUE
        if backend_dn:
            self.log.info("List backend %s" % backend_dn)
            base  = backend_dn
            scope = ldap.SCOPE_BASE
        elif suffix:
            self.log.info("List backend with suffix=%s" % suffix)
            base  = DN_PLUGIN
            scope = ldap.SCOPE_SUBTREE
            filt = "(&%s(|(%s=%s)(%s=%s)))" % (filt,
                                               BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX],suffix,
                                               BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX], normalizeDN(suffix))
        elif bename:
            self.log.info("List backend 'cn=%s'" % bename)
            base  = "%s=%s,%s" % (BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME], bename, DN_LDBM)
            scope = ldap.SCOPE_BASE
        else:
            self.log.info("List all the backends")
            base = DN_PLUGIN
            scope = ldap.SCOPE_SUBTREE
            
        try:
            ents = self.conn.search_s(base, scope, filt)
        except ldap.NO_SUCH_OBJECT:
            return None
        
        return ents
                                  
        
    def _readonly(self, bename=None, readonly='on', suffix=None):
        """Put a database in readonly mode
            @param  bename  -   the backend name (eg. addressbook1)
            @param  readonly-   'on' or 'off'

            NOTE: I can ldif2db to a read-only database. After the
                  import, the database will still be in readonly.
                  
            NOTE: When a db is read-only, it seems you need to restart 
                  the directory server before creating further 
                  agreements or initialize consumers
        """
        if bename and suffix:
            raise ValueError("Specify either bename or suffix")

        if suffix:
            raise NotImplementedError()

        self.conn.modify_s(','.join(('cn=' + bename, DN_LDBM)), [
            (ldap.MOD_REPLACE, BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_READONLY], readonly)
        ])
        
        
    def delete(self, suffix=None, backend_dn=None, bename=None):
        """
        Deletes the backend entry with the following steps:

        Delete the indexes entries under this backend
        Delete the encrypted attributes entries under this backend
        Delete the encrypted attributes keys entries under this backend 

        If a mapping tree entry uses this backend (nsslapd-backend), it raise UnwillingToPerformError
        
        If 'suffix'/'backend_dn'/'benamebase' are specified. 
        It uses 'backend_dn' first, then 'suffix', then 'benamebase'.
        
        If neither 'suffix', 'backend_dn' and 'benamebase' are specified, it raise InvalidArgumentError 
        @param suffix - suffix of the backend
        @param backend_dn - DN of the backend entry
        @param bename - 'commonname'/'cn' of the backend (e.g. 'userRoot')
        
        @return None
        
        @raise InvalidArgumentError - if missing arguments or invalid
                UnwillingToPerformError - if several backends match the argument
                                     provided suffix does not match backend suffix
                                     It exists a mapping tree that use that backend
        
        
        """
        
        # First check the backend exists and retrieved its suffix
        be_ents = self.conn.backend.list(suffix=suffix, backend_dn=backend_dn, bename=bename)
        if len(be_ents) == 0:
            raise InvalidArgumentError("Unable to retrieve the backend (%r, %r, %r)" % (suffix, backend_dn, bename))
        elif len(be_ents) > 1:
            for ent in be_ents:
                self.log.fatal("Multiple backend match the definition: %s" % ent.dn)
            if (not suffix) and (not backend_dn) and (not bename):
                raise InvalidArgumentError("suffix and backend DN and backend name are missing")
            raise UnwillingToPerformError("Not able to identify the backend to delete")
        else:
            be_ent = be_ents[0]
            be_suffix = be_ent.getValue(BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX])
            
        # Verify the provided suffix is the one stored in the found backend
        if suffix:
            if normalizeDN(suffix) != normalizeDN(be_suffix):
                raise UnwillingToPerformError("provided suffix (%s) differs from backend suffix (%s)" % (suffix, be_suffix))
        
        # now check there is no mapping tree for that suffix
        mt_ents = self.conn.mappingtree.list(suffix=be_suffix)
        if len(mt_ents) > 0:
            raise UnwillingToPerformError("It still exists a mapping tree (%s) for that backend (%s)" % (mt_ents[0].dn, be_ent.dn))
            
        # Now delete the indexes
        found_bename = be_ent.getValue(BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME])
        if not bename:
            bename = found_bename
        elif bename != found_bename:
            raise UnwillingToPerformError("Backend name specified (%s) differs from the retrieved one (%s)" % (bename, found_bename))
        
        self.conn.index.delete_all(bename)
        
        # finally delete the backend children and the backend itself
        ents = self.conn.search_s(be_ent.dn, ldap.SCOPE_ONELEVEL)
        for ent in ents:
            self.log.debug("Delete entry children %s" % (ent.dn))
            self.conn.delete_s(ent.dn)
            
        self.log.debug("Delete backend entry %s" % (be_ent.dn))
        self.conn.delete_s(be_ent.dn)
            
        return

    def create(self, suffix=None, properties=None):
        """
            Creates backend entry and returns its dn. 
            
            If the properties 'chain-bind-pwd' and 'chain-bind-dn' and 'chain-urls' are specified 
            the backend is a chained backend. 
            A chaining backend is created under 'cn=chaining database,cn=plugins,cn=config'. 
            
            A local backend is created under 'cn=ldbm database,cn=plugins,cn=config' 
            
            @param suffix - suffix stored in the backend
            @param properties - dictionary with properties values
            supported properties are
                BACKEND_NAME          = 'name'
                BACKEND_READONLY      = 'read-only'
                BACKEND_REQ_INDEX     = 'require-index'
                BACKEND_CACHE_ENTRIES = 'entry-cache-number'
                BACKEND_CACHE_SIZE    = 'entry-cache-size'
                BACKEND_DNCACHE_SIZE  = 'dn-cache-size'
                BACKEND_DIRECTORY     = 'directory'
                BACKEND_DB_DEADLOCK   = 'db-deadlock'
                BACKEND_CHAIN_BIND_DN = 'chain-bind-dn'
                BACKEND_CHAIN_BIND_PW = 'chain-bind-pw'
                BACKEND_CHAIN_URLS    = 'chain-urls'
                
            @return backend DN of the created backend
            
            @raise ValueError - If missing suffix 
                    InvalidArgumentError - If it already exists a backend for that suffix or a backend with the same DN

        """
        def _getBackendName(parent):
            '''
                Use to build a backend name that is not already used
            '''
            index = 1
            while True:
                bename = "local%ddb" % index
                base   = "%s=%s,%s" % (BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME], bename, parent)
                scope  = ldap.SCOPE_BASE
                filt   = "(objectclass=%s)" % BACKEND_OBJECTCLASS_VALUE
                self.log.debug("_getBackendName: baser=%s : fileter=%s" % (base, filt))
                try:
                    ents = self.conn.getEntry(base, ldap.SCOPE_BASE, filt)
                except (NoSuchEntryError, ldap.NO_SUCH_OBJECT) as e:
                    self.log.info("backend name will be %s" % bename)
                    return bename
                index += 1
                
        # suffix is mandatory
        if not suffix:
            raise ValueError("suffix is mandatory")
        else:
            nsuffix = normalizeDN(suffix)
        
        # Check it does not already exist a backend for that suffix
        ents = self.conn.backend.list(suffix=suffix)
        if len(ents) != 0:
            raise InvalidArgumentError("It already exists backend(s) for %s: %s" % (suffix, ents[0].dn))
        
        # Check if we are creating a local/chained backend
        chained_suffix = properties and (BACKEND_CHAIN_BIND_DN in properties) and (BACKEND_CHAIN_BIND_PW in properties) and (BACKEND_CHAIN_URLS in properties)
        if chained_suffix:
            self.log.info("Creating a chaining backend")
            dnbase = DN_CHAIN
        else:
            self.log.info("Creating a local backend")
            dnbase = DN_LDBM
        
        # Get the future backend name
        if properties and BACKEND_NAME in properties:
            cn = properties[BACKEND_NAME]
        else:
            cn = _getBackendName(dnbase)
        
        # Check the future backend name does not already exists
        # we can imagine having no backends for 'suffix' but having a backend 
        # with the same name
        dn = "%s=%s,%s" % (BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME], cn, dnbase)
        ents = self.conn.backend.list(backend_dn=dn)
        if ents:
            raise InvalidArgumentError("It already exists a backend with that DN: %s" % ents[0].dn)
        
        # All checks are done, Time to create the backend
        try:
            entry = Entry(dn)
            entry.update({
                'objectclass': ['top', 'extensibleObject', BACKEND_OBJECTCLASS_VALUE],
                BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_NAME]: cn,
                BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX]: nsuffix
            })
            
            if chained_suffix:
                entry.update({
                             BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_CHAIN_URLS]:    properties[BACKEND_CHAIN_URLS],
                             BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_CHAIN_BIND_DN]: properties[BACKEND_CHAIN_BIND_DN],
                             BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_CHAIN_BIND_PW]: properties[BACKEND_CHAIN_BIND_PW]
                             })

            self.log.debug("adding entry: %r" % entry)
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS, e:
            self.log.error("Entry already exists: %r" % dn)
            raise ldap.ALREADY_EXISTS("%s : %r" % (e, dn))
        except ldap.LDAPError, e:
            self.log.error("Could not add backend entry: %r" % dn)


        backend_entry = self.conn._test_entry(dn, ldap.SCOPE_BASE)
        
        return backend_entry
    
    def getProperties(self, suffix=None, backend_dn=None, bename=None, properties=None):
        raise NotImplemented
    
    def setProperties(self,suffix=None, backend_dn=None, bename=None, properties=None):
        raise NotImplemented
    
    def toSuffix(self, entry=None, name=None):
        '''
            Return, for a given backend entry, the suffix values.
            Suffix values are identical from a LDAP point of views. Suffix values may
            be surrounded by ", or containing '\' escape characters.
            
            @param entry - LDAP entry of the backend
            @param name  - backend DN
            
            @result list of values of suffix attribute (aka 'cn')
            
            @raise ldap.NO_SUCH_OBJECT - in name is invalid DN
                   ValueError - entry does not contains the suffix attribute
                   InvalidArgumentError - if both entry/name are missing
        '''
        attr_suffix = BACKEND_PROPNAME_TO_ATTRNAME[BACKEND_SUFFIX]
        if entry:
            if not entry.hasValue(attr_suffix):
                raise ValueError("Entry has no %s attribute %r" % (attr_suffix, entry))
            return entry.getValues(attr_suffix)
        elif name:
            filt = "(objectclass=%s)" % BACKEND_OBJECTCLASS_VALUE
            
            try:
                attrs = [attr_suffix]
                ent = self.conn.getEntry(name, ldap.SCOPE_BASE, filt, attrs)
                self.log.debug("toSuffix: %s found by its DN" % ent.dn)
            except NoSuchEntryError:
                raise ldap.NO_SUCH_OBJECT("Backend DN not found: %s" % name)
            
            if not ent.hasValue(attr_suffix):
                raise ValueError("Entry has no %s attribute %r" % (attr_suffix, ent))
            return ent.getValues(attr_suffix)
        else:
            raise InvalidArgumentError("entry or name are mandatory")
        
    def requireIndex(self, suffix):
        '''
        Should be moved in setProperties
        '''
        entries_backend = self.backend.list(suffix=suffix)
        # assume 1 local backend
        dn = entries_backend[0].dn
        replace = [(ldap.MOD_REPLACE, 'nsslapd-require-index', 'on')]
        self.modify_s(dn, replace)


