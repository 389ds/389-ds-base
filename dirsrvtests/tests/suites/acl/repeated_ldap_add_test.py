# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from subprocess import Popen

import pytest
from lib389.paths import Paths
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DN_DM, DEFAULT_SUFFIX, PASSWORD, SERVERID_STANDALONE

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

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

def get_ldap_error_msg(e, type):
    return e.args[0][type]

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
        if superior is not None:
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
            server.modify_s(dn, [(ldap.MOD_REPLACE, 'description', b'test')])
        elif op == 'modrdn':
            if superior is not None:
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
        log.info('Desc {}'.format(get_ldap_error_msg(e,'desc')))
        assert isinstance(e, rc)
        if 'matched' in e.args:
            log.info('Matched is returned: {}'.format(get_ldap_error_msg(e, 'matched')))
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


@pytest.mark.bz1347760
def test_repeated_ldap_add(topology_st):
    """Prevent revealing the entry info to whom has no access rights.

    :id: 76d278bd-3e51-4579-951a-753e6703b4df
    :setup: Standalone instance
    :steps:
        1.  Disable accesslog logbuffering
        2.  Bind as "cn=Directory Manager"
        3.  Add a organisational unit as BOU
        4.  Add a bind user as uid=buser123,ou=BOU,dc=example,dc=com
        5.  Add a test user as uid=tuser0,ou=People,dc=example,dc=com
        6.  Delete aci in dc=example,dc=com
        7.  Bind as Directory Manager, acquire an access log path and instance dir
        8.  Bind as uid=buser123,ou=BOU,dc=example,dc=com who has no right to read the entry
        9.  Bind as uid=bogus,ou=people,dc=bogus,bogus who does not exist
        10. Bind as uid=buser123,ou=BOU,dc=example,dc=com,bogus with wrong password
        11. Adding aci for uid=buser123,ou=BOU,dc=example,dc=com to ou=BOU,dc=example,dc=com.
        12. Bind as uid=buser123,ou=BOU,dc=example,dc=com now who has right to read the entry
    :expectedresults:
        1.  Operation should be successful
        2.  Operation should be successful
        3.  Operation should be successful
        4.  Operation should be successful
        5.  Operation should be successful
        6.  Operation should be successful
        7.  Operation should be successful
        8.  Bind operation should be successful with no search result
        9.  Bind operation should Fail
        10. Bind operation should Fail
        11. Operation should be successful
        12. Bind operation should be successful with search result
    """
    log.info('Testing Bug 1347760 - Information disclosure via repeated use of LDAP ADD operation, etc.')

    log.info('Disabling accesslog logbuffering')
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-accesslog-logbuffering', b'off')])

    log.info('Bind as {%s,%s}' % (DN_DM, PASSWORD))
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info('Adding ou=%s a bind user belongs to.' % BOU)
    topology_st.standalone.add_s(Entry((BINDOU, {
        'objectclass': 'top organizationalunit'.split(),
        'ou': BOU})))

    log.info('Adding a bind user.')
    topology_st.standalone.add_s(Entry((BINDDN,
                                        {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                         'cn': 'bind user',
                                         'sn': 'user',
                                         'userPassword': BINDPW})))

    log.info('Adding a test user.')
    topology_st.standalone.add_s(Entry((TESTDN,
                                        {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                         'cn': 'test user',
                                         'sn': 'user',
                                         'userPassword': TESTPW})))

    log.info('Deleting aci in %s.' % DEFAULT_SUFFIX)
    topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', None)])

    log.info('While binding as DM, acquire an access log path and instance dir')
    ds_paths = Paths(serverid=topology_st.standalone.serverid,
                     instance=topology_st.standalone)
    file_path = ds_paths.access_log
    inst_dir = ds_paths.inst_dir

    log.info('Bind case 1. the bind user has no rights to read the entry itself, bind should be successful.')
    log.info('Bind as {%s,%s} who has no access rights.' % (BINDDN, BINDPW))
    try:
        topology_st.standalone.simple_bind_s(BINDDN, BINDPW)
    except ldap.LDAPError as e:
        log.info('Desc {}'.format(get_ldap_error_msg(e,'desc')))
        assert False

    file_obj = open(file_path, "r")
    log.info('Access log path: %s' % file_path)

    log.info(
        'Bind case 2-1. the bind user does not exist, bind should fail with error %s' % ldap.INVALID_CREDENTIALS.__name__)
    log.info('Bind as {%s,%s} who does not exist.' % (BOGUSDN, 'bogus'))
    try:
        topology_st.standalone.simple_bind_s(BOGUSDN, 'bogus')
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc {}'.format(get_ldap_error_msg(e,'desc')))
        assert isinstance(e, ldap.INVALID_CREDENTIALS)
        regex = re.compile('No such entry')
        cause = pattern_accesslog(file_obj, regex)
        if cause is None:
            log.fatal('Cause not found - %s' % cause)
            assert False
        else:
            log.info('Cause found - %s' % cause)
    time.sleep(1)

    log.info(
        'Bind case 2-2. the bind user\'s suffix does not exist, bind should fail with error %s' % ldap.INVALID_CREDENTIALS.__name__)
    log.info('Bind as {%s,%s} who does not exist.' % (BOGUSSUFFIX, 'bogus'))
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        topology_st.standalone.simple_bind_s(BOGUSSUFFIX, 'bogus')
    regex = re.compile('No suffix for bind')
    cause = pattern_accesslog(file_obj, regex)
    if cause is None:
        log.fatal('Cause not found - %s' % cause)
        assert False
    else:
        log.info('Cause found - %s' % cause)
    time.sleep(1)

    log.info(
        'Bind case 2-3. the bind user\'s password is wrong, bind should fail with error %s' % ldap.INVALID_CREDENTIALS.__name__)
    log.info('Bind as {%s,%s} who does not exist.' % (BINDDN, 'bogus'))
    try:
        topology_st.standalone.simple_bind_s(BINDDN, 'bogus')
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc {}'.format(get_ldap_error_msg(e,'desc')))
        assert isinstance(e, ldap.INVALID_CREDENTIALS)
        regex = re.compile('Invalid credentials')
        cause = pattern_accesslog(file_obj, regex)
        if cause is None:
            log.fatal('Cause not found - %s' % cause)
            assert False
        else:
            log.info('Cause found - %s' % cause)
    time.sleep(1)

    log.info('Adding aci for %s to %s.' % (BINDDN, BINDOU))
    acival = '(targetattr="*")(version 3.0; acl "%s"; allow(all) userdn = "ldap:///%s";)' % (BUID, BINDDN)
    log.info('aci: %s' % acival)
    log.info('Bind as {%s,%s}' % (DN_DM, PASSWORD))
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(BINDOU, [(ldap.MOD_ADD, 'aci', ensure_bytes(acival))])
    time.sleep(1)

    log.info('Bind case 3. the bind user has the right to read the entry itself, bind should be successful.')
    log.info('Bind as {%s,%s} which should be ok.\n' % (BINDDN, BINDPW))
    topology_st.standalone.simple_bind_s(BINDDN, BINDPW)

    log.info('The following operations are against the subtree the bind user %s has no rights.' % BINDDN)
    # Search
    exists = True
    rc = ldap.SUCCESS
    log.info(
        'Search case 1. the bind user has no rights to read the search entry, it should return no search results with %s' % rc)
    check_op_result(topology_st.standalone, 'search', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.SUCCESS
    log.info(
        'Search case 2-1. the search entry does not exist, the search should return no search results with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'search', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.SUCCESS
    log.info(
        'Search case 2-2. the search entry does not exist, the search should return no search results with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'search', BOGUSDN2, None, exists, rc)

    # Add
    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Add case 1. the bind user has no rights AND the adding entry exists, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'add', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Add case 2-1. the bind user has no rights AND the adding entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'add', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Add case 2-2. the bind user has no rights AND the adding entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'add', BOGUSDN2, None, exists, rc)

    # Modify
    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modify case 1. the bind user has no rights AND the modifying entry exists, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modify', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modify case 2-1. the bind user has no rights AND the modifying entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modify', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modify case 2-2. the bind user has no rights AND the modifying entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modify', BOGUSDN2, None, exists, rc)

    # Modrdn
    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modrdn case 1. the bind user has no rights AND the renaming entry exists, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modrdn', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modrdn case 2-1. the bind user has no rights AND the renaming entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modrdn', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modrdn case 2-2. the bind user has no rights AND the renaming entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modrdn', BOGUSDN2, None, exists, rc)

    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modrdn case 3. the bind user has no rights AND the node moving an entry to exists, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modrdn', TESTDN, GROUPOU, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modrdn case 4-1. the bind user has no rights AND the node moving an entry to does not, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modrdn', TESTDN, BOGUSOU, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Modrdn case 4-2. the bind user has no rights AND the node moving an entry to does not, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modrdn', TESTDN, BOGUSOU, exists, rc)

    # Delete
    exists = True
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Delete case 1. the bind user has no rights AND the deleting entry exists, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'delete', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Delete case 2-1. the bind user has no rights AND the deleting entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'delete', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.INSUFFICIENT_ACCESS
    log.info(
        'Delete case 2-2. the bind user has no rights AND the deleting entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'delete', BOGUSDN2, None, exists, rc)

    log.info('EXTRA: Check no regressions')
    log.info('Adding aci for %s to %s.' % (BINDDN, DEFAULT_SUFFIX))
    acival = '(targetattr="*")(version 3.0; acl "%s-all"; allow(all) userdn = "ldap:///%s";)' % (BUID, BINDDN)
    log.info('Bind as {%s,%s}' % (DN_DM, PASSWORD))
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', ensure_bytes(acival))])
    time.sleep(1)

    log.info('Bind as {%s,%s}.' % (BINDDN, BINDPW))
    try:
        topology_st.standalone.simple_bind_s(BINDDN, BINDPW)
    except ldap.LDAPError as e:
        log.info('Desc {}'.format(get_ldap_error_msg(e,'desc')))
        assert False
    time.sleep(1)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Search case. the search entry does not exist, the search should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'search', BOGUSDN2, None, exists, rc)
    file_obj.close()

    exists = True
    rc = ldap.ALREADY_EXISTS
    log.info('Add case. the adding entry already exists, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'add', TESTDN, None, exists, rc)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Modify case. the modifying entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modify', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Modrdn case 1. the renaming entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modrdn', BOGUSDN, None, exists, rc)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Modrdn case 2. the node moving an entry to does not, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'modrdn', TESTDN, BOGUSOU, exists, rc)

    exists = False
    rc = ldap.NO_SUCH_OBJECT
    log.info('Delete case. the deleting entry does not exist, it should fail with %s' % rc.__name__)
    check_op_result(topology_st.standalone, 'delete', BOGUSDN, None, exists, rc)

    log.info('Inactivate %s' % BINDDN)
    if ds_paths.version < '1.3':
        nsinactivate = '%s/ns-inactivate.pl' % inst_dir
        cli_cmd = [nsinactivate, '-D', DN_DM, '-w', PASSWORD, '-I', BINDDN]
    else:
        dsidm = '%s/dsidm' % ds_paths.sbin_dir
        cli_cmd = [dsidm, SERVERID_STANDALONE, '-b', DEFAULT_SUFFIX, 'account', 'lock', BINDDN]
    log.info(cli_cmd)
    p = Popen(cli_cmd)
    assert (p.wait() == 0)

    log.info('Bind as {%s,%s} which should fail with %s.' % (BINDDN, BUID, ldap.UNWILLING_TO_PERFORM.__name__))
    try:
        topology_st.standalone.simple_bind_s(BINDDN, BUID)
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc {}'.format(get_ldap_error_msg(e,'desc')))
        assert isinstance(e, ldap.UNWILLING_TO_PERFORM)

    log.info('Bind as {%s,%s} which should fail with %s.' % (BINDDN, 'bogus', ldap.UNWILLING_TO_PERFORM.__name__))
    try:
        topology_st.standalone.simple_bind_s(BINDDN, 'bogus')
    except ldap.LDAPError as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        log.info('Desc {}'.format(get_ldap_error_msg(e,'desc')))
        assert isinstance(e, ldap.UNWILLING_TO_PERFORM)

    log.info('SUCCESS')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

