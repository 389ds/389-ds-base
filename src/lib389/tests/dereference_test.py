'''
Created on Aug 1, 2015

@author: William Brown
'''
from lib389._constants import *
from lib389 import DirSrv,Entry
import ldap

INSTANCE_PORT     = 54321
INSTANCE_SERVERID = 'dereferenceds'

class Test_dereference():
    def setUp(self):
        instance = DirSrv(verbose=False)
        instance.log.debug("Instance allocated")
        args = {SER_HOST:          LOCALHOST,
                SER_PORT:          INSTANCE_PORT,
                SER_SERVERID_PROP: INSTANCE_SERVERID
                }
        instance.allocate(args)
        if instance.exists():
            instance.delete()
        instance.create()
        instance.open()
        self.instance = instance

    def tearDown(self):
        if self.instance.exists():
            #self.instance.db2ldif(bename='userRoot', suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[], encrypt=False, \
            #repl_data=False, outputfile='%s/ldif/%s.ldif' % (self.instance.dbdir,INSTANCE_SERVERID ))
            #self.instance.clearBackupFS()
            #self.instance.backupFS()
            self.instance.delete()

    def add_user(self):
        # Create a user entry
        for i in range(0,2):
            uentry = Entry('uid=test%s,%s' % (i, DEFAULT_SUFFIX))
            uentry.setValues('objectclass', 'top', 'extensibleobject')
            uentry.setValues('uid', 'test')
            self.instance.add_s(uentry)

    def add_group(self):
        gentry = Entry('cn=testgroup,%s' % DEFAULT_SUFFIX)
        gentry.setValues('objectclass', 'top', 'extensibleobject')
        gentry.setValues('cn', 'testgroup')
        for i in range(0,2):
            gentry.setValues('uniqueMember', 'uid=test%s,%s' % (i,DEFAULT_SUFFIX))
        self.instance.add_s(gentry)

    def test_dereference(self):
        try:
            result, control_response = self.instance.dereference('uniqueMember:dn,uid;uniqueMember:dn,uid', filterstr='(cn=testgroup)')
            assert False
        except ldap.UNAVAILABLE_CRITICAL_EXTENSION:
            # This is a good thing! It means our deref Control Value is correctly formatted.
            pass
        result, control_response = self.instance.dereference('uniqueMember:cn,uid,objectclass', filterstr='(cn=testgroup)')

        assert result[0][2][0].entry == [{'derefVal': 'uid=test1,dc=example,dc=com', 'derefAttr': 'uniqueMember', 'attrVals': [{'vals': ['top', 'extensibleobject'], 'type': 'objectclass'}, {'vals': ['test', 'test1'], 'type': 'uid'}]}]

if __name__ == "__main__":
    test = Test_dereference()
    test.setUp()
    test.add_user()
    test.add_group()
    test.test_dereference()
    test.tearDown()

