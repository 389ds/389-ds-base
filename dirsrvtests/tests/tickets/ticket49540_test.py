import logging
import pytest
import os
import ldap
import time
import re
from lib389._constants import *
from lib389.tasks import *
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389 import Entry

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

HOMEDIRECTORY_INDEX = 'cn=homeDirectory,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
HOMEDIRECTORY_CN = "homedirectory"
MATCHINGRULE = 'nsMatchingRule'
USER_CN = 'user_'

def create_index_entry(topo):
    log.info("\n\nindex homeDirectory")
    try:
        ent = topo.getEntry(HOMEDIRECTORY_INDEX, ldap.SCOPE_BASE)
    except ldap.NO_SUCH_OBJECT:
        topo.add_s(Entry((HOMEDIRECTORY_INDEX, {
            'objectclass': "top nsIndex".split(),
            'cn': HOMEDIRECTORY_CN,
            'nsSystemIndex': 'false',
            MATCHINGRULE: ['caseIgnoreIA5Match', 'caseExactIA5Match' ],
            'nsIndexType': ['eq', 'sub', 'pres']})))


def provision_users(topo):
    test_users = []
    homeValue = b'x' * (32 * 1024)  # just to slow down indexing
    for i in range(100):
        CN = '%s%d' % (USER_CN, i)
        users = UserAccounts(topo, SUFFIX)
        user_props = TEST_USER_PROPERTIES.copy()
        user_props.update({'uid': CN, 'cn': CN, 'sn': '_%s' % CN, HOMEDIRECTORY_CN: homeValue})
        testuser = users.create(properties=user_props)
        test_users.append(testuser)
    return test_users

def start_start_status(server):
    args = {TASK_WAIT: False}
    indexTask = Tasks(server)
    indexTask.reindex(suffix=SUFFIX, attrname='homeDirectory', args=args)
    return indexTask

def check_task_status(server, indexTask, test_entry):
    finish_pattern = re.compile(".*Finished indexing.*")
    mod = [(ldap.MOD_REPLACE, 'sn', b'foo')]
    for i in range(10):
        log.info("check_task_status =========> %d th loop" % i)
        try:
            ent = server.getEntry(indexTask.dn, ldap.SCOPE_BASE)
            if ent.hasAttr('nsTaskStatus'):
                value = str(ent.getValue('nsTaskStatus'))
                finish = finish_pattern.search(value)
                log.info("%s ---> %s" % (indexTask.dn, value))
            else:
                finish = None
                log.info("%s ---> NO STATUS" % (indexTask.dn))

            if not finish:
                # This is not yet finished try an update
                try:
                    server.modify_s(test_entry, mod)

                    # weird, may be indexing just complete
                    ent = server.getEntry(indexTask.dn, ldap.SCOPE_BASE, ['nsTaskStatus'])
                    assert (ent.hasAttr('nsTaskStatus') and regex.search(ent.getValue('nsTaskStatus')))
                    log.info("Okay, it just finished so the MOD was successful")
                except ldap.UNWILLING_TO_PERFORM:
                    log.info("=========> Great it was expected in the middle of index")
            else:
                # The update should be successful
                server.modify_s(test_entry, mod)

        except ldap.NO_SUCH_OBJECT:
            log.info("%s: no found" % (indexTask.dn))

        time.sleep(1)

def test_ticket49540(topo):
    """Specify a test case purpose or name here

    :id: 1df16d5a-1b92-46b7-8435-876b87545748
    :setup: Standalone Instance
    :steps:
        1. Create homeDirectory index (especially with substring)
        2. Creates 100 users with large homeDirectory value => long to index
        3. Start an indexing task WITHOUT waiting for its completion
        4. Monitor that until task.status = 'Finish', any update -> UNWILLING to perform
    :expectedresults:
        1. Index configuration succeeds
        2. users entry are successfully created
        3. Indexing task is started
        4. If the task.status does not contain 'Finished indexing', any update should return UNWILLING_TO_PERFORM
           When it contains 'Finished indexing', updates should be successful
    """

    server = topo.standalone
    create_index_entry(server)
    test_users = provision_users(server)

    indexTask = start_start_status(server)
    check_task_status(server, indexTask, test_users[0].dn)

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

