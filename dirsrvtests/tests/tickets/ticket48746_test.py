# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import SUFFIX, DEFAULT_SUFFIX, DEFAULT_BENAME

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

NEW_ACCOUNT = "new_account"
MAX_ACCOUNTS = 20

MIXED_VALUE = "/home/mYhOmEdIrEcToRy"
LOWER_VALUE = "/home/myhomedirectory"
HOMEDIRECTORY_INDEX = 'cn=homeDirectory,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
HOMEDIRECTORY_CN = "homedirectory"
MATCHINGRULE = 'nsMatchingRule'
UIDNUMBER_INDEX = 'cn=uidnumber,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
UIDNUMBER_CN = "uidnumber"


def test_ticket48746_init(topology_st):
    log.info("Initialization: add dummy entries for the tests")
    for cpt in range(MAX_ACCOUNTS):
        name = "%s%d" % (NEW_ACCOUNT, cpt)
        topology_st.standalone.add_s(Entry(("uid=%s,%s" % (name, SUFFIX), {
            'objectclass': "top posixAccount".split(),
            'uid': name,
            'cn': name,
            'uidnumber': str(111),
            'gidnumber': str(222),
            'homedirectory': "/home/tbordaz_%d" % cpt})))


def test_ticket48746_homeDirectory_indexed_cis(topology_st):
    log.info("\n\nindex homeDirectory in caseIgnoreIA5Match and caseExactIA5Match")
    try:
        ent = topology_st.standalone.getEntry(HOMEDIRECTORY_INDEX, ldap.SCOPE_BASE)
    except ldap.NO_SUCH_OBJECT:
        topology_st.standalone.add_s(Entry((HOMEDIRECTORY_INDEX, {
            'objectclass': "top nsIndex".split(),
            'cn': HOMEDIRECTORY_CN,
            'nsSystemIndex': 'false',
            'nsIndexType': 'eq'})))
    # log.info("attach debugger")
    # time.sleep(60)

    IGNORE_MR_NAME = b'caseIgnoreIA5Match'
    EXACT_MR_NAME = b'caseExactIA5Match'
    mod = [(ldap.MOD_REPLACE, MATCHINGRULE, (IGNORE_MR_NAME, EXACT_MR_NAME))]
    topology_st.standalone.modify_s(HOMEDIRECTORY_INDEX, mod)

    # topology_st.standalone.stop(timeout=10)
    log.info("successfully checked that filter with exact mr , a filter with lowercase eq is failing")
    # assert topology_st.standalone.db2index(bename=DEFAULT_BENAME, suffixes=None, attrs=['homeDirectory'])
    # topology_st.standalone.start(timeout=10)
    args = {TASK_WAIT: True}
    topology_st.standalone.tasks.reindex(suffix=SUFFIX, attrname='homeDirectory', args=args)

    log.info("Check indexing succeeded with a specified matching rule")
    file_obj = open(topology_st.standalone.errlog, "r")

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
        assert not found


def test_ticket48746_homeDirectory_mixed_value(topology_st):
    # Set a homedirectory value with mixed case
    name = "uid=%s1,%s" % (NEW_ACCOUNT, SUFFIX)
    mod = [(ldap.MOD_REPLACE, 'homeDirectory', ensure_bytes(MIXED_VALUE))]
    topology_st.standalone.modify_s(name, mod)


def test_ticket48746_extensible_search_after_index(topology_st):
    name = "uid=%s1,%s" % (NEW_ACCOUNT, SUFFIX)

    # check with the exact stored value
    #     log.info("Default: can retrieve an entry filter syntax with exact stored value")
    #     ent = topology_st.standalone.getEntry(name, ldap.SCOPE_BASE, "(homeDirectory=%s)" % MIXED_VALUE)
    #     log.info("attach debugger")
    #     time.sleep(60)

    # This search is enought to trigger the crash
    # because it loads a registered filter MR plugin that has no indexer create function
    # following index will trigger the crash
    log.info("Default: can retrieve an entry filter caseExactIA5Match with exact stored value")
    ent = topology_st.standalone.getEntry(name, ldap.SCOPE_BASE, "(homeDirectory:caseExactIA5Match:=%s)" % MIXED_VALUE)


def test_ticket48746_homeDirectory_indexed_ces(topology_st):
    log.info("\n\nindex homeDirectory in  caseExactIA5Match, this would trigger the crash")
    try:
        ent = topology_st.standalone.getEntry(HOMEDIRECTORY_INDEX, ldap.SCOPE_BASE)
    except ldap.NO_SUCH_OBJECT:
        topology_st.standalone.add_s(Entry((HOMEDIRECTORY_INDEX, {
            'objectclass': "top nsIndex".split(),
            'cn': HOMEDIRECTORY_CN,
            'nsSystemIndex': 'false',
            'nsIndexType': 'eq'})))
    #     log.info("attach debugger")
    #     time.sleep(60)

    EXACT_MR_NAME = b'caseExactIA5Match'
    mod = [(ldap.MOD_REPLACE, MATCHINGRULE, (EXACT_MR_NAME))]
    topology_st.standalone.modify_s(HOMEDIRECTORY_INDEX, mod)

    # topology_st.standalone.stop(timeout=10)
    log.info("successfully checked that filter with exact mr , a filter with lowercase eq is failing")
    # assert topology_st.standalone.db2index(bename=DEFAULT_BENAME, suffixes=None, attrs=['homeDirectory'])
    # topology_st.standalone.start(timeout=10)
    args = {TASK_WAIT: True}
    topology_st.standalone.tasks.reindex(suffix=SUFFIX, attrname='homeDirectory', args=args)

    log.info("Check indexing succeeded with a specified matching rule")
    file_obj = open(topology_st.standalone.errlog, "r")

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
        assert not found


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
