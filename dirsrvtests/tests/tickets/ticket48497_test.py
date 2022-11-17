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

from lib389._constants import DEFAULT_SUFFIX, SUFFIX

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


def test_ticket48497_init(topology_st):
    log.info("Initialization: add dummy entries for the tests")
    for cpt in range(MAX_ACCOUNTS):
        name = "%s%d" % (NEW_ACCOUNT, cpt)
        topology_st.standalone.add_s(Entry(("uid=%s,%s" % (name, SUFFIX), {
            'objectclass': "top posixAccount".split(),
            'uid': name,
            'cn': name,
            'uidnumber': str(111),
            'gidnumber': str(222),
            'homedirectory': "/home/tb_%d" % cpt})))


def test_ticket48497_homeDirectory_mixed_value(topology_st):
    # Set a homedirectory value with mixed case
    name = "uid=%s1,%s" % (NEW_ACCOUNT, SUFFIX)
    mod = [(ldap.MOD_REPLACE, 'homeDirectory', ensure_bytes(MIXED_VALUE))]
    topology_st.standalone.modify_s(name, mod)


def test_ticket48497_extensible_search(topology_st):
    name = "uid=%s1,%s" % (NEW_ACCOUNT, SUFFIX)

    # check with the exact stored value
    log.info("Default: can retrieve an entry filter syntax with exact stored value")
    ent = topology_st.standalone.getEntry(name, ldap.SCOPE_BASE, "(homeDirectory=%s)" % MIXED_VALUE)
    log.info("Default: can retrieve an entry filter caseExactIA5Match with exact stored value")
    ent = topology_st.standalone.getEntry(name, ldap.SCOPE_BASE, "(homeDirectory:caseExactIA5Match:=%s)" % MIXED_VALUE)

    # check with a lower case value that is different from the stored value
    log.info("Default: can not retrieve an entry filter syntax match with lowered stored value")
    try:
        ent = topology_st.standalone.getEntry(name, ldap.SCOPE_BASE, "(homeDirectory=%s)" % LOWER_VALUE)
        assert ent is None
    except ldap.NO_SUCH_OBJECT:
        pass
    log.info("Default: can not retrieve an entry filter caseExactIA5Match with lowered stored value")
    try:
        ent = topology_st.standalone.getEntry(name, ldap.SCOPE_BASE,
                                              "(homeDirectory:caseExactIA5Match:=%s)" % LOWER_VALUE)
        assert ent is None
    except ldap.NO_SUCH_OBJECT:
        pass
    log.info("Default: can retrieve an entry filter caseIgnoreIA5Match with lowered stored value")
    ent = topology_st.standalone.getEntry(name, ldap.SCOPE_BASE, "(homeDirectory:caseIgnoreIA5Match:=%s)" % LOWER_VALUE)


def test_ticket48497_homeDirectory_index_cfg(topology_st):
    log.info("\n\nindex homeDirectory in caseIgnoreIA5Match and caseExactIA5Match")
    try:
        ent = topology_st.standalone.getEntry(HOMEDIRECTORY_INDEX, ldap.SCOPE_BASE)
    except ldap.NO_SUCH_OBJECT:
        topology_st.standalone.add_s(Entry((HOMEDIRECTORY_INDEX, {
            'objectclass': "top nsIndex".split(),
            'cn': HOMEDIRECTORY_CN,
            'nsSystemIndex': 'false',
            'nsIndexType': 'eq'})))

    IGNORE_MR_NAME = b'caseIgnoreIA5Match'
    EXACT_MR_NAME = b'caseExactIA5Match'
    mod = [(ldap.MOD_REPLACE, MATCHINGRULE, (IGNORE_MR_NAME, EXACT_MR_NAME))]
    topology_st.standalone.modify_s(HOMEDIRECTORY_INDEX, mod)


def test_ticket48497_homeDirectory_index_run(topology_st):
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
        assert 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
