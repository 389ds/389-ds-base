# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Exercise objectclass index entry IDs when the suffix is created after replication."""

import logging
import os
import re
import pytest

from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.idm.domain import Domain
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.replica import Replicas
from test389.topologies import topology_no_sample

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _assert_oc_index_missing_key(inst, key):
    """Dump the objectclass index with dbscan -r; equality key ``key`` must not appear."""
    inst.stop()
    try:
        if inst.get_db_lib() == "mdb":
            path = os.path.join(inst.dbdir, DEFAULT_BENAME.lower(), "objectclass")
        else:
            path = os.path.join(inst.dbdir, DEFAULT_BENAME, "objectclass.db")
        out = inst.dbscan(
            None,
            None,
            args=["-f", path, "-r"],
            stopping=False,
            check_result=False,
        ).decode("utf-8", errors="replace")
    finally:
        inst.start()
    log.info("dbscan objectclass index (expect no %s):\n%s", key, out)
    for line in out.splitlines():
        if line.strip() == key:
            raise AssertionError(
                f"objectclass index unexpectedly contains equality key {key!r}:\n{out}"
            )


def _assert_oc_index_key_contains_id(inst, key, eid):
    """Run dbscan -r on the objectclass index; require key and entry ID (see dbscan idl_format)."""
    inst.stop()
    try:
        if inst.get_db_lib() == "mdb":
            path = os.path.join(inst.dbdir, DEFAULT_BENAME.lower(), "objectclass")
        else:
            path = os.path.join(inst.dbdir, DEFAULT_BENAME, "objectclass.db")
        out = inst.dbscan(
            None,
            None,
            args=["-f", path, "-k", key, "-r"],
            stopping=False,
            check_result=False,
        ).decode("utf-8", errors="replace")
    finally:
        inst.start()
    log.info("dbscan -k %s output:\n%s", key, out)
    if not re.search(rf"\b{eid}\b", out):
        raise AssertionError(
            f"expected objectclass index key {key!r} to list id {eid}; output:\n{out}"
        )


def test_objectclass_index_ids_repl_before_suffix(topology_no_sample):
    """objectclass equality keys track entry IDs when the suffix is added after replication.

    :id: c4a8e2b1-7f3d-4a1e-9c02-8b6d5e4f3a10
    :setup: Standalone instance with userRoot for dc=example,dc=com but no suffix entry;
            replication enabled as a single supplier.
    :steps:
        1. Enable replication on the backend (no suffix entry yet).
        2. dbscan objectclass index must not have an ``=domain`` key yet.
        3. dbscan objectclass index: ``=nstombstone`` must include ID 2 (RUV tombstone).
        4. Add the suffix entry (dc=example,dc=com).
        5. dbscan: ``=domain`` must include ID 1.
        6. Add an organizationalUnit under the suffix.
        7. dbscan: ``=organizationalunit`` must include ID 3.
    :expectedresults:
        1. Replication is configured.
        2. No domain objectclass key before the suffix exists.
        3. Tombstone objectclass is indexed for the RUV entry.
        4. Suffix entry exists.
        5. Domain objectclass maps to the suffix entry id.
        6. OU entry exists.
        7. organizationalUnit objectclass maps to the new entry id.
    """
    inst = topology_no_sample.standalone

    log.info("Enable replication (supplier) without creating the suffix entry")
    replicas = Replicas(inst)
    replicas.create(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaId': '1',
        'nsDS5Flags': '1',
        'nsDS5ReplicaType': '3',
        'nsDS5ReplicaBindDN': 'cn=replication manager,cn=config',
    })

    log.info("Objectclass index must not have =domain before the suffix entry exists")
    _assert_oc_index_missing_key(inst, "=domain")

    log.info("Check RUV tombstone is indexed on objectclass with id 2")
    _assert_oc_index_key_contains_id(inst, "=nstombstone", 2)

    log.info("Create suffix entry %s", DEFAULT_SUFFIX)
    Domain(inst, DEFAULT_SUFFIX).create(properties={"dc": "example"})

    log.info("Check domain objectclass is indexed with id 1")
    _assert_oc_index_key_contains_id(inst, "=domain", 1)

    log.info("Create organizational unit ou=testou under the suffix")
    OrganizationalUnits(inst, DEFAULT_SUFFIX).create(properties={"ou": "testou"})

    log.info("Check organizationalUnit objectclass is indexed with id 3")
    _assert_oc_index_key_contains_id(inst, "=organizationalunit", 3)
