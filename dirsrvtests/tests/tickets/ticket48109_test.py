import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

UID_INDEX = 'cn=uid,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

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

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket48109_0(topology):
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
        topology.standalone.modify_s(UID_INDEX,
                                     [(ldap.MOD_ADD, 'objectClass', 'extensibleObject'),
                                      (ldap.MOD_ADD, 'nsIndexType', 'sub'),
                                      (ldap.MOD_ADD, 'nsSubStrBegin', '2'),
                                      (ldap.MOD_ADD, 'nsSubStrEnd', '2')])
    except ldap.LDAPError, e:
        log.error('Failed to add substr lengths: error ' + e.message['desc'])
        assert False

    # restart the server to apply the indexing
    topology.standalone.restart(timeout=10)

    # add a test user 
    UID = 'auser0'
    USER_DN = 'uid=%s,%s' % (UID, SUFFIX)
    try:
        topology.standalone.add_s(Entry((USER_DN, {
                                         'objectclass': 'top person organizationalPerson inetOrgPerson'.split(),
                                         'cn': 'a user0',
                                         'sn': 'user0',
                                         'givenname': 'a',
                                         'mail': UID})))
    except ldap.LDAPError, e:
        log.error('Failed to add ' + USER_DN + ': error ' + e.message['desc'])
        assert False

    entries = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(uid=a*)')
    assert len(entries) == 1

    # restart the server to check the access log
    topology.standalone.restart(timeout=10)

    cmdline = 'egrep %s %s | egrep "uid=a\*"' % (SUFFIX, topology.standalone.accesslog)
    p = os.popen(cmdline, "r")
    l0 = p.readline()
    if l0 == "":
        log.error('Search with "(uid=a*)" is not logged in ' + topology.standalone.accesslog)
        assert False
    else:
        #regex = re.compile('\(conn=[0-9]* op=[0-9]*\) SRCH .*')
        regex = re.compile(r'.*\s+(conn=\d+ op=\d+)\s+SRCH .*')
        match = regex.match(l0)
        log.info('match: %s' % match.group(1))
        cmdline = 'egrep "%s" %s | egrep "RESULT"' % (match.group(1), topology.standalone.accesslog)
        p = os.popen(cmdline, "r")
        l1 = p.readline()
        if l1 == "":
            log.error('Search result of "(uid=a*)" is not logged in ' + topology.standalone.accesslog)
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
        topology.standalone.modify_s(UID_INDEX,
                                     [(ldap.MOD_DELETE, 'objectClass', 'extensibleObject'),
                                      (ldap.MOD_DELETE, 'nsIndexType', 'sub'),
                                      (ldap.MOD_DELETE, 'nsSubStrBegin', '2'),
                                      (ldap.MOD_DELETE, 'nsSubStrEnd', '2')])
    except ldap.LDAPError, e:
        log.error('Failed to delete substr lengths: error ' + e.message['desc'])
        assert False


def test_ticket48109_1(topology):
    '''
    Set SubStr lengths to cn=uid,cn=index,...
      nsIndexType: sub
      nsMatchingRule: nsSubStrBegin=2
      nsMatchingRule: nsSubStrEnd=2
    '''
    log.info('Test case 1')
    # add substr setting to UID_INDEX
    try:
        topology.standalone.modify_s(UID_INDEX,
                                     [(ldap.MOD_ADD, 'nsIndexType', 'sub'),
                                      (ldap.MOD_ADD, 'nsMatchingRule', 'nssubstrbegin=2'),
                                      (ldap.MOD_ADD, 'nsMatchingRule', 'nssubstrend=2')])
    except ldap.LDAPError, e:
        log.error('Failed to add substr lengths: error ' + e.message['desc'])
        assert False

    # restart the server to apply the indexing
    topology.standalone.restart(timeout=10)

    # add a test user 
    UID = 'buser1'
    USER_DN = 'uid=%s,%s' % (UID, SUFFIX)
    try:
        topology.standalone.add_s(Entry((USER_DN, {
                                         'objectclass': 'top person organizationalPerson inetOrgPerson'.split(),
                                         'cn': 'b user1',
                                         'sn': 'user1',
                                         'givenname': 'b',
                                         'mail': UID})))
    except ldap.LDAPError, e:
        log.error('Failed to add ' + USER_DN + ': error ' + e.message['desc'])
        assert False

    entries = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(uid=b*)')
    assert len(entries) == 1

    # restart the server to check the access log
    topology.standalone.restart(timeout=10)

    cmdline = 'egrep %s %s | egrep "uid=b\*"' % (SUFFIX, topology.standalone.accesslog)
    p = os.popen(cmdline, "r")
    l0 = p.readline()
    if l0 == "":
        log.error('Search with "(uid=b*)" is not logged in ' + topology.standalone.accesslog)
        assert False
    else:
        #regex = re.compile('\(conn=[0-9]* op=[0-9]*\) SRCH .*')
        regex = re.compile(r'.*\s+(conn=\d+ op=\d+)\s+SRCH .*')
        match = regex.match(l0)
        log.info('match: %s' % match.group(1))
        cmdline = 'egrep "%s" %s | egrep "RESULT"' % (match.group(1), topology.standalone.accesslog)
        p = os.popen(cmdline, "r")
        l1 = p.readline()
        if l1 == "":
            log.error('Search result of "(uid=*b)" is not logged in ' + topology.standalone.accesslog)
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
        topology.standalone.modify_s(UID_INDEX,
                                     [(ldap.MOD_DELETE, 'nsIndexType', 'sub'),
                                      (ldap.MOD_DELETE, 'nsMatchingRule', 'nssubstrbegin=2'),
                                      (ldap.MOD_DELETE, 'nsMatchingRule', 'nssubstrend=2')])
    except ldap.LDAPError, e:
        log.error('Failed to delete substr lengths: error ' + e.message['desc'])
        assert False


def test_ticket48109_2(topology):
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
        topology.standalone.modify_s(UID_INDEX,
                                     [(ldap.MOD_ADD, 'nsIndexType', 'sub'),
                                      (ldap.MOD_ADD, 'nsMatchingRule', 'nssubstrbegin=3'),
                                      (ldap.MOD_ADD, 'nsMatchingRule', 'nssubstrend=3'),
                                      (ldap.MOD_ADD, 'objectClass', 'extensibleObject'),
                                      (ldap.MOD_ADD, 'nsSubStrBegin', '2'),
                                      (ldap.MOD_ADD, 'nsSubStrEnd', '2')])
    except ldap.LDAPError, e:
        log.error('Failed to add substr lengths: error ' + e.message['desc'])
        assert False

    # restart the server to apply the indexing
    topology.standalone.restart(timeout=10)

    # add a test user 
    UID = 'cuser2'
    USER_DN = 'uid=%s,%s' % (UID, SUFFIX)
    try:
        topology.standalone.add_s(Entry((USER_DN, {
                                         'objectclass': 'top person organizationalPerson inetOrgPerson'.split(),
                                         'cn': 'c user2',
                                         'sn': 'user2',
                                         'givenname': 'c',
                                         'mail': UID})))
    except ldap.LDAPError, e:
        log.error('Failed to add ' + USER_DN + ': error ' + e.message['desc'])
        assert False

    entries = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(uid=c*)')
    assert len(entries) == 1

    entries = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(uid=*2)')
    assert len(entries) == 1

    # restart the server to check the access log
    topology.standalone.restart(timeout=10)

    cmdline = 'egrep %s %s | egrep "uid=c\*"' % (SUFFIX, topology.standalone.accesslog)
    p = os.popen(cmdline, "r")
    l0 = p.readline()
    if l0 == "":
        log.error('Search with "(uid=c*)" is not logged in ' + topology.standalone.accesslog)
        assert False
    else:
        #regex = re.compile('\(conn=[0-9]* op=[0-9]*\) SRCH .*')
        regex = re.compile(r'.*\s+(conn=\d+ op=\d+)\s+SRCH .*')
        match = regex.match(l0)
        log.info('match: %s' % match.group(1))
        cmdline = 'egrep "%s" %s | egrep "RESULT"' % (match.group(1), topology.standalone.accesslog)
        p = os.popen(cmdline, "r")
        l1 = p.readline()
        if l1 == "":
            log.error('Search result of "(uid=c*)" is not logged in ' + topology.standalone.accesslog)
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

    cmdline = 'egrep %s %s | egrep "uid=\*2"' % (SUFFIX, topology.standalone.accesslog)
    p = os.popen(cmdline, "r")
    l0 = p.readline()
    if l0 == "":
        log.error('Search with "(uid=*2)" is not logged in ' + topology.standalone.accesslog)
        assert False
    else:
        #regex = re.compile('\(conn=[0-9]* op=[0-9]*\) SRCH .*')
        regex = re.compile(r'.*\s+(conn=\d+ op=\d+)\s+SRCH .*')
        match = regex.match(l0)
        log.info('match: %s' % match.group(1))
        cmdline = 'egrep "%s" %s | egrep "RESULT"' % (match.group(1), topology.standalone.accesslog)
        p = os.popen(cmdline, "r")
        l1 = p.readline()
        if l1 == "":
            log.error('Search result of "(uid=*2)" is not logged in ' + topology.standalone.accesslog)
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
        topology.standalone.modify_s(UID_INDEX,
                                     [(ldap.MOD_DELETE, 'nsIndexType', 'sub'),
                                      (ldap.MOD_DELETE, 'nsMatchingRule', 'nssubstrbegin=3'),
                                      (ldap.MOD_DELETE, 'nsMatchingRule', 'nssubstrend=3'),
                                      (ldap.MOD_DELETE, 'objectClass', 'extensibleObject'),
                                      (ldap.MOD_DELETE, 'nsSubStrBegin', '2'),
                                      (ldap.MOD_DELETE, 'nsSubStrEnd', '2')])
    except ldap.LDAPError, e:
        log.error('Failed to delete substr lengths: error ' + e.message['desc'])
        assert False

    log.info('Test complete')


def test_ticket48109_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket48109_0(topo)
    test_ticket48109_1(topo)
    test_ticket48109_2(topo)
    test_ticket48109_final(topo)


if __name__ == '__main__':
    run_isolated()

