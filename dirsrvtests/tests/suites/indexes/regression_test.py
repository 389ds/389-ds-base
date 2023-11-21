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
from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.backend import Backend, Backends, DatabaseConfig
from lib389.cos import  CosClassicDefinition, CosClassicDefinitions, CosTemplate
from lib389.dbgen import dbgen_users
from lib389.idm.domain import Domain
from lib389.idm.group import Groups, Group
from lib389.idm.nscontainer import nsContainer
from lib389.idm.user import UserAccount, UserAccounts
from lib389.index import Indexes
from lib389.plugins import MemberOfPlugin
from lib389.properties import TASK_WAIT
from lib389.tasks import Tasks, Task
from lib389.topologies import topology_st as topo
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1

SUFFIX2 = 'dc=example2,dc=com'
BENAME2 = 'be2'

DEBUGGING = os.getenv("DEBUGGING", default=False)


@pytest.fixture(scope="function")
def add_backend_and_ldif_50K_users(request, topo):
    """
    Add an empty backend and associated 50K users ldif file
    """

    tasks = Tasks(topo.standalone)
    import_ldif = f'{topo.standalone.ldifdir}/be2_50K_users.ldif'
    be2 = Backend(topo.standalone)
    be2.create(properties={
            'cn': BENAME2,
            'nsslapd-suffix': SUFFIX2,
        },
    )

    def fin():
        nonlocal be2
        if not DEBUGGING:
            be2.delete()

    request.addfinalizer(fin)
    parent = f'ou=people,{SUFFIX2}'
    dbgen_users(topo.standalone, 50000, import_ldif, SUFFIX2, generic=True, parent=parent)
    assert tasks.importLDIF(
        suffix=SUFFIX2,
        input_file=import_ldif,
        args={TASK_WAIT: True}
    ) == 0

    return import_ldif


@pytest.fixture(scope="function")
def add_a_group_with_users(request, topo):
    """
    Add a group and users, which are members of this group.
    """
    groups = Groups(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    group = groups.create(properties={'cn': 'test_group'})
    users_list = []
    users_num = 100
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    for num in range(users_num):
        USER_NAME = f'test_{num}'
        user = users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': f'{num}',
            'gidNumber': f'{num}',
            'homeDirectory': f'/home/{USER_NAME}'
        })
        users_list.append(user)
        group.add_member(user.dn)

    def fin():
        """
        Removes group and users.
        """
        # If the server crashed, start it again to do the cleanup
        if not topo.standalone.status():
            topo.standalone.start()
        for user in users_list:
            user.delete()
        group.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def set_small_idlistscanlimit(request, topo):
    """
    Set nsslapd-idlistscanlimit to a smaller value to accelerate the reproducer
    """
    db_cfg = DatabaseConfig(topo.standalone)
    old_idlistscanlimit = db_cfg.get_attr_vals_utf8('nsslapd-idlistscanlimit')
    db_cfg.set([('nsslapd-idlistscanlimit', '100')])
    topo.standalone.restart()

    def fin():
        """
        Set nsslapd-idlistscanlimit back to the default value
        """
        # If the server crashed, start it again to do the cleanup
        if not topo.standalone.status():
            topo.standalone.start()
        db_cfg.set([('nsslapd-idlistscanlimit', old_idlistscanlimit)])
        topo.standalone.restart()

    request.addfinalizer(fin)

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
@pytest.mark.skipif(ds_is_older("1.4.4.4"), reason="Not implemented")
def test_reindex_task_creates_abandoned_index_file(topo):
    """
    Recreating an index for the same attribute but changing
    the case of for example 1 letter, results in abandoned indexfile

    :id: 07ae5274-481a-4fa8-8074-e0de50d89ac6
    :customerscenario: True
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
    assert os.path.exists(f"{inst.ds_paths.db_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    index.delete()
    assert not os.path.exists(f"{inst.ds_paths.db_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")

    index = indexes.create(properties={
        'cn': attr_name,
        'nsSystemIndex': 'false',
        'nsIndexType': ['eq', 'pres']
        })

    backend.reindex()
    time.sleep(3)
    assert not os.path.exists(f"{inst.ds_paths.db_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    assert os.path.exists(f"{inst.ds_paths.db_dir}/{DEFAULT_BENAME}/{attr_name}.db")

    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, f"{attr_name}={attr_value}")
    assert len(entries) > 0
    inst.restart()
    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, f"{attr_name}={attr_value}")
    assert len(entries) > 0

    backend.reindex()
    time.sleep(3)
    assert not os.path.exists(f"{inst.ds_paths.db_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    assert os.path.exists(f"{inst.ds_paths.db_dir}/{DEFAULT_BENAME}/{attr_name}.db")


@pytest.mark.bz1905450
def test_unindexed_internal_search_crashes_server(topo, add_a_group_with_users, set_small_idlistscanlimit):
    """
    An internal unindexed search was able to crash the server due to missing logging function.

    :id: 2d0e4070-96d6-46e5-b2c8-9495925e3e87
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Add a group with users
        2. Change nsslapd-idlistscanlimit to a smaller value to accelerate the reproducer
        3. Enable memberOf plugin
        4. Restart the instance
        5. Run memberOf fixup task
        6. Wait for the task to complete
    :expectedresults:
        1. Should succeed
        2. Should succeed
        3. Should succeed
        4. Should succeed
        5. Should succeed
        6. Server should not crash
    """
    inst = topo.standalone
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    inst.restart()
    task = memberof.fixup(DEFAULT_SUFFIX)
    task.wait()
    assert inst.status()


def test_reject_virtual_attr_for_indexing(topo):
    """Reject trying to add an index for a virtual attribute (nsrole and COS)

    :id: 0fffa7a8-aaec-44d6-bdbc-93cf4b197b56
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Create COS
        2. Adding index for nsRole is rejected
        3. Adding index for COS attribute is rejected
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    # Create COS:  add container, create template, and definition
    nsContainer(topo.standalone, f'cn=cosClassicTemplates,{DEFAULT_SUFFIX}').create(properties={'cn': 'cosClassicTemplates'})
    properties = {'employeeType': 'EngType',
                  'cn': '"cn=filterRoleEngRole,dc=example,dc=com",cn=cosClassicTemplates,dc=example,dc=com'
                  }
    CosTemplate(topo.standalone,
                'cn="cn=filterRoleEngRole,dc=example,dc=com",cn=cosClassicTemplates,{}'.format(
                    DEFAULT_SUFFIX)) \
        .create(properties=properties)
    properties = {'cosTemplateDn': 'cn=cosClassicTemplate,{}'.format(DEFAULT_SUFFIX),
                  'cosAttribute': 'employeeType',
                  'cosSpecifier': 'nsrole',
                  'cn': 'cosClassicGenerateEmployeeTypeUsingnsrole'}
    CosClassicDefinition(topo.standalone, 'cn=cosClassicGenerateEmployeeTypeUsingnsrole,{}'.format(DEFAULT_SUFFIX)) \
        .create(properties=properties)

    # Test nsrole and cos attribute
    be_insts = Backends(topo.standalone).list()
    for be in be_insts:
        if be.get_attr_val_utf8_l('nsslapd-suffix') == DEFAULT_SUFFIX:
            # Attempt to add nsRole as index
            with pytest.raises(ValueError):
                be.add_index('nsrole', ['eq'])
            # Attempt to add COS attribute as index
            with pytest.raises(ValueError):
                be.add_index('employeeType', ['eq'])
            break

def test_task_status(topo):
    """Check that finished tasks have both a status and exit code

    :id: 56d03656-79a6-11ee-bfc3-482ae39447e5
    :setup: Standalone instance
    :steps:
        1. Start a Reindex task on 'cn' and wait until it is completed
        2. Check that task has a status
        3. Check that exit code is 0
        4. Start a Reindex task on 'badattr' and wait until it is completed
        5. Check that task has a status
        6. Check that exit code is 0
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    tasks = Tasks(topo.standalone)
    # completed reindex tasks MUST have a status because freeipa check it.

    # Reindex 'cn'
    tasks.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname='cn',
        args={TASK_WAIT: True}
    )
    reindex_task = Task(topo.standalone, tasks.dn)
    assert reindex_task.status()
    assert reindex_task.get_exit_code() == 0

    # Reindex 'badattr'
    tasks.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname='badattr',
        args={TASK_WAIT: True}
    )
    reindex_task = Task(topo.standalone, tasks.dn)
    assert reindex_task.status()
    # Bad attribute are skipped without setting error code
    assert reindex_task.get_exit_code() == 0


def test_task_and_be(topo, add_backend_and_ldif_50K_users):
    """Check that backend is writable after finishing a tasks

    :id: 047869da-7a4d-11ee-895c-482ae39447e5
    :setup: Standalone instance + a second backend with 50K users
    :steps:
        1. Start an Import task and wait until it is completed
        2. Modify the suffix entry description
        3. Start a Reindex task on all attributes and wait until it is completed
        4. Modify the suffix entry description
        5. Start an Export task and wait until it is completed
        6. Modify the suffix entry description
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    tasks = Tasks(topo.standalone)
    user = UserAccount(topo.standalone, f'uid=user00001,ou=people,{SUFFIX2}')
    ldif_file = add_backend_and_ldif_50K_users

    # Import
    tasks.importLDIF(
        suffix=SUFFIX2,
        input_file=ldif_file,
        args={TASK_WAIT: True}
    ) == 0
    descval = 'test_task_and_be tc1'
    user.set('description', descval)
    assert user.get_attr_val_utf8_l('description') == descval

    # Reindex some attributes
    assert tasks.reindex(
        suffix=SUFFIX2,
        attrname=[ 'description', 'rdn', 'uid', 'cn', 'sn', 'badattr' ],
        args={TASK_WAIT: True}
    ) == 0
    descval = 'test_task_and_be tc2'
    user.set('description', descval)
    assert user.get_attr_val_utf8_l('description') == descval
    users = UserAccounts(topo.standalone, SUFFIX2, rdn=None)
    user = users.create(properties={
        'uid': 'user1',
        'sn': 'user1',
        'cn': 'user1',
        'uidNumber': '1001',
        'gidNumber': '1001',
        'homeDirectory': '/home/user1'
    })

    # Export
    assert tasks.exportLDIF(
        suffix=SUFFIX2,
        output_file=f'{ldif_file}2',
        args={TASK_WAIT: True}
    ) == 0
    descval = 'test_task_and_be tc3'
    user.set('description', descval)
    assert user.get_attr_val_utf8_l('description') == descval


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
