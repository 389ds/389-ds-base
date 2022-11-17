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

from lib389._constants import DN_DM, PASSWORD, DEFAULT_SUFFIX, DATA_DIR

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

MYSUFFIX = 'dc=example,dc=com'
MYSUFFIXBE = 'userRoot'
_MYLDIF = 'example1k_posix.ldif'
UIDNUMBERDN = "cn=uidnumber,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"


def runDbVerify(topology_st):
    topology_st.standalone.log.info("\n\n	+++++ dbverify +++++\n")
    sbin_dir = get_sbin_dir()
    dbverifyCMD = sbin_dir + "/dbverify -Z " + topology_st.standalone.serverid + " -V"
    dbverifyOUT = os.popen(dbverifyCMD, "r")
    topology_st.standalone.log.info("Running %s" % dbverifyCMD)
    running = True
    error = False
    while running:
        l = dbverifyOUT.readline()
        if l == "":
            running = False
        elif "libdb:" in l:
            running = False
            error = True
            topology_st.standalone.log.info("%s" % l)
        elif "verify failed" in l:
            error = True
            running = False
            topology_st.standalone.log.info("%s" % l)

    if error:
        topology_st.standalone.log.fatal("dbverify failed")
        assert False
    else:
        topology_st.standalone.log.info("dbverify passed")


def reindexUidNumber(topology_st):
    topology_st.standalone.log.info("\n\n	+++++ reindex uidnumber +++++\n")
    try:
        args = {TASK_WAIT: True}
        topology_st.standalone.tasks.reindex(suffix=MYSUFFIX, attrname='uidNumber', args=args)
    except:
        topology_st.standalone.log.fatal("Reindexing failed")
        assert False


def test_ticket48212(topology_st):
    """
    Import posixAccount entries.
    Index uidNumber
    add nsMatchingRule: integerOrderingMatch
    run dbverify to see if it reports the db corruption or not
    delete nsMatchingRule: integerOrderingMatch
    run dbverify to see if it reports the db corruption or not
    if no corruption is reported, the bug fix was verified.
    """
    log.info(
        'Testing Ticket 48212 - Dynamic nsMatchingRule changes had no effect on the attrinfo thus following reindexing, as well.')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    data_dir_path = topology_st.standalone.getDir(__file__, DATA_DIR)
    ldif_file = f"{data_dir_path}ticket48212/{_MYLDIF}"
    try:
        ldif_dir = topology_st.standalone.get_ldif_dir()
        shutil.copy(ldif_file, ldif_dir)
        ldif_file = ldif_dir + '/' + _MYLDIF
    except:
        log.fatal('Failed to copy ldif to instance ldif dir')
        assert False

    topology_st.standalone.log.info(
        "\n\n######################### Import Test data (%s) ######################\n" % ldif_file)
    args = {TASK_WAIT: True}
    importTask = Tasks(topology_st.standalone)
    importTask.importLDIF(MYSUFFIX, MYSUFFIXBE, ldif_file, args)
    args = {TASK_WAIT: True}

    runDbVerify(topology_st)

    topology_st.standalone.log.info("\n\n######################### Add index by uidnumber ######################\n")
    try:
        topology_st.standalone.add_s(Entry((UIDNUMBERDN, {'objectclass': "top nsIndex".split(),
                                                          'cn': 'uidnumber',
                                                          'nsSystemIndex': 'false',
                                                          'nsIndexType': "pres eq".split()})))
    except ValueError:
        topology_st.standalone.log.fatal("add_s failed: %s", ValueError)

    topology_st.standalone.log.info("\n\n######################### reindexing... ######################\n")
    reindexUidNumber(topology_st)

    runDbVerify(topology_st)

    topology_st.standalone.log.info("\n\n######################### Add nsMatchingRule ######################\n")
    try:
        topology_st.standalone.modify_s(UIDNUMBERDN, [(ldap.MOD_ADD, 'nsMatchingRule', b'integerOrderingMatch')])
    except ValueError:
        topology_st.standalone.log.fatal("modify_s failed: %s", ValueError)

    topology_st.standalone.log.info("\n\n######################### reindexing... ######################\n")
    reindexUidNumber(topology_st)

    runDbVerify(topology_st)

    topology_st.standalone.log.info("\n\n######################### Delete nsMatchingRule ######################\n")
    try:
        topology_st.standalone.modify_s(UIDNUMBERDN, [(ldap.MOD_DELETE, 'nsMatchingRule', b'integerOrderingMatch')])
    except ValueError:
        topology_st.standalone.log.fatal("modify_s failed: %s", ValueError)

    reindexUidNumber(topology_st)

    runDbVerify(topology_st)

    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
