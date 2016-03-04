import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_USER = 'uid=test,%s' % DEFAULT_SUFFIX
# Well, it's better than "password" or "password1"
TEST_PASS = 'banana cream pie'

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket48363(topology):
    """
    Test the implementation of rfc3673, '+' for all operational attributes.

    Please see: https://tools.ietf.org/html/rfc3673

    """

    # Test the implementation of the supportFeatures

    # Section 2:
    # Servers supporting this feature SHOULD publish the Object Identifier
    # 1.3.6.1.4.1.4203.1.5.1 as a value of the 'supportedFeatures'
    # [RFC3674] attribute in the root DSE.

    results = topology.standalone.search_s('', ldap.SCOPE_BASE, 'objectClass=*', ['supportedFeatures'] )[0]
    if results.hasAttr('supportedfeatures') is False:
        assert False
    if results.hasValue('supportedfeatures', '1.3.6.1.4.1.4203.1.5.1') is False:
        assert False

    # Section 2:
    # The presence of the attribute description "+" (ASCII 43) in the list
    # of attributes in a Search Request [RFC2251] SHALL signify a request
    # for the return of all operational attributes.

    # Test the two backends, rootdse, and a real ldbm backend

    # Root DSE
    results = topology.standalone.search_s('', ldap.SCOPE_BASE, 'objectClass=*', ['+'] )[0]
    # There are a number of obvious ones in rootdse. These are:

    rootdse_op_attrs = [
        'supportedExtension',
        'supportedControl',
        'supportedFeatures',
        'supportedSASLMechanisms',
        'supportedLDAPVersion',
        'vendorName',
        'vendorVersion',
    ]

    for opattr in rootdse_op_attrs:
        if results.hasAttr(opattr) is False:
            assert False

    # LDBM backend
    # We are going to examine the root of the suffix, as it's a good easy target
    dc_op_attrs = [
        'nsuniqueid',
        'entrydn',
        'entryid',
        'aci',
    ]
    dc_user_attrs = [
        'objectClass',
        'dc',
    ]

    # We should show that the following work:

    # '+'
    results = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE, 'objectClass=*', ['+'] )[0]
    for opattr in dc_op_attrs:
        if results.hasAttr(opattr) is False:
            assert False
    for userattr in dc_user_attrs:
        if results.hasAttr(userattr) is False:
            assert True

    # '+' '*'
    results = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE, 'objectClass=*', ['+', '*'] )[0]
    for opattr in dc_op_attrs:
        if results.hasAttr(opattr) is False:
            assert False
    for userattr in dc_user_attrs:
        if results.hasAttr(userattr) is False:
            assert False

    # Section 2:
    # Client implementors should also note
    # that certain operational attributes may be returned only if requested
    # by name even when "+" is present.

    # We do not currently have any types that are excluded.
    # However, we should ensure that a search for "+ namedType" returns
    # both all operational and the namedType

    # '+' dc
    results = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE, 'objectClass=*', ['+', 'dc'] )[0]
    for opattr in dc_op_attrs:
        if results.hasAttr(opattr) is False:
            assert False
    if results.hasAttr('dc') is False:
        assert False
    if results.hasAttr('objectclass') is False:
        assert True

    # '*' nsUniqueId
    results = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE, 'objectClass=*', ['*', 'nsuniqueid'] )[0]
    for userattr in dc_user_attrs:
        if results.hasAttr(userattr) is False:
            assert False
    if results.hasAttr('nsuniqueid') is False:
        assert False
    if results.hasAttr('entrydn') is False:
        assert True

    # Section 2:
    # As with all search requests, client implementors should note that
    # results may not include all requested attributes due to access
    # controls or other restrictions.

    # Test that with a user with limit read aci, that these are enforced on
    # the + request.

    # Create the user
    uentry = Entry(TEST_USER)
    uentry.setValues('objectclass', 'top', 'extensibleobject')
    uentry.setValues('uid', 'test')
    uentry.setValues('userPassword', TEST_PASS)
    topology.standalone.add_s(uentry)

    # Give them a limited read aci: We may need to purge other acis
    anonaci = '(targetattr!="userPassword")(version 3.0; acl "Enable anonymous access"; allow (read, search, compare) userdn="ldap:///anyone";)'
    topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', anonaci)])

    # Now we need to create an aci that allows anon/all read to only a few attrs
    # Lets make one real, and one operational.

    anonaci = '(targetattr="objectclass || dc || nsuniqueid")(version 3.0; acl "Enable anonymous access"; allow (read, search, compare) userdn="ldap:///anyone";)'
    topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', anonaci)])

    # bind as them, and test.
    topology.standalone.simple_bind_s(TEST_USER, TEST_PASS)
    results = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE, 'objectClass=*', ['*', '+'] )[0]

    if results.hasAttr('dc') is False:
        assert False
    if results.hasAttr('nsuniqueid') is False:
        assert False
    if results.hasAttr('entrydn') is False:
        assert True

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
