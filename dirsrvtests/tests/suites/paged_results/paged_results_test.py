# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from random import sample

import pytest
from ldap.controls import SimplePagedResultsControl, GetEffectiveRightsControl
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389._constants import DN_LDBM, DN_DM, DEFAULT_SUFFIX, BACKEND_NAME, PASSWORD

from sss_control import SSSRequestControl

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)

TEST_USER_NAME = 'simplepaged_test'
TEST_USER_DN = 'uid={},{}'.format(TEST_USER_NAME, DEFAULT_SUFFIX)
TEST_USER_PWD = 'simplepaged_test'
NEW_SUFFIX_1_NAME = 'test_parent'
NEW_SUFFIX_1 = 'o={}'.format(NEW_SUFFIX_1_NAME)
NEW_SUFFIX_2_NAME = 'child'
NEW_SUFFIX_2 = 'ou={},{}'.format(NEW_SUFFIX_2_NAME, NEW_SUFFIX_1)
NEW_BACKEND_1 = 'parent_base'
NEW_BACKEND_2 = 'child_base'


@pytest.fixture(scope="module")
def test_user(topology_st, request):
    """User for binding operation"""

    log.info('Adding user {}'.format(TEST_USER_DN))
    try:
        topology_st.standalone.add_s(Entry((TEST_USER_DN, {
            'objectclass': 'top person'.split(),
            'objectclass': 'organizationalPerson',
            'objectclass': 'inetorgperson',
            'cn': TEST_USER_NAME,
            'sn': TEST_USER_NAME,
            'userpassword': TEST_USER_PWD,
            'mail': '%s@redhat.com' % TEST_USER_NAME,
            'uid': TEST_USER_NAME
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add user (%s): error (%s)' % (TEST_USER_DN,
                                                           e.message['desc']))
        raise e

    def fin():
        log.info('Deleting user {}'.format(TEST_USER_DN))
        topology_st.standalone.delete_s(TEST_USER_DN)

    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def new_suffixes(topology_st):
    """Add two suffixes with backends, one is a parent
    of the another
    """

    log.info('Adding suffix:{} and backend: {}'.format(NEW_SUFFIX_1, NEW_BACKEND_1))
    topology_st.standalone.backend.create(NEW_SUFFIX_1,
                                          {BACKEND_NAME: NEW_BACKEND_1})
    topology_st.standalone.mappingtree.create(NEW_SUFFIX_1,
                                              bename=NEW_BACKEND_1)
    try:
        topology_st.standalone.add_s(Entry((NEW_SUFFIX_1, {
            'objectclass': 'top',
            'objectclass': 'organization',
            'o': NEW_SUFFIX_1_NAME
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add suffix ({}): error ({})'.format(NEW_SUFFIX_1,
                                                                 e.message['desc']))
        raise

    log.info('Adding suffix:{} and backend: {}'.format(NEW_SUFFIX_2, NEW_BACKEND_2))
    topology_st.standalone.backend.create(NEW_SUFFIX_2,
                                          {BACKEND_NAME: NEW_BACKEND_2})
    topology_st.standalone.mappingtree.create(NEW_SUFFIX_2,
                                              bename=NEW_BACKEND_2,
                                              parent=NEW_SUFFIX_1)

    try:
        topology_st.standalone.add_s(Entry((NEW_SUFFIX_2, {
            'objectclass': 'top',
            'objectclass': 'organizationalunit',
            'ou': NEW_SUFFIX_2_NAME
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add suffix ({}): error ({})'.format(NEW_SUFFIX_2,
                                                                 e.message['desc']))
        raise

    log.info('Adding ACI to allow our test user to search')
    ACI_TARGET = '(targetattr != "userPassword || aci")'
    ACI_ALLOW = '(version 3.0; acl "Enable anonymous access";allow (read, search, compare)'
    ACI_SUBJECT = '(userdn = "ldap:///anyone");)'
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology_st.standalone.modify_s(NEW_SUFFIX_1, mod)


def add_users(topology_st, users_num, suffix):
    """Add users to the default suffix

    Return the list of added user DNs.
    """

    users_list = []
    log.info('Adding %d users' % users_num)
    for num in sample(range(1000), users_num):
        num_ran = int(round(num))
        USER_NAME = 'test%05d' % num_ran
        USER_DN = 'uid=%s,%s' % (USER_NAME, suffix)
        users_list.append(USER_DN)
        try:
            topology_st.standalone.add_s(Entry((USER_DN, {
                'objectclass': 'top person'.split(),
                'objectclass': 'organizationalPerson',
                'objectclass': 'inetorgperson',
                'cn': USER_NAME,
                'sn': USER_NAME,
                'userpassword': 'pass%s' % num_ran,
                'mail': '%s@redhat.com' % USER_NAME,
                'uid': USER_NAME})))
        except ldap.LDAPError as e:
            log.error('Failed to add user (%s): error (%s)' % (USER_DN,
                                                               e.message['desc']))
            raise e
    return users_list


def del_users(topology_st, users_list):
    """Delete users with DNs from given list"""

    log.info('Deleting %d users' % len(users_list))
    for user_dn in users_list:
        try:
            topology_st.standalone.delete_s(user_dn)
        except ldap.LDAPError as e:
            log.error('Failed to delete user (%s): error (%s)' % (user_dn,
                                                                  e.message['desc']))
            raise e


def change_conf_attr(topology_st, suffix, attr_name, attr_value):
    """Change configurational attribute in the given suffix.

    Returns previous attribute value.
    """

    try:
        entries = topology_st.standalone.search_s(suffix, ldap.SCOPE_BASE,
                                                  'objectclass=top',
                                                  [attr_name])
        attr_value_bck = entries[0].data.get(attr_name)
        log.info('Set %s to %s. Previous value - %s. Modified suffix - %s.' % (
            attr_name, attr_value, attr_value_bck, suffix))
        if attr_value is None:
            topology_st.standalone.modify_s(suffix, [(ldap.MOD_DELETE,
                                                      attr_name,
                                                      attr_value)])
        else:
            topology_st.standalone.modify_s(suffix, [(ldap.MOD_REPLACE,
                                                      attr_name,
                                                      attr_value)])
    except ldap.LDAPError as e:
        log.error('Failed to change attr value (%s): error (%s)' % (attr_name,
                                                                    e.message['desc']))
        raise e

    return attr_value_bck


def paged_search(topology_st, suffix, controls, search_flt, searchreq_attrlist):
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
    msgid = topology_st.standalone.search_ext(suffix,
                                              ldap.SCOPE_SUBTREE,
                                              search_flt,
                                              searchreq_attrlist,
                                              serverctrls=controls)
    while True:
        log.info('Getting page %d' % (pages,))
        rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)
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
                msgid = topology_st.standalone.search_ext(suffix,
                                                          ldap.SCOPE_SUBTREE,
                                                          search_flt,
                                                          searchreq_attrlist,
                                                          serverctrls=controls)
            else:
                break  # No more pages available
        else:
            break

    assert not pctrls[0].cookie
    return all_results


@pytest.mark.parametrize("page_size,users_num",
                         [(6, 5), (5, 5), (5, 25)])
def test_search_success(topology_st, test_user, page_size, users_num):
    """Verify that search with a simple paged results control
    returns all entries it should without errors.

    :id: ddd15b70-64f1-4a85-a793-b24761e50354
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Bind as test user
            2. Search through added users with a simple paged control
    :expectedresults: All users should be found
    """

    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

        all_results = paged_search(topology_st, DEFAULT_SUFFIX, [req_ctrl],
                                   search_flt, searchreq_attrlist)

        log.info('%d results' % len(all_results))
        assert len(all_results) == len(users_list)
    finally:
        log.info('Set Directory Manager bind back (test_search_success)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)


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
def test_search_limits_fail(topology_st, test_user, page_size, users_num,
                            suffix, attr_name, attr_value, expected_err):
    """Verify that search with a simple paged results control
    throws expected exceptoins when corresponding limits are
    exceeded.

    :id: e3067107-bd6d-493d-9989-3e641a9337b0
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Bind as test user
            2. Set limit attribute to the value that will cause
               an expected exception
            3. Search through added users with a simple paged control
    :expectedresults: Should fail with appropriate exception
    """

    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    attr_value_bck = change_conf_attr(topology_st, suffix, attr_name, attr_value)
    conf_param_dict = {attr_name: attr_value}
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    controls = []

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls.append(req_ctrl)
        if attr_name == 'nsslapd-idlistscanlimit':
            sort_ctrl = SSSRequestControl(True, ['sn'])
            controls.append(sort_ctrl)
        log.info('Initiate ldapsearch with created control instance')
        msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  search_flt,
                                                  searchreq_attrlist,
                                                  serverctrls=controls)

        time_val = conf_param_dict.get('nsslapd-timelimit')
        if time_val:
            time.sleep(int(time_val) + 10)

        pages = 0
        all_results = []
        pctrls = []
        while True:
            log.info('Getting page %d' % (pages,))
            if pages == 0 and (time_val or attr_name in ('nsslapd-lookthroughlimit',
                                                         'nsslapd-pagesizelimit')):
                rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)
            else:
                with pytest.raises(expected_err):
                    rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)
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
                    msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                              ldap.SCOPE_SUBTREE,
                                                              search_flt,
                                                              searchreq_attrlist,
                                                              serverctrls=controls)
                else:
                    break  # No more pages available
            else:
                break
    finally:
        log.info('Set Directory Manager bind back (test_search_limits_fail)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)
        change_conf_attr(topology_st, suffix, attr_name, attr_value_bck)


def test_search_sort_success(topology_st, test_user):
    """Verify that search with a simple paged results control
    and a server side sort control returns all entries
    it should without errors.

    :id: 17d8b150-ed43-41e1-b80f-ee9b4ce45155
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Bind as test user
            2. Search through added users with a simple paged control
               and a server side sort control
    :expectedresults: All users should be found and sorted
    """

    users_num = 50
    page_size = 5
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        sort_ctrl = SSSRequestControl(True, ['sn'])

        log.info('Initiate ldapsearch with created control instance')
        log.info('Collect data with sorting')
        controls = [req_ctrl, sort_ctrl]
        results_sorted = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                      search_flt, searchreq_attrlist)

        log.info('Substring numbers from user DNs')
        r_nums = map(lambda x: int(x[0][8:13]), results_sorted)

        log.info('Assert that list is sorted')
        assert all(r_nums[i] <= r_nums[i + 1] for i in range(len(r_nums) - 1))
    finally:
        log.info('Set Directory Manager bind back (test_search_sort_success)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)


def test_search_abandon(topology_st, test_user):
    """Verify that search with simple paged results control
    can be abandon

    :id: 0008538b-7585-4356-839f-268828066978
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Bind as test user
            2. Search through added users with a simple paged control
            3. Abandon the search
    :expectedresults: It will throw an ldap.TIMEOUT exception, while trying
             to get the rest of the search results
    """

    users_num = 10
    page_size = 2
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        log.info('Initiate a search with a paged results control')
        msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  search_flt,
                                                  searchreq_attrlist,
                                                  serverctrls=controls)
        log.info('Abandon the search')
        topology_st.standalone.abandon(msgid)

        log.info('Expect an ldap.TIMEOUT exception, while trying to get the search results')
        with pytest.raises(ldap.TIMEOUT):
            topology_st.standalone.result3(msgid, timeout=5)
    finally:
        log.info('Set Directory Manager bind back (test_search_abandon)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)


def test_search_with_timelimit(topology_st, test_user):
    """Verify that after performing multiple simple paged searches
    to completion, each with a timelimit, it wouldn't fail, if we sleep
    for a time more than the timelimit.

    :id: 6cd7234b-136c-419f-bf3e-43aa73592cff
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Bind as test user
            2. Search through added users with a simple paged control
               and timelimit set to 5
            3. When the returned cookie is empty, wait 10 seconds
            4. Perform steps 2 and 3 three times in a row
    :expectedresults: No error happens
    """

    users_num = 100
    page_size = 50
    timelimit = 5
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        for ii in range(3):
            log.info('Iteration %d' % ii)
            msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                      ldap.SCOPE_SUBTREE,
                                                      search_flt,
                                                      searchreq_attrlist,
                                                      serverctrls=controls,
                                                      timeout=timelimit)

            pages = 0
            pctrls = []
            while True:
                log.info('Getting page %d' % (pages,))
                rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)
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
                        msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                                  ldap.SCOPE_SUBTREE,
                                                                  search_flt,
                                                                  searchreq_attrlist,
                                                                  serverctrls=controls,
                                                                  timeout=timelimit)
                    else:
                        log.info('Done with this search - sleeping %d seconds' % (
                            timelimit * 2))
                        time.sleep(timelimit * 2)
                        break  # No more pages available
                else:
                    break
    finally:
        log.info('Set Directory Manager bind back (test_search_with_timelimit)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)


@pytest.mark.parametrize('aci_subject',
                         ('dns = "localhost.localdomain"',
                          'ip = "::1" or ip = "127.0.0.1"'))
def test_search_dns_ip_aci(topology_st, test_user, aci_subject):
    """Verify that after performing multiple simple paged searches
    to completion on the suffix with DNS or IP based ACI

    :id: bbfddc46-a8c8-49ae-8c90-7265d05b22a9
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Back up and remove all previous ACI from suffix
            2. Add an anonymous ACI for DNS check
            3. Bind as test user
            4. Search through added users with a simple paged control
            5. Perform steps 4 three times in a row
            6. Return ACI to the initial state
            7. Go through all steps onece again, but use IP subjectdn
               insted of DNS
    :expectedresults: No error happens, all users should be found and sorted
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
        ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
        try:
            topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_REPLACE,
                                                              'aci',
                                                              ACI_BODY)])
        except ldap.LDAPError as e:
            log.fatal('Failed to add ACI: error (%s)' % (e.message['desc']))
            raise e

        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        log.info('Initiate three searches with a paged results control')
        for ii in range(3):
            log.info('%d search' % (ii + 1))
            all_results = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                       search_flt, searchreq_attrlist)
            log.info('%d results' % len(all_results))
            assert len(all_results) == len(users_list)
        log.info('If we are here, then no error has happened. We are good.')

    finally:
        log.info('Set Directory Manager bind back (test_search_dns_ip_aci)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        log.info('Restore ACI')
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE,
                                                          'aci',
                                                          None)])
        for aci in acis_bck:
            topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD,
                                                              'aci',
                                                              aci.getRawAci())])
        del_users(topology_st, users_list)


def test_search_multiple_paging(topology_st, test_user):
    """Verify that after performing multiple simple paged searches
    on a single connection without a complition, it wouldn't fail.

    :id: 628b29a6-2d47-4116-a88d-00b87405ef7f
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Bind as test user
            2. Initiate the search with a simple paged control
            3. Acquire the returned cookie only one time
            4. Perform steps 2 and 3 three times in a row
    :expectedresults: No error happens
    """

    users_num = 100
    page_size = 30
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        for ii in range(3):
            log.info('Iteration %d' % ii)
            msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                      ldap.SCOPE_SUBTREE,
                                                      search_flt,
                                                      searchreq_attrlist,
                                                      serverctrls=controls)
            rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)
            pctrls = [
                c
                for c in rctrls
                if c.controlType == SimplePagedResultsControl.controlType
                ]

            # Copy cookie from response control to request control
            req_ctrl.cookie = pctrls[0].cookie
            msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                      ldap.SCOPE_SUBTREE,
                                                      search_flt,
                                                      searchreq_attrlist,
                                                      serverctrls=controls)
    finally:
        log.info('Set Directory Manager bind back (test_search_multiple_paging)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)


@pytest.mark.parametrize("invalid_cookie", [1000, -1])
def test_search_invalid_cookie(topology_st, test_user, invalid_cookie):
    """Verify that using invalid cookie while performing
    search with the simple paged results control throws
    a TypeError exception

    :id: 107be12d-4fe4-47fe-ae86-f3e340a56f42
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Bind as test user
            2. Initiate the search with a simple paged control
            3. Put an invalid cookie (-1, 1000) to the control
            4. Continue the search
    :expectedresults: It will throw an TypeError exception
    """

    users_num = 100
    page_size = 50
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  search_flt,
                                                  searchreq_attrlist,
                                                  serverctrls=controls)
        rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)

        log.info('Put an invalid cookie (%d) to the control. TypeError is expected' %
                 invalid_cookie)
        req_ctrl.cookie = invalid_cookie
        with pytest.raises(TypeError):
            msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                      ldap.SCOPE_SUBTREE,
                                                      search_flt,
                                                      searchreq_attrlist,
                                                      serverctrls=controls)
    finally:
        log.info('Set Directory Manager bind back (test_search_invalid_cookie)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)


def test_search_abandon_with_zero_size(topology_st, test_user):
    """Verify that search with simple paged results control
    can be abandon using page_size = 0

    :id: d2fd9a10-84e1-4b69-a8a7-36ca1427c171
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Bind as test user
            2. Search through added users with a simple paged control
               and page_size = 0
    :expectedresults: No cookie should be returned at all
    """

    users_num = 10
    page_size = 0
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  search_flt,
                                                  searchreq_attrlist,
                                                  serverctrls=controls)
        rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)
        pctrls = [
            c
            for c in rctrls
            if c.controlType == SimplePagedResultsControl.controlType
            ]
        assert not pctrls[0].cookie
    finally:
        log.info('Set Directory Manager bind back (test_search_abandon_with_zero_size)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)


def test_search_pagedsizelimit_success(topology_st, test_user):
    """Verify that search with a simple paged results control
    returns all entries it should without errors while
    valid value set to nsslapd-pagedsizelimit.

    :id: 88193f10-f6f0-42f5-ae9c-ff34b8f9ee8c
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            10 users for the search base
    :steps: 1. Set nsslapd-pagedsizelimit: 20
            2. Bind as test user
            3. Search through added users with a simple paged control
               using page_size = 10
    :expectedresults: All users should be found
    """

    users_num = 10
    page_size = 10
    attr_name = 'nsslapd-pagedsizelimit'
    attr_value = '20'
    attr_value_bck = change_conf_attr(topology_st, DN_CONFIG,
                                      attr_name, attr_value)
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        all_results = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                   search_flt, searchreq_attrlist)

        log.info('%d results' % len(all_results))
        assert len(all_results) == len(users_list)

    finally:
        log.info('Set Directory Manager bind back (test_search_pagedsizelimit_success)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)
        change_conf_attr(topology_st, DN_CONFIG,
                         'nsslapd-pagedsizelimit', attr_value_bck)


@pytest.mark.parametrize('conf_attr,user_attr,expected_rs',
                         (('5', '15', 'PASS'), ('15', '5', ldap.SIZELIMIT_EXCEEDED)))
def test_search_nspagedsizelimit(topology_st, test_user,
                                 conf_attr, user_attr, expected_rs):
    """Verify that nsPagedSizeLimit attribute overrides
    nsslapd-pagedsizelimit while performing search with
    the simple paged results control.

    :id: b08c6ad2-ba28-447a-9f04-5377c3661d0d
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            10 users for the search base
    :steps: 1. Set nsslapd-pagedsizelimit: 5
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
    :expectedresults: After the steps 1-4, it should PASS.
             After the steps 7-10, it should throw
             SIZELIMIT_EXCEEDED exception
    """

    users_num = 10
    page_size = 10
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    conf_attr_bck = change_conf_attr(topology_st, DN_CONFIG,
                                     'nsslapd-pagedsizelimit', conf_attr)
    user_attr_bck = change_conf_attr(topology_st, TEST_USER_DN,
                                     'nsPagedSizeLimit', user_attr)

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        if expected_rs == ldap.SIZELIMIT_EXCEEDED:
            log.info('Expect to fail with SIZELIMIT_EXCEEDED')
            with pytest.raises(expected_rs):
                all_results = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                           search_flt, searchreq_attrlist)
        elif expected_rs == 'PASS':
            log.info('Expect to pass')
            all_results = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                       search_flt, searchreq_attrlist)
            log.info('%d results' % len(all_results))
            assert len(all_results) == len(users_list)

    finally:
        log.info('Set Directory Manager bind back (test_search_nspagedsizelimit)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)
        change_conf_attr(topology_st, DN_CONFIG,
                         'nsslapd-pagedsizelimit', conf_attr_bck)
        change_conf_attr(topology_st, TEST_USER_DN,
                         'nsPagedSizeLimit', user_attr_bck)


@pytest.mark.parametrize('conf_attr_values,expected_rs',
                         ((('5000', '100', '100'), ldap.ADMINLIMIT_EXCEEDED),
                          (('5000', '120', '122'), 'PASS')))
def test_search_paged_limits(topology_st, test_user, conf_attr_values, expected_rs):
    """Verify that nsslapd-idlistscanlimit and
    nsslapd-lookthroughlimit can limit the administrator
    search abilities.

    :id: e0f8b916-7276-4bd3-9e73-8696a4468811
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            10 users for the search base
    :steps: 1. Set nsslapd-sizelimit and nsslapd-pagedsizelimit to 5000
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
    :expectedresults: After the steps 1-4, it should PASS.
             After the steps 7-10, it should throw
             ADMINLIMIT_EXCEEDED exception
    """

    users_num = 101
    page_size = 10
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    size_attr_bck = change_conf_attr(topology_st, DN_CONFIG,
                                     'nsslapd-sizelimit', conf_attr_values[0])
    pagedsize_attr_bck = change_conf_attr(topology_st, DN_CONFIG,
                                          'nsslapd-pagedsizelimit', conf_attr_values[0])
    idlistscan_attr_bck = change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM,
                                           'nsslapd-idlistscanlimit', conf_attr_values[1])
    lookthrough_attr_bck = change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM,
                                            'nsslapd-lookthroughlimit', conf_attr_values[2])

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        if expected_rs == ldap.ADMINLIMIT_EXCEEDED:
            log.info('Expect to fail with ADMINLIMIT_EXCEEDED')
            with pytest.raises(expected_rs):
                all_results = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                           search_flt, searchreq_attrlist)
        elif expected_rs == 'PASS':
            log.info('Expect to pass')
            all_results = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                       search_flt, searchreq_attrlist)
            log.info('%d results' % len(all_results))
            assert len(all_results) == len(users_list)
    finally:
        log.info('Set Directory Manager bind back (test_search_paged_limits)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)
        change_conf_attr(topology_st, DN_CONFIG,
                         'nsslapd-sizelimit', size_attr_bck)
        change_conf_attr(topology_st, DN_CONFIG,
                         'nsslapd-pagedsizelimit', pagedsize_attr_bck)
        change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM,
                         'nsslapd-lookthroughlimit', lookthrough_attr_bck)
        change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM,
                         'nsslapd-idlistscanlimit', idlistscan_attr_bck)


@pytest.mark.parametrize('conf_attr_values,expected_rs',
                         ((('1000', '100', '100'), ldap.ADMINLIMIT_EXCEEDED),
                          (('1000', '120', '122'), 'PASS')))
def test_search_paged_user_limits(topology_st, test_user, conf_attr_values, expected_rs):
    """Verify that nsPagedIDListScanLimit and nsPagedLookthroughLimit
    override nsslapd-idlistscanlimit and nsslapd-lookthroughlimit
    while performing search with the simple paged results control.

    :id: 69e393e9-1ab8-4f4e-b4a1-06ca63dc7b1b
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            10 users for the search base
    :steps: 1. Set nsslapd-idlistscanlimit: 1000
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
    :expectedresults: After the steps 1-4, it should PASS.
             After the steps 8-11, it should throw
             ADMINLIMIT_EXCEEDED exception
    """

    users_num = 101
    page_size = 10
    users_list = add_users(topology_st, users_num, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    lookthrough_attr_bck = change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM,
                                            'nsslapd-lookthroughlimit', conf_attr_values[0])
    idlistscan_attr_bck = change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM,
                                           'nsslapd-idlistscanlimit', conf_attr_values[0])
    user_idlistscan_attr_bck = change_conf_attr(topology_st, TEST_USER_DN,
                                                'nsPagedIDListScanLimit', conf_attr_values[1])
    user_lookthrough_attr_bck = change_conf_attr(topology_st, TEST_USER_DN,
                                                 'nsPagedLookthroughLimit', conf_attr_values[2])

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        controls = [req_ctrl]

        if expected_rs == ldap.ADMINLIMIT_EXCEEDED:
            log.info('Expect to fail with ADMINLIMIT_EXCEEDED')
            with pytest.raises(expected_rs):
                all_results = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                           search_flt, searchreq_attrlist)
        elif expected_rs == 'PASS':
            log.info('Expect to pass')
            all_results = paged_search(topology_st, DEFAULT_SUFFIX, controls,
                                       search_flt, searchreq_attrlist)
            log.info('%d results' % len(all_results))
            assert len(all_results) == len(users_list)
    finally:
        log.info('Set Directory Manager bind back (test_search_paged_user_limits)')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)
        change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM,
                         'nsslapd-lookthroughlimit', lookthrough_attr_bck)
        change_conf_attr(topology_st, 'cn=config,%s' % DN_LDBM,
                         'nsslapd-idlistscanlimit', idlistscan_attr_bck)
        change_conf_attr(topology_st, TEST_USER_DN,
                         'nsPagedIDListScanLimit', user_idlistscan_attr_bck)
        change_conf_attr(topology_st, TEST_USER_DN,
                         'nsPagedLookthroughLimit', user_lookthrough_attr_bck)


def test_ger_basic(topology_st, test_user):
    """Verify that search with a simple paged results control
    and get effective rights control returns all entries
    it should without errors.

    :id: 7b0bdfc7-a2f2-4c1a-bcab-f1eb8b330d45
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            variated number of users for the search base
    :steps: 1. Search through added users with a simple paged control
               and get effective rights control
    :expectedresults: All users should be found, every found entry should have
             an 'attributeLevelRights' returned
    """

    users_list = add_users(topology_st, 20, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    page_size = 4

    try:
        log.info('Set bind to directory manager')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

        spr_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
        ger_ctrl = GetEffectiveRightsControl(True, "dn: " + DN_DM)

        all_results = paged_search(topology_st, DEFAULT_SUFFIX, [spr_ctrl, ger_ctrl],
                                   search_flt, searchreq_attrlist)

        log.info('{} results'.format(len(all_results)))
        assert len(all_results) == len(users_list)
        log.info('Check for attributeLevelRights')
        assert all(attrs['attributeLevelRights'][0] for dn, attrs in all_results)
    finally:
        log.info('Remove added users')
        del_users(topology_st, users_list)


def test_multi_suffix_search(topology_st, test_user, new_suffixes):
    """Verify that page result search returns empty cookie
    if there is no returned entry.

    :id: 9712345b-9e38-4df6-8794-05f12c457d39
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            two suffixes with backends, one is inserted into another,
            10 users for the search base within each suffix
    :steps: 1. Bind as test user
            2. Search through all 20 added users with a simple paged control
               using page_size = 4
            3. Wait some time logs to be updated
            3. Check access log
    :expectedresults: All users should be found, the access log should contain
             the pr_cookie for each page request and it should be equal 0,
             except the last one should be equal -1
    """

    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    page_size = 4
    users_num = 20

    log.info('Clear the access log')
    topology_st.standalone.deleteAccessLogs()

    users_list_1 = add_users(topology_st, users_num / 2, NEW_SUFFIX_1)
    users_list_2 = add_users(topology_st, users_num / 2, NEW_SUFFIX_2)

    try:
        log.info('Set DM bind')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

        all_results = paged_search(topology_st, NEW_SUFFIX_1, [req_ctrl],
                                   search_flt, searchreq_attrlist)

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
        del_users(topology_st, users_list_1)
        del_users(topology_st, users_list_2)


@pytest.mark.parametrize('conf_attr_value', (None, '-1', '1000'))
def test_maxsimplepaged_per_conn_success(topology_st, test_user, conf_attr_value):
    """Verify that nsslapd-maxsimplepaged-per-conn acts according design

    :id: 192e2f25-04ee-4ff9-9340-d875dcbe8011
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            20 users for the search base
    :steps: 1. Set nsslapd-maxsimplepaged-per-conn in cn=config
               to the next values: no value, -1, some positive
            2. Search through the added users with a simple paged control
               using page size = 4
    :expectedresults: If no value or value = -1 - all users should be found,
             default behaviour;
             If the value is positive, the value is the max simple paged
             results requests per connection.
    """

    users_list = add_users(topology_st, 20, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    page_size = 4
    if conf_attr_value:
        max_per_con_bck = change_conf_attr(topology_st, DN_CONFIG,
                                           'nsslapd-maxsimplepaged-per-conn',
                                           conf_attr_value)

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

        all_results = paged_search(topology_st, DEFAULT_SUFFIX, [req_ctrl],
                                   search_flt, searchreq_attrlist)

        log.info('{} results'.format(len(all_results)))
        assert len(all_results) == len(users_list)
    finally:
        log.info('Remove added users')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)
        if conf_attr_value:
            change_conf_attr(topology_st, DN_CONFIG,
                             'nsslapd-maxsimplepaged-per-conn', max_per_con_bck)


@pytest.mark.parametrize('conf_attr_value', ('0', '1'))
def test_maxsimplepaged_per_conn_failure(topology_st, test_user, conf_attr_value):
    """Verify that nsslapd-maxsimplepaged-per-conn acts according design

    :id: eb609e63-2829-4331-8439-a35f99694efa
    :feature: Simple paged results
    :setup: Standalone instance, test user for binding,
            20 users for the search base
    :steps: 1. Set nsslapd-maxsimplepaged-per-conn = 0 in cn=config
            2. Search through the added users with a simple paged control
               using page size = 4
            3. Set nsslapd-maxsimplepaged-per-conn = 1 in cn=config
            4. Search through the added users with a simple paged control
               using page size = 4 two times, but don't close the connections
    :expectedresults: During the searches UNWILLING_TO_PERFORM should be throwned
    """

    users_list = add_users(topology_st, 20, DEFAULT_SUFFIX)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    page_size = 4
    max_per_con_bck = change_conf_attr(topology_st, DN_CONFIG,
                                       'nsslapd-maxsimplepaged-per-conn',
                                       conf_attr_value)

    try:
        log.info('Set user bind')
        topology_st.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

        log.info('Create simple paged results control instance')
        req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')

        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                      ldap.SCOPE_SUBTREE,
                                                      search_flt,
                                                      searchreq_attrlist,
                                                      serverctrls=[req_ctrl])
            rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)

            # If nsslapd-maxsimplepaged-per-conn = 1,
            # it should pass this point, but failed on the next search
            assert conf_attr_value == '1'
            msgid = topology_st.standalone.search_ext(DEFAULT_SUFFIX,
                                                      ldap.SCOPE_SUBTREE,
                                                      search_flt,
                                                      searchreq_attrlist,
                                                      serverctrls=[req_ctrl])
            rtype, rdata, rmsgid, rctrls = topology_st.standalone.result3(msgid)
    finally:
        log.info('Remove added users')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        del_users(topology_st, users_list)
        change_conf_attr(topology_st, DN_CONFIG,
                         'nsslapd-maxsimplepaged-per-conn', max_per_con_bck)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
