# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import ldap
import time
from lib389.topologies import topology_st as topology_st_pre
from lib389.dirsrv_log import DirsrvAccessLog
from lib389._mapped_object import DSLdapObjects
from lib389._constants import DEFAULT_SUFFIX
from lib389.extensibleobject import UnsafeExtensibleObjects

pytestmark = pytest.mark.tier1

def _check_value(inst_cfg, value, exvalue=None):
    if exvalue is None:
        exvalue = value
    inst_cfg.set('nsslapd-verify-filter-schema', value)
    assert(inst_cfg.get_attr_val_utf8('nsslapd-verify-filter-schema') == exvalue)

@pytest.fixture(scope="module")
def topology_st(topology_st_pre):
    raw_objects = UnsafeExtensibleObjects(topology_st_pre.standalone, basedn=DEFAULT_SUFFIX)
    # Add an object that won't be able to be queried due to invalid attrs.
    raw_objects.create(properties = {
        "cn": "test_obj",
        "a": "a",
        "b": "b",
        "uid": "foo"
    })
    return topology_st_pre


@pytest.mark.ds50349
def test_filter_validation_config(topology_st):
    """Test that the new on/warn/off setting can be set and read
    correctly

    :id: ac14dad5-5bdf-474f-9936-7ce2d20fb8b6
    :setup: Standalone instance
    :steps:
        1. Check the default value of nsslapd-verify-filter-schema
        2. Set the value to "on".
        3. Read the value is "on".
        4. Set the value to "warn".
        5. Read the value is "warn".
        6. Set the value to "off".
        7. Read the value is "off".
        8. Delete the value (reset)
        9. Check the reset value matches 1.
    :expectedresults:
        1. Value is "on", "off", or "warn".
        2. Success
        3. Value is "on"
        4. Success
        5. Value is "warn"
        6. Success
        7. Value is "off"
        8. Success
        9. Value is same as from 1.
    """
    inst_cfg = topology_st.standalone.config

    initial_value = inst_cfg.get_attr_val_utf8('nsslapd-verify-filter-schema')

    # Check legacy values that may have been set
    _check_value(inst_cfg, "on", "reject-invalid")
    _check_value(inst_cfg, "warn", "process-safe")
    _check_value(inst_cfg, "off")
    # Check the more descriptive values
    _check_value(inst_cfg, "reject-invalid")
    _check_value(inst_cfg, "process-safe")
    _check_value(inst_cfg, "warn-invalid")
    _check_value(inst_cfg, "off")

    # This should fail

    with pytest.raises(ldap.OPERATIONS_ERROR):
        _check_value(inst_cfg, "thnaounaou")

    inst_cfg.remove_all('nsslapd-verify-filter-schema')
    final_value = inst_cfg.get_attr_val_utf8('nsslapd-verify-filter-schema')
    assert(initial_value == final_value)


@pytest.mark.ds50349
def test_filter_validation_enabled(topology_st):
    """Test that queries which are invalid, are correctly rejected by the server.

    :id: 05afdbbd-0d7f-4774-958c-2139827fed70
    :setup: Standalone instance
    :steps:
        1. Search a well formed query
        2. Search a poorly formed query
        3. Search a poorly formed complex (and/or) query
        4. Test the server can be restarted
    :expectedresults:
        1. No warnings
        2. Query is rejected (err)
        3. Query is rejected (err)
        4. Server restarts
    """
    inst = topology_st.standalone

    # In case the default has changed, we set the value to warn.
    inst.config.set("nsslapd-verify-filter-schema", "reject-invalid")
    raw_objects = DSLdapObjects(inst, basedn=DEFAULT_SUFFIX)

    # Check a good query has no errors.
    r = raw_objects.filter("(objectClass=*)")

    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        # Check a bad one DOES emit an error.
        r = raw_objects.filter("(a=a)")

    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        # Check a bad complex one does emit an error.
        raw_objects.filter("(&(a=a)(b=b)(objectClass=*))")

    # Does restart work?
    inst.restart()


@pytest.mark.ds50349
def test_filter_validation_warn_safe(topology_st):
    """Test that queries which are invalid, are correctly marked as "notes=F" in
    the access log, and return no entries or partial sets.

    :id: 7c8b3374-63c7-4201-9032-faae84c86d50
    :setup: Standalone instance
    :steps:
        1. Search a well formed query
        2. Search a poorly formed query
        3. Search a poorly formed complex (and/or) query
    :expectedresults:
        1. No warnings
        2. notes=F is present
        3. notes=F is present
    """
    inst = topology_st.standalone

    # In case the default has changed, we set the value to warn.
    inst.config.set("nsslapd-verify-filter-schema", "process-safe")
    # Set the access log to un-buffered so we get it immediately.
    inst.config.set("nsslapd-accesslog-logbuffering", "off")
    time.sleep(.5)

    # Setup the query object.
    # Now we don't care if there are any results, we only care about good/bad queries.
    # To do this we have to bypass some of the lib389 magic, and just emit raw queries
    # to check them. Turns out lib389 is well designed and this just works as expected
    # if you use a single DSLdapObjects and filter. :)
    raw_objects = DSLdapObjects(inst, basedn=DEFAULT_SUFFIX)

    # Find any initial notes=F
    access_log = DirsrvAccessLog(inst)
    r_init = access_log.match(".*notes=F.*")

    # Check a good query has no warnings.
    r = raw_objects.filter("(objectClass=*)")
    time.sleep(.5)
    assert(len(r) > 0)
    r_s1 = access_log.match(".*notes=F.*")
    # Should be the same number of log lines IE 0.
    assert(len(r_init) == len(r_s1))

    # Check a bad one DOES emit a warning.
    r = raw_objects.filter("(a=a)")
    time.sleep(.5)
    assert(len(r) == 0)
    r_s2 = access_log.match(".*notes=F.*")
    # Should be the greater number of log lines IE +1
    assert(len(r_init) + 1 == len(r_s2))

    # Check a bad complex one does emit a warning.
    r = raw_objects.filter("(&(a=a)(b=b)(objectClass=*))")
    time.sleep(.5)
    assert(len(r) == 0)
    r_s3 = access_log.match(".*notes=F.*")
    # Should be the greater number of log lines IE +2
    assert(len(r_init) + 2 == len(r_s3))

    # Check that we can still get things when partial
    r = raw_objects.filter("(|(a=a)(b=b)(uid=foo))")
    time.sleep(.5)
    assert(len(r) == 1)
    r_s4 = access_log.match(".*notes=F.*")
    # Should be the greate number of log lines IE +2
    assert(len(r_init) + 3 == len(r_s4))


@pytest.mark.ds50349
def test_filter_validation_warn_unsafe(topology_st):
    """Test that queries which are invalid, are correctly marked as "notes=F" in
    the access log, and uses the legacy query behaviour to return unsafe sets.

    :id: 8b2b23fe-d878-435c-bc84-8c298be4ca1f
    :setup: Standalone instance
    :steps:
        1. Search a well formed query
        2. Search a poorly formed query
        3. Search a poorly formed complex (and/or) query
    :expectedresults:
        1. No warnings
        2. notes=F is present
        3. notes=F is present
    """
    inst = topology_st.standalone

    # In case the default has changed, we set the value to warn.
    inst.config.set("nsslapd-verify-filter-schema", "warn-invalid")
    # Set the access log to un-buffered so we get it immediately.
    inst.config.set("nsslapd-accesslog-logbuffering", "off")
    time.sleep(.5)

    # Setup the query object.
    # Now we don't care if there are any results, we only care about good/bad queries.
    # To do this we have to bypass some of the lib389 magic, and just emit raw queries
    # to check them. Turns out lib389 is well designed and this just works as expected
    # if you use a single DSLdapObjects and filter. :)
    raw_objects = DSLdapObjects(inst, basedn=DEFAULT_SUFFIX)

    # Find any initial notes=F
    access_log = DirsrvAccessLog(inst)
    r_init = access_log.match(".*notes=(U,)?F.*")

    # Check a good query has no warnings.
    r = raw_objects.filter("(objectClass=*)")
    time.sleep(.5)
    assert(len(r) > 0)
    r_s1 = access_log.match(".*notes=(U,)?F.*")
    # Should be the same number of log lines IE 0.
    assert(len(r_init) == len(r_s1))

    # Check a bad one DOES emit a warning.
    r = raw_objects.filter("(a=a)")
    time.sleep(.5)
    assert(len(r) == 1)
    # NOTE: Unlike warn-process-safely, these become UNINDEXED and show in the logs.
    r_s2 = access_log.match(".*notes=(U,)?F.*")
    # Should be the greater number of log lines IE +1
    assert(len(r_init) + 1 == len(r_s2))

    # Check a bad complex one does emit a warning.
    r = raw_objects.filter("(&(a=a)(b=b)(objectClass=*))")
    time.sleep(.5)
    assert(len(r) == 1)
    r_s3 = access_log.match(".*notes=(U,)?F.*")
    # Should be the greater number of log lines IE +2
    assert(len(r_init) + 2 == len(r_s3))

    # Check that we can still get things when partial
    r = raw_objects.filter("(|(a=a)(b=b)(uid=foo))")
    time.sleep(.5)
    assert(len(r) == 1)
    r_s4 = access_log.match(".*notes=(U,)?F.*")
    # Should be the greater number of log lines IE +2
    assert(len(r_init) + 3 == len(r_s4))
