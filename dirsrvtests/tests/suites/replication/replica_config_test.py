import logging
import pytest
import copy
import os
import ldap
from lib389._constants import *
from lib389 import Entry
from lib389.topologies import topology_st as topo

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

REPLICA_DN = 'cn=replica,cn="dc=example,dc=com",cn=mapping tree,cn=config'
AGMT_DN = 'cn=test_agreement,cn=replica,cn="dc=example,dc=com",cn=mapping tree,cn=config'
notnum = 'invalid'
too_big = '9223372036854775807'
overflow = '9999999999999999999999999999999999999999999999999999999999999999999'

replica_dict = {'objectclass': 'top nsDS5Replica'.split(),
                'nsDS5ReplicaRoot': 'dc=example,dc=com',
                'nsDS5ReplicaType': '3',
                'nsDS5Flags': '1',
                'nsDS5ReplicaId': '65534',
                'nsds5ReplicaPurgeDelay': '604800',
                'nsDS5ReplicaBindDN': 'cn=u',
                'cn': 'replica'}

agmt_dict = {'objectClass': 'top nsDS5ReplicationAgreement'.split(),
             'cn': 'test_agreement',
             'nsDS5ReplicaRoot': 'dc=example,dc=com',
             'nsDS5ReplicaHost': 'localhost.localdomain',
             'nsDS5ReplicaPort': '5555',
             'nsDS5ReplicaBindDN': 'uid=tester',
             'nsds5ReplicaCredentials': 'password',
             'nsDS5ReplicaTransportInfo': 'LDAP',
             'nsDS5ReplicaBindMethod': 'SIMPLE'}


repl_add_attrs = [('nsDS5ReplicaType', '-1', '4', overflow, notnum, '1'),
                  ('nsDS5Flags', '-1', '2', overflow, notnum, '1'),
                  ('nsDS5ReplicaId', '0', '65535', overflow, notnum, '1'),
                  ('nsds5ReplicaPurgeDelay', '-2', too_big, overflow, notnum, '1'),
                  ('nsDS5ReplicaBindDnGroupCheckInterval', '-2', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaTombstonePurgeInterval', '-2', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaProtocolTimeout', '-1', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaReleaseTimeout', '-1', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaBackoffMin', '0', too_big, overflow, notnum, '3'),
                  ('nsds5ReplicaBackoffMax', '0', too_big, overflow, notnum, '6')]

repl_mod_attrs = [('nsDS5Flags', '-1', '2', overflow, notnum, '1'),
                  ('nsds5ReplicaPurgeDelay', '-2', too_big, overflow, notnum, '1'),
                  ('nsDS5ReplicaBindDnGroupCheckInterval', '-2', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaTombstonePurgeInterval', '-2', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaProtocolTimeout', '-1', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaReleaseTimeout', '-1', too_big, overflow, notnum, '1'),
                  ('nsds5ReplicaBackoffMin', '0', too_big, overflow, notnum, '3'),
                  ('nsds5ReplicaBackoffMax', '0', too_big, overflow, notnum, '6')]

agmt_attrs = [
              ('nsds5ReplicaPort', '0', '65535', overflow, notnum, '389'),
              ('nsds5ReplicaTimeout', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaBusyWaitTime', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaSessionPauseTime', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaFlowControlWindow', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaFlowControlPause', '-1', too_big, overflow, notnum, '6'),
              ('nsds5ReplicaProtocolTimeout', '-1', too_big, overflow, notnum, '6')]


def replica_setup(topo):
    """Add a valid replica config entry to modify
    """
    try:
        topo.standalone.delete_s(REPLICA_DN)
    except:
        pass

    try:
        topo.standalone.add_s(Entry((REPLICA_DN, replica_dict)))
    except ldap.LDAPError as e:
        log.fatal("Failed to add replica entry: " + str(e))
        assert False


def replica_reset(topo):
    try:
        topo.standalone.delete_s(REPLICA_DN)
    except:
        pass


def agmt_setup(topo):
    """Add a valid replica config entry to modify
    """
    try:
        topo.standalone.delete_s(AGMT_DN)
    except:
        pass

    try:
        topo.standalone.add_s(Entry((AGMT_DN, agmt_dict)))
    except ldap.LDAPError as e:
        log.fatal("Failed to add agreement entry: " + str(e))
        assert False


def agmt_reset(topo):
    try:
        topo.standalone.delete_s(AGMT_DN)
    except:
        pass


@pytest.mark.parametrize("attr, too_small, too_big, overflow, notnum, valid", repl_add_attrs)
def test_replica_num_add(topo, attr, too_small, too_big, overflow, notnum, valid):
    """Test all the number values you can set for a replica config entry

    :id: a8b47d4a-a089-4d70-8070-e6181209bf92
    :setup: standalone instance
    :steps:
        1. Use a value that is too small
        2. Use a value that is too big
        3. Use a value that overflows the int
        4. Use a value with character value (not a number)
        5. Use a valid value
    :expectedresults:
        1. Add is rejected
        2. Add is rejected
        3. Add is rejected
        4. Add is rejected
        5. Add is allowed
    """

    replica_reset(topo)

    # Test too small
    my_replica = copy.deepcopy(replica_dict)
    my_replica[attr] = too_small
    try:
        topo.standalone.add_s(Entry((REPLICA_DN, my_replica)))
        log.fatal("Incorrectly allowed to add replica entry with {}:{}".format(attr, too_small))
        assert False
    except ldap.LDAPError as e:
        log.info("Correctly failed to add replica entry with {}:{}  error: {}".format(attr, too_small, str(e)))

    # Test too big
    my_replica = copy.deepcopy(replica_dict)
    my_replica[attr] = too_big
    try:
        topo.standalone.add_s(Entry((REPLICA_DN, my_replica)))
        log.fatal("Incorrectly allowed to add replica entry with {}:{}".format(attr, too_big))
        assert False
    except ldap.LDAPError as e:
        log.info("Correctly failed to add replica entry with {}:{}  error: {}".format(attr, too_big, str(e)))

    # Test overflow
    my_replica = copy.deepcopy(replica_dict)
    my_replica[attr] = overflow
    try:
        topo.standalone.add_s(Entry((REPLICA_DN, my_replica)))
        log.fatal("Incorrectly allowed to add replica entry with {}:{}".format(attr, overflow))
        assert False
    except ldap.LDAPError as e:
        log.info("Correctly failed to add replica entry with {}:{}  error: {}".format(attr, overflow, str(e)))

    # test not a number
    my_replica = copy.deepcopy(replica_dict)
    my_replica[attr] = notnum
    try:
        topo.standalone.add_s(Entry((REPLICA_DN, my_replica)))
        log.fatal("Incorrectly allowed to add replica entry with {}:{}".format(attr, notnum))
        assert False
    except ldap.LDAPError as e:
        log.info("Correctly failed to add replica entry with {}:{}  error: {}".format(attr, notnum, str(e)))

    # Test valid value
    my_replica = copy.deepcopy(replica_dict)
    my_replica[attr] = valid
    try:
        topo.standalone.add_s(Entry((REPLICA_DN, my_replica)))
        log.info("Correctly allowed to add replica entry with {}: {}".format(attr, valid))
    except ldap.LDAPError as e:
        log.fatal("Incorrectly failed to add replica entry with {}: {}  error: {}".format(attr, valid, str(e)))
        assert False


@pytest.mark.parametrize("attr, too_small, too_big, overflow, notnum, valid", repl_mod_attrs)
def test_replica_num_modify(topo, attr, too_small, too_big, overflow, notnum, valid):
    """Test all the number values you can set for a replica config entry

    :id: a8b47d4a-a089-4d70-8070-e6181209bf93
    :setup: standalone instance
    :steps:
        1. Replace a value that is too small
        2. Repalce a value that is too big
        3. Replace a value that overflows the int
        4. Replace a value with character value (not a number)
        5. Replace a vlue with a valid value
    :expectedresults:
        1. Value is rejected
        2. Value is rejected
        3. Value is rejected
        4. Value is rejected
        5. Value is allowed
    """

    # Value too small
    replica_setup(topo)
    try:
        topo.standalone.modify_s(REPLICA_DN, [(ldap.MOD_REPLACE, attr, too_small)])
        log.fatal('Invalid value for {}:{} was incorrectly allowed'.format(attr, too_small))
        assert False
    except:
        log.info('Invalid value for {}:{} was correctly rejected'.format(attr, too_small))

    # Value too big
    replica_setup(topo)
    try:
        topo.standalone.modify_s(REPLICA_DN, [(ldap.MOD_REPLACE, attr, too_big)])
        log.fatal('Invalid value for {}:{} was incorrectly allowed'.format(attr, too_big))
        assert False
    except:
        log.info('Invalid value for {}:{} was correctly rejected'.format(attr, too_big))

    # Value overflow
    replica_setup(topo)
    try:
        topo.standalone.modify_s(REPLICA_DN, [(ldap.MOD_REPLACE, attr, overflow)])
        log.fatal('Invalid value for {}:{} was incorrectly allowed'.format(attr, overflow))
        assert False
    except:
        log.info('Invalid value for {}:{} was correctly rejected'.format(attr, overflow))

    # Value not a number
    replica_setup(topo)
    try:
        topo.standalone.modify_s(REPLICA_DN, [(ldap.MOD_REPLACE, attr, notnum)])
        log.fatal('Invalid value for {}:{} was incorrectly allowed'.format(attr, notnum))
        assert False
    except:
        log.info('Invalid value for {}:{} was correctly rejected'.format(attr, notnum))

    # Value is valid
    replica_setup(topo)
    try:
        topo.standalone.modify_s(REPLICA_DN, [(ldap.MOD_REPLACE, attr, valid)])
        log.info('Correctly added valid agreement attribute value: {}:{}'.format(attr, valid))
    except ldap.LDAPError as e:
        log.fatal('Valid value for {}:{} was incorrectly rejected.  Error {}'.format(attr, valid, str(e)))
        assert False


@pytest.mark.parametrize("attr, too_small, too_big, overflow, notnum, valid", agmt_attrs)
def test_agmt_num_add(topo, attr, too_small, too_big, overflow, notnum, valid):
    """Test all the number values you can set for a replica config entry

    :id: a8b47d4a-a089-4d70-8070-e6181209bf94
    :setup: standalone instance
    :steps:
        1. Use a value that is too small
        2. Use a value that is too big
        3. Use a value that overflows the int
        4. Use a value with character value (not a number)
        5. Use a valid value
    :expectedresults:
        1. Add is rejected
        2. Add is rejected
        3. Add is rejected
        4. Add is rejected
        5. Add is allowed
    """
    agmt_reset(topo)

    # Test too small
    my_agmt = copy.deepcopy(agmt_dict)
    my_agmt[attr] = too_small
    try:
        topo.standalone.add_s(Entry((AGMT_DN, my_agmt)))
        log.fatal("Incorrectly allowed to add agreement entry with {}:{}".format(attr, too_small))
        assert False
    except ldap.LDAPError as e:
        log.info("Correctly failed to add agreement entry with {}:{}  error: {}".format(attr, too_small, str(e)))

    # Test too big
    my_agmt = copy.deepcopy(agmt_dict)
    my_agmt[attr] = too_big
    try:
        topo.standalone.add_s(Entry((AGMT_DN, my_agmt)))
        log.fatal("Incorrectly allowed to add agreement entry with {}:{}".format(attr, too_big))
        assert False
    except ldap.LDAPError as e:
        log.info("Correctly failed to add agreement entry with {}:{}  error: {}".format(attr, too_big, str(e)))

    # Test overflow
    my_agmt = copy.deepcopy(agmt_dict)
    my_agmt[attr] = overflow
    try:
        topo.standalone.add_s(Entry((AGMT_DN, my_agmt)))
        log.fatal("Incorrectly allowed to add agreement entry with {}:{}".format(attr, overflow))
        assert False
    except ldap.LDAPError as e:
        log.info("Correctly failed to add agreement entry with {}:{}  error: {}".format(attr, overflow, str(e)))

    # test not a number
    my_agmt = copy.deepcopy(agmt_dict)
    my_agmt[attr] = notnum
    try:
        topo.standalone.add_s(Entry((AGMT_DN, my_agmt)))
        log.fatal("Incorrectly allowed to add agreement entry with {}:{}".format(attr, notnum))
        assert False
    except ldap.LDAPError as e:
        log.info("Correctly failed to add agreement entry with {}:{}  error: {}".format(attr, notnum, str(e)))

    # Test valid value
    my_agmt = copy.deepcopy(agmt_dict)
    my_agmt[attr] = valid
    try:
        topo.standalone.add_s(Entry((AGMT_DN, my_agmt)))
        log.info("Correctly allowed to add agreement entry with {}: {}".format(attr, valid))
    except ldap.LDAPError as e:
        log.fatal("Incorrectly failed to add agreement entry with {}: {}  error: {}".format(attr, valid, str(e)))
        assert False


@pytest.mark.parametrize("attr, too_small, too_big, overflow, notnum, valid", agmt_attrs)
def test_agmt_num_modify(topo, attr, too_small, too_big, overflow, notnum, valid):
    """Test all the number values you can set for a replica config entry

    :id: a8b47d4a-a089-4d70-8070-e6181209bf95
    :setup: standalone instance
    :steps:
        1. Replace a value that is too small
        2. Replace a value that is too big
        3. Replace a value that overflows the int
        4. Replace a value with character value (not a number)
        5. Replace a vlue with a valid value
    :expectedresults:
        1. Value is rejected
        2. Value is rejected
        3. Value is rejected
        4. Value is rejected
        5. Value is allowed
    """

    # Value too small
    agmt_setup(topo)
    try:
        topo.standalone.modify_s(AGMT_DN, [(ldap.MOD_REPLACE, attr, too_small)])
        log.fatal('Invalid value for {}:{} was incorrectly allowed'.format(attr, too_small))
        assert False
    except:
        log.info('Invalid value for {}:{} was correctly rejected'.format(attr, too_small))

    # Value too big
    agmt_setup(topo)
    try:
        topo.standalone.modify_s(AGMT_DN, [(ldap.MOD_REPLACE, attr, too_big)])
        log.fatal('Invalid value for {}:{} was incorrectly allowed'.format(attr, too_big))
        assert False
    except:
        log.info('Invalid value for {}:{} was correctly rejected'.format(attr, too_big))

    # Value overflow
    agmt_setup(topo)
    try:
        topo.standalone.modify_s(AGMT_DN, [(ldap.MOD_REPLACE, attr, overflow)])
        log.fatal('Invalid value for {}:{} was incorrectly allowed'.format(attr, overflow))
        assert False
    except:
        log.info('Invalid value for {}:{} was correctly rejected'.format(attr, overflow))

    # Value not a number
    agmt_setup(topo)
    try:
        topo.standalone.modify_s(AGMT_DN, [(ldap.MOD_REPLACE, attr, notnum)])
        log.fatal('Invalid value for {}:{} was incorrectly allowed'.format(attr, notnum))
        assert False
    except:
        log.info('Invalid value for {}:{} was correctly rejected'.format(attr, notnum))

    # Value is valid
    agmt_setup(topo)
    try:
        topo.standalone.modify_s(AGMT_DN, [(ldap.MOD_REPLACE, attr, valid)])
    except ldap.LDAPError as e:
        log.fatal('Valid value for {}:{} was incorrectly rejected.  Error {}'.format(attr, valid, str(e)))
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

