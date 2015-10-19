'''
Created on Feb 11, 2014

@author: tbordaz
'''

import ldap
from ldap.controls.readentry import PostReadControl
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

    def create(self, suffix=None, be_name=None, attr=None, args=None):
        if not suffix and not be_name:
            raise ValueError("suffix/backend name is mandatory parameter")

        if not attr:
            raise ValueError("attr is mandatory parameter")

        indexTypes = args.get(INDEX_TYPE, None)
        matchingRules = args.get(INDEX_MATCHING_RULE, None)

        return self.addIndex(suffix, be_name, attr, indexTypes=indexTypes, matchingRules=matchingRules)

    def addIndex(self, suffix, be_name, attr, indexTypes, matchingRules, postReadCtrl=None):
        """Specify the suffix (should contain 1 local database backend),
            the name of the attribute to index, and the types of indexes
            to create e.g. "pres", "eq", "sub"
        """
        msg_id = None
        if be_name:
            dn = ('cn=%s,cn=index,cn=%s,cn=ldbm database,cn=plugins,cn=config' %
                 (attr, be_name))
        else:
            entries_backend = self.conn.backend.list(suffix=suffix)
            # assume 1 local backend
            dn = "cn=%s,cn=index,%s" % (attr, entries_backend[0].dn)

        if postReadCtrl:
            add_record = [
                          ('nsSystemIndex', ['false']),
                          ('cn', [attr]),
                          ('objectclass', ['top', 'nsindex']),
                          ('nsIndexType', indexTypes)
                         ]
            if matchingRules:
                add_record.append(('nsMatchingRule', matchingRules))

        else:
            entry = Entry(dn)
            entry.setValues('objectclass', 'top', 'nsIndex')
            entry.setValues('cn', attr)
            entry.setValues('nsSystemIndex', "false")
            entry.setValues('nsIndexType', indexTypes)
            if matchingRules:
                entry.setValues('nsMatchingRule', matchingRules)

        try:
            if postReadCtrl:
                pr = PostReadControl(criticality=True, attrList=['*'])
                msg_id = self.conn.add_ext(dn, add_record, serverctrls=[pr])
            else:
                self.conn.add_s(entry)
        except ldap.LDAPError as e:
            raise e

        return msg_id

    def modIndex(self, suffix, attr, mod):
        """just a wrapper around a plain old ldap modify, but will
        find the correct index entry based on the suffix and attribute"""
        entries_backend = self.conn.backend.list(suffix=suffix)
        # assume 1 local backend
        dn = "cn=%s,cn=index,%s" % (attr, entries_backend[0].dn)
        self.conn.modify_s(dn, mod)
