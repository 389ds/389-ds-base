'''
Created on Aug 1, 2015

@author: William Brown
'''
from lib389._constants import *
from lib389 import DirSrv,Entry

INSTANCE_PORT     = 54321
INSTANCE_SERVERID = 'effectiverightsds'
INSTANCE_PREFIX   = None

class Test_effective_rights():
    def setUp(self):
        instance = DirSrv(verbose=False)
        instance.log.debug("Instance allocated")
        args = {SER_HOST:          LOCALHOST,
                SER_PORT:          INSTANCE_PORT,
                SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
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
            self.instance.delete()

    def add_user(self):
        # Create a user entry
        uentry = Entry('uid=test,%s' % DEFAULT_SUFFIX)
        uentry.setValues('objectclass', 'top', 'extensibleobject')
        uentry.setValues('uid', 'test')
        self.instance.add_s(uentry)
        #self.instance.log.debug("Created user entry as:" ,uentry.dn)

    def add_group(self):
        # Create a group for the user to have some rights to
        gentry = Entry('cn=testgroup,%s' % DEFAULT_SUFFIX)
        gentry.setValues('objectclass', 'top', 'extensibleobject')
        gentry.setValues('cn', 'testgroup')
        self.instance.add_s(gentry)

    def test_effective_rights(self):
        # Run an effective rights search
        result = self.instance.get_effective_rights('uid=test,%s' % DEFAULT_SUFFIX, filterstr='(cn=testgroup)', attrlist=['cn'])

        rights = result[0]
        assert rights.getValue('attributeLevelRights') == 'cn:rsc'
        assert rights.getValue('entryLevelRights') == 'v'

if __name__ == "__main__":
    test = Test_effective_rights()
    test.setUp()
    test.add_user()
    test.add_group()
    test.test_effective_rights()
    test.tearDown()

