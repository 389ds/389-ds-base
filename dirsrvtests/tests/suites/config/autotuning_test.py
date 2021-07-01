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

from lib389._constants import DN_CONFIG_LDBM, DN_CONFIG_LDBM_BDB, DN_USERROOT_LDBM, DEFAULT_SUFFIX

pytestmark = pytest.mark.tier0

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_threads_basic(topo):
    """Check that a number of threads are able to be autotuned

    :id: 371fb9c4-9607-4a4b-a4a2-6f00809d6257
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-threadnumber to -1
        2. Check that number of threads is positive
    :expectedresults:
        1. nsslapd-threadnumber should be successfully set
        2. nsslapd-threadnumber is positive
    """

    log.info("Set nsslapd-threadnumber: -1 to enable autotuning")
    topo.standalone.config.set("nsslapd-threadnumber", "-1")

    log.info("Assert nsslapd-threadnumber is equal to the documented expected value")
    assert topo.standalone.config.get_attr_val_int("nsslapd-threadnumber") > 0


def test_threads_warning(topo):
    """Check that we log a warning if the thread number is too high or low

    :id: db92412b-2812-49de-84b0-00f452cd254f
    :setup: Standalone Instance
    :steps:
        1. Get autotuned thread number
        2. Set threads way higher than hw threads, and find a warning in the log
        3. Set threads way lower than hw threads, and find a warning in the log
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    topo.standalone.config.set("nsslapd-threadnumber", "-1")
    autotuned_value = topo.standalone.config.get_attr_val_utf8("nsslapd-threadnumber")

    topo.standalone.config.set("nsslapd-threadnumber", str(int(autotuned_value) * 4))
    time.sleep(.5)
    assert topo.standalone.ds_error_log.match('.*higher.*hurt server performance.*')

    if int(autotuned_value) > 1:
        # If autotuned is 1, there isn't anything to test here
        topo.standalone.config.set("nsslapd-threadnumber", "1")
        time.sleep(.5)
        assert topo.standalone.ds_error_log.match('.*lower.*hurt server performance.*')


@pytest.mark.parametrize("invalid_value", ('-2', '0', 'invalid'))
def test_threads_invalid_value(topo, invalid_value):
    """Check nsslapd-threadnumber for an invalid values

    :id: 1979eddf-8222-4c9d-809d-269c26de636e
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-threadnumber to -2, 0, invalid_str
    :expectedresults:
        1. The operation should fail
    """

    log.info("Set nsslapd-threadnumber: {}. Operation should fail".format(invalid_value))
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo.standalone.config.set("nsslapd-threadnumber", invalid_value)


def test_threads_back_from_manual_value(topo):
    """Check that thread autotuning works after manual tuning

    :id: 4b674016-e5ca-426b-a9c0-a94745a7dd25
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-threadnumber to -1 and save the autotuned value
        2. Decrease nsslapd-threadnumber by 2
        3. Set nsslapd-threadnumber to -1
        4. Check that nsslapd-threadnumber is back to autotuned value
    :expectedresults:
        1. nsslapd-threadnumber should be successfully set
        2. nsslapd-threadnumber should be successfully decreased
        3. nsslapd-threadnumber should be successfully set
        4. nsslapd-threadnumber is set back to the autotuned value
    """

    log.info("Set nsslapd-threadnumber: -1 to enable autotuning and save the new value")
    topo.standalone.config.set("nsslapd-threadnumber", "-1")
    autotuned_value = topo.standalone.config.get_attr_val_utf8("nsslapd-threadnumber")

    log.info("Set nsslapd-threadnumber to the autotuned value decreased by 2")
    new_value = str(int(autotuned_value) - 2)
    topo.standalone.config.set("nsslapd-threadnumber", new_value)
    assert topo.standalone.config.get_attr_val_utf8("nsslapd-threadnumber") == new_value

    log.info("Set nsslapd-threadnumber: -1 to enable autotuning")
    topo.standalone.config.set("nsslapd-threadnumber", "-1")

    log.info("Assert nsslapd-threadnumber is back to the autotuned value")
    assert topo.standalone.config.get_attr_val_utf8("nsslapd-threadnumber") == autotuned_value


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
@pytest.mark.parametrize("autosize,autosize_split", (('', ''), ('', '0'), ('10', '40'), ('', '40'),
                                                     ('10', ''), ('10', '40'), ('10', '0')))
def test_cache_autosize_non_zero(topo, autosize, autosize_split):
    """Check that autosizing works works properly in different combinations

    :id: 83fa099c-a6c9-457a-82db-0982b67e8598
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Set in the cn=config,cn=ldbm database,cn=plugins,cn=config:
           nsslapd-cache-autosize, nsslapd-cache-autosize-split to the next value pairs:
           ('', ''), ('', '0'), ('10', '40'), ('', '40'),
           ('10', ''), ('10', '40'), ('10', '0')
           '' - for deleting the value (set to default)
        2. Try to modify nsslapd-dbcachesize and nsslapd-cachememsize to
           some real value, it should be rejected
        3. Restart the instance
        4. Check nsslapd-dbcachesize and nsslapd-cachememsize
    :expectedresults:
        1. nsslapd-cache-autosize, nsslapd-cache-autosize-split are successfully set
        2. Modify operation should be rejected
        3. The instance should be successfully restarted
        4. nsslapd-dbcachesize and nsslapd-cachememsize should set
           to value greater than 512KB
    """

    config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM)
    bdb_config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM_BDB)
    userroot_ldbm = DSLdapObject(topo.standalone, DN_USERROOT_LDBM)

    cachesize = '33333333'

    if ds_is_older('1.4.2'):
        dbcachesize_val = config_ldbm.get_attr_val('nsslapd-dbcachesize')
        autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
        autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')
    else:
        dbcachesize_val = bdb_config_ldbm.get_attr_val('nsslapd-dbcachesize')
        autosize_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize')
        autosize_split_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

    cachenensize_val = userroot_ldbm.get_attr_val('nsslapd-cachememsize')
    dncachenensize_val = userroot_ldbm.get_attr_val('nsslapd-dncachememsize')

    log.info("Check nsslapd-dbcachesize and nsslapd-cachememsize before the test")
    log.info("nsslapd-dbcachesize == {}".format(dbcachesize_val))
    log.info("nsslapd-cachememsize == {}".format(cachenensize_val))
    log.info("nsslapd-dncachememsize == {}".format(dncachenensize_val))
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

    if ds_is_older('1.4.2'):
        dbcachesize_val = config_ldbm.get_attr_val('nsslapd-dbcachesize')
        autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
        autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')
    else:
        dbcachesize_val = bdb_config_ldbm.get_attr_val('nsslapd-dbcachesize')
        autosize_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize')
        autosize_split_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

    cachenensize_val = userroot_ldbm.get_attr_val('nsslapd-cachememsize')
    dncachenensize_val = userroot_ldbm.get_attr_val('nsslapd-dncachememsize')

    log.info("Check nsslapd-dbcachesize and nsslapd-cachememsize in the appropriate range.")
    log.info("nsslapd-dbcachesize == {}".format(dbcachesize_val))
    log.info("nsslapd-cachememsize == {}".format(cachenensize_val))
    log.info("nsslapd-dncachememsize == {}".format(dncachenensize_val))
    log.info("nsslapd-cache-autosize == {}".format(autosize_val))
    log.info("nsslapd-cache-autosize-split == {}".format(autosize_split_val))
    assert int(dbcachesize_val) >= 512000
    assert int(cachenensize_val) >= 512000
    assert int(dncachenensize_val) >= 512000


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
@pytest.mark.parametrize("autosize_split", ('0', '', '40'))
def test_cache_autosize_basic_sane(topo, autosize_split):
    """Check that autotuning cachesizes works properly with different values

    :id: 9dc363ef-f551-446d-8b83-8ac45dabb8df
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Set in the cn=config,cn=ldbm database,cn=plugins,cn=config:
           nsslapd-cache-autosize, nsslapd-cache-autosize-split to the next value pairs:
           ('0', '0'), ('0', ''), ('0', '40')
           '' - for deleting the value (set to default)
        2. Set in the cn=config,cn=ldbm database,cn=plugins,cn=config:
           nsslapd-dbcachesize: 0 and some same value
        3. Set in the cn=UserRoot,cn=ldbm database,cn=plugins,cn=config:
           nsslapd-cachememsize: 0 and some same value
        4. Restart the instance
        5. Check nsslapd-dbcachesize and nsslapd-cachememsize
    :expectedresults:
        1. nsslapd-cache-autosize, nsslapd-cache-autosize-split are successfully set
        2. nsslapd-dbcachesize are successfully set
        3. nsslapd-cachememsize are successfully set
        4. The instance should be successfully restarted
        5. nsslapd-dbcachesize and nsslapd-cachememsize should set
           to value greater than 512KB
    """

    config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM)
    bdb_config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM_BDB)
    userroot_ldbm = DSLdapObject(topo.standalone, DN_USERROOT_LDBM)
    config_ldbm.set('nsslapd-cache-autosize', '0')

    # Test with caches with both real values and 0
    for cachesize in ('0', '33333333'):
        if ds_is_older('1.4.2'):
            dbcachesize_val = config_ldbm.get_attr_val('nsslapd-dbcachesize')
            autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
            autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')
        else:
            dbcachesize_val = bdb_config_ldbm.get_attr_val('nsslapd-dbcachesize')
            autosize_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize')
            autosize_split_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

        cachenensize_val = userroot_ldbm.get_attr_val('nsslapd-cachememsize')
        dncachenensize_val = userroot_ldbm.get_attr_val('nsslapd-dncachememsize')

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

        if ds_is_older('1.4.2'):
            dbcachesize_val = config_ldbm.get_attr_val('nsslapd-dbcachesize')
            autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
            autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')
        else:
            dbcachesize_val = bdb_config_ldbm.get_attr_val('nsslapd-dbcachesize')
            autosize_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize')
            autosize_split_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

        cachenensize_val = userroot_ldbm.get_attr_val('nsslapd-cachememsize')
        dncachenensize_val = userroot_ldbm.get_attr_val('nsslapd-dncachememsize')

        log.info("Check nsslapd-dbcachesize and nsslapd-cachememsize in the appropriate range.")
        log.info("nsslapd-dbcachesize == {}".format(dbcachesize_val))
        log.info("nsslapd-cachememsize == {}".format(cachenensize_val))
        log.info("nsslapd-dncachememsize == {}".format(dncachenensize_val))
        log.info("nsslapd-cache-autosize == {}".format(autosize_val))
        log.info("nsslapd-cache-autosize-split == {}".format(autosize_split_val))
        assert int(dbcachesize_val) >= 512000
        assert int(cachenensize_val) >= 512000
        assert int(dncachenensize_val) >= 512000


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
@pytest.mark.parametrize("invalid_value", ('-2', '102', 'invalid'))
def test_cache_autosize_invalid_values(topo, invalid_value):
    """Check that we can't set invalid values to autosize attributes

    :id: 2f0d01b5-ca91-4dc2-97bc-ad0ac8d08633
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Stop the instance
        2. Set in the cn=config,cn=ldbm database,cn=plugins,cn=config:
           nsslapd-cache-autosize and nsslapd-cache-autosize-split
           to invalid values like (-2, 102, invalid_str)
        3. Try to start the instance
    :expectedresults:
        1. The instance should stop successfully
        2. nsslapd-cache-autosize, nsslapd-cache-autosize-split are successfully set
        3. Starting the instance should fail
    """

    config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM)
    bdb_config_ldbm = DSLdapObject(topo.standalone, DN_CONFIG_LDBM_BDB)
    if ds_is_older('1.4.2'):
        autosize_val = config_ldbm.get_attr_val('nsslapd-cache-autosize')
        autosize_split_val = config_ldbm.get_attr_val('nsslapd-cache-autosize-split')
    else:
        autosize_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize')
        autosize_split_val = bdb_config_ldbm.get_attr_val('nsslapd-cache-autosize-split')

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
