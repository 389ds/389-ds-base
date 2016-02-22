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

import string
import random

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    # Creating standalone instance ...
    standalone = DirSrv(verbose=True)
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
        # This is useful for analysing the test env.
        #standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[], encrypt=False, \
        #    repl_data=True, outputfile='%s/ldif/%s.ldif' % (standalone.dbdir,SERVERID_STANDALONE ))
        #standalone.clearBackupFS()
        #standalone.backupFS()
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket48383(topology):
    """
    This test case will check that we re-alloc buffer sizes on import.c

    We achieve this by setting the servers dbcachesize to a stupid small value
    and adding huge objects to ds.

    Then when we run db2index, either:
    * If we are not using the re-alloc code, it will FAIL (Bad)
    * If we re-alloc properly, it all works regardless.
    """

    topology.standalone.config.set('nsslapd-maxbersize', '200000000')
    topology.standalone.restart()

    # Create some stupid huge objects / attributes in DS.
    # seeAlso is indexed by default. Lets do that!
    # This will take a while ...
    data = [random.choice(string.letters) for x in xrange(10000000)]
    s = "".join(data)

    # This was here for an iteration test.
    i = 1
    USER_DN = 'uid=user%s,ou=people,%s' % (i, DEFAULT_SUFFIX)
    padding = ['%s' % n for n in range(400)]

    user = Entry((USER_DN, {
                      'objectclass': 'top posixAccount person extensibleObject'.split(),
                      'uid': 'user%s' % (i),
                      'cn': 'user%s' % (i),
                      'uidNumber': '%s' % (i),
                      'gidNumber': '%s' % (i),
                      'homeDirectory': '/home/user%s' % (i),
                      'description': 'user description',
                      'sn' : s ,
                      'padding' : padding ,
                 }))

    try:
        topology.standalone.add_s(user)
    except ldap.LDAPError as e:
        log.fatal('test 48383: Failed to user%s: error %s ' % (i, e.message['desc']))
        assert False
    # Set the dbsize really low.

    topology.standalone.backend.setProperties(bename=DEFAULT_BENAME,
        prop='nsslapd-cachememsize', values='1')

    ## Does ds try and set a minimum possible value for this?
    ## Yes: [16/Feb/2016:16:39:18 +1000] - WARNING: cache too small, increasing to 500K bytes
    # Given the formula, by default, this means DS will make the buffsize 400k
    # So an object with a 1MB attribute should break indexing

    # stop the server
    topology.standalone.stop(timeout=30)
    # Now export and import the DB. It's easier than db2index ...
    topology.standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[], encrypt=False, \
        repl_data=True, outputfile='%s/ldif/%s.ldif' % (topology.standalone.dbdir,SERVERID_STANDALONE ))

    result = topology.standalone.ldif2db(DEFAULT_BENAME, None, None, False, '%s/ldif/%s.ldif' % (topology.standalone.dbdir,SERVERID_STANDALONE ))

    assert(result)

    # see if user1 exists at all ....

    result_user = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=user1)')

    assert(len(result_user) > 0)

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
