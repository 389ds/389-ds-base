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

installation1_prefix = None

NEW_ACCOUNT    = "new_account"
MAX_ACCOUNTS   = 20

MIXED_VALUE="/home/mYhOmEdIrEcToRy"
LOWER_VALUE="/home/myhomedirectory"
HOMEDIRECTORY_INDEX = 'cn=homeDirectory,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
HOMEDIRECTORY_CN="homedirectory"
MATCHINGRULE = 'nsMatchingRule'
UIDNUMBER_INDEX = 'cn=uidnumber,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
UIDNUMBER_CN="uidnumber"


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
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
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


def test_ticket48745_init(topology):
    log.info("Initialization: add dummy entries for the tests")
    for cpt in range(MAX_ACCOUNTS):
        name = "%s%d" % (NEW_ACCOUNT, cpt)
        topology.standalone.add_s(Entry(("uid=%s,%s" % (name, SUFFIX), {
                                            'objectclass': "top posixAccount".split(),
                                            'uid': name,
                                            'cn': name,
                                            'uidnumber': str(111),
                                            'gidnumber': str(222),
                                            'homedirectory': "/home/tbordaz_%d" % cpt})))


def test_ticket48745_homeDirectory_indexed_cis(topology):
    log.info("\n\nindex homeDirectory in caseIgnoreIA5Match and caseExactIA5Match")
    try:
        ent = topology.standalone.getEntry(HOMEDIRECTORY_INDEX, ldap.SCOPE_BASE)
    except ldap.NO_SUCH_OBJECT:
        topology.standalone.add_s(Entry((HOMEDIRECTORY_INDEX, {
                                            'objectclass': "top nsIndex".split(),
                                            'cn': HOMEDIRECTORY_CN,
                                            'nsSystemIndex': 'false',
                                            'nsIndexType': 'eq'})))
    #log.info("attach debugger")
    #time.sleep(60)

    IGNORE_MR_NAME='caseIgnoreIA5Match'
    EXACT_MR_NAME='caseExactIA5Match'
    mod = [(ldap.MOD_REPLACE, MATCHINGRULE, (IGNORE_MR_NAME, EXACT_MR_NAME))]
    topology.standalone.modify_s(HOMEDIRECTORY_INDEX, mod)

    #topology.standalone.stop(timeout=10)
    log.info("successfully checked that filter with exact mr , a filter with lowercase eq is failing")
    #assert topology.standalone.db2index(bename=DEFAULT_BENAME, suffixes=None, attrs=['homeDirectory'])
    #topology.standalone.start(timeout=10)
    args = {TASK_WAIT: True}
    topology.standalone.tasks.reindex(suffix=SUFFIX, attrname='homeDirectory', args=args)

    log.info("Check indexing succeeded with a specified matching rule")
    file_path = os.path.join(topology.standalone.prefix, "var/log/dirsrv/slapd-%s/errors" % topology.standalone.serverid)
    file_obj = open(file_path, "r")

    # Check if the MR configuration failure occurs
    regex = re.compile("unknown or invalid matching rule")
    while True:
        line = file_obj.readline()
        found = regex.search(line)
        if ((line == '') or (found)):
            break

    if (found):
        log.info("The configuration of a specific MR fails")
        log.info(line)
        assert 0


def test_ticket48745_homeDirectory_mixed_value(topology):
    # Set a homedirectory value with mixed case
    name = "uid=%s1,%s" % (NEW_ACCOUNT, SUFFIX)
    mod = [(ldap.MOD_REPLACE, 'homeDirectory', MIXED_VALUE)]
    topology.standalone.modify_s(name, mod)


def test_ticket48745_extensible_search_after_index(topology):
    name = "uid=%s1,%s" % (NEW_ACCOUNT, SUFFIX)

    # check with the exact stored value
    log.info("Default: can retrieve an entry filter syntax with exact stored value")
    ent = topology.standalone.getEntry(SUFFIX, ldap.SCOPE_SUBTREE, "(homeDirectory=%s)" % MIXED_VALUE)
#     log.info("attach debugger")
#     time.sleep(60)

    # This search will fail because a
    # subtree search with caseExactIA5Match will find a key
    # where the value has been lowercase
    log.info("Default: can retrieve an entry filter caseExactIA5Match with exact stored value")
    ent = topology.standalone.getEntry(SUFFIX, ldap.SCOPE_SUBTREE, "(homeDirectory:caseExactIA5Match:=%s)" % MIXED_VALUE)
    assert ent

    # But do additional searches.. just for more tests
    # check with a lower case value that is different from the stored value
    log.info("Default: can not retrieve an entry filter syntax match with lowered stored value")
    try:
        ent = topology.standalone.getEntry(SUFFIX, ldap.SCOPE_SUBTREE, "(homeDirectory=%s)" % LOWER_VALUE)
        assert ent is None
    except ldap.NO_SUCH_OBJECT:
        pass
    log.info("Default: can not retrieve an entry filter caseExactIA5Match with lowered stored value")
    try:
        ent = topology.standalone.getEntry(SUFFIX, ldap.SCOPE_SUBTREE, "(homeDirectory:caseExactIA5Match:=%s)" % LOWER_VALUE)
        assert ent is None
    except ldap.NO_SUCH_OBJECT:
        pass
    log.info("Default: can retrieve an entry filter caseIgnoreIA5Match with lowered stored value")
    ent = topology.standalone.getEntry(SUFFIX, ldap.SCOPE_SUBTREE, "(homeDirectory:caseIgnoreIA5Match:=%s)" % LOWER_VALUE)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
#     global installation1_prefix
#     installation1_prefix = None
#     topo = topology(True)
#     test_ticket48745_init(topo)
#
#     test_ticket48745_homeDirectory_indexed_cis(topo)
#     test_ticket48745_homeDirectory_mixed_value(topo)
#     test_ticket48745_extensible_search_after_index(topo)

    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
