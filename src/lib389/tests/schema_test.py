'''
Created on Aug 3, 2015

@author: William Brown
'''
from lib389._constants import *
from lib389 import DirSrv,Entry

INSTANCE_PORT     = 54321
INSTANCE_SERVERID = 'schemainspectds'
INSTANCE_PREFIX   = None

class Test_schema():
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

    def test_schema(self):
        must_expect = ['uidObject', 'account', 'posixAccount', 'shadowAccount']
        may_expect = ['cosDefinition', 'inetOrgPerson', 'inetUser', 'mailRecipient']
        attrtype, must, may = self.instance.schema.query_attributetype('uid')
        assert attrtype.names == ('uid', 'userid')
        for oc in must:
            assert oc.names[0] in must_expect
        for oc in may:
            assert oc.names[0] in may_expect
        assert self.instance.schema.query_objectclass('account').names == ('account', )

if __name__ == "__main__":
    test = Test_schema()
    test.setUp()
    test.test_schema()
    test.tearDown()


