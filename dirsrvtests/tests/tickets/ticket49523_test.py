import logging
import pytest
import os
import ldap
import time
import re
from lib389.plugins import MemberOfPlugin
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389 import Entry

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


USER_CN='user_'
GROUP_CN='group_'
def _user_get_dn(no):
    cn = '%s%d' % (USER_CN, no)
    dn = 'cn=%s,ou=people,%s' % (cn, SUFFIX)
    return (cn, dn)

def add_user(server, no, desc='dummy', sleep=True):
    (cn, dn) = _user_get_dn(no)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person'],
                             'cn': [cn],
                             'description': [desc],
                             'sn': [cn],
                             'description': ['add on that host']})))
    if sleep:
        time.sleep(2)
        
def add_group(server, nr, sleep=True):
    cn = '%s%d' % (GROUP_CN, nr)
    dn = 'cn=%s,ou=groups,%s' % (cn, SUFFIX)
    server.add_s(Entry((dn, {'objectclass': ['top', 'groupofnames'],
                             'description': 'group %d' % nr})))
    if sleep:
        time.sleep(2)

def update_member(server, member_dn, group_dn, op, sleep=True):
    mod = [(op, 'member', member_dn)]
    server.modify_s(group_dn, mod)
    if sleep:
        time.sleep(2)
        
def _find_memberof(server, member_dn, group_dn, find_result=True):
    ent = server.getEntry(member_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
    found = False
    if ent.hasAttr('memberof'):

        for val in ent.getValues('memberof'):
            server.log.info("!!!!!!! %s: memberof->%s" % (member_dn, val))
            server.log.info("!!!!!!! %s" % (val))
            server.log.info("!!!!!!! %s" % (group_dn))
            if val.lower() == group_dn.lower():
                found = True
                break

    if find_result:
        assert (found)
    else:
        assert (not found)
        
def pattern_accesslog(server, log_pattern):
    file_obj = open(server.accesslog, "r")

    found = False
    # Use a while true iteration because 'for line in file: hit a
    while True:
        line = file_obj.readline()
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break

    return found

def pattern_errorlog(server, log_pattern):
    file_obj = open(server.errlog, "r")

    found = None
    # Use a while true iteration because 'for line in file: hit a
    while True:
        line = file_obj.readline()
        found = log_pattern.search(line)
        server.log.fatal("%s --> %s" % (line, found))
        if ((line == '') or (found)):
            break

    return found
        
def test_ticket49523(topo):
    """Specify a test case purpose or name here

    :id: e2af0aaa-447e-4e85-a5ce-57ae66260d0b
    :setup: Fill in set up configuration here
    :steps:
        1. Fill in test case steps here
        2. And indent them like this (RST format requirement)
    :expectedresults:
        1. Fill in the result that is expected
        2. For each test step
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["master1"].serverid)
    inst = topo.standalone
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    inst.restart()
    
    # Step 2
    for i in range(10):
        add_user(inst, i, desc='add user')
    
    add_group(inst, 1)
    
    group_parent_dn = 'ou=groups,%s' % (SUFFIX)
    group_rdn = 'cn=%s%d' % (GROUP_CN, 1)
    group_dn  = '%s,%s' % (group_rdn, group_parent_dn)
    (member_cn, member_dn) = _user_get_dn(1)
    update_member(inst, member_dn, group_dn, ldap.MOD_ADD, sleep=False)
    
    _find_memberof(inst, member_dn, group_dn, find_result=True)
    
    pattern = ".*oc_check_allowed_sv - Entry.*cn=%s.* -- attribute.*not allowed.*" % member_cn
    log.fatal("pattern = %s" % pattern)
    regex = re.compile(pattern)
    assert pattern_errorlog(inst, regex)
    
    regex = re.compile(".*schema violation caught - repair operation.*")
    assert pattern_errorlog(inst, regex)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

