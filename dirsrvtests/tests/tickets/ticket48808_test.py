import time
import ldap
import logging
import pytest
from random import sample
from ldap.controls import SimplePagedResultsControl
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_USER_NAME = 'simplepaged_test'
TEST_USER_DN = 'uid=%s,%s' % (TEST_USER_NAME, DEFAULT_SUFFIX)
TEST_USER_PWD = 'simplepaged_test'


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


@pytest.fixture(scope="module")
def test_user(topology):
    """User for binding operation"""

    try:
        topology.standalone.add_s(Entry((TEST_USER_DN, {
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


def add_users(topology, users_num):
    """Add users to the default suffix
    and return a list of added user DNs.
    """

    users_list = []
    log.info('Adding %d users' % users_num)
    for num in sample(range(1000), users_num):
        num_ran = int(round(num))
        USER_NAME = 'test%05d' % num_ran
        USER_DN = 'uid=%s,%s' % (USER_NAME, DEFAULT_SUFFIX)
        users_list.append(USER_DN)
        try:
            topology.standalone.add_s(Entry((USER_DN, {
                                             'objectclass': 'top person'.split(),
                                             'objectclass': 'organizationalPerson',
                                             'objectclass': 'inetorgperson',
                                             'cn': USER_NAME,
                                             'sn': USER_NAME,
                                             'userpassword': 'pass%s' % num_ran,
                                             'mail': '%s@redhat.com' % USER_NAME,
                                             'uid': USER_NAME
                                              })))
        except ldap.LDAPError as e:
            log.error('Failed to add user (%s): error (%s)' % (USER_DN,
                                                               e.message['desc']))
            raise e
    return users_list


def del_users(topology, users_list):
    """Delete users with DNs from given list"""

    log.info('Deleting %d users' % len(users_list))
    for user_dn in users_list:
        try:
            topology.standalone.delete_s(user_dn)
        except ldap.LDAPError as e:
            log.error('Failed to delete user (%s): error (%s)' % (user_dn,
                                                                  e.message['desc']))
            raise e


def change_conf_attr(topology, suffix, attr_name, attr_value):
    """Change configurational attribute in the given suffix.
    Funtion returns previous attribute value.
    """

    try:
        entries = topology.standalone.search_s(suffix, ldap.SCOPE_BASE,
                                                'objectclass=top',
                                                [attr_name])
        attr_value_bck = entries[0].data.get(attr_name)
        log.info('Set %s to %s. Previous value - %s. Modified suffix - %s.' % (
                        attr_name, attr_value, attr_value_bck, suffix))
        if attr_value is None:
            topology.standalone.modify_s(suffix, [(ldap.MOD_DELETE,
                                                        attr_name,
                                                        attr_value)])
        else:
            topology.standalone.modify_s(suffix, [(ldap.MOD_REPLACE,
                                                        attr_name,
                                                        attr_value)])
    except ldap.LDAPError as e:
           log.error('Failed to change attr value (%s): error (%s)' % (attr_name,
                                                                       e.message['desc']))
           raise e

    return attr_value_bck


def paged_search(topology, controls, search_flt, searchreq_attrlist):
    """Search at the DEFAULT_SUFFIX with ldap.SCOPE_SUBTREE
    using Simple Paged Control(should the first item in the
    list controls.
    Return the list with results summarized from all pages
    """

    pages = 0
    pctrls = []
    all_results = []
    req_ctrl = controls[0]
    msgid = topology.standalone.search_ext(DEFAULT_SUFFIX,
                                           ldap.SCOPE_SUBTREE,
                                           search_flt,
                                           searchreq_attrlist,
                                           serverctrls=controls)
    while True:
        log.info('Getting page %d' % (pages,))
        rtype, rdata, rmsgid, rctrls = topology.standalone.result3(msgid)
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
                msgid = topology.standalone.search_ext(DEFAULT_SUFFIX,
                                                       ldap.SCOPE_SUBTREE,
                                                       search_flt,
                                                       searchreq_attrlist,
                                                       serverctrls=controls)
            else:
                break # no more pages available
        else:
            break

    assert not pctrls[0].cookie
    return all_results


def test_ticket48808(topology, test_user):
    log.info('Run multiple paging controls on a single connection')
    users_num = 100
    page_size = 30
    users_list = add_users(topology, users_num)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    log.info('Set user bind')
    topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

    log.info('Create simple paged results control instance')
    req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
    controls = [req_ctrl]

    for ii in xrange(3):
        log.info('Iteration %d' % ii)
        msgid = topology.standalone.search_ext(DEFAULT_SUFFIX,
                                                ldap.SCOPE_SUBTREE,
                                                search_flt,
                                                searchreq_attrlist,
                                                serverctrls=controls)
        rtype, rdata, rmsgid, rctrls = topology.standalone.result3(msgid)
        pctrls = [
            c
            for c in rctrls
            if c.controlType == SimplePagedResultsControl.controlType
        ]

        req_ctrl.cookie = pctrls[0].cookie
        msgid = topology.standalone.search_ext(DEFAULT_SUFFIX,
                                                ldap.SCOPE_SUBTREE,
                                                search_flt,
                                                searchreq_attrlist,
                                                serverctrls=controls)
    log.info('Set Directory Manager bind back')
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    del_users(topology, users_list)

    log.info('Abandon the search')
    users_num = 10
    page_size = 0
    users_list = add_users(topology, users_num)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']

    log.info('Set user bind')
    topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

    log.info('Create simple paged results control instance')
    req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
    controls = [req_ctrl]

    msgid = topology.standalone.search_ext(DEFAULT_SUFFIX,
                                           ldap.SCOPE_SUBTREE,
                                           search_flt,
                                           searchreq_attrlist,
                                           serverctrls=controls)
    rtype, rdata, rmsgid, rctrls = topology.standalone.result3(msgid)
    pctrls = [
        c
        for c in rctrls
        if c.controlType == SimplePagedResultsControl.controlType
    ]
    assert not pctrls[0].cookie

    log.info('Set Directory Manager bind back')
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    del_users(topology, users_list)

    log.info("Search should fail with 'nsPagedSizeLimit = 5'"
             "and 'nsslapd-pagedsizelimit = 15' with 10 users")
    conf_attr = '15'
    user_attr = '5'
    expected_rs = ldap.SIZELIMIT_EXCEEDED
    users_num = 10
    page_size = 10
    users_list = add_users(topology, users_num)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    conf_attr_bck = change_conf_attr(topology, DN_CONFIG,
                                        'nsslapd-pagedsizelimit', conf_attr)
    user_attr_bck = change_conf_attr(topology, TEST_USER_DN,
                                        'nsPagedSizeLimit', user_attr)

    log.info('Set user bind')
    topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

    log.info('Create simple paged results control instance')
    req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
    controls = [req_ctrl]

    log.info('Expect to fail with SIZELIMIT_EXCEEDED')
    with pytest.raises(expected_rs):
        all_results = paged_search(topology, controls,
                                   search_flt, searchreq_attrlist)

    log.info('Set Directory Manager bind back')
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    del_users(topology, users_list)
    change_conf_attr(topology, DN_CONFIG,
                     'nsslapd-pagedsizelimit', conf_attr_bck)
    change_conf_attr(topology, TEST_USER_DN,
                     'nsPagedSizeLimit', user_attr_bck)

    log.info("Search should pass with 'nsPagedSizeLimit = 15'"
             "and 'nsslapd-pagedsizelimit = 5' with 10 users")
    conf_attr = '5'
    user_attr = '15'
    users_num = 10
    page_size = 10
    users_list = add_users(topology, users_num)
    search_flt = r'(uid=test*)'
    searchreq_attrlist = ['dn', 'sn']
    conf_attr_bck = change_conf_attr(topology, DN_CONFIG,
                                    'nsslapd-pagedsizelimit', conf_attr)
    user_attr_bck = change_conf_attr(topology, TEST_USER_DN,
                                    'nsPagedSizeLimit', user_attr)

    log.info('Set user bind')
    topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)

    log.info('Create simple paged results control instance')
    req_ctrl = SimplePagedResultsControl(True, size=page_size, cookie='')
    controls = [req_ctrl]

    log.info('Search should PASS')
    all_results = paged_search(topology, controls,
                               search_flt, searchreq_attrlist)
    log.info('%d results' % len(all_results))
    assert len(all_results) == len(users_list)

    log.info('Set Directory Manager bind back')
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    del_users(topology, users_list)
    change_conf_attr(topology, DN_CONFIG,
                    'nsslapd-pagedsizelimit', conf_attr_bck)
    change_conf_attr(topology, TEST_USER_DN,
                    'nsPagedSizeLimit', user_attr_bck)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
