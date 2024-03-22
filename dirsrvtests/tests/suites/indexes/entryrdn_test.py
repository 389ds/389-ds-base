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
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.topologies import topology_m2 as topo_m2
from lib389.agreement import Agreements
from lib389.utils import ds_is_older, ensure_bytes
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
    count = dbscanOut.count(ensure_bytes(pattern))
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


def create_long_rdn_users(users, users_dict):
    for key in range(0x41, 0x5B):
        # generate a name longer that lmdb key size limit (511)
        longname = chr(key) * 700
        userproperties = {
            'uid': longname,
            'sn': longname,
            'cn': longname,
            'uidNumber': str(1000+key),
            'gidNumber': str(1000+key),
            'homeDirectory': f'/home/{longname}'
        }
        user = users.create(properties=userproperties)
        users_dict[key] = user
    for user in users_dict.values():
        assert user.exists()


def test_long_rdn(topo_m2):
    """
    Test operation on entries with rdn longer than lmdb maximum key size (511 bytes).

    :id: 47a06e92-e14f-11ee-b492-482ae39447e5
    :setup: 2 Suplier instances
    :steps:
        1. Add Users with a long name
        2. Check Users exists
        3. Export supplier1 with replication data
        4. Check Users exists
        5. Import supplier2 with previously exported ldif file
        6. Check Users exists
        7. Reindex entryrdn on supplier1
        8. Check Users exists
        9. Perform bulk import from supplier1 to supplier2
        10. Wait until bulk import is completed
        11. Check Users exists
        12. Delete Users exists
        13. Create an organizational unit with long ou
        14. Add children with long rdn
        15. Rename the organizational unit
        16. Delete the users with a long rdn
        17. Delete the ou with a long rdn
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
    """
    s1 = topo_m2.ms["supplier1"]
    s2 = topo_m2.ms["supplier2"]
    ldif_dir = s1.get_ldif_dir()
    log.info("Create users with a long rdn...")
    users_dict_s1 = {}
    users_dict_s2 = {}
    users = UserAccounts(s1, DEFAULT_SUFFIX, rdn=None)
    create_long_rdn_users(users, users_dict_s1)
    for (key, user) in users_dict_s1.items():
        users_dict_s2[key] = UserAccount(s2, user.dn)

    log.info("Exporting LDIF online...")
    export_ldif = ldif_dir + '/export.ldif'
    export_task = Backends(s1).export_ldif(be_names=DEFAULT_BENAME, ldif=export_ldif, replication=True)
    export_task.wait()
    assert export_task.get_exit_code() == 0
    for user in users_dict_s1.values():
        assert user.exists()

    log.info("Importing LDIF online...")
    import_task = ImportTask(s2)
    import_task.import_suffix_from_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()
    assert import_task.get_exit_code() == 0
    for user in users_dict_s2.values():
        assert user.exists()

    log.info("Reindex online...")
    task = Tasks(s2)
    task.reindex(suffix=DEFAULT_SUFFIX, args={'wait': True})
    for user in users_dict_s2.values():
        assert user.exists()

    log.info("Bulk import...")
    agmt = Agreements(s1).list()[0]
    agmt.begin_reinit()
    (done, error) = agmt.wait_reinit()
    assert done is True
    assert error is False
    for user in users_dict_s2.values():
        assert user.exists()

    log.info("Delete the users with a long rdn...")
    for user in users_dict_s1.values():
        user.delete()
    for user in users_dict_s1.values():
        assert not user.exists()

    log.info("Create an organizational unit with long ou")
    longname = '+' * 700
    ous = OrganizationalUnits(s1, DEFAULT_SUFFIX)
    ou = ous.create(properties={ 'ou': longname })

    log.info("Add children with long rdn")
    users_dict_s1 = {}
    users = UserAccounts(s1, ou.dn, rdn=None)
    create_long_rdn_users(users, users_dict_s1)

    log.info("Rename the organizational unit")
    longname = '-' * 700
    ou.rename(f'ou={longname}')

    # Check that user exists
    users_list = []
    for user in users_dict_s1.values():
        renamed_dn = user.dn.replace('+', '-')
        renamed_user = UserAccount(s1, renamed_dn)
        users_list.append(renamed_user)
    for user in users_list:
        assert user.exists()

    log.info("Delete the users with a long rdn")
    for user in users_list:
        user.delete()
    for user in users_list:
        assert not user.exists()

    log.info("Delete the ou with a long rdn")
    ou.delete()
    assert not ou.exists()


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
