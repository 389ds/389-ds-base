# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2c2 as topo

from lib389._constants import (PLUGIN_ACCT_POLICY, DN_PLUGIN, DN_CONFIG, DN_DM, PASSWORD,
                              DEFAULT_SUFFIX, SUFFIX)

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

ACCPOL_DN = "cn={},{}".format(PLUGIN_ACCT_POLICY, DN_PLUGIN)
ACCP_CONF = "{},{}".format(DN_CONFIG, ACCPOL_DN)
USER_PW = 'Secret123'


def _last_login_time(topo, userdn, inst_name, last_login):
    """Find lastLoginTime attribute value for a given supplier/consumer"""

    if 'supplier' in inst_name:
        if (last_login == 'bind_n_check'):
            topo.ms[inst_name].simple_bind_s(userdn, USER_PW)
        topo.ms[inst_name].simple_bind_s(DN_DM, PASSWORD)
        entry = topo.ms[inst_name].search_s(userdn, ldap.SCOPE_BASE, 'objectClass=*', ['lastLoginTime'])
    else:
        if (last_login == 'bind_n_check'):
            topo.cs[inst_name].simple_bind_s(userdn, USER_PW)
        topo.cs[inst_name].simple_bind_s(DN_DM, PASSWORD)
        entry = topo.cs[inst_name].search_s(userdn, ldap.SCOPE_BASE, 'objectClass=*', ['lastLoginTime'])
    lastLogin = entry[0].lastLoginTime
    time.sleep(1)
    return lastLogin


def _enable_plugin(topo, inst_name):
    """Enable account policy plugin and configure required attributes"""

    log.info('Enable account policy plugin and configure required attributes')
    if 'supplier' in inst_name:
        log.info('Configure Account policy plugin on {}'.format(inst_name))
        topo.ms[inst_name].simple_bind_s(DN_DM, PASSWORD)
        try:
            topo.ms[inst_name].plugins.enable(name=PLUGIN_ACCT_POLICY)
            topo.ms[inst_name].modify_s(ACCPOL_DN, [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', ensure_bytes(ACCP_CONF))])
            topo.ms[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', b'yes')])
            topo.ms[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'stateattrname', b'lastLoginTime')])
            topo.ms[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'altstateattrname', b'createTimestamp')])
            topo.ms[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'specattrname', b'acctPolicySubentry')])
            topo.ms[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'limitattrname', b'accountInactivityLimit')])
            topo.ms[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'accountInactivityLimit', b'3600')])
        except ldap.LDAPError as e:
            log.error('Failed to configure {} plugin for inst-{} error: {}'.format(PLUGIN_ACCT_POLICY, inst_name, str(e)))
        topo.ms[inst_name].restart(timeout=10)
    else:
        log.info('Configure Account policy plugin on {}'.format(inst_name))
        topo.cs[inst_name].simple_bind_s(DN_DM, PASSWORD)
        try:
            topo.cs[inst_name].plugins.enable(name=PLUGIN_ACCT_POLICY)
            topo.cs[inst_name].modify_s(ACCPOL_DN, [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', ensure_bytes(ACCP_CONF))])
            topo.cs[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'alwaysrecordlogin', b'yes')])
            topo.cs[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'stateattrname', b'lastLoginTime')])
            topo.cs[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'altstateattrname', b'createTimestamp')])
            topo.cs[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'specattrname', b'acctPolicySubentry')])
            topo.cs[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'limitattrname', b'accountInactivityLimit')])
            topo.cs[inst_name].modify_s(ACCP_CONF, [(ldap.MOD_REPLACE, 'accountInactivityLimit', b'3600')])
        except ldap.LDAPError as e:
            log.error('Failed to configure {} plugin for inst-{} error {}'.format(PLUGIN_ACCT_POLICY, inst_name, str(e)))
        topo.cs[inst_name].restart(timeout=10)


def test_ticket48944(topo):
    """On a read only replica invalid state info can accumulate

    :id: 833be131-f3bf-493e-97c6-3121438a07b1
    :feature: Account Policy Plugin
    :setup: Two supplier and two consumer setup
    :steps: 1. Configure Account policy plugin with alwaysrecordlogin set to yes
            2. Check if entries are synced across suppliers and consumers
            3. Stop all suppliers and consumers
            4. Start supplier1 and bind as user1 to create lastLoginTime attribute
            5. Start supplier2 and wait for the sync of lastLoginTime attribute
            6. Stop supplier1 and bind as user1 from supplier2
            7. Check if lastLoginTime attribute is updated and greater than supplier1
            8. Stop supplier2, start consumer1, consumer2 and then supplier2
            9. Check if lastLoginTime attribute is updated on both consumers
            10. Bind as user1 to both consumers and check the value is updated
            11. Check if lastLoginTime attribute is not updated from consumers
            12. Start supplier1 and make sure the lastLoginTime attribute is not updated on consumers
            13. Bind as user1 from supplier1 and check if all suppliers and consumers have the same value
            14. Check error logs of consumers for "deletedattribute;deleted" message
    :expectedresults: No accumulation of replica invalid state info on consumers
    """

    log.info("Ticket 48944 - On a read only replica invalid state info can accumulate")
    user_name = 'newbzusr'
    tuserdn = 'uid={}1,ou=people,{}'.format(user_name, SUFFIX)
    inst_list = ['supplier1', 'supplier2', 'consumer1', 'consumer2']
    for inst_name in inst_list:
        _enable_plugin(topo, inst_name)

    log.info('Sleep for 10secs for the server to come up')
    time.sleep(10)
    log.info('Add few entries to server and check if entries are replicated')
    for nos in range(10):
        userdn = 'uid={}{},ou=people,{}'.format(user_name, nos, SUFFIX)
        try:
            topo.ms['supplier1'].add_s(Entry((userdn, {
                'objectclass': 'top person'.split(),
                'objectclass': 'inetorgperson',
                'cn': user_name,
                'sn': user_name,
                'userpassword': USER_PW,
                'mail': '{}@redhat.com'.format(user_name)})))
        except ldap.LDAPError as e:
            log.error('Failed to add {} user: error {}'.format(userdn, e.message['desc']))
            raise e

    log.info('Checking if entries are synced across suppliers and consumers')
    entries_m1 = topo.ms['supplier1'].search_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid={}*'.format(user_name), ['uid=*'])
    exp_entries = str(entries_m1).count('dn: uid={}*'.format(user_name))
    entries_m2 = topo.ms['supplier2'].search_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid={}*'.format(user_name), ['uid=*'])
    act_entries = str(entries_m2).count('dn: uid={}*'.format(user_name))
    assert act_entries == exp_entries
    inst_list = ['consumer1', 'consumer2']
    for inst in inst_list:
        entries_other = topo.cs[inst].search_s(SUFFIX, ldap.SCOPE_SUBTREE, 'uid={}*'.format(user_name), ['uid=*'])
        act_entries = str(entries_other).count('dn: uid={}*'.format(user_name))
        assert act_entries == exp_entries

    topo.ms['supplier2'].stop(timeout=10)
    topo.ms['supplier1'].stop(timeout=10)
    topo.cs['consumer1'].stop(timeout=10)
    topo.cs['consumer2'].stop(timeout=10)

    topo.ms['supplier1'].start(timeout=10)
    lastLogin_m1_1 = _last_login_time(topo, tuserdn, 'supplier1', 'bind_n_check')

    log.info('Start supplier2 to sync lastLoginTime attribute from supplier1')
    topo.ms['supplier2'].start(timeout=10)
    time.sleep(5)
    log.info('Stop supplier1')
    topo.ms['supplier1'].stop(timeout=10)
    log.info('Bind as user1 to supplier2 and check if lastLoginTime attribute is greater than supplier1')
    lastLogin_m2_1 = _last_login_time(topo, tuserdn, 'supplier2', 'bind_n_check')
    assert lastLogin_m2_1 > lastLogin_m1_1

    log.info('Start all servers except supplier1')
    topo.ms['supplier2'].stop(timeout=10)
    topo.cs['consumer1'].start(timeout=10)
    topo.cs['consumer2'].start(timeout=10)
    topo.ms['supplier2'].start(timeout=10)
    time.sleep(10)
    log.info('Check if consumers are updated with lastLoginTime attribute value from supplier2')
    lastLogin_c1_1 = _last_login_time(topo, tuserdn, 'consumer1', 'check')
    assert lastLogin_c1_1 == lastLogin_m2_1

    lastLogin_c2_1 = _last_login_time(topo, tuserdn, 'consumer2', 'check')
    assert lastLogin_c2_1 == lastLogin_m2_1

    log.info('Check if lastLoginTime update in consumers not synced to supplier2')
    lastLogin_c1_2 = _last_login_time(topo, tuserdn, 'consumer1', 'bind_n_check')
    assert lastLogin_c1_2 > lastLogin_m2_1

    lastLogin_c2_2 = _last_login_time(topo, tuserdn, 'consumer2', 'bind_n_check')
    assert lastLogin_c2_2 > lastLogin_m2_1

    time.sleep(10)  # Allow replication to kick in
    lastLogin_m2_2 = _last_login_time(topo, tuserdn, 'supplier2', 'check')
    assert lastLogin_m2_2 == lastLogin_m2_1

    log.info('Start supplier1 and check if its updating its older lastLoginTime attribute to consumers')
    topo.ms['supplier1'].start(timeout=10)
    time.sleep(10)
    lastLogin_c1_3 = _last_login_time(topo, tuserdn, 'consumer1', 'check')
    assert lastLogin_c1_3 == lastLogin_c1_2

    lastLogin_c2_3 = _last_login_time(topo, tuserdn, 'consumer2', 'check')
    assert lastLogin_c2_3 == lastLogin_c2_2

    log.info('Check if lastLoginTime update from supplier2 is synced to all suppliers and consumers')
    lastLogin_m2_3 = _last_login_time(topo, tuserdn, 'supplier2', 'bind_n_check')
    time.sleep(10)  # Allow replication to kick in
    lastLogin_m1_2 = _last_login_time(topo, tuserdn, 'supplier1', 'check')
    lastLogin_c1_4 = _last_login_time(topo, tuserdn, 'consumer1', 'check')
    lastLogin_c2_4 = _last_login_time(topo, tuserdn, 'consumer2', 'check')
    assert lastLogin_m2_3 == lastLogin_m1_2 == lastLogin_c2_4 == lastLogin_c1_4

    log.info('Checking consumer error logs for replica invalid state info')
    assert not topo.cs['consumer2'].ds_error_log.match('.*deletedattribute;deleted.*')
    assert not topo.cs['consumer1'].ds_error_log.match('.*deletedattribute;deleted.*')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
