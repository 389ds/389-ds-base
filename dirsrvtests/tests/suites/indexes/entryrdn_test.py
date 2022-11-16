# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import os
import pytest
import ldap
import logging
from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.backend import Backends
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.topologies import topology_m2 as topo_m2
from lib389.agreement import Agreements
from lib389.utils import ds_is_older
from lib389.tasks import Tasks,ExportTask, ImportTask
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1


OUNAME = 'NewOU'
OUDN = f'ou={OUNAME},ou=people,{DEFAULT_SUFFIX}'
OUPROPERTIES = { 'ou' : OUNAME }
USERNAME = 'NewUser'
USERID = '100'
USERSDN = f'uid={USERNAME},{OUDN}'
USERPROPERTIES = {
        'uid': USERNAME,
        'sn': USERNAME,
        'cn': USERNAME,
        'uidNumber': USERID,
        'gidNumber': USERID,
        'homeDirectory': f'/home/{USERNAME}'
    }

# 2 tombstone entry + 1 RUV have 2 records in entryrdn index
# Each record have 1 rdn + 1 normalized rdn both containing nsuniqueid
# So 3 * 2 * 2 nsuniqueid substrings are expected
EXPECTED_NB_NSNIQUEID = 12

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def checkdbscancount(inst, pattern, expected_count):
    inst.restart()
    dbscanOut = inst.dbscan(args=['-f', f'{inst.dbdir}/{DEFAULT_BENAME}/entryrdn.db', '-A'], stopping=False)
    count = dbscanOut.count(pattern)
    if count != expected_count:
        log.info(f"dbscan output is: {dbscanOut}")
    assert count == expected_count


def test_tombstone(topo_m2):
    """
    An internal unindexed search was able to crash the server due to missing logging function.

    :id: a12eacac-4e35-11ed-8625-482ae39447e5
    :setup: 2 Suplier instances
    :steps:
        1. Add an OrganizationalUnit
        2. Add an User as child of the new OrganizationalUnit
        3. Modify User's description
        4. Delele User
        5. Delete OrganizationalUnit
        6. Dump supplier1 entryrdn index
        7. Check that nsuniqueid appears three times in the dump result
        8. Export supplier1 with replication data
        9. Import supplier2 with previously exported ldif file
        10. Dump entryrdn index
        11. Check that nsuniqueid appears three times in the dump result
        12. Reindex entryrdn on supplier1
        13. Dump entryrdn index on supplier
        14. Check that nsuniqueid appears three times in the dump result
        15. Perform bulk import from supplier1 to supplier2
        16. Wait until bulk import is completed
        17. Dump entryrdn index on supplier
        18. Check that nsuniqueid appears three times in the dump result
    :expectedresults:
        1. Should succeed
        2. Should succeed
        3. Should succeed
        4. Should succeed
        5. Should succeed
        6. Should succeed
        7. Should succeed
        8. Should succeed
        9. Should succeed
        10. Should succeed
        11. Should succeed
        12. Should succeed
        13. Should succeed
        14. Should succeed
        15. Should succeed
        16. Should succeed
        17. Should succeed
        18. Should succeed
    """
    s1 = topo_m2.ms["supplier1"]
    s2 = topo_m2.ms["supplier2"]
    ldif_dir = s1.get_ldif_dir()

    log.info("Create tombstones...")
    ous = OrganizationalUnits(s1, DEFAULT_SUFFIX)
    ou = ous.create(properties=OUPROPERTIES)
    users = UserAccounts(s1, DEFAULT_SUFFIX, rdn=None)
    user = users.create(properties=USERPROPERTIES)
    user.replace('description', 'New Description')
    user.delete()
    ou.delete()
    # Need to restart the server otherwise bdb changes may not be up to date.
    checkdbscancount(s1, 'nsuniqueid', EXPECTED_NB_NSNIQUEID)

    log.info("Exporting LDIF online...")
    export_ldif = ldif_dir + '/export.ldif'
    export_task = Backends(s1).export_ldif(be_names=DEFAULT_BENAME, ldif=export_ldif, replication=True)
    export_task.wait()

    log.info("Importing LDIF online...")
    import_task = ImportTask(s2)
    import_task.import_suffix_from_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()
    checkdbscancount(s1, 'nsuniqueid', EXPECTED_NB_NSNIQUEID)

    log.info("Reindex online...")
    task = Tasks(s2)
    task.reindex(suffix=DEFAULT_SUFFIX, args={'wait': True})
    checkdbscancount(s1, 'nsuniqueid', EXPECTED_NB_NSNIQUEID)

    log.info("Bulk import...")
    agmt = Agreements(s1).list()[0]
    agmt.begin_reinit()
    (done, error) = agmt.wait_reinit()
    assert done is True
    assert error is False
    checkdbscancount(s1, 'nsuniqueid', EXPECTED_NB_NSNIQUEID)



if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
