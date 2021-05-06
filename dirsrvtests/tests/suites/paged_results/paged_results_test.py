# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import socket
from random import sample

import pytest
from ldap.controls import SimplePagedResultsControl, GetEffectiveRightsControl
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389._constants import DN_LDBM, DN_DM, DEFAULT_SUFFIX

from lib389._controls import SSSRequestControl

from lib389.idm.user import UserAccounts
from lib389.idm.organization import Organization
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.backend import Backends

from lib389._mapped_object import DSLdapObject

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)

TEST_USER_PWD = 'simplepaged_test'

NEW_SUFFIX_1_NAME = 'test_parent'
NEW_SUFFIX_1 = 'o={}'.format(NEW_SUFFIX_1_NAME)
NEW_SUFFIX_2_NAME = 'child'
NEW_SUFFIX_2 = 'ou={},{}'.format(NEW_SUFFIX_2_NAME, NEW_SUFFIX_1)
NEW_BACKEND_1 = 'parent_base'
NEW_BACKEND_2 = 'child_base'

HOSTNAME = socket.getfqdn()
IP_ADDRESS = socket.gethostbyname(HOSTNAME)


@pytest.fixture(scope="module")
def create_user(topology_st, request):
    """User for binding operation"""

    log.info('Adding user simplepaged_test')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'simplepaged_test',
        'cn': 'simplepaged_test',
        'sn': 'simplepaged_test',
        'uidNumber': '1234',
        'gidNumber': '1234',
        'homeDirectory': '/home/simplepaged_test',
        'userPassword': TEST_USER_PWD,
    })

    # Now add the ACI so simplepage_test can read the users ...
    ACI_BODY = ensure_bytes('(targetattr= "uid || sn || dn")(version 3.0; acl "Allow read for user"; allow (read,search,compare) userdn = "ldap:///all";)')
    topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_REPLACE, 'aci', ACI_BODY)])

    def fin():
        log.info('Deleting user simplepaged_test')
        user.delete()

    request.addfinalizer(fin)

    return user

@pytest.fixture(scope="module")
def new_suffixes(topology_st):
    """Add two suffixes with backends, one is a parent
    of the another
    """

    log.info('Adding suffix:{} and backend: {}'.format(NEW_SUFFIX_1, NEW_BACKEND_1))

    bes = Backends(topology_st.standalone)

    bes.create(properties={
        'cn': 'NEW_BACKEND_1',
        'nsslapd-suffix': NEW_SUFFIX_1,
    })
    # Create the root objects with their ACI
    log.info('Adding ACI to allow our test user to search')
    ACI_TARGET = '(targetattr != "userPassword || aci")'
    ACI_ALLOW = '(version 3.0; acl "Enable anonymous access";allow (read, search, compare)'
    ACI_SUBJECT = '(userdn = "ldap:///anyone");)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    o_1 = Organization(topology_st.standalone, NEW_SUFFIX_1)
    o_1.create(properties={
        'o': NEW_SUFFIX_1_NAME,
        'aci': ACI_BODY,
    })

    log.info('Adding suffix:{} and backend: {}'.format(NEW_SUFFIX_2, NEW_BACKEND_2))
    be_2 = bes.create(properties={
        'cn': 'NEW_BACKEND_2',
        'nsslapd-suffix': NEW_SUFFIX_2,
    })

    # We have to adjust the MT to say that BE_1 is a parent.
    mt = be_2.get_mapping_tree()
    mt.set_parent(NEW_SUFFIX_1)

    ou_2 = OrganizationalUnit(topology_st.standalone, NEW_SUFFIX_2)
    ou_2.create(properties={
        'ou': NEW_SUFFIX_2_NAME
    })


def add_users(topology_st, users_num, suffix):
    """Add users to the default suffix

    Return the list of added user DNs.
    """

    users_list = []
    users = UserAccounts(topology_st.standalone, suffix, rdn=None)

    log.info('Adding %d users' % users_num)
    for num in sample(range(1000), users_num):
        num_ran = int(round(num))
        USER_NAME = 'test%05d' % num_ran

        user = users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': '%s' % num_ran,
            'gidNumber': '%s' % num_ran,
            'homeDirectory': '/home/%s' % USER_NAME,
            'mail': '%s@redhat.com' % USER_NAME,
            'userpassword': 'pass%s' % num_ran,
        })
        users_list.append(user)
    return users_list


def del_users(users_list):
    """Delete users with DNs from given list"""

    log.info('Deleting %d users' % len(users_list))
    for user in users_list:
        user.delete()


def change_conf_attr(topology_st, suffix, attr_name, attr_value):
    """Change configuration attribute in the given suffix.

    Returns previous attribute value.
    """

    entry = DSLdapObject(topology_st.standalone, suffix)

    attr_value_bck = entry.get_attr_val_bytes(attr_name)
    log.info('Set %s to %s. Previous value - %s. Modified suffix - %s.' % (
        attr_name, attr_value, attr_value_bck, suffix))
    if attr_value is None:
        entry.remove_all(attr_name)
    else:
        entry.replace(attr_name, attr_value)
    return attr_value_bck


def paged_search(conn, suffix, controls, search_flt, searchreq_attrlist):
    """Search at the DEFAULT_SUFFIX with ldap.SCOPE_SUBTREE
    using Simple Paged Control(should the first item in the
    list controls.
    Assert that no cookie left at the end.

    Return the list with results summarized from all pages.
    """

    pages = 0
    pctrls = []
    all_results = []
    req_pr_ctrl = controls[0]
    log.info('Running simple paged result search with - '
             'search suffix: {}; filter: {}; attr list {}; '
             'page_size = {}; controls: {}.'.format(suffix, search_flt,
                                                    searchreq_attrlist,
                                                    req_pr_ctrl.size,
                                                    str(controls)))
    msgid = conn.search_ext(suffix, ldap.SCOPE_SUBTREE, search_flt, searchreq_attrlist, serverctrls=controls)
    while True:
        log.info('Getting page %d' % (pages,))
        rtype, rdata, rmsgid, rctrls = conn.result3(msgid)
        log.debug('Data: {}'.format(rdata))
        all_results.extend(rdata)
        pages += 1
        pctrls = [
            c
            for c in rctrls
            if c.controlType == SimplePagedResultsControl.controlType
            ]

        if pctrls:
            if pctrls[0].cookie:
                # Copy cookie from response control to request control
                log.debug('Cookie: {}'.format(pctrls[0].cookie))
                req_pr_ctrl.cookie = pctrls[0].cookie
                msgid = conn.search_ext(suffix, ldap.SCOPE_SUBTREE, search_flt, searchreq_attrlist, serverctrls=controls)
            else:
                break  # No more pages available
        else:
            break

    assert not pctrls[0].cookie
    return all_results


@pytest.mark.parametrize("page_size,users_num", [(6, 5), (5, 5), (5, 25)])
def test_search_success(topology_st, create_user, page_size, users_num):
    """Verify that search with a simple paged results control
    returns all entries it should without errors.

    :id: ddd15b70-64f1-4a85-a793-b24761e50354
    :parametrized: yes
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Bind as test user
        2. Search through added users with a simple paged control
    :expectedresults:
        1. Bind should be successful
        2. All users should be found
    """

    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    log.info('Set user bind %s ' % create_user)
    conn = create_user.bind(TEST_USER_PWD)

    req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
    all_results = paged_search(conn, DEFAULT_SUFFIX, [req_ctrl], search_flt, searchreq_attrlist)

    log.info('%d results' % len(all_results))
    assert len(all_results) == len(users_list)

    del_users(users_list)


@pytest.mark.parametrize("page_size,users_num,suffix,attr_name,attr_value,expected_err", [
    (50, 200, 'cn=config,%s' % DN_LDBM, 'nsslapd-idlistscanlimit', '100',
     ldap.UNWILLING_TO_PERFORM),
    (5, 15, DN_CONFIG, 'nsslapd-timelimit', '20',
     ldap.UNAVAILABLE_CRITICAL_EXTENSION),
    (21, 50, DN_CONFIG, 'nsslapd-sizelimit', '20',
     ldap.SIZELIMIT_EXCEEDED),
    (21, 50, DN_CONFIG, 'nsslapd-pagedsizelimit', '5',
     ldap.SIZELIMIT_EXCEEDED),
    (5, 50, 'cn=config,%s' % DN_LDBM, 'nsslapd-lookthroughlimit', '20',
     ldap.ADMINLIMIT_EXCEEDED)])
def test_search_limits_fail(topology_st, create_user, page_size, users_num,
                            suffix, attr_name, attr_value, expected_err):
    """Verify that search with a simple paged results control
    throws expected exceptoins when corresponding limits are
    exceeded.

    :id: e3067107-bd6d-493d-9989-3e641a9337b0
    :parametrized: yes
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Bind as test user
        2. Set limit attribute to the value that will cause
           an expected exception
        3. Search through added users with a simple paged control
    :expectedresults:
        1. Bind should be successful
        2. Operation should be successful
        3. Should fail with appropriate exception
    """

    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    attr_value_bck = change_conf_attr(topology_st, suffix, attr_name, attr_value)
    conf_param_dict = {attr_name: attr_value}
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    controls = []

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls.append(req_ctrl)
        if attr_name == 'nsslapd-idlistscanlimit':
            sort_ctrl = SSSRequestControl(True, ['sn'])
            controls.append(sort_ctrl)
        log.info('Initiate ldapsearch with created control instance')
        msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                search_flt, searchreq_attrlist, serverctrls=controls)

        time_val = conf_param_dict.get('nsslapd-timelimit')
        if time_val:
            time.sleep(int(time_val) + 10)

        pages = 0
        all_results = []
        pctrls = []
        while True:
            log.info('Getting page %d' % (pages,))
            if pages == 0 and (time_val or attr_name == 'nsslapd-pagesizelimit'):
                rtype, rdata, rmsgid, rctrls = conn.result3(msgid)
            else:
                with pytest.raises(expected_err):
                    rtype, rdata, rmsgid, rctrls = conn.result3(msgid)
                    all_results.extend(rdata)
                    pages += 1
                    pctrls = [
                        c
                        for c in rctrls
                        if c.controlType == SimplePagedResultsControl.controlType
                        ]

            if pctrls:
                if pctrls[0].cookie:
                    # Copy cookie from response control to request control
                    req_ctrl.cookie = pctrls[0].cookie
                    msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                            search_flt, searchreq_attrlist, serverctrls=controls)
                else:
                    break  # No more pages available
            else:
                break
    finally:
        del_users(users_list)
        change_conf_attr(topology_st, suffix, attr_name, attr_value_bck)


def test_search_sort_success(topology_st, create_user):
    """Verify that search with a simple paged results control
    and a server side sort control returns all entries
    it should without errors.

    :id: 17d8b150-ed43-41e1-b80f-ee9b4ce45155
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Bind as test user
        2. Search through added users with a simple paged control
           and a server side sort control
    :expectedresults:
        1. Bind should be successful
        2. All users should be found and sorted
    """

    users_num = 50
    page_size = 5
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        conn = create_user.bind(TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        sort_ctrl = SSSRequestControl(True, ['sn'])

        log.info('Initiate ldapsearch with created control instance')
        log.info('Collect data with sorting')
        controls = [req_ctrl, sort_ctrl]
        results_sorted = paged_search(conn, DEFAULT_SUFFIX, controls,
                                      search_flt, searchreq_attrlist)

        log.info('Substring numbers from user DNs')
        # r_nums = map(lambda x: int(x[0][8:13]), results_sorted)
        r_nums = [int(x[0][8:13]) for x in results_sorted]

        log.info('Assert that list is sorted')
        assert all(r_nums[i] <= r_nums[i + 1] for i in range(len(r_nums) - 1))
    finally:
        del_users(users_list)


def test_search_abandon(topology_st, create_user):
    """Verify that search with simple paged results control
    can be abandon

    :id: 0008538b-7585-4356-839f-268828066978
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Bind as test user
        2. Search through added users with a simple paged control
        3. Abandon the search
    :expectedresults:
        1. Bind should be successful
        2. Search should be started successfully
        3. It should throw an ldap.TIMEOUT exception
           while trying to get the rest of the search results
    """

    users_num = 10
    page_size = 2
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        log.info('Initiate a search with a paged results control')
        msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                search_flt, searchreq_attrlist, serverctrls=controls)
        log.info('Abandon the search')
        conn.abandon(msgid)

        log.info('Expect an ldap.TIMEOUT exception, while trying to get the search results')
        with pytest.raises(ldap.TIMEOUT):
            conn.result3(msgid, timeout=5)
    finally:
        del_users(users_list)


def test_search_with_timelimit(topology_st, create_user):
    """Verify that after performing multiple simple paged searches
    to completion, each with a timelimit, it wouldn't fail, if we sleep
    for a time more than the timelimit.

    :id: 6cd7234b-136c-419f-bf3e-43aa73592cff
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Bind as test user
        2. Search through added users with a simple paged control
           and timelimit set to 5
        3. When the returned cookie is empty, wait 10 seconds
        4. Perform steps 2 and 3 three times in a row
    :expectedresults:
        1. Bind should be successful
        2. No error should happen
        3. 10 seconds should pass
        4. No error should happen
    """

    users_num = 100
    page_size = 50
    timelimit = 5
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        for ii in range(3):
            log.info('Iteration %d' % ii)
            msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, search_flt,
                                    searchreq_attrlist, serverctrls=controls, timeout=timelimit)

            pages = 0
            pctrls = []
            while True:
                log.info('Getting page %d' % (pages,))
                rtype, rdata, rmsgid, rctrls = conn.result3(msgid)
                pages += 1
                pctrls = [
                    c
                    for c in rctrls
                    if c.controlType == SimplePagedResultsControl.controlType
                    ]

                if pctrls:
                    if pctrls[0].cookie:
                        # Copy cookie from response control to request control
                        req_ctrl.cookie = pctrls[0].cookie
                        msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, search_flt,
                                                searchreq_attrlist, serverctrls=controls, timeout=timelimit)
                    else:
                        log.info('Done with this search - sleeping %d seconds' % (
                            timelimit * 2))
                        time.sleep(timelimit * 2)
                        break  # No more pages available
                else:
                    break
    finally:
        del_users(users_list)

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
@pytest.mark.parametrize('aci_subject',
                         ('dns = "{}"'.format(HOSTNAME),
                          'ip = "{}"'.format(IP_ADDRESS)),
                          ids=['fqdn','ip'])
def test_search_dns_ip_aci(topology_st, create_user, aci_subject):
    """Verify that after performing multiple simple paged searches
    to completion on the suffix with DNS or IP based ACI

    :id: bbfddc46-a8c8-49ae-8c90-7265d05b22a9
    :parametrized: yes
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Back up and remove all previous ACI from suffix
        2. Add an anonymous ACI for DNS check
        3. Bind as test user
        4. Search through added users with a simple paged control
        5. Perform steps 4 three times in a row
        6. Return ACI to the initial state
        7. Go through all steps once again, but use IP subject dn
           instead of DNS
    :expectedresults:
        1. Operation should be successful
        2. Anonymous ACI should be successfully added
        3. Bind should be successful
        4. No error happens, all users should be found and sorted
        5. Results should remain the same
        6. ACI should be successfully returned
        7. Results should be the same with ACI with IP subject dn
    """

    users_num = 100
    page_size = 5
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Back up current suffix ACI')
        acis_bck = topology_st.standalone.aci.list(DEFAULT_SUFFIX, ldap.SCOPE_BASE)

        log.info('Add test ACI')
        ACI_TARGET = '(targetattr != "userPassword")'
        ACI_ALLOW = '(version 3.0;acl "Anonymous access within domain"; allow (read,compare,search)'
        ACI_SUBJECT = '(userdn = "ldap:///anyone") and (%s);)' % aci_subject
        ACI_BODY = ensure_bytes(ACI_TARGET + ACI_ALLOW + ACI_SUBJECT)
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_REPLACE, 'aci', ACI_BODY)])
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD, uri=f'ldap://{IP_ADDRESS}:{topology_st.standalone.port}')

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        log.info('Initiate three searches with a paged results control')
        for ii in range(3):
            log.info('%d search' % (ii + 1))
            all_results = paged_search(conn, DEFAULT_SUFFIX, controls,
                                       search_flt, searchreq_attrlist)
            log.info('%d results' % len(all_results))
            assert len(all_results) == len(users_list)
        log.info('If we are here, then no error has happened. We are good.')

    finally:
        log.info('Restore ACI')
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', None)])
        for aci in acis_bck:
            topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', aci.getRawAci())])
        del_users(users_list)


def test_search_multiple_paging(topology_st, create_user):
    """Verify that after performing multiple simple paged searches
    on a single connection without a complition, it wouldn't fail.

    :id: 628b29a6-2d47-4116-a88d-00b87405ef7f
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Bind as test user
        2. Initiate the search with a simple paged control
        3. Acquire the returned cookie only one time
        4. Perform steps 2 and 3 three times in a row
    :expectedresults:
        1. Bind should be successful
        2. Search should be successfully initiated
        3. Cookie should be successfully acquired
        4. No error happens
    """

    users_num = 100
    page_size = 30
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        for ii in range(3):
            log.info('Iteration %d' % ii)
            msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                    search_flt, searchreq_attrlist, serverctrls=controls)
            rtype, rdata, rmsgid, rctrls = conn.result3(msgid)
            pctrls = [
                c
                for c in rctrls
                if c.controlType == SimplePagedResultsControl.controlType
                ]

            # Copy cookie from response control to request control
            req_ctrl.cookie = pctrls[0].cookie
            msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                    search_flt, searchreq_attrlist, serverctrls=controls)
    finally:
        del_users(users_list)


@pytest.mark.parametrize("invalid_cookie", [1000, -1])
def test_search_invalid_cookie(topology_st, create_user, invalid_cookie):
    """Verify that using invalid cookie while performing
    search with the simple paged results control throws
    a TypeError exception

    :id: 107be12d-4fe4-47fe-ae86-f3e340a56f42
    :parametrized: yes
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Bind as test user
        2. Initiate the search with a simple paged control
        3. Put an invalid cookie (-1, 1000) to the control
        4. Continue the search
    :expectedresults:
        1. Bind should be successful
        2. Search should be successfully initiated
        3. Cookie should be added
        4. It should throw a TypeError exception
    """

    users_num = 100
    page_size = 50
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                search_flt, searchreq_attrlist, serverctrls=controls)
        rtype, rdata, rmsgid, rctrls = conn.result3(msgid)

        log.info('Put an invalid cookie (%d) to the control. TypeError is expected' %
                 invalid_cookie)
        req_ctrl.cookie = invalid_cookie
        with pytest.raises(TypeError):
            msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                    search_flt, searchreq_attrlist, serverctrls=controls)
    finally:
        del_users(users_list)


def test_search_abandon_with_zero_size(topology_st, create_user):
    """Verify that search with simple paged results control
    can be abandon using page_size = 0

    :id: d2fd9a10-84e1-4b69-a8a7-36ca1427c171
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Bind as test user
        2. Search through added users with a simple paged control
           and page_size = 0
    :expectedresults:
        1. Bind should be successful
        2. No cookie should be returned at all
    """

    users_num = 10
    page_size = 0
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                search_flt, searchreq_attrlist, serverctrls=controls)
        rtype, rdata, rmsgid, rctrls = conn.result3(msgid)
        pctrls = [
            c
            for c in rctrls
            if c.controlType == SimplePagedResultsControl.controlType
            ]
        assert not pctrls[0].cookie
    finally:
        del_users(users_list)


def test_search_pagedsizelimit_success(topology_st, create_user):
    """Verify that search with a simple paged results control
    returns all entries it should without errors while
    valid value set to nsslapd-pagedsizelimit.

    :id: 88193f10-f6f0-42f5-ae9c-ff34b8f9ee8c
    :setup: Standalone instance, test user for binding,
            10 users for the search base
    :steps:
        1. Set nsslapd-pagedsizelimit: 20
        2. Bind as test user
        3. Search through added users with a simple paged control
           using page_size = 10
    :expectedresults:
        1. nsslapd-pagedsizelimit should be successfully set
        2. Bind should be successful
        3. All users should be found
    """

    users_num = 10
    page_size = 10
    attr_name = 'nsslapd-pagedsizelimit'
    attr_value = '20'
    attr_value_bck = change_conf_attr(topology_st, DN_CONFIG, attr_name, attr_value)
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        all_results = paged_search(conn, DEFAULT_SUFFIX, controls, search_flt, searchreq_attrlist)

        log.info('%d results' % len(all_results))
        assert len(all_results) == len(users_list)

    finally:
        del_users(users_list)
        change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-pagedsizelimit', attr_value_bck)


@pytest.mark.parametrize('conf_attr,user_attr,expected_rs',
                         (('5', '15', 'PASS'), ('15', '5', ldap.SIZELIMIT_EXCEEDED)))
def test_search_nspagedsizelimit(topology_st, create_user,
                                 conf_attr, user_attr, expected_rs):
    """Verify that nsPagedSizeLimit attribute overrides
    nsslapd-pagedsizelimit while performing search with
    the simple paged results control.

    :id: b08c6ad2-ba28-447a-9f04-5377c3661d0d
    :parametrized: yes
    :setup: Standalone instance, test user for binding,
            10 users for the search base
    :steps:
        1. Set nsslapd-pagedsizelimit: 5
        2. Set nsPagedSizeLimit: 15
        3. Bind as test user
        4. Search through added users with a simple paged control
           using page_size = 10
        5. Bind as Directory Manager
        6. Restore all values
        7. Set nsslapd-pagedsizelimit: 15
        8. Set nsPagedSizeLimit: 5
        9. Bind as test user
        10. Search through added users with a simple paged control
            using page_size = 10
    :expectedresults:
        1. nsslapd-pagedsizelimit should be successfully set
        2. nsPagedSizeLimit should be successfully set
        3. Bind should be successful
        4. No error happens, all users should be found
        5. Bind should be successful
        6. All values should be restored
        7. nsslapd-pagedsizelimit should be successfully set
        8. nsPagedSizeLimit should be successfully set
        9. Bind should be successful
        10. It should throw SIZELIMIT_EXCEEDED exception
    """

    users_num = 10
    page_size = 10
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    conf_attr_bck = change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-pagedsizelimit', conf_attr)
    user_attr_bck = change_conf_attr(topology_st, create_user.dn, 'nsPagedSizeLimit', user_attr)

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        if expected_rs == ldap.SIZELIMIT_EXCEEDED:
            log.info('Expect to fail with SIZELIMIT_EXCEEDED')
            with pytest.raises(expected_rs):
                all_results = paged_search(conn, DEFAULT_SUFFIX, controls, search_flt, searchreq_attrlist)
        elif expected_rs == 'PASS':
            log.info('Expect to pass')
            all_results = paged_search(conn, DEFAULT_SUFFIX, controls, search_flt, searchreq_attrlist)
            log.info('%d results' % len(all_results))
            assert len(all_results) == len(users_list)

    finally:
        del_users(users_list)
        change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-pagedsizelimit', conf_attr_bck)
        change_conf_attr(topology_st, create_user.dn, 'nsPagedSizeLimit', user_attr_bck)


@pytest.mark.parametrize('conf_attr_values,expected_rs',
                         ((('5000', '100', '100'), ldap.ADMINLIMIT_EXCEEDED),
                          (('5000', '120', '122'), 'PASS')))
def test_search_paged_limits(topology_st, create_user, conf_attr_values, expected_rs):
    """Verify that nsslapd-idlistscanlimit and
    nsslapd-lookthroughlimit can limit the administrator
    search abilities.

    :id: e0f8b916-7276-4bd3-9e73-8696a4468811
    :parametrized: yes
    :setup: Standalone instance, test user for binding,
            10 users for the search base
    :steps:
        1. Set nsslapd-sizelimit and nsslapd-pagedsizelimit to 5000
        2. Set nsslapd-idlistscanlimit: 120
        3. Set nsslapd-lookthroughlimit: 122
        4. Bind as test user
        5. Search through added users with a simple paged control
           using page_size = 10
        6. Bind as Directory Manager
        7. Set nsslapd-idlistscanlimit: 100
        8. Set nsslapd-lookthroughlimit: 100
        9. Bind as test user
        10. Search through added users with a simple paged control
            using page_size = 10
    :expectedresults:
        1. nsslapd-sizelimit and nsslapd-pagedsizelimit
           should be successfully set
        2. nsslapd-idlistscanlimit should be successfully set
        3. nsslapd-lookthroughlimit should be successfully set
        4. Bind should be successful
        5. No error happens, all users should be found
        6. Bind should be successful
        7. nsslapd-idlistscanlimit should be successfully set
        8. nsslapd-lookthroughlimit should be successfully set
        9. Bind should be successful
        10. It should throw ADMINLIMIT_EXCEEDED exception
    """

    users_num = 101
    page_size = 10
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    size_attr_bck = change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-sizelimit', conf_attr_values[0])
    pagedsize_attr_bck = change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-pagedsizelimit', conf_attr_values[0])
    idlistscan_attr_bck = change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM, 'nsslapd-idlistscanlimit', conf_attr_values[1])
    lookthrough_attr_bck = change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM, 'nsslapd-lookthroughlimit', conf_attr_values[2])

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        if expected_rs == ldap.ADMINLIMIT_EXCEEDED:
            log.info('Expect to fail with ADMINLIMIT_EXCEEDED')
            with pytest.raises(expected_rs):
                all_results = paged_search(conn, DEFAULT_SUFFIX, controls, search_flt, searchreq_attrlist)
        elif expected_rs == 'PASS':
            log.info('Expect to pass')
            all_results = paged_search(conn, DEFAULT_SUFFIX, controls, search_flt, searchreq_attrlist)
            log.info('%d results' % len(all_results))
            assert len(all_results) == len(users_list)
    finally:
        del_users(users_list)
        change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-sizelimit', size_attr_bck)
        change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-pagedsizelimit', pagedsize_attr_bck)
        change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM, 'nsslapd-lookthroughlimit', lookthrough_attr_bck)
        change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM, 'nsslapd-idlistscanlimit', idlistscan_attr_bck)


@pytest.mark.parametrize('conf_attr_values,expected_rs',
                         ((('1000', '100', '100'), ldap.ADMINLIMIT_EXCEEDED),
                          (('1000', '120', '122'), 'PASS')))
def test_search_paged_user_limits(topology_st, create_user, conf_attr_values, expected_rs):
    """Verify that nsPagedIDListScanLimit and nsPagedLookthroughLimit
    override nsslapd-idlistscanlimit and nsslapd-lookthroughlimit
    while performing search with the simple paged results control.

    :id: 69e393e9-1ab8-4f4e-b4a1-06ca63dc7b1b
    :parametrized: yes
    :setup: Standalone instance, test user for binding,
            10 users for the search base
    :steps:
        1. Set nsslapd-idlistscanlimit: 1000
        2. Set nsslapd-lookthroughlimit: 1000
        3. Set nsPagedIDListScanLimit: 120
        4. Set nsPagedLookthroughLimit: 122
        5. Bind as test user
        6. Search through added users with a simple paged control
           using page_size = 10
        7. Bind as Directory Manager
        8. Set nsPagedIDListScanLimit: 100
        9. Set nsPagedLookthroughLimit: 100
        10. Bind as test user
        11. Search through added users with a simple paged control
            using page_size = 10
    :expectedresults:
        1. nsslapd-idlistscanlimit should be successfully set
        2. nsslapd-lookthroughlimit should be successfully set
        3. nsPagedIDListScanLimit should be successfully set
        4. nsPagedLookthroughLimit should be successfully set
        5. Bind should be successful
        6. No error happens, all users should be found
        7. Bind should be successful
        8. nsPagedIDListScanLimit should be successfully set
        9. nsPagedLookthroughLimit should be successfully set
        10. Bind should be successful
        11. It should throw ADMINLIMIT_EXCEEDED exception
    """

    users_num = 101
    page_size = 10
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    lookthrough_attr_bck = change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM, 'nsslapd-lookthroughlimit', conf_attr_values[0])
    idlistscan_attr_bck = change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM, 'nsslapd-idlistscanlimit', conf_attr_values[0])
    user_idlistscan_attr_bck = change_conf_attr(topology_st, create_user.dn, 'nsPagedIDListScanLimit', conf_attr_values[1])
    user_lookthrough_attr_bck = change_conf_attr(topology_st, create_user.dn, 'nsPagedLookthroughLimit', conf_attr_values[2])

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        if expected_rs == ldap.ADMINLIMIT_EXCEEDED:
            log.info('Expect to fail with ADMINLIMIT_EXCEEDED')
            with pytest.raises(expected_rs):
                all_results = paged_search(conn, DEFAULT_SUFFIX, controls, search_flt, searchreq_attrlist)
        elif expected_rs == 'PASS':
            log.info('Expect to pass')
            all_results = paged_search(conn, DEFAULT_SUFFIX, controls, search_flt, searchreq_attrlist)
            log.info('%d results' % len(all_results))
            assert len(all_results) == len(users_list)
    finally:
        del_users(users_list)
        change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM, 'nsslapd-lookthroughlimit', lookthrough_attr_bck)
        change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM, 'nsslapd-idlistscanlimit', idlistscan_attr_bck)
        change_conf_attr(topology_st, create_user.dn, 'nsPagedIDListScanLimit', user_idlistscan_attr_bck)
        change_conf_attr(topology_st, create_user.dn, 'nsPagedLookthroughLimit', user_lookthrough_attr_bck)


def test_ger_basic(topology_st, create_user):
    """Verify that search with a simple paged results control
    and get effective rights control returns all entries
    it should without errors.

    :id: 7b0bdfc7-a2f2-4c1a-bcab-f1eb8b330d45
    :setup: Standalone instance, test user for binding,
            varying number of users for the search base
    :steps:
        1. Search through added users with a simple paged control
           and get effective rights control
    :expectedresults:
        1. All users should be found, every found entry should have
           an 'attributeLevelRights' returned
    """

    users_list = add_users(topology_st, 20, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    page_size = 4

    try:
        spr_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        ger_ctrl = GetEffectiveRightsControl(True, ensure_bytes("dn: " + DN_DM))

        all_results = paged_search(topology_st.standalone, DEFAULT_SUFFIX, [spr_ctrl, ger_ctrl],
                                   search_flt, searchreq_attrlist)

        log.info('{} results'.format(len(all_results)))
        assert len(all_results) == len(users_list)
        log.info('Check for attributeLevelRights')
        assert all(attrs['attributeLevelRights'][0] for dn, attrs in all_results)
    finally:
        log.info('Remove added users')
        del_users(users_list)


def test_multi_suffix_search(topology_st, create_user, new_suffixes):
    """Verify that page result search returns empty cookie
    if there is no returned entry.

    :id: 9712345b-9e38-4df6-8794-05f12c457d39
    :setup: Standalone instance, test user for binding,
            two suffixes with backends, one is inserted into another,
            10 users for the search base within each suffix
    :steps:
        1. Bind as test user
        2. Search through all 20 added users with a simple paged control
           using page_size = 4
        3. Wait some time for the logs to be updated
        4. Check access log
    :expectedresults:
        1. Bind should be successful
        2. All users should be found
        3. Some time should pass
        4. The access log should contain the pr_cookie for each page request
           and it should be equal 0, except the last one should be equal -1
    """

    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    page_size = 4
    users_num = 20

    log.info('Clear the access log')
    topology_st.standalone.deleteAccessLogs()

    users_list_1 = add_users(topology_st, 10, NEW_SUFFIX_1)
    users_list_2 = add_users(topology_st, 10, NEW_SUFFIX_2)

    try:
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

        all_results = paged_search(topology_st.standalone, NEW_SUFFIX_1, [req_ctrl], search_flt, searchreq_attrlist)

        log.info('{} results'.format(len(all_results)))
        assert len(all_results) == users_num

        log.info('Restart the server to flush the logs')
        topology_st.standalone.restart(timeout=10)

        access_log_lines = topology_st.standalone.ds_access_log.match('.*pr_cookie=.*')
        pr_cookie_list = ([line.rsplit('=', 1)[-1] for line in access_log_lines])
        pr_cookie_list = [int(pr_cookie) for pr_cookie in pr_cookie_list]
        log.info('Assert that last pr_cookie == -1 and others pr_cookie == 0')
        pr_cookie_zeros = list(pr_cookie == 0 for pr_cookie in pr_cookie_list[0:-1])
        assert all(pr_cookie_zeros)
        assert pr_cookie_list[-1] == -1
    finally:
        log.info('Remove added users')
        del_users(users_list_1)
        del_users(users_list_2)


@pytest.mark.parametrize('conf_attr_value', (None, '-1', '1000'))
def test_maxsimplepaged_per_conn_success(topology_st, create_user, conf_attr_value):
    """Verify that nsslapd-maxsimplepaged-per-conn acts according design

    :id: 192e2f25-04ee-4ff9-9340-d875dcbe8011
    :parametrized: yes
    :setup: Standalone instance, test user for binding,
            20 users for the search base
    :steps:
        1. Set nsslapd-maxsimplepaged-per-conn in cn=config
           to the next values: no value, -1, some positive
        2. Search through the added users with a simple paged control
           using page size = 4
    :expectedresults:
        1. nsslapd-maxsimplepaged-per-conn should be successfully set
        2. If no value or value = -1 - all users should be found,
           default behaviour; If the value is positive,
           the value is the max simple paged results requests per connection.
    """

    users_list = add_users(topology_st, 20, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    page_size = 4
    if conf_attr_value:
        max_per_con_bck = change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-maxsimplepaged-per-conn', conf_attr_value)

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

        all_results = paged_search(conn, DEFAULT_SUFFIX, [req_ctrl], search_flt, searchreq_attrlist)

        log.info('{} results'.format(len(all_results)))
        assert len(all_results) == len(users_list)
    finally:
        log.info('Remove added users')
        del_users(users_list)
        if conf_attr_value:
            change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-maxsimplepaged-per-conn', max_per_con_bck)


@pytest.mark.parametrize('conf_attr_value', ('0', '1'))
def test_maxsimplepaged_per_conn_failure(topology_st, create_user, conf_attr_value):
    """Verify that nsslapd-maxsimplepaged-per-conn acts according design

    :id: eb609e63-2829-4331-8439-a35f99694efa
    :parametrized: yes
    :setup: Standalone instance, test user for binding,
            20 users for the search base
    :steps:
        1. Set nsslapd-maxsimplepaged-per-conn = 0 in cn=config
        2. Search through the added users with a simple paged control
           using page size = 4
        3. Set nsslapd-maxsimplepaged-per-conn = 1 in cn=config
        4. Search through the added users with a simple paged control
           using page size = 4 two times, but don't close the connections
    :expectedresults:
        1. nsslapd-maxsimplepaged-per-conn should be successfully set
        2. UNWILLING_TO_PERFORM should be thrown
        3. Bind should be successful
        4. UNWILLING_TO_PERFORM should be thrown
    """

    users_list = add_users(topology_st, 20, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    page_size = 4
    max_per_con_bck = change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-maxsimplepaged-per-conn', conf_attr_value)

    try:
        log.info('Set user bind')
        conn = create_user.bind(TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                    search_flt, searchreq_attrlist, serverctrls=[req_ctrl])
            rtype, rdata, rmsgid, rctrls = conn.result3(msgid)

            # If nsslapd-maxsimplepaged-per-conn = 1,
            # it should pass this point, but failed on the next search
            assert conf_attr_value == '1'
            msgid = conn.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                    search_flt, searchreq_attrlist, serverctrls=[req_ctrl])
            rtype, rdata, rmsgid, rctrls = conn.result3(msgid)
    finally:
        log.info('Remove added users')
        del_users(users_list)
        change_conf_attr(topology_st, DN_CONFIG, 'nsslapd-maxsimplepaged-per-conn', max_per_con_bck)

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
