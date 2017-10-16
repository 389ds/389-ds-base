import logging
import pytest
import os
import time
import ldap
import subprocess
from lib389.utils import ds_is_older
from lib389.topologies import topology_m1h1c1 as topo
from lib389._constants import *
from lib389 import Entry

# Skip on older versions
pytestmark = pytest.mark.skipif(ds_is_older('1.3.7'), reason="Not implemented")

USER_CN='user_'
GROUP_CN='group_'
FIXUP_FILTER = '(objectClass=*)'
FIXUP_CMD = 'fixup-memberof.pl'

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def memberof_fixup_task(server):
    sbin_dir = server.get_sbin_dir()
    memof_task = os.path.join(sbin_dir, FIXUP_CMD)
    try:
        output = subprocess.check_output(
            [memof_task, '-D', DN_DM, '-w', PASSWORD, '-b', SUFFIX, '-Z', SERVERID_CONSUMER_1, '-f', FIXUP_FILTER])
    except subprocess.CalledProcessError as err:
        output = err.output
    log.info('output: {}'.format(output))
    expected = "Successfully added task entry"
    assert expected in output

def config_memberof(server):

    server.plugins.enable(name=PLUGIN_MEMBER_OF)
    MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
    server.modify_s(MEMBEROF_PLUGIN_DN, [(ldap.MOD_REPLACE,
                                          'memberOfAllBackends',
                                          'on'),
                                          (ldap.MOD_REPLACE, 'memberOfAutoAddOC', 'nsMemberOf')])
    # Configure fractional to prevent total init to send memberof
    ents = server.agreement.list(suffix=DEFAULT_SUFFIX)
    for ent in ents:
        log.info('update %s to add nsDS5ReplicatedAttributeListTotal' % ent.dn)
        server.modify_s(ent.dn,
                              [(ldap.MOD_REPLACE,
                                'nsDS5ReplicatedAttributeListTotal',
                                '(objectclass=*) $ EXCLUDE '),
                               (ldap.MOD_REPLACE,
                                'nsDS5ReplicatedAttributeList',
                                '(objectclass=*) $ EXCLUDE memberOf')])


def send_updates_now(server):

    ents = server.agreement.list(suffix=DEFAULT_SUFFIX)
    for ent in ents:
        server.agreement.pause(ent.dn)
        server.agreement.resume(ent.dn)
                                
def add_user(server, no, desc='dummy', sleep=True):
    cn = '%s%d' % (USER_CN, no)
    dn = 'cn=%s,ou=people,%s' % (cn, SUFFIX)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'inetuser'],
                             'sn': ['_%s' % cn],
                             'description': [desc]})))
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


def test_ticket49064(topo):
    """Specify a test case purpose or name here

    :id: 60c11636-55a1-4704-9e09-2c6bcc828de4
    :setup: 1 Master - 1 Hub - 1 Consumer
    :steps:
        1. Configure replication to EXCLUDE memberof
        2. Enable memberof plugin
        3. Create users/groups
        4. make user_1 member of group_1
        5. Checks that user_1 is memberof group_1 on M,H,C
        6. make group_1 member of group_2 (nest group)
        7. Checks that user_1 is memberof group_1 and group_2 on M,H,C
        8. Check group_1 is memberof group_2 on M,H,C
        9. remove group_1 from group_2
        10. Check group_1 and user_1 are NOT memberof group_2 on M,H,C
        11. remove user_1 from group_1
        12. Check user_1 is NOT memberof group_1 and group_2 on M,H,C
        13. Disable memberof on C1
        14. make user_1 member of group_1
        15. Checks that user is memberof group_1 on M,H but not on C
        16. Enable memberof on C1
        17. Checks that user is memberof group_1 on M,H but not on C
        18. Run memberof fixup task
        19. Checks that user is memberof group_1 on M,H,C

        
    :expectedresults:
        no assert for membership check
    """


    M1 = topo.ms["master1"]
    H1 = topo.hs["hub1"]
    C1 = topo.cs["consumer1"]

    # Step 1 & 2
    M1.config.enable_log('audit')
    config_memberof(M1)
    M1.restart()
    
    H1.config.enable_log('audit')
    config_memberof(H1)
    H1.restart()
    
    C1.config.enable_log('audit')
    config_memberof(C1)
    C1.restart()
    
    # Step 3
    for i in range(10):
        add_user(M1, i, desc='add on m1')
    for i in range(3):
        add_group(M1, i)
        
    # Step 4
    member_dn = 'cn=%s%d,ou=people,%s' % (USER_CN,  1, SUFFIX)
    group_dn  = 'cn=%s%d,ou=groups,%s' % (GROUP_CN, 1, SUFFIX)
    update_member(M1, member_dn, group_dn, ldap.MOD_ADD, sleep=True)
    
    # Step 5
    for i in [M1, H1, C1]:
        _find_memberof(i, member_dn, group_dn, find_result=True)
 

    # Step 6
    user_dn = 'cn=%s%d,ou=people,%s' % (USER_CN,  1, SUFFIX)
    grp1_dn = 'cn=%s%d,ou=groups,%s' % (GROUP_CN, 1, SUFFIX)
    grp2_dn = 'cn=%s%d,ou=groups,%s' % (GROUP_CN, 2, SUFFIX)
    update_member(M1, grp1_dn, grp2_dn, ldap.MOD_ADD, sleep=True)
    
    # Step 7
    for i in [grp1_dn, grp2_dn]:
        for inst in [M1, H1, C1]:
            _find_memberof(inst, user_dn, i, find_result=True)

    # Step 8
    for i in [M1, H1, C1]:
        _find_memberof(i, grp1_dn, grp2_dn, find_result=True)
        
    # Step 9
    user_dn = 'cn=%s%d,ou=people,%s' % (USER_CN,  1, SUFFIX)
    grp1_dn = 'cn=%s%d,ou=groups,%s' % (GROUP_CN, 1, SUFFIX)
    grp2_dn = 'cn=%s%d,ou=groups,%s' % (GROUP_CN, 2, SUFFIX)
    update_member(M1, grp1_dn, grp2_dn, ldap.MOD_DELETE, sleep=True)

    # Step 10
    for inst in [M1, H1, C1]:
        for i in [grp1_dn, user_dn]:
            _find_memberof(inst, i, grp2_dn, find_result=False)
    
    # Step 11
    member_dn = 'cn=%s%d,ou=people,%s' % (USER_CN,  1, SUFFIX)
    group_dn  = 'cn=%s%d,ou=groups,%s' % (GROUP_CN, 1, SUFFIX)
    update_member(M1, member_dn, group_dn, ldap.MOD_DELETE, sleep=True)
    
    # Step 12
    for inst in [M1, H1, C1]:
        for grp in [grp1_dn, grp2_dn]:
            _find_memberof(inst, member_dn, grp, find_result=False)
    
    # Step 13
    C1.plugins.disable(name=PLUGIN_MEMBER_OF)
    C1.restart()
    
    # Step 14
    member_dn = 'cn=%s%d,ou=people,%s' % (USER_CN,  1, SUFFIX)
    group_dn  = 'cn=%s%d,ou=groups,%s' % (GROUP_CN, 1, SUFFIX)
    update_member(M1, member_dn, group_dn, ldap.MOD_ADD, sleep=True)
    
    # Step 15
    for i in [M1, H1]:
        _find_memberof(i, member_dn, group_dn, find_result=True)
    _find_memberof(C1, member_dn, group_dn, find_result=False)
    
    # Step 16
    C1.plugins.enable(name=PLUGIN_MEMBER_OF)
    C1.restart()
    
    # Step 17
    for i in [M1, H1]:
        _find_memberof(i, member_dn, group_dn, find_result=True)
    _find_memberof(C1, member_dn, group_dn, find_result=False)
    
    # Step 18
    memberof_fixup_task(C1)
    time.sleep(5)

    # Step 19
    for i in [M1, H1, C1]:
        _find_memberof(i, member_dn, group_dn, find_result=True)
        
    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["master1"].serverid)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

