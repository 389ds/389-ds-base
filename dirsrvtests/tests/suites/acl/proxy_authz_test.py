# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# Proxy authorization with subtree ACIs and the same uid values under two subtrees.
import ldap
import pytest
import logging
from ldap.controls.simple import ProxyAuthzControl
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PW_DM
from lib389.idm.domain import Domain
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts
from lib389.utils import ds_is_older, ensure_bytes
from lib389.idm.directorymanager import DirectoryManager

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

pytestmark = [
    pytest.mark.tier1,
    pytest.mark.skipif(
        ds_is_older("1.3.5"),
        reason="Proxy ACI with authorization identity (not on older 389)",
    ),
]


@pytest.fixture(scope="module")
def testuser(topo, request):
    """Create a test user for the tests."""
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    testuser = users.create(properties={
        'uid': 'test',
        'cn': 'test',
        'sn': 'test',
        'uidNumber': '1001',
        'gidNumber': '2001',
        'homeDirectory': '/home/test',
        'userPassword': PW_DM
        })
    log.info(f"Created test user: {testuser.dn}")

    def _cleanup():
        testuser.delete()
        log.info(f"Cleanup: removed {testuser.dn}")
    
    request.addfinalizer(_cleanup)
    return testuser


@pytest.fixture(scope="module")
def proxyuser(topo, request):
    """Create a proxy user for the tests."""
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    proxyuser = users.create(properties={
        'uid': 'proxy',
        'cn': 'proxy',
        'sn': 'proxy',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/proxy',
        'userPassword': PW_DM
        })
    log.info(f"Created proxy user: {proxyuser.dn}")

    def _cleanup():
        proxyuser.delete()
        log.info(f"Cleanup: removed {proxyuser.dn}")
    
    request.addfinalizer(_cleanup)
    return proxyuser


@pytest.fixture(scope="module")
def setup(request, topo, testuser, proxyuser):
    """Setup the test environment."""
    inst = topo.standalone

    # Create OUs
    ous = OrganizationalUnits(inst, DEFAULT_SUFFIX)
    ou_objects = {}
    for ou in ('green', 'red'):
        if ous.exists(dn=f'ou={ou},{DEFAULT_SUFFIX}'):
            ou_objects[ou] = ous.get(dn=f'ou={ou},{DEFAULT_SUFFIX}')
        else:
            orgunit = ous.create(properties={'ou': ou})
            ou_objects[ou] = orgunit

    # Backup and remove default acis
    dm = DirectoryManager(inst)
    dm.rebind()
    domain = Domain(inst, DEFAULT_SUFFIX)
    saved_acis = list(domain.get_attr_vals("aci"))
    domain.remove_all("aci")
    log.info(f"Removed {len(saved_acis)} default acis")

    # Create acis
    aci_target = f'(target = "ldap:///{ou_objects["green"].dn}")'
    aci_targetattr = (
        '(targetattr = "objectclass || cn || sn || uid || givenname ")'
    )

    # aci for search-read to green subtree for test user
    aci_body = (
        aci_target + aci_targetattr + f'(version 3.0; acl "Allow search-read to green subtree"; allow (read, search, compare) userdn = "ldap:///{testuser.dn.lower()}";)'
    )
    domain.add("aci", aci_body)
    log.info(f"Created aci: {aci_body}")

    # aci for use of proxy auth to green subtree for proxy user
    aci_body = (
        aci_target + aci_targetattr + f'(version 3.0; acl "Allow use of proxy auth to green subtree"; allow (proxy) userdn = "ldap:///{proxyuser.dn}";)'
    )
    domain.add("aci", aci_body)
    log.info(f"Created aci: {aci_body}")

    # Create test users in the subtrees
    test_users = []
    for idx in range(2):
        for subtree in ou_objects.values():
            users = UserAccounts(inst, DEFAULT_SUFFIX, rdn=f'ou={subtree.rdn}')
            user = users.create(properties={
                'uid': f'test{idx}',
                'cn': f'test{idx}',
                'sn': f'test{idx}',
                'uidNumber': f'{2000 + idx}',
                'gidNumber': '100',
                'homeDirectory': f'/home/test{idx}',
                'userPassword': PW_DM,
                'givenName': 'test',
                'mail': f'test{idx}@example.com',
                'description': 'description',
                'employeeNumber': f'{idx}',
                'telephoneNumber': f'{idx}{idx}{idx}',
                'mobile': f'{idx}{idx}{idx}',
                'l': 'MV',
                'title': 'Engineer'
            })
            test_users.append(user)
            log.info(f"Created test user: {user.dn}")

    def _cleanup():
        dm.rebind()
        domain.remove_all("aci")
        for aci in saved_acis:
            domain.add("aci", aci)
        log.info(f"Cleanup: restored {len(saved_acis)} acis")

        for user in test_users:
            user.delete()
            log.info(f"Cleanup: removed {user.dn}")
    
        for orgunit in ou_objects.values():
            orgunit.delete()
            log.info(f"Cleanup: removed {orgunit.dn}")
    
    request.addfinalizer(_cleanup)


@pytest.mark.parametrize("user_name,expect", [
    ('test', True),
    ('proxy', False),
])
def test_proxy_authz_search_user(topo, setup, user_name, expect):
    """Verify direct search visibility for regular and proxy users.

    :id: c1c04f2e-e3ab-4204-a738-cdba8b5311e0
    :parametrized: yes
    :setup: Standalone instance with green/red OUs, test/proxy users, and proxy-related ACIs
    :steps:
        1. Bind as the selected user.
        2. Search the suffix subtree for uid=test1.
    :expectedresults:
        1. Bind operation succeeds.
        2. The entry is found if the user is allowed to search, otherwise error is raised.
    """
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).get(user_name)
    user.rebind(password=PW_DM)

    if expect:
        UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=green').get('test1')
    else:
        with pytest.raises(ldap.NO_SUCH_OBJECT):
            UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn='ou=green').get('test1')


@pytest.mark.parametrize("subtree, expect", [('green', True), ('red', False)])
def test_proxy_authz_search_with_proxy_auth_as_test_user(topo, setup, testuser, proxyuser, subtree, expect):
    """Verify proxy user can search as test user with proxy auth control.

    :id: 31d22f3e-14ab-454f-9530-c75fe6e0f5ff
    :setup: Standalone instance with green/red OUs, test/proxy users, and proxy-related ACIs
    :steps:
        1. Create a ProxyAuthz control authorizing as the test user DN.
        2. Bind as the proxy user.
        3. Search the suffix subtree for uid=test1 using the proxy auth control.
    :expectedresults:
        1. Proxy authorization control is accepted.
        2. Bind as proxy user succeeds.
        3. Exactly one entry is returned, matching test-user access to the green subtree.
    """
    proxy_ctrl = ProxyAuthzControl(
        criticality=True, authzId=ensure_bytes(f"dn: {testuser.dn}")
    )

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=f'ou={subtree}')
    backup_controls = users._server_controls
    try:
        users._server_controls = [proxy_ctrl]

        proxyuser.bind(password=PW_DM)
        if expect:
            users.get('test1')
        else:
            with pytest.raises(ldap.NO_SUCH_OBJECT):
                users.get('test1')
    finally:
        users._server_controls = backup_controls


@pytest.mark.parametrize("subtree", ['green', 'red'])
def test_proxy_authz_search_dm(topo, setup, subtree):
    """Verify Directory Manager search returns all matching entries.

    :id: b705f745-d378-4891-8890-5f3e9ef24f17
    :setup: Standalone instance with green/red OUs, test/proxy users, and proxy-related ACIs
    :steps:
        1. Bind as Directory Manager.
        2. Search the suffix subtree for uid=test1.
    :expectedresults:
        1. Bind as Directory Manager succeeds.
        2. Two matching entries are returned from both test subtrees.
    """
    dm = DirectoryManager(topo.standalone, DN_DM)
    dm.rebind(password=PW_DM)

    UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=f'ou={subtree}').get('test1')


@pytest.mark.parametrize("user_name,subtree,expect", [
    ('test', 'green', True),
    ('proxy', 'green', False),
    ('test', 'red', False),
    ('proxy', 'red', False),
])
def test_proxy_authz_search_dm_auth_as_user(topo, setup, user_name, subtree, expect):
    """Verify Directory Manager proxy auth search as selected user identity.

    :id: c9493f75-c740-46f9-bfd8-d267fd85fdcd
    :parametrized: yes
    :setup: Standalone instance with green/red OUs, test/proxy users, and proxy-related ACIs
    :steps:
        1. Resolve the selected user DN.
        2. Create a ProxyAuthz control using that user DN.
        3. Bind as Directory Manager.
        4. Search the suffix subtree for uid=test1 with the proxy auth control.
    :expectedresults:
        1. User DN is resolved successfully.
        2. Proxy authorization control is accepted.
        3. Bind as Directory Manager succeeds.
        4. Returned entry count matches the expected result for the proxied identity.
    """
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX).get(user_name)

    proxy_ctrl = ProxyAuthzControl(
        criticality=True, authzId=ensure_bytes(f"dn: {user.dn}")
    )

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=f'ou={subtree}')
    backup_controls = users._server_controls
    try:
        users._server_controls = [proxy_ctrl]

        dm = DirectoryManager(topo.standalone, DN_DM)
        dm.rebind(password=PW_DM)

        if expect:
            users.get('test1')
        else:
            with pytest.raises(ldap.NO_SUCH_OBJECT):
                users.get('test1')
    finally:
        users._server_controls = backup_controls