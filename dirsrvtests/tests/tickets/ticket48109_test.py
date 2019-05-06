# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
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

from lib389._constants import SUFFIX, DEFAULT_SUFFIX

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

UID_INDEX = 'cn=uid,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'


def test_ticket48109(topology_st):
    '''
    Set SubStr lengths to cn=uid,cn=index,...
      objectClass: extensibleObject
      nsIndexType: sub
      nsSubStrBegin: 2
      nsSubStrEnd: 2
    '''
    log.info('Test case 0')
    # add substr setting to UID_INDEX
    try:
        topology_st.standalone.modify_s(UID_INDEX,
                                        [(ldap.MOD_ADD, 'objectClass', b'extensibleObject'),
                                         (ldap.MOD_ADD, 'nsIndexType', b'sub'),
                                         (ldap.MOD_ADD, 'nsSubStrBegin', b'2'),
                                         (ldap.MOD_ADD, 'nsSubStrEnd', b'2')])
    except ldap.LDAPError as e:
        log.error('Failed to add substr lengths: error {}'.format(e.args[0]['desc']))
        assert False

    # restart the server to apply the indexing
    topology_st.standalone.restart(timeout=10)

    # add a test user
    UID = 'auser0'
    USER_DN = 'uid=%s,%s' % (UID, SUFFIX)
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top person organizationalPerson inetOrgPerson'.split(),
            'cn': 'a user0',
            'sn': 'user0',
            'givenname': 'a',
            'mail': UID})))
    except ldap.LDAPError as e:
        log.error('Failed to add ' + USER_DN + ': error {}'.format(e.args[0]['desc']))
        assert False

    entries = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(uid=a*)')
    assert len(entries) == 1

    # restart the server to check the access log
    topology_st.standalone.restart(timeout=10)

    cmdline = 'egrep %s %s | egrep "uid=a\*"' % (SUFFIX, topology_st.standalone.accesslog)
    p = os.popen(cmdline, "r")
    l0 = p.readline()
    if l0 == "":
        log.error('Search with "(uid=a*)" is not logged in ' + topology_st.standalone.accesslog)
        assert False
    else:
        # regex = re.compile('\(conn=[0-9]* op=[0-9]*\) SRCH .*')
        regex = re.compile(r'.*\s+(conn=\d+ op=\d+)\s+SRCH .*')
        match = regex.match(l0)
        log.info('match: %s' % match.group(1))
        cmdline = 'egrep "%s" %s | egrep "RESULT"' % (match.group(1), topology_st.standalone.accesslog)
        p = os.popen(cmdline, "r")
        l1 = p.readline()
        if l1 == "":
            log.error('Search result of "(uid=a*)" is not logged in ' + topology_st.standalone.accesslog)
            assert False
        else:
            log.info('l1: %s' % l1)
            regex = re.compile(r'.*nentries=(\d+)\s+.*')
            match = regex.match(l1)
            log.info('match: nentires=%s' % match.group(1))
            if match.group(1) == "0":
                log.error('Entry uid=a* not found.')
                assert False
            else:
                log.info('Entry uid=a* found.')
                regex = re.compile(r'.*(notes=[AU]).*')
                match = regex.match(l1)
                if match:
                    log.error('%s - substr index was not used' % match.group(1))
                    assert False
                else:
                    log.info('Test case 0 - OK - substr index used')

    # clean up substr setting to UID_INDEX
    try:
        topology_st.standalone.modify_s(UID_INDEX,
                                        [(ldap.MOD_DELETE, 'objectClass', b'extensibleObject'),
                                         (ldap.MOD_DELETE, 'nsIndexType', b'sub'),
                                         (ldap.MOD_DELETE, 'nsSubStrBegin', b'2'),
                                         (ldap.MOD_DELETE, 'nsSubStrEnd', b'2')])
    except ldap.LDAPError as e:
        log.error('Failed to delete substr lengths: error {}'.format(e.args[0]['desc']))
        assert False

    '''
    Set SubStr lengths to cn=uid,cn=index,...
      nsIndexType: sub
      nsMatchingRule: nsSubStrBegin=2
      nsMatchingRule: nsSubStrEnd=2
    '''
    log.info('Test case 1')
    # add substr setting to UID_INDEX
    try:
        topology_st.standalone.modify_s(UID_INDEX,
                                        [(ldap.MOD_ADD, 'nsIndexType', b'sub'),
                                         (ldap.MOD_ADD, 'nsMatchingRule', b'nssubstrbegin=2'),
                                         (ldap.MOD_ADD, 'nsMatchingRule', b'nssubstrend=2')])
    except ldap.LDAPError as e:
        log.error('Failed to add substr lengths: error {}'.format(e.args[0]['desc']))
        assert False

    # restart the server to apply the indexing
    topology_st.standalone.restart(timeout=10)

    # add a test user
    UID = 'buser1'
    USER_DN = 'uid=%s,%s' % (UID, SUFFIX)
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top person organizationalPerson inetOrgPerson'.split(),
            'cn': 'b user1',
            'sn': 'user1',
            'givenname': 'b',
            'mail': UID})))
    except ldap.LDAPError as e:
        log.error('Failed to add ' + USER_DN + ': error {}'.format(e.args[0]['desc']))
        assert False

    entries = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(uid=b*)')
    assert len(entries) == 1

    # restart the server to check the access log
    topology_st.standalone.restart(timeout=10)

    cmdline = 'egrep %s %s | egrep "uid=b\*"' % (SUFFIX, topology_st.standalone.accesslog)
    p = os.popen(cmdline, "r")
    l0 = p.readline()
    if l0 == "":
        log.error('Search with "(uid=b*)" is not logged in ' + topology_st.standalone.accesslog)
        assert False
    else:
        # regex = re.compile('\(conn=[0-9]* op=[0-9]*\) SRCH .*')
        regex = re.compile(r'.*\s+(conn=\d+ op=\d+)\s+SRCH .*')
        match = regex.match(l0)
        log.info('match: %s' % match.group(1))
        cmdline = 'egrep "%s" %s | egrep "RESULT"' % (match.group(1), topology_st.standalone.accesslog)
        p = os.popen(cmdline, "r")
        l1 = p.readline()
        if l1 == "":
            log.error('Search result of "(uid=*b)" is not logged in ' + topology_st.standalone.accesslog)
            assert False
        else:
            log.info('l1: %s' % l1)
            regex = re.compile(r'.*nentries=(\d+)\s+.*')
            match = regex.match(l1)
            log.info('match: nentires=%s' % match.group(1))
            if match.group(1) == "0":
                log.error('Entry uid=*b not found.')
                assert False
            else:
                log.info('Entry uid=*b found.')
                regex = re.compile(r'.*(notes=[AU]).*')
                match = regex.match(l1)
                if match:
                    log.error('%s - substr index was not used' % match.group(1))
                    assert False
                else:
                    log.info('Test case 1 - OK - substr index used')

    # clean up substr setting to UID_INDEX
    try:
        topology_st.standalone.modify_s(UID_INDEX,
                                        [(ldap.MOD_DELETE, 'nsIndexType', b'sub'),
                                         (ldap.MOD_DELETE, 'nsMatchingRule', b'nssubstrbegin=2'),
                                         (ldap.MOD_DELETE, 'nsMatchingRule', b'nssubstrend=2')])
    except ldap.LDAPError as e:
        log.error('Failed to delete substr lengths: error {}'.format(e.args[0]['desc']))
        assert False

    '''
    Set SubStr conflict formats/lengths to cn=uid,cn=index,...
      objectClass: extensibleObject
      nsIndexType: sub
      nsMatchingRule: nsSubStrBegin=3
      nsMatchingRule: nsSubStrEnd=3
      nsSubStrBegin: 2
      nsSubStrEnd: 2
    nsSubStr{Begin,End} are honored.
    '''
    log.info('Test case 2')

    # add substr setting to UID_INDEX
    try:
        topology_st.standalone.modify_s(UID_INDEX,
                                        [(ldap.MOD_ADD, 'nsIndexType', b'sub'),
                                         (ldap.MOD_ADD, 'nsMatchingRule', b'nssubstrbegin=3'),
                                         (ldap.MOD_ADD, 'nsMatchingRule', b'nssubstrend=3'),
                                         (ldap.MOD_ADD, 'objectClass', b'extensibleObject'),
                                         (ldap.MOD_ADD, 'nsSubStrBegin', b'2'),
                                         (ldap.MOD_ADD, 'nsSubStrEnd', b'2')])
    except ldap.LDAPError as e:
        log.error('Failed to add substr lengths: error {}'.format(e.args[0]['desc']))
        assert False

    # restart the server to apply the indexing
    topology_st.standalone.restart(timeout=10)

    # add a test user
    UID = 'cuser2'
    USER_DN = 'uid=%s,%s' % (UID, SUFFIX)
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top person organizationalPerson inetOrgPerson'.split(),
            'cn': 'c user2',
            'sn': 'user2',
            'givenname': 'c',
            'mail': UID})))
    except ldap.LDAPError as e:
        log.error('Failed to add ' + USER_DN + ': error {}'.format(e.args[0]['desc']))
        assert False

    entries = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(uid=c*)')
    assert len(entries) == 1

    entries = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*2)')
    assert len(entries) == 1

    # restart the server to check the access log
    topology_st.standalone.restart(timeout=10)

    cmdline = 'egrep %s %s | egrep "uid=c\*"' % (SUFFIX, topology_st.standalone.accesslog)
    p = os.popen(cmdline, "r")
    l0 = p.readline()
    if l0 == "":
        log.error('Search with "(uid=c*)" is not logged in ' + topology_st.standalone.accesslog)
        assert False
    else:
        # regex = re.compile('\(conn=[0-9]* op=[0-9]*\) SRCH .*')
        regex = re.compile(r'.*\s+(conn=\d+ op=\d+)\s+SRCH .*')
        match = regex.match(l0)
        log.info('match: %s' % match.group(1))
        cmdline = 'egrep "%s" %s | egrep "RESULT"' % (match.group(1), topology_st.standalone.accesslog)
        p = os.popen(cmdline, "r")
        l1 = p.readline()
        if l1 == "":
            log.error('Search result of "(uid=c*)" is not logged in ' + topology_st.standalone.accesslog)
            assert False
        else:
            log.info('l1: %s' % l1)
            regex = re.compile(r'.*nentries=(\d+)\s+.*')
            match = regex.match(l1)
            log.info('match: nentires=%s' % match.group(1))
            if match.group(1) == "0":
                log.error('Entry uid=c* not found.')
                assert False
            else:
                log.info('Entry uid=c* found.')
                regex = re.compile(r'.*(notes=[AU]).*')
                match = regex.match(l1)
                if match:
                    log.error('%s - substr index was not used' % match.group(1))
                    assert False
                else:
                    log.info('Test case 2-1 - OK - correct substr index used')

    cmdline = 'egrep %s %s | egrep "uid=\*2"' % (SUFFIX, topology_st.standalone.accesslog)
    p = os.popen(cmdline, "r")
    l0 = p.readline()
    if l0 == "":
        log.error('Search with "(uid=*2)" is not logged in ' + topology_st.standalone.accesslog)
        assert False
    else:
        # regex = re.compile('\(conn=[0-9]* op=[0-9]*\) SRCH .*')
        regex = re.compile(r'.*\s+(conn=\d+ op=\d+)\s+SRCH .*')
        match = regex.match(l0)
        log.info('match: %s' % match.group(1))
        cmdline = 'egrep "%s" %s | egrep "RESULT"' % (match.group(1), topology_st.standalone.accesslog)
        p = os.popen(cmdline, "r")
        l1 = p.readline()
        if l1 == "":
            log.error('Search result of "(uid=*2)" is not logged in ' + topology_st.standalone.accesslog)
            assert False
        else:
            log.info('l1: %s' % l1)
            regex = re.compile(r'.*nentries=(\d+)\s+.*')
            match = regex.match(l1)
            log.info('match: nentires=%s' % match.group(1))
            if match.group(1) == "0":
                log.error('Entry uid=*2 not found.')
                assert False
            else:
                log.info('Entry uid=*2 found.')
                regex = re.compile(r'.*(notes=[AU]).*')
                match = regex.match(l1)
                if match:
                    log.error('%s - substr index was not used' % match.group(1))
                    assert False
                else:
                    log.info('Test case 2-2 - OK - correct substr index used')

    # clean up substr setting to UID_INDEX
    try:
        topology_st.standalone.modify_s(UID_INDEX,
                                        [(ldap.MOD_DELETE, 'nsIndexType', b'sub'),
                                         (ldap.MOD_DELETE, 'nsMatchingRule', b'nssubstrbegin=3'),
                                         (ldap.MOD_DELETE, 'nsMatchingRule', b'nssubstrend=3'),
                                         (ldap.MOD_DELETE, 'objectClass', b'extensibleObject'),
                                         (ldap.MOD_DELETE, 'nsSubStrBegin', b'2'),
                                         (ldap.MOD_DELETE, 'nsSubStrEnd', b'2')])
    except ldap.LDAPError as e:
        log.error('Failed to delete substr lengths: error {}'.format(e.args[0]['desc']))
        assert False
    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
