# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import random
import string

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, SERVERID_STANDALONE

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket48383(topology_st):
    """
    This test case will check that we re-alloc buffer sizes on import.c

    We achieve this by setting the servers dbcachesize to a stupid small value
    and adding huge objects to ds.

    Then when we run db2index, either:
    * If we are not using the re-alloc code, it will FAIL (Bad)
    * If we re-alloc properly, it all works regardless.
    """

    topology_st.standalone.config.set('nsslapd-maxbersize', '200000000')
    topology_st.standalone.restart()

    # Create some stupid huge objects / attributes in DS.
    # seeAlso is indexed by default. Lets do that!
    # This will take a while ...
    data = [random.choice(string.ascii_letters) for x in range(10000000)]
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
        'sn': s,
        'padding': padding,
    }))

    topology_st.standalone.add_s(user)

    # Set the dbsize really low.
    try:
        topology_st.standalone.modify_s(DEFAULT_BENAME, [(ldap.MOD_REPLACE,
                                                          'nsslapd-cachememsize', b'1')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change nsslapd-cachememsize {}'.format(e.args[0]['desc']))

    ## Does ds try and set a minimum possible value for this?
    ## Yes: [16/Feb/2016:16:39:18 +1000] - WARNING: cache too small, increasing to 500K bytes
    # Given the formula, by default, this means DS will make the buffsize 400k
    # So an object with a 1MB attribute should break indexing

    ldifpath = os.path.join(topology_st.standalone.get_ldif_dir(), "%s.ldif" % SERVERID_STANDALONE)

    # stop the server
    topology_st.standalone.stop()
    # Now export and import the DB. It's easier than db2index ...
    topology_st.standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[],
                                   encrypt=False, repl_data=True, outputfile=ldifpath)

    result = topology_st.standalone.ldif2db(DEFAULT_BENAME, None, None, False, ldifpath)

    assert (result)
    topology_st.standalone.start()

    # see if user1 exists at all ....

    result_user = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(uid=user1)')

    assert (len(result_user) > 0)

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
