'''
Created on Feb 11, 2014

@author: tbordaz
'''

import ldap
from lib389._constants import *
from lib389.properties import *
from lib389 import Entry


class Index(object):
    
    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log
    
    def delete_all(self, benamebase):
        dn = "cn=index,cn=" + benamebase + "," + DN_LDBM
        
        # delete each defined index
        ents = self.conn.search_s(dn, ldap.SCOPE_ONELEVEL)
        for ent in ents:
            self.log.debug("Delete index entry %s" % (ent.dn))
            self.conn.delete_s(ent.dn)
            
        # Then delete the top index entry
        self.log.debug("Delete head index entry %s" % (dn))
        self.conn.delete_s(dn)
    
    def create(self, suffix=None, attr=None, args=None):
        if not suffix:
            raise ValueError("suffix is mandatory parameter")
        
        if not attr:
            raise ValueError("attr is mandatory parameter")
        
        indexTypes = args.get(INDEX_TYPE, None)
        matchingRules = args.get(INDEX_MATCHING_RULE, None)
        
        self.addIndex(suffix, attr, indexTypes=indexTypes, matchingRules=matchingRules)
        
        

    def addIndex(self, suffix, attr, indexTypes, matchingRules):
        """Specify the suffix (should contain 1 local database backend),
            the name of the attribute to index, and the types of indexes
            to create e.g. "pres", "eq", "sub"
        """
        entries_backend = self.conn.backend.list(suffix=suffix)
        # assume 1 local backend
        dn = "cn=%s,cn=index,%s" % (attr, entries_backend[0].dn)
        entry = Entry(dn)
        entry.setValues('objectclass', 'top', 'nsIndex')
        entry.setValues('cn', attr)
        entry.setValues('nsSystemIndex', "false")
        entry.setValues('nsIndexType', indexTypes)
        if matchingRules:
            entry.setValues('nsMatchingRule', matchingRules)
        try:
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS:
            print "Index for attr %s for backend %s already exists" % (
                attr, dn)

    def modIndex(self, suffix, attr, mod):
        """just a wrapper around a plain old ldap modify, but will
        find the correct index entry based on the suffix and attribute"""
        entries_backend = self.conn.backend.list(suffix=suffix)
        # assume 1 local backend
        dn = "cn=%s,cn=index,%s" % (attr, entries_backend[0].dn)
        self.conn.modify_s(dn, mod)
