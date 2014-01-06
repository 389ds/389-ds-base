'''
Created on Jan 6, 2014

@author: tbordaz
'''

import ldap
import os
from lib389 import DirSrv, Entry, InvalidArgumentError
from lib389._constants import *
from lib389.properties import *


class Changelog(object):
    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Changelog.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def list(self, suffix=None, changelogdn=None):
        if not changelogdn:
            raise InvalidArgumentError("changelog DN is missing")
        
        base  = changelogdn
        filtr = "(objectclass=extensibleobject)"
            
        # now do the effective search
        ents = self.conn.search_s(base, ldap.SCOPE_BASE, filtr)
        return ents
        
    def create(self, dbname=DEFAULT_CHANGELOG_DB):
        """Add and return the replication changelog entry.

            If dbname starts with "/" then it's considered a full path,
            otherwise it's relative to self.dbdir
        """
        dn = DN_CHANGELOG
        attribute, changelog_name = dn.split(",")[0].split("=", 1)
        dirpath = os.path.join(self.conn.dbdir, dbname)
        entry = Entry(dn)
        entry.update({
            'objectclass': ("top", "extensibleobject"),
            CHANGELOG_PROPNAME_TO_ATTRNAME[CHANGELOG_NAME]: changelog_name,
            CHANGELOG_PROPNAME_TO_ATTRNAME[CHANGELOG_DIR]:  dirpath
        })
        self.log.debug("adding changelog entry: %r" % entry)
        self.changelogdir = dirpath
        try:
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS:
            self.log.warn("entry %s already exists" % dn)
        return(dn)
            
    def delete(self, changelogdn=None):
        raise NotImplemented
    
    def setProperties(self, changelogdn=None, properties=None):
        if not changelogdn:
            raise InvalidArgumentError("changelog DN is missing")
        
        ents = self.changelog.list(changelogdn=changelogdn)
        if len(ents) != 1:
            raise ValueError("Changelog entry not found: %s" % changelogdn)
        
        # check that the given properties are valid
        for prop in properties:
            # skip the prefix to add/del value
            if not inProperties(prop, CHANGELOG_PROPNAME_TO_ATTRNAME):
                raise ValueError("unknown property: %s" % prop)
        
        # build the MODS
        mods = []
        for prop in properties:
            # take the operation type from the property name
            val = rawProperty(prop)
            if str(prop).startswith('+'):
                op = ldap.MOD_ADD
            elif str(prop).startswith('-'):
                op = ldap.MOD_DELETE
            else:
                op = ldap.MOD_REPLACE
            
            mods.append((op, REPLICA_PROPNAME_TO_ATTRNAME[val], properties[prop]))
            
        # that is fine now to apply the MOD
        self.conn.modify_s(ents[0].dn, mods)
        

    def getProperties(self, changelogdn=None, properties=None):
        raise NotImplemented