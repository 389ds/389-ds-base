# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import logging
import pytest
import os
from lib389 import DirSrv, pid_from_file
from lib389.dseldif import DSEldif
from lib389.tasks import *
from lib389.topologies import topology_m2, topology_st as topo
from lib389.utils import *
from lib389._constants import DN_CONFIG, DEFAULT_SUFFIX, DEFAULT_BENAME
from lib389._mapped_object import DSLdapObjects
from lib389.cli_base import FakeArgs
from lib389.cli_conf.backend import db_config_set
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.group import Groups
from lib389.instance.setup import SetupDs
from lib389.config import LDBMConfig, BDB_LDBMConfig, Config
from lib389.cos import CosPointerDefinitions, CosTemplates
from lib389.backend import Backends, DatabaseConfig
from lib389.monitor import MonitorLDBM, Monitor
from lib389.plugins import ReferentialIntegrityPlugin

pytestmark = pytest.mark.tier0

USER_DN = 'uid=test_user,%s' % DEFAULT_SUFFIX
PSTACK_CMD = '/usr/bin/pstack'

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

@pytest.fixture(scope="module")
def big_file():
    TEMP_BIG_FILE = ''
    # 1024*1024=1048576
    # B for 1 MiB
    # Big for 3 MiB
    for x in range(1048576):
        TEMP_BIG_FILE += '+'

    return TEMP_BIG_FILE


@pytest.mark.skipif(ds_is_older('1.4.3.16'), reason="This config setting exists in 1.4.3.16 and higher")
def test_nagle_default_value(topo):
    """Test that nsslapd-nagle attribute is off by default

    :id: 00361f5d-d638-4d39-8231-66fa52637203
    :setup: Standalone instance
    :steps:
        1. Create instance
        2. Check the value of nsslapd-nagle
    :expectedresults:
        1. Success
        2. The value of nsslapd-nagle should be off
    """

    log.info('Check the value of nsslapd-nagle attribute is off by default')
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-nagle') == 'off'


def test_maxbersize_repl(topology_m2, big_file):
    """maxbersize is ignored in the replicated operations.

    :id: ad57de60-7d56-4323-bbca-5556e5cdb126
    :setup: MMR with two suppliers, test user,
            1 MiB big value for any attribute
    :steps:
        1. Set maxbersize attribute to a small value (20KiB) on supplier2
        2. Add the big value to supplier2
        3. Add the big value to supplier1
        4. Check if the big value was successfully replicated to supplier2
    :expectedresults:
        1. maxbersize should be successfully set
        2. Adding the big value to supplier2 failed
        3. Adding the big value to supplier1 succeed
        4. The big value is successfully replicated to supplier2
    """

    users_m1 = UserAccounts(topology_m2.ms["supplier1"], DEFAULT_SUFFIX)
    users_m2 = UserAccounts(topology_m2.ms["supplier2"], DEFAULT_SUFFIX)

    user_m1 = users_m1.create(properties=TEST_USER_PROPERTIES)
    time.sleep(2)
    user_m2 = users_m2.get(dn=user_m1.dn)

    log.info("Set nsslapd-maxbersize: 20K to supplier2")
    topology_m2.ms["supplier2"].config.set('nsslapd-maxbersize', '20480')

    topology_m2.ms["supplier2"].restart()

    log.info('Try to add attribute with a big value to supplier2 - expect to FAIL')
    with pytest.raises(ldap.SERVER_DOWN):
        user_m2.add('jpegphoto', big_file)

    topology_m2.ms["supplier2"].restart()
    topology_m2.ms["supplier1"].restart()

    log.info('Try to add attribute with a big value to supplier1 - expect to PASS')
    user_m1.add('jpegphoto', big_file)

    time.sleep(2)

    log.info('Check if a big value was successfully added to supplier1')

    photo_m1 = user_m1.get_attr_vals('jpegphoto')

    log.info('Check if a big value was successfully replicated to supplier2')
    photo_m2 = user_m2.get_attr_vals('jpegphoto')
    nbtries = 0;
    while photo_m2 != photo_m1 and nbtries < 10:
        nbtries = nbtries + 1
        photo_m2 = user_m2.get_attr_vals('jpegphoto')
    assert photo_m2 == photo_m1

def test_config_listen_backport_size(topology_m2):
    """Check that nsslapd-listen-backlog-size acted as expected

    :id: a4385d58-a6ab-491e-a604-6df0e8ed91cd
    :setup: MMR with two suppliers
    :steps:
        1. Search for nsslapd-listen-backlog-size
        2. Set nsslapd-listen-backlog-size to a positive value
        3. Set nsslapd-listen-backlog-size to a negative value
        4. Set nsslapd-listen-backlog-size to an invalid value
        5. Set nsslapd-listen-backlog-size back to a default value
    :expectedresults:
        1. Search should be successful
        2. nsslapd-listen-backlog-size should be successfully set
        3. nsslapd-listen-backlog-size should be successfully set
        4. Modification with an invalid value should throw an error
        5. nsslapd-listen-backlog-size should be successfully set
    """

    default_val = topology_m2.ms["supplier1"].config.get_attr_val_bytes('nsslapd-listen-backlog-size')

    topology_m2.ms["supplier1"].config.replace('nsslapd-listen-backlog-size', '256')

    topology_m2.ms["supplier1"].config.replace('nsslapd-listen-backlog-size', '-1')

    with pytest.raises(ldap.LDAPError):
        topology_m2.ms["supplier1"].config.replace('nsslapd-listen-backlog-size', 'ZZ')

    topology_m2.ms["supplier1"].config.replace('nsslapd-listen-backlog-size', default_val)


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_config_deadlock_policy(topology_m2):
    """Check that nsslapd-db-deadlock-policy acted as expected

    :id: a24e25fd-bc15-47fa-b018-372f6a2ec59c
    :setup: MMR with two suppliers
    :steps:
        1. Search for nsslapd-db-deadlock-policy and check if
           it contains a default value
        2. Set nsslapd-db-deadlock-policy to a positive value
        3. Set nsslapd-db-deadlock-policy to a negative value
        4. Set nsslapd-db-deadlock-policy to an invalid value
        5. Set nsslapd-db-deadlock-policy back to a default value
    :expectedresults:
        1. Search should be a successful and should contain a default value
        2. nsslapd-db-deadlock-policy should be successfully set
        3. nsslapd-db-deadlock-policy should be successfully set
        4. Modification with an invalid value should throw an error
        5. nsslapd-db-deadlock-policy should be successfully set
    """

    default_val = b'9'

    ldbmconfig = LDBMConfig(topology_m2.ms["supplier1"])
    bdbconfig = BDB_LDBMConfig(topology_m2.ms["supplier1"])

    if ds_is_older('1.4.2'):
        deadlock_policy = ldbmconfig.get_attr_val_bytes('nsslapd-db-deadlock-policy')
    else:
        deadlock_policy = bdbconfig.get_attr_val_bytes('nsslapd-db-deadlock-policy')

    assert deadlock_policy == default_val

    # Try a range of valid values
    for val in (b'0', b'5', b'9'):
        ldbmconfig.replace('nsslapd-db-deadlock-policy', val)
        if ds_is_older('1.4.2'):
            deadlock_policy = ldbmconfig.get_attr_val_bytes('nsslapd-db-deadlock-policy')
        else:
            deadlock_policy = bdbconfig.get_attr_val_bytes('nsslapd-db-deadlock-policy')

        assert deadlock_policy == val

    # Try a range of invalid values
    for val in ('-1', '10'):
        with pytest.raises(ldap.LDAPError):
            ldbmconfig.replace('nsslapd-db-deadlock-policy', val)

    # Cleanup - undo what we've done
    ldbmconfig.replace('nsslapd-db-deadlock-policy', deadlock_policy)


def test_defaultnamingcontext(topo):
    """Tests configuration attribute defaultNamingContext in the rootdse

    :id: de9a21d3-00f9-4c6d-bb40-56aa1ba36578
    :setup: Standalone instance
    :steps:
        1. Check the attribute nsslapd-defaultnamingcontext is present in cn=config
        2. Delete nsslapd-defaultnamingcontext attribute
        3. Add new valid Suffix and modify nsslapd-defaultnamingcontext with new suffix
        4. Add new invalid value at runtime to nsslapd-defaultnamingcontext
        5. Modify nsslapd-defaultnamingcontext with blank value
        6. Add new suffix when nsslapd-defaultnamingcontext is empty
        7. Check the value of the nsslapd-defaultnamingcontext automatically have the new suffix
        8. Adding new suffix when nsslapd-defaultnamingcontext is not empty
        9. Check the value of the nsslapd-defaultnamingcontext has not changed
        10. Remove the newly added suffix and check the values of the attribute is not changed
        11. Remove the original suffix which is currently nsslapd-defaultnamingcontext
        12. Check nsslapd-defaultnamingcontext become empty.
    :expectedresults:
        1. This should be successful
        2. It should give 'server unwilling to perform' error
        3. It should be successful
        4. It should give 'no such object' error
        5. It should be successful
        6. Add should be successful
        7. nsslapd-defaultnamingcontext should have new suffix
        8. Add should be successful
        9. defaultnamingcontext should not change
        10. Remove should be successful and defaultnamingcontext should not change
        11. Removal should be successful
        12. nsslapd-defaultnamingcontext should be empty
    """

    backends = Backends(topo.standalone)
    test_suffix1 = 'dc=test1,dc=com'
    test_db1 = 'test1_db'
    test_suffix2 = 'dc=test2,dc=com'
    test_db2 = 'test2_db'
    test_suffix3 = 'dc=test3,dc=com'
    test_db3 = 'test3_db'

    log.info("Check the attribute nsslapd-defaultnamingcontext is present in cn=config")
    assert topo.standalone.config.present('nsslapd-defaultnamingcontext')

    log.info("Delete nsslapd-defaultnamingcontext attribute")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topo.standalone.config.remove_all('nsslapd-defaultnamingcontext')

    b1 = backends.create(properties={'cn': test_db1,
                                     'nsslapd-suffix': test_suffix1})

    log.info("modify nsslapd-defaultnamingcontext with new suffix")
    topo.standalone.config.replace('nsslapd-defaultnamingcontext', test_suffix1)

    log.info("Add new invalid value at runtime to nsslapd-defaultnamingcontext")
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        topo.standalone.config.replace('nsslapd-defaultnamingcontext', 'some_invalid_value')

    log.info("Modify nsslapd-defaultnamingcontext with blank value")
    topo.standalone.config.replace('nsslapd-defaultnamingcontext', ' ')

    log.info("Add new suffix when nsslapd-defaultnamingcontext is empty")
    b2 = backends.create(properties={'cn': test_db2,
                                     'nsslapd-suffix': test_suffix2})

    log.info("Check the value of the nsslapd-defaultnamingcontext automatically have the new suffix")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-defaultnamingcontext') == test_suffix2

    log.info("Adding new suffix when nsslapd-defaultnamingcontext is not empty")
    b3 = backends.create(properties={'cn': test_db3,
                                     'nsslapd-suffix': test_suffix3})

    log.info("Check the value of the nsslapd-defaultnamingcontext has not changed")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-defaultnamingcontext') == test_suffix2

    log.info("Remove the newly added suffix and check the values of the attribute is not changed")
    b3.delete()
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-defaultnamingcontext') == test_suffix2

    log.info("Remove all the suffix at the end")
    b1.delete()
    b2.delete()


def test_allow_add_delete_config_attributes(topo):
    """Tests configuration attributes are allowed to add and delete

    :id: d9a3f264-4111-406b-9900-a70e5403458a
    :setup: Standalone instance
    :steps:
        1. Add a new valid attribute at runtime to cn=config
        2. Check if the new valid attribute is present
        3. Delete nsslapd-listenhost to restore the default value
        4. Restart the server
        5. Check nsslapd-listenhost is present with  default value
        6. Add new invalid attribute at runtime to cn=config
        7. Make sure the invalid attribute is not added
    :expectedresults:
        1. This should be successful
        2. This should be successful
        3. This should be successful
        4. This should be successful
        5. This should be successful
        6. It should give 'server unwilling to perform' error
        7. Invalid attribute should not be added
    """
    default_listenhost = topo.standalone.config.get_attr_val_utf8('nsslapd-listenhost')

    log.info("Add a new valid attribute at runtime to cn=config")
    topo.standalone.config.add('nsslapd-listenhost', 'localhost')
    assert topo.standalone.config.present('nsslapd-listenhost', 'localhost')

    log.info("Delete nsslapd-listenhost to restore the default value")
    topo.standalone.config.remove('nsslapd-listenhost', 'localhost')
    topo.standalone.restart()
    assert topo.standalone.config.present('nsslapd-listenhost', default_listenhost)

    log.info("Add new invalid attribute at runtime to cn=config")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topo.standalone.config.add('invalid-attribute', 'invalid-value')

    log.info("Make sure the invalid attribute is not added")
    assert not topo.standalone.config.present('invalid-attribute', 'invalid-value')


def test_ignore_virtual_attrs(topo):
    """Test nsslapd-ignore-virtual-attrs configuration attribute

    :id: 9915d71b-2c71-4ac0-91d7-92655d53541b
    :customerscenario: True
    :setup: Standalone instance
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON
         3. Set the valid values i.e. on/ON and off/OFF for nsslapd-ignore-virtual-attrs
         4. Set invalid value for attribute nsslapd-ignore-virtual-attrs
         5. Set nsslapd-ignore-virtual-attrs=off
         6. Add cosPointer, cosTemplate and test entry to default suffix, where virtual attribute is postal code
         7. Test if virtual attribute i.e. postal code shown in test entry while nsslapd-ignore-virtual-attrs: off
         8. Set nsslapd-ignore-virtual-attrs=on
         9. Test if virtual attribute i.e. postal code not shown while nsslapd-ignore-virtual-attrs: on
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should fail
         5. This should be successful
         6. This should be successful
         7. Postal code should be present
         8. This should be successful
         9. Postal code should not be present
    """

    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    log.info("Set the valid values i.e. on/ON and off/OFF for nsslapd-ignore-virtual-attrs")
    for attribute_value in ['on', 'off', 'ON', 'OFF']:
        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', attribute_value)
        assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs', attribute_value)

    log.info("Set invalid value for attribute nsslapd-ignore-virtual-attrs")
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'invalid_value')

    cos_template_properties = {
        'cn': 'cosTemplateExample',
        'postalcode': '117'
    }
    cos_templates = CosTemplates(topo.standalone, DEFAULT_SUFFIX, 'ou=People')
    test_cos_template = cos_templates.create(properties=cos_template_properties)

    log.info("Add cosPointer, cosTemplate and test entry to default suffix, where virtual attribute is postal code")
    cos_pointer_properties = {
        'cn': 'cosPointer',
        'description': 'cosPointer example',
        'cosTemplateDn': 'cn=cosTemplateExample,ou=People,dc=example,dc=com',
        'cosAttribute': 'postalcode',
    }
    cos_pointer_definitions = CosPointerDefinitions(topo.standalone, DEFAULT_SUFFIX, 'ou=People')
    test_cos_pointer_definition = cos_pointer_definitions.create(properties=cos_pointer_properties)

    test_users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    test_user = test_users.create(properties=TEST_USER_PROPERTIES)

    log.info("Test if virtual attribute i.e. postal code shown in test entry while nsslapd-ignore-virtual-attrs: off")
    assert test_user.present('postalcode', '117')

    log.info("Set nsslapd-ignore-virtual-attrs=on")
    topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'on')

    log.info("Test if virtual attribute i.e. postal code not shown while nsslapd-ignore-virtual-attrs: on")
    assert not test_user.present('postalcode', '117')

def test_ignore_virtual_attrs_after_restart(topo):
    """Test nsslapd-ignore-virtual-attrs configuration attribute
       The attribute is ON by default. If it set to OFF, it keeps
       its value on restart

    :id: ac368649-4fda-473c-9ef8-e0c728b162af
    :customerscenario: True
    :setup: Standalone instance
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON
         3. Set nsslapd-ignore-virtual-attrs=off
         4. restart the instance
         5. Check the attribute nsslapd-ignore-virtual-attrs is OFF
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
    """

    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    log.info("Set nsslapd-ignore-virtual-attrs = off")
    topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'off')

    topo.standalone.restart()

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be OFF")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs', 'off')

def test_ndn_cache_enabled(topo):
    """Test nsslapd-ignore-virtual-attrs configuration attribute

    :id: 2caa3ec0-cd05-458e-9e21-3b73cf4697ff
    :setup: Standalone instance
    :steps:
         1. Check the attribute nsslapd-ndn-cache-enabled is present in cn=config
         2. Check the attribute nsslapd-ndn-cache-enabled has the default value set as ON
         3. Check the attribute nsslapd-ndn-cache-max-size is present in cn=config
         4. Check the backend monitor output for Normalized DN cache statistics while nsslapd-ndn-cache-enabled is OFF
         5. Set nsslapd-ndn-cache-enabled ON and check the backend monitor output for Normalized DN cache statistics
         6. Set invalid value for nsslapd-ndn-cache-enabled
         7. Set invalid value for nsslapd-ndn-cache-max-size
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. Backend monitor output should not have NDN cache statistics
         5. Backend monitor output should have NDN cache statistics
         6. This should fail
         7. This should fail
    """
    log.info("Check the attribute nsslapd-ndn-cache-enabled is present in cn=config")
    assert topo.standalone.config.present('nsslapd-ndn-cache-enabled')

    log.info("Check the attribute nsslapd-ndn-cache-enabled has the default value set as ON")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-ndn-cache-enabled') == 'on'

    log.info("Check the attribute nsslapd-ndn-cache-max-size is present in cn=config")
    assert topo.standalone.config.present('nsslapd-ndn-cache-max-size')

    backends = Backends(topo.standalone)
    backend = backends.get(DEFAULT_BENAME)

    log.info("Ticket#49593 : NDN cache stats should be under the global stats - Implemented in 1.4")
    log.info("Fetch the monitor value according to the ds version")
    if ds_is_older('1.4'):
        monitor = backend.get_monitor()
    else:
        monitor = MonitorLDBM(topo.standalone)

    log.info("Check the backend monitor output for Normalized DN cache statistics, "
             "while nsslapd-ndn-cache-enabled is off")
    topo.standalone.config.set('nsslapd-ndn-cache-enabled', 'off')
    topo.standalone.restart()
    assert not monitor.present('normalizedDnCacheHits')

    log.info("Check the backend monitor output for Normalized DN cache statistics, "
             "while nsslapd-ndn-cache-enabled is on")
    topo.standalone.config.set('nsslapd-ndn-cache-enabled', 'on')
    topo.standalone.restart()
    assert monitor.present('normalizedDnCacheHits')

    log.info("Set invalid value for nsslapd-ndn-cache-enabled")
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-ndn-cache-enabled', 'invalid_value')

    log.info("Set invalid value for nsslapd-ndn-cache-max-size")
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set('nsslapd-ndn-cache-max-size', 'invalid_value')


def test_require_index(topo):
    """Test nsslapd-ignore-virtual-attrs configuration attribute

    :id: fb6e31f2-acc2-4e75-a195-5c356faeb803
    :setup: Standalone instance
    :steps:
        1. Set "nsslapd-require-index" to "on"
        2. Test an unindexed search is rejected
    :expectedresults:
        1. Success
        2. Success
    """

    # Set the config
    be_insts = Backends(topo.standalone).list()
    for be in be_insts:
        if be.get_attr_val_utf8_l('nsslapd-suffix') == DEFAULT_SUFFIX:
            be.set('nsslapd-require-index', 'on')

    db_cfg = DatabaseConfig(topo.standalone)
    db_cfg.set([('nsslapd-idlistscanlimit', '100')])

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(101):
        users.create_test_user(uid=i)

    # Issue unindexed search,a nd make sure it is rejected
    raw_objects = DSLdapObjects(topo.standalone, basedn=DEFAULT_SUFFIX)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        raw_objects.filter("(description=test*)")



@pytest.mark.skipif(ds_is_older('1.4.2'), reason="The config setting only exists in 1.4.2 and higher")
def test_require_internal_index(topo):
    """Test nsslapd-ignore-virtual-attrs configuration attribute

    :id: 22b94f30-59e3-4f27-89a1-c4f4be036f7f
    :setup: Standalone instance
    :steps:
        1. Set "nsslapd-require-internalop-index" to "on"
        2. Enable RI plugin, and configure it to use an attribute that is not indexed
        3. Create a user and add it a group
        4. Deleting user should be rejected as the RI plugin issues an unindexed internal search
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    # Set the config
    be_insts = Backends(topo.standalone).list()
    for be in be_insts:
        if be.get_attr_val_utf8_l('nsslapd-suffix') == DEFAULT_SUFFIX:
            be.set('nsslapd-require-index', 'off')
            be.set('nsslapd-require-internalop-index', 'on')

    # Configure RI plugin
    rip = ReferentialIntegrityPlugin(topo.standalone)
    rip.set('referint-membership-attr', 'description')
    rip.enable()

    # Create a bunch of users
    db_cfg = DatabaseConfig(topo.standalone)
    db_cfg.set([('nsslapd-idlistscanlimit', '100')])
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(102, 202):
        users.create_test_user(uid=i)

    # Create user and group
    user = users.create(properties={
        'uid': 'indexuser',
        'cn' : 'indexuser',
        'sn' : 'user',
        'uidNumber' : '1010',
        'gidNumber' : '2010',
        'homeDirectory' : '/home/indexuser'
    })
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'group',
                                      'member': user.dn})

    # Restart the server
    topo.standalone.restart()

    # Deletion of user should be rejected
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        user.delete()


def get_pstack(pid):
    """Get a pstack of the pid."""
    res = subprocess.run((PSTACK_CMD, str(pid)), stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, encoding='utf-8')
    return str(res.stdout)

def check_number_of_threads(cfgnbthreads, monitor, pid):
    monresults = monitor.get_threads()
    # Add waitingthreads and busythreads
    waiting = int(monresults[3][0])
    busy = int(monresults[4][0])
    log.info('Number of threads: configured={cfgnbthreads} waiting={waiting} busy={busy}')

    monnbthreads = int(monresults[3][0]) + int(monresults[4][0]);
    assert monnbthreads == cfgnbthreads
    if os.path.isfile(PSTACK_CMD):
        pstackresult = get_pstack(pid)
        assert pstackresult.count('connection_threadmain') == cfgnbthreads
    else:
        log.info('pstack is not installed ==> skipping pstack test.')

def test_changing_threadnumber(topo):
    """Test nsslapd-ignore-virtual-attrs configuration attribute

    :id: 11bcf426-061c-11ee-8c22-482ae39447e5
    :setup: Standalone instance
    :steps:
        1. Check that feature is supported
        2  Get nsslapd-threadnumber original value
        3. Change nsslapd-threadnumber to 40
        4. Check that monitoring and pstack shows the same number than configured number of threads
        5. Create a user and add it a group
        6. Change nsslapd-threadnumber to 10
        7. Check that monitoring and pstack shows the same number than configured number of threads
        8. Set back the number of threads to the original value
        9. Check that monitoring and pstack shows the same number than configured number of threads
    :expectedresults:
        1. Skip the test if monitoring result does not have the new attributes.
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """
    inst = topo.standalone
    pid = pid_from_file(inst.pid_file())
    assert pid != 0 and pid != None

    config = Config(inst)
    cfgattr = 'nsslapd-threadnumber'
    cfgnbthreads = config.get_attr_vals_utf8(cfgattr)[0]

    monitor = Monitor(inst)
    monresults = monitor.get_threads()
    if len(monresults) < 5:
        pytest.skip("This version does not support dynamic change of nsslapd-threadnumber without restart.")

    config.replace(cfgattr, '40');
    time.sleep(3)
    check_number_of_threads(40, monitor, pid)

    config.replace(cfgattr, '10');
    # No need to wait here (threads are closed before config change result is returned)
    check_number_of_threads(10, monitor, pid)

    config.replace(cfgattr, cfgnbthreads);
    time.sleep(3)
    check_number_of_threads(int(cfgnbthreads), monitor, pid)


@pytest.fixture(scope="module")
def create_lmdb_instance(request):
    verbose = log.level > logging.DEBUG
    instname = 'i_lmdb'
    assert SetupDs(verbose=True, log=log).create_from_dict( {
        'general' : {},
        'slapd' : {
            'instance_name': instname,
            'db_lib': 'mdb',
            'mdb_max_size': '0.5 Gb',
        },
        'backend-userroot': {
            'sample_entries': 'yes',
            'suffix': DEFAULT_SUFFIX,
        },
    } )
    inst = DirSrv(verbose=verbose, external_log=log)
    inst.local_simple_allocate(instname, binddn=DN_DM, password=PW_DM)
    inst.setup_ldapi()

    def fin():
        inst.delete()

    request.addfinalizer(fin)
    inst.open()
    return inst


def set_and_check(inst, db_config, dsconf_attr, ldap_attr, val):
    val = str(val)
    args = FakeArgs()
    setattr(args, dsconf_attr, val)
    db_config_set(inst, db_config.dn, log, args)
    cfg_vals = db_config.get()
    assert ldap_attr in cfg_vals
    assert cfg_vals[ldap_attr][0] == val


def test_lmdb_config(create_lmdb_instance):
    """Test nsslapd-ignore-virtual-attrs configuration attribute

    :id: bca28086-61cf-11ee-a064-482ae39447e5
    :setup: Custom instance named 'i_lmdb' having db_lib=mdb and lmdb_size=0.5
    :steps:
        1. Get dscreate create-template output
        2. Check that 'db_lib' is in output
        3. Check that 'lmdb_size' is in output
        4. Get the database config
        5. Check that nsslapd-backend-implement is mdb
        6. Check that nsslapd-mdb-max-size is 536870912 (i.e 0.5Gb)
        7. Set a value for nsslapd-mdb-max-size and test the value is properly set
        8. Set a value for nsslapd-mdb-max-readers and test the value is properly set
        9. Set a value for nsslapd-mdb-max-dbs and test the value is properly set
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
    """

    res = subprocess.run(('dscreate', 'create-template'), stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, encoding='utf-8')
    inst = create_lmdb_instance
    assert 'db_lib' in res.stdout
    assert 'mdb_max_size' in res.stdout
    db_config = DatabaseConfig(inst)
    cfg_vals = db_config.get()
    assert 'nsslapd-backend-implement' in cfg_vals
    assert cfg_vals['nsslapd-backend-implement'][0] == 'mdb'
    assert 'nsslapd-mdb-max-size' in cfg_vals
    assert cfg_vals['nsslapd-mdb-max-size'][0] == '536870912'
    set_and_check(inst, db_config, 'mdb_max_size', 'nsslapd-mdb-max-size', parse_size('2G'))
    set_and_check(inst, db_config, 'mdb_max_readers', 'nsslapd-mdb-max-readers', 200)
    set_and_check(inst, db_config, 'mdb_max_dbs', 'nsslapd-mdb-max-dbs', 200)


def test_numlisteners_limit(topo):
    """Test higher limit of nsslapd-numlisteners than 4
    DS allows a higher value of nsslapd-numlisteners than it's limit of 4

    :id: 96869ea9-c7b4-4a4f-85f9-ea1d3f4a63aa
    :setup: Standalone Instance
    :steps:
        1. Check default nsslapd-numlisteners value is 1
        2. Set nsslapd-numlisteners value to 4
        3. Check nsslapd-numlisteners value is set to 4
        4. Check dse.ldif value of nsslapd-numlisteners is set to 4
        5. systemctl restart dirsrv@localhost
        6. Check nsslapd-numlisteners value is 4, after server restart
        7. Check if nsslapd-numlisteners value is still 4
    :expectedresults:
        1. nsslapd-numlisteners config value should show 1 by default
        2. nsslapd-numlisteners value should be successfully set to 4
        3. nsslapd-numlisteners is indeed set to 4
        4. nsslapd-numlisteners value in localhost dse.ldif file is set to 4
        5. restart DS instance is successful
        6. nsslapd-numlisteners value is still 4 after server restart
        7. nsslapd-numlisteners is still 4 even if we try to set it to 5
    """
    # Check default value for nsslapd-numlisteners is 1
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-numlisteners') == '1'

    # Set nsslapd-numlisteners value to 4
    topo.standalone.config.set('nsslapd-numlisteners', '4')

    # Check nsslapd-numlisteners value is set to 4
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-numlisteners') == '4'

    # Check instance dse.ldif value is set to 4
    inst = topo.standalone
    dse_ldif = DSEldif(inst)
    numlisteners = dse_ldif.get(DN_CONFIG, 'nsslapd-numlisteners')
    assert numlisteners[0] == '4'

    # Restart instance
    topo.standalone.restart()

    # Check nsslapd-numlisteners value is set to 4
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-numlisteners') == '4'

    # Check if nsslapd-numlisteners value is not set more than 4
    topo.standalone.config.set('nsslapd-numlisteners', '5')

    assert topo.standalone.config.get_attr_val_utf8('nsslapd-numlisteners') == '4'


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


