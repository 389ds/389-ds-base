# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from ldap.controls.simple import ProxyAuthzControl

log = logging.getLogger(__name__)

PROXY_USER_DN    = 'cn=proxy,ou=people,%s' % SUFFIX
TEST_USER_DN    = 'cn=test,ou=people,%s' % SUFFIX
USER_PW    = 'password'


# subtrees used in test
SUBTREE_GREEN = "ou=green,%s" % SUFFIX
SUBTREE_RED   = "ou=red,%s" % SUFFIX
SUBTREES = (SUBTREE_GREEN, SUBTREE_RED)


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def test_ticket48366_init(topology):
    """
        It creates identical entries in 3 subtrees
        It creates aci which allow access to a set of attrs
            in two of these subtrees for bound users
        It creates a user to be used for test

    """


    topology.standalone.log.info("Add subtree: %s" % SUBTREE_GREEN)
    topology.standalone.add_s(Entry((SUBTREE_GREEN, {
                                            'objectclass': "top organizationalunit".split(),
                                            'ou': "green_one"})))
    topology.standalone.log.info("Add subtree: %s" % SUBTREE_RED)
    topology.standalone.add_s(Entry((SUBTREE_RED, {
                                            'objectclass': "top organizationalunit".split(),
                                            'ou': "red"})))

    # add proxy user and test user
    topology.standalone.log.info("Add %s" % TEST_USER_DN)
    topology.standalone.add_s(Entry((TEST_USER_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           'test',
                                            'cn':           'test',
                                            'userpassword': USER_PW})))
    topology.standalone.log.info("Add %s" % PROXY_USER_DN)
    topology.standalone.add_s(Entry((PROXY_USER_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           'proxy',
                                            'cn':           'proxy',
                                            'userpassword': USER_PW})))

    # enable acl error logging
    # mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '128')]
    # topology.standalone.modify_s(DN_CONFIG, mod)

    # get rid of default ACIs
    mod = [(ldap.MOD_DELETE, 'aci', None)]
    topology.standalone.modify_s(SUFFIX, mod)

    # Ok Now add the proper ACIs
    ACI_TARGET       = "(target = \"ldap:///%s\")" % SUBTREE_GREEN
    ACI_TARGETATTR   = "(targetattr = \"objectclass || cn || sn || uid || givenname \")"
    ACI_ALLOW        = "(version 3.0; acl \"Allow search-read to green subtree\"; allow (read, search, compare)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % TEST_USER_DN
    ACI_BODY         = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.standalone.modify_s(SUFFIX, mod)

    ACI_ALLOW        = "(version 3.0; acl \"Allow use pf proxy auth to green subtree\"; allow (proxy)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % PROXY_USER_DN
    ACI_BODY         = ACI_TARGET + ACI_TARGETATTR + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.standalone.modify_s(SUFFIX, mod)

    log.info("Adding %d test entries...")
    for id in range(2):
        name = "%s%d" % ('test', id)
        mail = "%s@example.com" % name
        for subtree in SUBTREES:
            topology.standalone.add_s(Entry(("cn=%s,%s" % (name, subtree), {
                                         'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                         'sn': name,
                                         'cn': name,
                                         'uid': name,
                                         'givenname': 'test',
                                         'mail': mail,
                                         'description': 'description',
                                         'employeenumber': "%d" % id,
                                         'telephonenumber': "%d%d%d" % (id,id,id),
                                         'mobile': "%d%d%d" % (id,id,id),
                                         'l': 'MV',
                                         'title': 'Engineer'})))



def test_ticket48366_search_user(topology):

    proxy_ctrl = ProxyAuthzControl(criticality=True, authzId="dn: "+TEST_USER_DN)
    # searching as test user should return one entry from the green subtree
    topology.standalone.simple_bind_s(TEST_USER_DN, PASSWORD)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid=test1')
    assert (len(ents) == 1)

    # searching as proxy user should return no entry
    topology.standalone.simple_bind_s(PROXY_USER_DN, PASSWORD)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid=test1')
    assert (len(ents) == 0)

    # serching as proxy user, authorizing as test user should return 1 entry
    ents = topology.standalone.search_ext_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid=test1', serverctrls=[proxy_ctrl])
    assert (len(ents) == 1)

def test_ticket48366_search_dm(topology):

    # searching as directory manager should return one entries from both subtrees
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid=test1')
    assert (len(ents) == 2)

    # searching as directory manager proxying test user should return one entry
    proxy_ctrl = ProxyAuthzControl(criticality=True, authzId="dn: "+TEST_USER_DN)
    ents = topology.standalone.search_ext_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid=test1', serverctrls=[proxy_ctrl])
    assert (len(ents) == 1)

    # searching as directory manager proxying proxy user should return no entry
    proxy_ctrl = ProxyAuthzControl(criticality=True, authzId="dn: "+PROXY_USER_DN)
    ents = topology.standalone.search_ext_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid=test1', serverctrls=[proxy_ctrl])
    assert (len(ents) == 0)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
