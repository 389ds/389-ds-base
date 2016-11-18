# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

USER_NUM = 20
TEST_USER = 'test_user'


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

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

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_ticket48265_test(topology):
    """
    Complex filter issues
    Ticket 47521 type complex filter:
      (&(|(uid=tuser*)(cn=Test user*))(&(givenname=test*3))(mail=tuser@example.com)(&(description=*)))
    Ticket 48264 type complex filter:
      (&(&(|(l=EU)(l=AP)(l=NA))(|(c=SE)(c=DE)))(|(uid=*test*)(cn=*test*))(l=eu))
    """

    log.info("Adding %d test entries..." % USER_NUM)
    for id in range(USER_NUM):
        name = "%s%d" % (TEST_USER, id)
        mail = "%s@example.com" % name
        secretary = "cn=%s,ou=secretary,%s" % (name, SUFFIX)
        topology.standalone.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
                                         'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                         'sn': name,
                                         'cn': name,
                                         'uid': name,
                                         'givenname': 'test',
                                         'mail': mail,
                                         'description': 'description',
                                         'secretary': secretary,
                                         'l': 'MV',
                                         'title': 'Engineer'})))

    log.info("Search with Ticket 47521 type complex filter")
    for id in range(USER_NUM):
        name = "%s%d" % (TEST_USER, id)
        mail = "%s@example.com" % name
        filter47521 = '(&(|(uid=%s*)(cn=%s*))(&(givenname=test))(mail=%s)(&(description=*)))' % (TEST_USER, TEST_USER, mail)
        entry = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, filter47521)
        assert len(entry) == 1

    log.info("Search with Ticket 48265 type complex filter")
    for id in range(USER_NUM):
        name = "%s%d" % (TEST_USER, id)
        mail = "%s@example.com" % name
        filter48265 = '(&(&(|(l=AA)(l=BB)(l=MV))(|(title=admin)(title=engineer)))(|(uid=%s)(mail=%s))(description=description))' % (name, mail)
        entry = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, filter48265)
        assert len(entry) == 1

    log.info('Test 48265 complete\n')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
