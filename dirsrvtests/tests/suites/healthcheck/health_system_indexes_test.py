# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import os

from lib389.backend import Backends
from lib389.index import Index
from lib389.plugins import (
    USNPlugin,
    RetroChangelogPlugin,
)
from lib389.utils import logging, ds_is_newer
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_st
from lib389.cli_ctl.health import health_check_run

pytestmark = pytest.mark.tier1

CMD_OUTPUT = "No issues found."
JSON_OUTPUT = "[]"
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def usn_plugin_enabled(topology_st, request):
    """Fixture to enable USN plugin and ensure cleanup after test"""
    standalone = topology_st.standalone

    log.info("Enable USN plugin")
    usn_plugin = USNPlugin(standalone)
    usn_plugin.enable()
    standalone.restart()

    def cleanup():
        log.info("Disable USN plugin")
        usn_plugin.disable()
        standalone.restart()

    request.addfinalizer(cleanup)
    return usn_plugin


@pytest.fixture(scope="function")
def retrocl_plugin_enabled(topology_st, request):
    """Fixture to enable RetroCL plugin and ensure cleanup after test"""
    standalone = topology_st.standalone

    log.info("Enable RetroCL plugin")
    retrocl_plugin = RetroChangelogPlugin(standalone)
    retrocl_plugin.enable()
    standalone.restart()

    def cleanup():
        log.info("Disable RetroCL plugin")
        retrocl_plugin.disable()
        standalone.restart()

    request.addfinalizer(cleanup)
    return retrocl_plugin


@pytest.fixture(scope="function")
def log_buffering_enabled(topology_st, request):
    """Fixture to enable log buffering and restore original setting after test"""
    standalone = topology_st.standalone

    original_value = standalone.config.get_attr_val_utf8("nsslapd-accesslog-logbuffering")

    log.info("Enable log buffering")
    standalone.config.set("nsslapd-accesslog-logbuffering", "on")

    def cleanup():
        log.info("Restore original log buffering setting")
        standalone.config.set("nsslapd-accesslog-logbuffering", original_value)

    request.addfinalizer(cleanup)
    return standalone


def run_healthcheck_and_flush_log(topology, instance, searched_code, json, searched_code2=None):
    args = FakeArgs()
    args.instance = instance.serverid
    args.verbose = instance.verbose
    args.list_errors = False
    args.list_checks = False
    args.check = [
        "config",
        "refint",
        "backends",
        "monitor-disk-space",
        "logs",
        "memberof",
    ]
    args.dry_run = False

    # If we are using BDB as a backend, we will get error DSBLE0006 on new versions
    if (
        ds_is_newer("3.0.0")
        and instance.get_db_lib() == "bdb"
        and (searched_code is CMD_OUTPUT or searched_code is JSON_OUTPUT)
    ):
        searched_code = "DSBLE0006"

    if json:
        log.info("Use healthcheck with --json option")
        args.json = json
        health_check_run(instance, topology.logcap.log, args)
        assert topology.logcap.contains(searched_code)
        log.info("healthcheck returned searched code: %s" % searched_code)

        if searched_code2 is not None:
            assert topology.logcap.contains(searched_code2)
            log.info("healthcheck returned searched code: %s" % searched_code2)
    else:
        log.info("Use healthcheck without --json option")
        args.json = json
        health_check_run(instance, topology.logcap.log, args)

        assert topology.logcap.contains(searched_code)
        log.info("healthcheck returned searched code: %s" % searched_code)

        if searched_code2 is not None:
            assert topology.logcap.contains(searched_code2)
            log.info("healthcheck returned searched code: %s" % searched_code2)

    log.info("Clear the log")
    topology.logcap.flush()


def test_missing_parentid(topology_st, log_buffering_enabled):
    """Check if healthcheck returns DSBLE0007 code when parentId system index is missing

    :id: 2653f16f-cc9c-4fad-9d8c-86a3457c6d0d
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Remove parentId index
        3. Use healthcheck without --json option
        4. Use healthcheck with --json option
        5. Re-add the parentId index
        6. Use healthcheck without --json option
        7. Use healthcheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. healthcheck reports DSBLE0007 code and related details
        4. healthcheck reports DSBLE0007 code and related details
        5. Success
        6. healthcheck reports no issues found
        7. healthcheck reports no issues found
    """

    RET_CODE = "DSBLE0007"
    PARENTID_DN = "cn=parentid,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"

    standalone = topology_st.standalone

    log.info("Remove parentId index")
    parentid_index = Index(standalone, PARENTID_DN)
    parentid_index.delete()

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info("Re-add the parentId index")
    backend = Backends(standalone).get("userRoot")
    backend.add_index("parentid", ["eq"], matching_rules=["integerOrderingMatch"])

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


def test_missing_matching_rule(topology_st, log_buffering_enabled):
    """Check if healthcheck returns DSBLE0007 code when parentId index is missing integerOrderingMatch

    :id: 7ffa71db-8995-430a-bed8-59bce944221c
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Remove integerOrderingMatch matching rule from parentId index
        3. Use healthcheck without --json option
        4. Use healthcheck with --json option
        5. Re-add the matching rule
        6. Use healthcheck without --json option
        7. Use healthcheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. healthcheck reports DSBLE0007 code and related details
        4. healthcheck reports DSBLE0007 code and related details
        5. Success
        6. healthcheck reports no issues found
        7. healthcheck reports no issues found
    """

    RET_CODE = "DSBLE0007"
    PARENTID_DN = "cn=parentid,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"

    standalone = topology_st.standalone

    log.info("Remove integerOrderingMatch matching rule from parentId index")
    parentid_index = Index(standalone, PARENTID_DN)
    parentid_index.remove("nsMatchingRule", "integerOrderingMatch")

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info("Re-add the integerOrderingMatch matching rule")
    parentid_index = Index(standalone, PARENTID_DN)
    parentid_index.add("nsMatchingRule", "integerOrderingMatch")

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


def test_usn_plugin_missing_entryusn(topology_st, usn_plugin_enabled, log_buffering_enabled):
    """Check if healthcheck returns DSBLE0007 code when USN plugin is enabled but entryusn index is missing

    :id: 4879dfc8-cd96-43e6-9ebc-053fc8e64ad0
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Enable USN plugin
        3. Remove entryusn index
        4. Use healthcheck without --json option
        5. Use healthcheck with --json option
        6. Re-add the entryusn index
        7. Use healthcheck without --json option
        8. Use healthcheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. healthcheck reports DSBLE0007 code and related details
        5. healthcheck reports DSBLE0007 code and related details
        6. Success
        7. healthcheck reports no issues found
        8. healthcheck reports no issues found
    """

    RET_CODE = "DSBLE0007"
    ENTRYUSN_DN = "cn=entryusn,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"

    standalone = topology_st.standalone

    log.info("Remove entryusn index")
    entryusn_index = Index(standalone, ENTRYUSN_DN)
    entryusn_index.delete()

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info("Re-add the entryusn index")
    backend = Backends(standalone).get("userRoot")
    backend.add_index("entryusn", ["eq"], matching_rules=["integerOrderingMatch"])

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


def test_usn_plugin_missing_matching_rule(topology_st, usn_plugin_enabled, log_buffering_enabled):
    """Check if healthcheck returns DSBLE0007 code when USN plugin is enabled but entryusn index is missing integerOrderingMatch

    :id: b00b419f-2ca6-451f-a9b2-f22ad6b10718
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Enable USN plugin
        3. Remove integerOrderingMatch matching rule from entryusn index
        4. Use healthcheck without --json option
        5. Use healthcheck with --json option
        6. Re-add the matching rule
        7. Use healthcheck without --json option
        8. Use healthcheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. healthcheck reports DSBLE0007 code and related details
        5. healthcheck reports DSBLE0007 code and related details
        6. Success
        7. healthcheck reports no issues found
        8. healthcheck reports no issues found
    """

    RET_CODE = "DSBLE0007"
    ENTRYUSN_DN = "cn=entryusn,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"

    standalone = topology_st.standalone

    log.info("Create or modify entryusn index without integerOrderingMatch")
    entryusn_index = Index(standalone, ENTRYUSN_DN)
    entryusn_index.remove("nsMatchingRule", "integerOrderingMatch")

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info("Re-add the integerOrderingMatch matching rule")
    entryusn_index = Index(standalone, ENTRYUSN_DN)
    entryusn_index.add("nsMatchingRule", "integerOrderingMatch")

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


def test_retrocl_plugin_missing_changenumber(topology_st, retrocl_plugin_enabled, log_buffering_enabled):
    """Check if healthcheck returns DSBLE0007 code when RetroCL plugin is enabled but changeNumber index is missing from changelog backend

    :id: 3e1a3625-4e6f-4e23-868d-6f32e018ad7e
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Enable RetroCL plugin
        3. Remove changeNumber index from changelog backend
        4. Use healthcheck without --json option
        5. Use healthcheck with --json option
        6. Re-add the changeNumber index
        7. Use healthcheck without --json option
        8. Use healthcheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. healthcheck reports DSBLE0007 code and related details
        5. healthcheck reports DSBLE0007 code and related details
        6. Success
        7. healthcheck reports no issues found
        8. healthcheck reports no issues found
    """

    RET_CODE = "DSBLE0007"

    standalone = topology_st.standalone

    log.info("Remove changeNumber index from changelog backend")
    changenumber_dn = "cn=changenumber,cn=index,cn=changelog,cn=ldbm database,cn=plugins,cn=config"
    changenumber_index = Index(standalone, changenumber_dn)
    changenumber_index.delete()

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info("Re-add the changeNumber index")
    backends = Backends(standalone)
    changelog_backend = backends.get("changelog")
    changelog_backend.add_index("changenumber", ["eq"], matching_rules=["integerOrderingMatch"])
    log.info("Successfully re-added changeNumber index")

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


def test_retrocl_plugin_missing_matching_rule(topology_st, retrocl_plugin_enabled, log_buffering_enabled):
    """Check if healthcheck returns DSBLE0007 code when RetroCL plugin is enabled but changeNumber index is missing integerOrderingMatch

    :id: 1c68b1b2-90a9-4ec0-815a-a626b20744fe
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Enable RetroCL plugin
        3. Remove integerOrderingMatch matching rule from changeNumber index
        4. Use healthcheck without --json option
        5. Use healthcheck with --json option
        6. Re-add the matching rule
        7. Use healthcheck without --json option
        8. Use healthcheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. healthcheck reports DSBLE0007 code and related details
        5. healthcheck reports DSBLE0007 code and related details
        6. Success
        7. healthcheck reports no issues found
        8. healthcheck reports no issues found
    """

    RET_CODE = "DSBLE0007"

    standalone = topology_st.standalone

    log.info("Remove integerOrderingMatch matching rule from changeNumber index")
    changenumber_dn = "cn=changenumber,cn=index,cn=changelog,cn=ldbm database,cn=plugins,cn=config"
    changenumber_index = Index(standalone, changenumber_dn)
    changenumber_index.remove("nsMatchingRule", "integerOrderingMatch")

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info("Re-add the integerOrderingMatch matching rule")
    changenumber_index = Index(standalone, changenumber_dn)
    changenumber_index.add("nsMatchingRule", "integerOrderingMatch")
    log.info("Successfully re-added integerOrderingMatch to changeNumber index")

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


def test_multiple_missing_indexes(topology_st, log_buffering_enabled):
    """Check if healthcheck returns DSBLE0007 code when multiple system indexes are missing

    :id: f7cfcd6e-3c47-4ba5-bb2b-1f8e7a29c899
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Remove multiple system indexes (parentId, nsUniqueId)
        3. Use healthcheck without --json option
        4. Use healthcheck with --json option
        5. Re-add the missing indexes
        6. Use healthcheck without --json option
        7. Use healthcheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. healthcheck reports DSBLE0007 code and related details
        4. healthcheck reports DSBLE0007 code and related details
        5. Success
        6. healthcheck reports no issues found
        7. healthcheck reports no issues found
    """

    RET_CODE = "DSBLE0007"
    PARENTID_DN = "cn=parentid,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"
    NSUNIQUEID_DN = "cn=nsuniqueid,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"

    standalone = topology_st.standalone

    log.info("Remove multiple system indexes")
    for index_dn in [PARENTID_DN, NSUNIQUEID_DN]:
        index = Index(standalone, index_dn)
        index.delete()
        log.info(f"Successfully removed index: {index_dn}")

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=RET_CODE)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=RET_CODE)

    log.info("Re-add the missing system indexes")
    backend = Backends(standalone).get("userRoot")
    backend.add_index("parentid", ["eq"], matching_rules=["integerOrderingMatch"])
    backend.add_index("nsuniqueid", ["eq"])

    run_healthcheck_and_flush_log(topology_st, standalone, json=False, searched_code=CMD_OUTPUT)
    run_healthcheck_and_flush_log(topology_st, standalone, json=True, searched_code=JSON_OUTPUT)


def test_upgrade_removes_parentid_scanlimit(topology_st):
    """Check if upgrade function removes nsIndexIDListScanLimit from parentid index

    :id: 2808886e-c1c1-441d-b3a3-299c4ef1ab4a
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Stop the server
        3. Use DSEldif to add nsIndexIDListScanLimit to parentid index
        4. Start the server (triggers upgrade)
        5. Verify nsIndexIDListScanLimit is removed from parentid index
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. nsIndexIDListScanLimit is no longer present
    """
    from lib389.dseldif import DSEldif

    standalone = topology_st.standalone
    PARENTID_DN = "cn=parentid,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"
    SCANLIMIT_VALUE = "limit=5000 type=eq flags=AND"

    log.info("Stop the server")
    standalone.stop()

    log.info("Add nsIndexIDListScanLimit to parentid index using DSEldif")
    dse_ldif = DSEldif(standalone)
    dse_ldif.add(PARENTID_DN, "nsIndexIDListScanLimit", SCANLIMIT_VALUE)

    # Verify it was added
    scanlimit = dse_ldif.get(PARENTID_DN, "nsIndexIDListScanLimit")
    assert scanlimit is not None, "Failed to add nsIndexIDListScanLimit"
    log.info(f"Added nsIndexIDListScanLimit: {scanlimit}")

    log.info("Start the server (triggers upgrade)")
    standalone.start()

    log.info("Verify nsIndexIDListScanLimit was removed by upgrade")
    # Check via LDAP - the upgrade should have removed it
    parentid_index = Index(standalone, PARENTID_DN)
    scanlimit_after = parentid_index.get_attr_vals_utf8("nsIndexIDListScanLimit")
    log.info(f"nsIndexIDListScanLimit after upgrade: {scanlimit_after}")

    # The upgrade function should have removed nsIndexIDListScanLimit
    assert not scanlimit_after, \
        f"nsIndexIDListScanLimit should have been removed but found: {scanlimit_after}"

    log.info("Upgrade successfully removed nsIndexIDListScanLimit from parentid index")

    # Verify idempotency - restart again and ensure no errors
    log.info("Restart server again to verify idempotency (no errors on second run)")
    standalone.restart()
    # Verify the attribute is still absent
    scanlimit_after_second = parentid_index.get_attr_vals_utf8("nsIndexIDListScanLimit")
    assert not scanlimit_after_second, \
        f"nsIndexIDListScanLimit should still be absent after second restart but found: {scanlimit_after_second}"
    log.info("Idempotency verified - no issues on second restart")


def test_upgrade_removes_ancestorid_index_config(topology_st):
    """Check if upgrade function removes ancestorid index config entry

    :id: 3f3d6e9b-75ac-4f0d-b2ce-7204e6eacd0a
    :setup: Standalone instance
    :steps:
        1. Create DS instance
        2. Stop the server
        3. Use DSEldif to add an ancestorid index config entry
        4. Start the server (triggers upgrade)
        5. Verify ancestorid index config entry is removed
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. ancestorid index config entry is no longer present
    """
    from lib389.dseldif import DSEldif

    standalone = topology_st.standalone
    ANCESTORID_DN = "cn=ancestorid,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config"

    log.info("Stop the server")
    standalone.stop()

    log.info("Add ancestorid index config entry using DSEldif")
    dse_ldif = DSEldif(standalone)

    # Create a fake ancestorid index entry
    ancestorid_entry = [
        "dn: {}\n".format(ANCESTORID_DN),
        "objectClass: top\n",
        "objectClass: nsIndex\n",
        "cn: ancestorid\n",
        "nsSystemIndex: true\n",
        "nsIndexType: eq\n",
        "nsMatchingRule: integerOrderingMatch\n",
        "\n"
    ]
    dse_ldif.add_entry(ancestorid_entry)

    # Verify it was added by re-reading dse.ldif
    dse_ldif2 = DSEldif(standalone)
    cn_value = dse_ldif2.get(ANCESTORID_DN, "cn")
    assert cn_value is not None, "Failed to add ancestorid index config entry"
    log.info(f"Added ancestorid index entry with cn: {cn_value}")

    log.info("Start the server (triggers upgrade)")
    standalone.start()

    log.info("Verify ancestorid index config entry was removed by upgrade")
    # Check via LDAP - the upgrade should have removed the entry
    try:
        ancestorid_index = Index(standalone, ANCESTORID_DN)
        # If we can get the entry, it wasn't removed - this is a failure
        cn_after = ancestorid_index.get_attr_vals_utf8("cn")
        assert False, f"ancestorid index config entry should have been removed but still exists: {cn_after}"
    except Exception as e:
        # Entry should not exist - this is expected
        log.info(f"ancestorid index config entry correctly removed (got exception: {e})")

    log.info("Upgrade successfully removed ancestorid index config entry")

    # Verify idempotency - restart again and ensure no errors
    log.info("Restart server again to verify idempotency (no errors on second run)")
    standalone.restart()
    # Verify the entry is still absent
    try:
        ancestorid_index = Index(standalone, ANCESTORID_DN)
        cn_after_second = ancestorid_index.get_attr_vals_utf8("cn")
        assert False, f"ancestorid index config entry should still be absent after second restart but found: {cn_after_second}"
    except Exception as e:
        log.info(f"Idempotency verified - ancestorid still absent after second restart (got exception: {e})")


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
