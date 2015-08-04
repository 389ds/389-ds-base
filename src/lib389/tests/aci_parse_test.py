'''
Created on Aug 3, 2015

@author: William Brown
'''
from lib389._constants import *
from lib389._aci import Aci
from lib389 import DirSrv,Entry
import ldap

INSTANCE_PORT     = 54321
INSTANCE_SERVERID = 'aciparseds'
#INSTANCE_PREFIX   = None

class Test_schema():
    def setUp(self):
        instance = DirSrv(verbose=False)
        instance.log.debug("Instance allocated")
        args = {SER_HOST:          LOCALHOST,
                SER_PORT:          INSTANCE_PORT,
                #SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
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

    def create_complex_acis(self):
        gentry = Entry('cn=testgroup,%s' % DEFAULT_SUFFIX)
        gentry.setValues('objectclass', 'top', 'extensibleobject')
        gentry.setValues('cn', 'testgroup')
        gentry.setValues('aci',
            """ (targetfilter="(ou=groups)")(targetattr = "uniqueMember || member")(version 3.0; acl "Allow test aci"; allow (read, search, write) (userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" and userdn="ldap:///dc=example,dc=com??sub?(manager=uid=wbrown,ou=managers,dc=example,dc=com) || ldap:///dc=example,dc=com??sub?(manager=uid=tbrown,ou=managers,dc=example,dc=com)" );) """,
            )
        self.instance.add_s(gentry)

    def test_aci(self):
        acis = self.instance.aci.list('cn=testgroup,%s' % DEFAULT_SUFFIX)
        assert len(acis) == 1
        aci = acis[0]
        assert aci.acidata == {
            'allow': [{'values': ['read', 'search', 'write']}], 
            'target': [], 'targetattr': [{'values': ['uniqueMember', 'member'], 'equal': True}],
            'targattrfilters': [],
            'deny': [],
            'acl': [{'values': ['Allow test aci']}],
            'deny_raw_bindrules': [],
            'targetattrfilters': [],
            'allow_raw_bindrules': [{'values': ['userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" and userdn="ldap:///dc=example,dc=com??sub?(manager=uid=wbrown,ou=managers,dc=example,dc=com) || ldap:///dc=example,dc=com??sub?(manager=uid=tbrown,ou=managers,dc=example,dc=com)" ']}],
            'targetfilter': [{'values': ['(ou=groups)'], 'equal': True}],
            'targetscope': [],
            'version 3.0;': [],
            'rawaci': ' (targetfilter="(ou=groups)")(targetattr = "uniqueMember || member")(version 3.0; acl "Allow test aci"; allow (read, search, write) (userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" and userdn="ldap:///dc=example,dc=com??sub?(manager=uid=wbrown,ou=managers,dc=example,dc=com) || ldap:///dc=example,dc=com??sub?(manager=uid=tbrown,ou=managers,dc=example,dc=com)" );) '
        }
        assert aci.getRawAci() == """(targetfilter ="(ou=groups)")(targetattr ="uniqueMember || member")(version 3.0; acl "Allow test aci";allow (read, search, write)(userdn="ldap:///dc=example,dc=com??sub?(ou=engineering)" and userdn="ldap:///dc=example,dc=com??sub?(manager=uid=wbrown,ou=managers,dc=example,dc=com) || ldap:///dc=example,dc=com??sub?(manager=uid=tbrown,ou=managers,dc=example,dc=com)" );)"""

if __name__ == "__main__":
    test = Test_schema()
    test.setUp()
    test.create_complex_acis()
    test.test_aci()
    test.tearDown()

