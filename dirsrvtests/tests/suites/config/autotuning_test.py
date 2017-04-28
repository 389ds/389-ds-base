# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389._mapped_object import DSLdapObject
from lib389.utils import *
from lib389.topologies import topology_st as topo

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_threads_basic(topo):
    """Check that a number of threads are able to be autotuned

    :ID: 371fb9c4-9607-4a4b-a4a2-6f00809d6257
    :feature: Autotuning
    :setup: Standalone instance
    :steps: 1. Set nsslapd-threadnumber to -1
            2. Get the number of CPUs on the system and match it with the value from docs
            3. Check that nsslapd-threadnumber is equal to the documented expected value
    :expectedresults: nsslapd-threadnumber is equal to the documented expected value
    """

    log.info("Set nsslapd-threadnumber: -1 to enable autotuning")
    topo.standalone.config.set("nsslapd-threadnumber", "-1")

    log.info("Assert nsslapd-threadnumber is equal to the documented expected value")
    assert topo.standalone.config.get_attr_val("nsslapd-threadnumber") > 0


@pytest.mark.parametrize("invalid_value", ('-2', '0', 'invalid'))
def test_threads_invalid_value(topo, invalid_value):
    """Check nsslapd-threadnumber for an invalid values

    :ID: 1979eddf-8222-4c9d-809d-269c26de636e
    :feature: Autotuning
    :setup: Standalone instance
    :steps: 1. Set nsslapd-threadnumber to -2, 0, 513, invalid_str
    :expectedresults: The operation should fail
    """

    log.info("Set nsslapd-threadnumber: {}. Operation should fail".format(invalid_value))
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set("nsslapd-threadnumber", invalid_value)


def test_threads_back_from_manual_value(topo):
    """Check that thread autotuning works after manual tuning

    :ID: 4b674016-e5ca-426b-a9c0-a94745a7dd25
    :feature: Autotuning
    :setup: Standalone instance
    :steps: 1. Set nsslapd-threadnumber to -1 and save the autotuned value
            2. Decrease nsslapd-threadnumber by 2
            3. Set nsslapd-threadnumber to -1
            4. Check that nsslapd-threadnumber is back to autotuned value
    :expectedresults: nsslapd-threadnumber is set back to the autotuned value
    """

    log.info("Set nsslapd-threadnumber: -1 to enable autotuning and save the new value")
    topo.standalone.config.set("nsslapd-threadnumber", "-1")
    autotuned_value = topo.standalone.config.get_attr_val("nsslapd-threadnumber")

    log.info("Set nsslapd-threadnumber to the autotuned value decreased by 2")
    new_value = str(int(autotuned_value) - 2)
    topo.standalone.config.set("nsslapd-threadnumber", new_value)
    assert topo.standalone.config.get_attr_val("nsslapd-threadnumber") == new_value

    log.info("Set nsslapd-threadnumber: -1 to enable autotuning")
    topo.standalone.config.set("nsslapd-threadnumber", "-1")

    log.info("Assert nsslapd-threadnumber is back to the autotuned value")
    assert topo.standalone.config.get_attr_val("nsslapd-threadnumber") == autotuned_value


@pytest.mark.parametrize("autosize,autosize_split", (('', ''), ('', '0'), ('10', '40'), ('', '40'),
                                                     ('10', ''), ('10', '40'), ('10', '0')))
def test_cache_autosize_non_zero(topo, autosize, autosize_split):
    """Check that autosizing works works properly in different combinations

    :ID: 83fa099c-a6c9-457a-82db-0982b67e8598
    :feature: Autotuning
    :setup: Standalone instance
    :steps: 1. Set in the cn=config,cn=ldbm database,cn=plugins,cn=config:
               nsslapd-cache-autosize, nsslapd-cache-autosize-split to the next value pairs:
                ('', ''), ('', '0'), ('10', '40'), ('', '40'),
                ('10', ''), ('10', '40'), ('10', '0')
               '' - for deleting the value (set to default)
            2. Try to modify nsslapd-dbcachesize and nsslapd-cachememsize to
               some real value, it should be rejected
            2. Restart the instance
            3. Check nsslapd-dbcachesize and nsslapd-cachememsize
    :expectedresults: Modify operation was rejected,
                      nsslapd-dbcachesize and nsslapd-cachememsize were set to
                      value in the expected range between 512KB and max int on the system
    """

    config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM)
    userroot_ldbm = DSLdapObject(topo.standalone, DN_USERROOT_LDBM)

    cachesize = '33333333'

    dbcachesize_val = config_ldbm.get_attr_val('nsslapd-dbcachesize')
    cachenensize_val = userroot_ldbm.get_attr_val('nsslapd-cachememsize')
    autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
    autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

    log.info("Check nsslapd-dbcachesize and nsslapd-cachememsize before the test")
    log.info("nsslapd-dbcachesize == {}".format(dbcachesize_val))
    log.info("nsslapd-cachememsize == {}".format(cachenensize_val))
    log.info("nsslapd-cache-autosize == {}".format(autosize_val))
    log.info("nsslapd-cache-autosize-split == {}".format(autosize_split_val))

    if autosize:
        log.info("Set nsslapd-cache-autosize to {}".format(autosize))
        config_ldbm.set('nsslapd-cache-autosize', autosize)
    else:
        log.info("Delete nsslapd-cache-autosize")
        try:
            config_ldbm.remove('nsslapd-cache-autosize', autosize_val)
        except ValueError:
            log.info("nsslapd-cache-autosize wasn't found")

    if autosize_split:
        log.info("Set nsslapd-cache-autosize-split to {}".format(autosize_split))
        config_ldbm.set('nsslapd-cache-autosize-split', autosize_split)
    else:
        log.info("Delete nsslapd-cache-autosize-split")
        try:
            config_ldbm.remove('nsslapd-cache-autosize-split', autosize_split_val)
        except ValueError:
            log.info("nsslapd-cache-autosize-split wasn't found")

    log.info("Trying to set nsslapd-cachememsize to {}".format(cachesize))
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        userroot_ldbm.set('nsslapd-cachememsize', cachesize)
    log.info("Trying to set nsslapd-dbcachesize to {}".format(cachesize))
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        config_ldbm.set('nsslapd-dbcachesize ', cachesize)
    topo.standalone.restart()

    dbcachesize_val = config_ldbm.get_attr_val('nsslapd-dbcachesize')
    cachenensize_val = userroot_ldbm.get_attr_val('nsslapd-cachememsize')
    autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
    autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

    log.info("Check nsslapd-dbcachesize and nsslapd-cachememsize in the appropriate range.")
    log.info("nsslapd-dbcachesize == {}".format(dbcachesize_val))
    log.info("nsslapd-cachememsize == {}".format(cachenensize_val))
    log.info("nsslapd-cache-autosize == {}".format(autosize_val))
    log.info("nsslapd-cache-autosize-split == {}".format(autosize_split_val))
    assert int(dbcachesize_val) >= 512000
    assert int(dbcachesize_val) <= sys.maxint
    assert int(cachenensize_val) >= 512000
    assert int(cachenensize_val) <= sys.maxint


@pytest.mark.parametrize("autosize_split", ('0', '', '40'))
def test_cache_autosize_basic_sane(topo, autosize_split):
    """Check that autotuning cachesizes works properly with different values

    :ID: 9dc363ef-f551-446d-8b83-8ac45dabb8df
    :feature: Autotuning
    :setup: Standalone instance
    :steps: 1. Set in the cn=config,cn=ldbm database,cn=plugins,cn=config:
               nsslapd-cache-autosize, nsslapd-cache-autosize-split to the next value pairs:
               ('0', '0'), ('0', ''), ('0', '40')
               '' - for deleting the value (set to default)
            2. Set in the cn=config,cn=ldbm database,cn=plugins,cn=config:
               nsslapd-dbcachesize: 0 and some same value
            3. Set in the cn=UserRoot,cn=ldbm database,cn=plugins,cn=config:
               nsslapd-cachememsize: 0 and some same value
            4. Restart the instance
            5. Check nsslapd-dbcachesize and nsslapd-cachememsize
    :expectedresults: nsslapd-dbcachesize and nsslapd-cachememsize were set to
                      value in the expected range between 512KB and max int on the system
    """

    config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM)
    userroot_ldbm = DSLdapObject(topo.standalone, DN_USERROOT_LDBM)
    config_ldbm.set('nsslapd-cache-autosize', '0')

    # Test with caches with both real values and 0
    for cachesize in ('0', '33333333'):
        dbcachesize_val = config_ldbm.get_attr_val('nsslapd-dbcachesize')
        cachenensize_val = userroot_ldbm.get_attr_val('nsslapd-cachememsize')
        autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
        autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

        log.info("Check nsslapd-dbcachesize and nsslapd-cachememsize before the test")
        log.info("nsslapd-dbcachesize == {}".format(dbcachesize_val))
        log.info("nsslapd-cachememsize == {}".format(cachenensize_val))
        log.info("nsslapd-cache-autosize == {}".format(autosize_val))
        log.info("nsslapd-cache-autosize-split == {}".format(autosize_split_val))

        if autosize_split:
            log.info("Set nsslapd-cache-autosize-split to {}".format(autosize_split))
            config_ldbm.set('nsslapd-cache-autosize-split', autosize_split)
        else:
            log.info("Delete nsslapd-cache-autosize-split")
            try:
                config_ldbm.remove('nsslapd-cache-autosize-split', autosize_split_val)
            except ValueError:
                log.info("nsslapd-cache-autosize-split wasn't found")

        log.info("Set nsslapd-dbcachesize to {}".format(cachesize))
        config_ldbm.set('nsslapd-dbcachesize', cachesize)
        log.info("Set nsslapd-cachememsize to {}".format(cachesize))
        userroot_ldbm.set('nsslapd-cachememsize', cachesize)
        topo.standalone.restart()

        dbcachesize_val = config_ldbm.get_attr_val('nsslapd-dbcachesize')
        cachenensize_val = userroot_ldbm.get_attr_val('nsslapd-cachememsize')
        autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
        autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

        log.info("Check nsslapd-dbcachesize and nsslapd-cachememsize in the appropriate range.")
        log.info("nsslapd-dbcachesize == {}".format(dbcachesize_val))
        log.info("nsslapd-cachememsize == {}".format(cachenensize_val))
        log.info("nsslapd-cache-autosize == {}".format(autosize_val))
        log.info("nsslapd-cache-autosize-split == {}".format(autosize_split_val))
        assert int(dbcachesize_val) >= 512000
        assert int(dbcachesize_val) <= sys.maxint
        assert int(cachenensize_val) >= 512000
        assert int(cachenensize_val) <= sys.maxint


@pytest.mark.parametrize("invalid_value", ('-2', '102', 'invalid'))
def test_cache_autosize_invalid_values(topo, invalid_value):
    """Check that we can't set invalid values to autosize attributes

    :ID: 2f0d01b5-ca91-4dc2-97bc-ad0ac8d08633
    :feature: Autotuning
    :setup: Standalone instance
    :steps: 1. Stop the instance
            2. Set in the cn=config,cn=ldbm database,cn=plugins,cn=config:
               nsslapd-cache-autosize and nsslapd-cache-autosize-split 
               to invalid values like (-2, 102, invalid_str)
            3. Try to start the instance
    :expectedresults: Start dirsrv operation should fail
    """

    config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM)
    autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
    autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

    log.info("Set nsslapd-cache-autosize-split to {}".format(invalid_value))
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        config_ldbm.set('nsslapd-cache-autosize-split', invalid_value)
        topo.standalone.restart()
    config_ldbm.remove('nsslapd-cache-autosize-split', autosize_split_val)

    log.info("Set nsslapd-cache-autosize to {}".format(invalid_value))
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        config_ldbm.set('nsslapd-cache-autosize', invalid_value)
        topo.standalone.restart()
    config_ldbm.remove('nsslapd-cache-autosize', autosize_val)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
