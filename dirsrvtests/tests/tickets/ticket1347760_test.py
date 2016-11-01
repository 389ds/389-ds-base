# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import ldap
import logging
import pytest
from subprocess import Popen
from lib389 import DirSrv, Entry
from lib389.paths import Paths
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

CONFIG_DN = 'cn=config'
BOU = 'BOU'
BINDOU = 'ou=%s,%s' % (BOU, DEFAULT_SUFFIX)
BUID = 'buser123'
TUID = 'tuser0'
BINDDN = 'uid=%s,%s' % (BUID, BINDOU)
BINDPW = BUID
TESTDN = 'uid=%s,ou=people,%s' % (TUID, DEFAULT_SUFFIX)
TESTPW = TUID
BOGUSDN = 'uid=bogus,%s' % DEFAULT_SUFFIX
BOGUSDN2 = 'uid=bogus,ou=people,%s' % DEFAULT_SUFFIX
BOGUSSUFFIX = 'uid=bogus,ou=people,dc=bogus'
GROUPOU = 'ou=groups,%s' % DEFAULT_SUFFIX
BOGUSOU = 'ou=OU,%s' % DEFAULT_SUFFIX


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

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def pattern_accesslog(file, log_pattern):
    for i in range(5):
        try:
            pattern_accesslog.last_pos += 1
        except AttributeError:
            pattern_accesslog.last_pos = 0

        found = None
        file.seek(pattern_accesslog.last_pos)

        # Use a while true iteration because 'for line in file: hit a
        # python bug that break file.tell()
        while True:
            line = file.readline()
            found = log_pattern.search(line)
            if ((line == '') or (found)):
                break

        pattern_accesslog.last_pos = file.tell()
        if found:
            return line
        else:
            time.sleep(1)
    return None


def check_op_result(server, op, dn, superior, exists, rc):
    targetdn = dn
    if op == 'search':
        if exists:
            opstr = 'Searching existing entry'
        else:
            opstr = 'Searching non-existing entry'
    elif op == 'add':
        if exists:
            opstr = 'Adding existing entry'
        else:
            opstr = 'Adding non-existing entry'
    elif op == 'modify':
        if exists:
            opstr = 'Modifying existing entry'
        else:
            opstr = 'Modifying non-existing entry'
    elif op == 'modrdn':
        if superior != None:
            targetdn = superior
            if exists:
                opstr = 'Moving to existing superior'
            else:
                opstr = 'Moving to non-existing superior'
        else:
            if exists:
                opstr = 'Renaming existing entry'
            else:
                opstr = 'Renaming non-existing entry'
    elif op == 'delete':
        if exists:
            opstr = 'Deleting existing entry'
        else:
            opstr = 'Deleting non-existing entry'

    if ldap.SUCCESS == rc:
        expstr = 'be ok'
    else:
        expstr = 'fail with %s' % rc.__name__

    log.info('%s %s, which should %s.' % (opstr, targetdn, expstr))
    time.sleep(1)
    hit = 0
    try:
        if op == 'search':
            centry = server.search_s(dn, ldap.SCOPE_BASE, 'objectclass=*')
        elif op == 'add':
            server.add_s(Entry((dn, {'objectclass': 'top extensibleObject'.split(),
                                     'cn': 'test entry'})))
        elif op == 'modify':
            server.modify_s(dn, [(ldap.MOD_REPLACE, 'description', 'test')])
        elif op == 'modrdn':
            if superior != None:
                server.rename_s(dn, 'uid=new', newsuperior=superior, delold=1)
            else:
                server.rename_s(dn, 'uid=new', delold=1)
        elif op == 'delete':
            server.delete_s(dn)
        else:
            log.fatal('Unknown operation %s' % op)
            assert False
    except ldap.LDAPError as e:
        hit = 1
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc ' + e.message['desc'])
        assert isinstance(e, rc)
        if e.message.has_key('matched'):
            log.info('Matched is returned: ' + e.message['matched'])
            if rc != ldap.NO_SUCH_OBJECT:
                assert False

    if ldap.SUCCESS == rc:
        if op == 'search':
            log.info('Search should return none')
            assert len(centry) == 0
    else:
        if 0 == hit:
            log.info('Expected to fail with %s, but passed' % rc.__name__)
            assert False

    log.info('PASSED\n')


def test_ticket1347760(topology):
    """
    Prevent revealing the entry info to whom has no access rights.
    """
    log.info('Testing Bug 1347760 - Information disclosure via repeated use of LDAP ADD operation, etc.')

    log.info('Disabling accesslog logbuffering')
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-accesslog-logbuffering', 'off')])

    log.info('Bind as {%s,%s}' % (DN_DM, PASSWORD))
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info('Adding ou=%s a bind user belongs to.' % BOU)
    topology.standalone.add_s(Entry((BINDOU, {
                              'objectclass': 'top organizationalunit'.split(),
                              'ou': BOU})))

    log.info('Adding a bind user.')
    topology.standalone.add_s(Entry((BINDDN,
                                     {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                      'cn': 'bind user',
                                      'sn': 'user',
                                      'userPassword': BINDPW})))

    log.info('Adding a test user.')
    topology.standalone.add_s(Entry((TESTDN,
                                     {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                      'cn': 'test user',
                                      'sn': 'user',
                                      'userPassword': TESTPW})))

    log.info('Deleting aci in %s.' % DEFAULT_SUFFIX)
    topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', None)])

    log.info('While binding as DM, acquire an access log path')
    ds_paths = Paths(serverid=topology.standalone.serverid,
                     instance=topology.standalone)
    file_path = ds_paths.access_log

    log.info('Bind case 1. the bind user has no rights to read the entry itself, bind should be successful.')
    log.info('Bind as {%s,%s} who has no access rights.' % (BINDDN, BINDPW))
    try:
        topology.standalone.simple_bind_s(BINDDN, BINDPW)
    except ldap.LDAPError as e:
        log.info('Desc ' + e.message['desc'])
        assert False

    file_obj = open(file_path, "r")
    log.info('Access log path: %s' % file_path)

    log.info('Bind case 2-1. the bind user does not exist, bind should fail with error %s' % ldap.INVALID_CREDENTIALS.__name__)
    log.info('Bind as {%s,%s} who does not exist.' % (BOGUSDN, 'bogus'))
    try:
        topology.standalone.simple_bind_s(BOGUSDN, 'bogus')
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc ' + e.message['desc'])
        assert isinstance(e, ldap.INVALID_CREDENTIALS)
        regex = re.compile('No such entry')
        cause = pattern_accesslog(file_obj, regex)
        if cause == None:
            log.fatal('Cause not found - %s' % cause)
            assert False
        else:
            log.info('Cause found - %s' % cause)
    time.sleep(1)

    log.info('Bind case 2-2. the bind user\'s suffix does not exist, bind should fail with error %s' % ldap.INVALID_CREDENTIALS.__name__)
    log.info('Bind as {%s,%s} who does not exist.' % (BOGUSSUFFIX, 'bogus'))
    try:
        topology.standalone.simple_bind_s(BOGUSSUFFIX, 'bogus')
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc ' + e.message['desc'])
        assert isinstance(e, ldap.INVALID_CREDENTIALS)
        regex = re.compile('No such suffix')
        cause = pattern_accesslog(file_obj, regex)
        if cause == None:
            log.fatal('Cause not found - %s' % cause)
            assert False
        else:
            log.info('Cause found - %s' % cause)
    time.sleep(1)

    log.info('Bind case 2-3. the bind user\'s password is wrong, bind should fail with error %s' % ldap.INVALID_CREDENTIALS.__name__)
    log.info('Bind as {%s,%s} who does not exist.' % (BINDDN, 'bogus'))
    try:
        topology.standalone.simple_bind_s(BINDDN, 'bogus')
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc ' + e.message['desc'])
        assert isinstance(e, ldap.INVALID_CREDENTIALS)
        regex = re.compile('Invalid credentials')
        cause = pattern_accesslog(file_obj, regex)
        if cause == None:
            log.fatal('Cause not found - %s' % cause)
            assert False
        else:
            log.info('Cause found - %s' % cause)
    time.sleep(1)

    log.info('Adding aci for %s to %s.' % (BINDDN, BINDOU))
    acival = '(targetattr="*")(version 3.0; acl "%s"; allow(all) userdn = "ldap:///%s";)' % (BUID, BINDDN)
    log.info('aci: %s' % acival)
    log.info('Bind as {%s,%s}' % (DN_DM, PASSWORD))
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(BINDOU, [(ldap.MOD_ADD, 'aci', acival)])
    time.sleep(1)

    log.info('Bind case 3. the bind user has the right to read the entry itself, bind should be successful.')
    log.info('Bind as {%s,%s} which should be ok.\n' % (BINDDN, BINDPW))
    topology.standalone.simple_bind_s(BINDDN, BINDPW)

    log.info('The following operations are against the subtree the bind user %s has no rights.' % BINDDN)
    # Search
    exists = True
    rc = ldap.SUCCESS
    log.info('Search case 1. the bind user has no rights to read the search entry, it should return no search results with %s' % rc)
    check_op_result(topology.standalone, 'search', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.SUCCESS
    log.info('Search case 2-1. the search entry does not exist, the search should return no search results with %s' % rc.__name__)
    check_op_result(topology.standalone, 'search', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.SUCCESS
    log.info('Search case 2-2. the search entry does not exist, the search should return no search results with %s' % rc.__name__)
    check_op_result(topology.standalone, 'search', BOGUSDN2, None, exists, rc)

    # Add
    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Add case 1. the bind user has no rights AND the adding entry exists, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'add', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Add case 2-1. the bind user has no rights AND the adding entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'add', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Add case 2-2. the bind user has no rights AND the adding entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'add', BOGUSDN2, None, exists, rc)

    # Modify
    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modify case 1. the bind user has no rights AND the modifying entry exists, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modify', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modify case 2-1. the bind user has no rights AND the modifying entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modify', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modify case 2-2. the bind user has no rights AND the modifying entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modify', BOGUSDN2, None, exists, rc)

    # Modrdn
    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modrdn case 1. the bind user has no rights AND the renaming entry exists, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modrdn', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modrdn case 2-1. the bind user has no rights AND the renaming entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modrdn', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modrdn case 2-2. the bind user has no rights AND the renaming entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modrdn', BOGUSDN2, None, exists, rc)

    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modrdn case 3. the bind user has no rights AND the node moving an entry to exists, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modrdn', TESTDN, GROUPOU, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modrdn case 4-1. the bind user has no rights AND the node moving an entry to does not, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modrdn', TESTDN, BOGUSOU, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Modrdn case 4-2. the bind user has no rights AND the node moving an entry to does not, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modrdn', TESTDN, BOGUSOU, exists, rc)

    # Delete
    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Delete case 1. the bind user has no rights AND the deleting entry exists, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'delete', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Delete case 2-1. the bind user has no rights AND the deleting entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'delete', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info('Delete case 2-2. the bind user has no rights AND the deleting entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'delete', BOGUSDN2, None, exists, rc)

    log.info('EXTRA: Check no regressions')
    log.info('Adding aci for %s to %s.' % (BINDDN, DEFAULT_SUFFIX))
    acival = '(targetattr="*")(version 3.0; acl "%s-all"; allow(all) userdn = "ldap:///%s";)' % (BUID, BINDDN)
    log.info('Bind as {%s,%s}' % (DN_DM, PASSWORD))
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', acival)])
    time.sleep(1)

    log.info('Bind as {%s,%s}.' % (BINDDN, BINDPW))
    try:
        topology.standalone.simple_bind_s(BINDDN, BINDPW)
    except ldap.LDAPError as e:
        log.info('Desc ' + e.message['desc'])
        assert False
    time.sleep(1)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Search case. the search entry does not exist, the search should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'search', BOGUSDN2, None, exists, rc)
    file_obj.close()

    exists = True
    rc = ldap.ALREADY_EXISTS
    log.info('Add case. the adding entry already exists, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'add', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Modify case. the modifying entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modify', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Modrdn case 1. the renaming entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modrdn', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Modrdn case 2. the node moving an entry to does not, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'modrdn', TESTDN, BOGUSOU, exists, rc)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Delete case. the deleting entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology.standalone, 'delete', BOGUSDN, None, exists, rc)

    log.info('Inactivate %s' % BINDDN)
    nsinactivate = '%s/sbin/ns-inactivate.pl' % topology.standalone.prefix
    p = Popen([nsinactivate, '-Z', 'standalone', '-D', DN_DM, '-w', PASSWORD, '-I', BINDDN])
    assert(p.wait() == 0)

    log.info('Bind as {%s,%s} which should fail with %s.' % (BINDDN, BUID, ldap.UNWILLING_TO_PERFORM.__name__))
    try:
        topology.standalone.simple_bind_s(BINDDN, BUID)
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc ' + e.message['desc'])
        assert isinstance(e, ldap.UNWILLING_TO_PERFORM)

    log.info('Bind as {%s,%s} which should fail with %s.' % (BINDDN, 'bogus', ldap.INVALID_CREDENTIALS.__name__))
    try:
        topology.standalone.simple_bind_s(BINDDN, 'bogus')
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc ' + e.message['desc'])
        assert isinstance(e, ldap.INVALID_CREDENTIALS)

    log.info('SUCCESS')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
