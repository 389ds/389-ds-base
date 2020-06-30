# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
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
from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.index import Indexes
from lib389.backend import Backends
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st as topo
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1


@pytest.mark.skipif(ds_is_older("1.4.4.4"), reason="Not implemented")
def test_reindex_task_creates_abandoned_index_file(topo):
    """
    Recreating an index for the same attribute but changing
    the case of for example 1 letter, results in abandoned indexfile

    :id: 07ae5274-481a-4fa8-8074-e0de50d89ac6
    :setup: Standalone instance
    :steps:
        1. Create a user object with additional attributes:
           objectClass: mozillaabpersonalpha
           mozillaCustom1: xyz
        2. Add an index entry mozillacustom1
        3. Reindex the backend
        4. Check the content of the index (after it has been flushed to disk) mozillacustom1.db
        5. Remove the index
        6. Notice the mozillacustom1.db is removed
        7. Recreate the index but now use the exact case as mentioned in the schema
        8. Reindex the backend
        9. Check the content of the index (after it has been flushed to disk) mozillaCustom1.db
        10. Check that an ldapsearch does not return a result (mozillacustom1=xyz)
        11. Check that an ldapsearch returns the results (mozillaCustom1=xyz)
        12. Restart the instance
        13. Notice that an ldapsearch does not return a result(mozillacustom1=xyz)
        14. Check that an ldapsearch does not return a result (mozillacustom1=xyz)
        15. Check that an ldapsearch returns the results (mozillaCustom1=xyz)
        16. Reindex the backend
        17. Notice the second indexfile for this attribute
        18. Check the content of the index (after it has been flushed to disk) no mozillacustom1.db
        19. Check the content of the index (after it has been flushed to disk) mozillaCustom1.db
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
        6. Should Success.
        7. Should Success.
        8. Should Success.
        9. Should Success.
        10. Should Success.
        11. Should Success.
        12. Should Success.
        13. Should Success.
        14. Should Success.
        15. Should Success.
        16. Should Success.
        17. Should Success.
        18. Should Success.
        19. Should Success.
    """

    inst = topo.standalone
    attr_name = "mozillaCustom1"
    attr_value = "xyz"

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create_test_user()
    user.add("objectClass", "mozillaabpersonalpha")
    user.add(attr_name, attr_value)

    backends = Backends(inst)
    backend = backends.get(DEFAULT_BENAME)
    indexes = backend.get_indexes()
    index = indexes.create(properties={
        'cn': attr_name.lower(),
        'nsSystemIndex': 'false',
        'nsIndexType': ['eq', 'pres']
        })

    backend.reindex()
    time.sleep(3)
    assert os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    index.delete()
    assert not os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")

    index = indexes.create(properties={
        'cn': attr_name,
        'nsSystemIndex': 'false',
        'nsIndexType': ['eq', 'pres']
        })

    backend.reindex()
    time.sleep(3)
    assert not os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    assert os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name}.db")

    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, f"{attr_name}={attr_value}")
    assert len(entries) > 0
    inst.restart()
    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, f"{attr_name}={attr_value}")
    assert len(entries) > 0

    backend.reindex()
    time.sleep(3)
    assert not os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    assert os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name}.db")


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
