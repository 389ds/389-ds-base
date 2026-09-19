# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Shared synthetic data and helpers for large-filter tests.

This module contains no tests. Functional and sanitizer modules share its
workloads without importing one test module from another.
"""

import ldap
import pytest

from contextlib import contextmanager
from ldap.filter import escape_filter_chars
from ldap.schema.models import AttributeType, ObjectClass
from lib389._constants import DEFAULT_SUFFIX, TASK_WAIT
from lib389._mapped_object import DSLdapObject, DSLdapObjects
from lib389.backend import Backends
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.schema import OBJECT_MODEL_PARAMS, ObjectclassKind, Schema
from lib389.tasks import Tasks
from lib389.utils import ensure_str


TOTAL_ENTRIES = 4200
SELECTED_ENTRIES = 612
DN_BRANCHES = 355

TEST_OU = "LargeFilterInteraction"
TEST_BASE = f"ou={TEST_OU},{DEFAULT_SUFFIX}"

OUTER_A = "lfOuterA"
OUTER_B = "lfOuterB"
DN_ATTR = "lfDnValue"
SUBSTRING_ATTR = "lfSubstringValue"
APPROX_ATTR = "lfApproxValue"
FALLBACK_ATTR = "lfFallbackValue"
GUARD_ATTR = "lfGuardValue"
EXCLUDED_ONE = "lfExcludedOne"
EXCLUDED_TWO = "lfExcludedTwo"
AUX_OC = "lfInteractionAux"

OUTER_VALUE = "selected"
GUARD_VALUE = "guarded"
HIT_POSITIONS = (0, DN_BRANCHES // 2, DN_BRANCHES - 1)
HIT_COHORT_SIZE = SELECTED_ENTRIES // len(HIT_POSITIONS)
LIVE_DNS = tuple(
    f"cn=lf-live-target-{i},ou=References,{DEFAULT_SUFFIX}"
    for i in range(len(HIT_POSITIONS))
)

OR_LOOKUP_ATTR = "nsslapd-enable-or-filter-lookup"

OWNERSHIP_TEST_OU = "LargeFilterOwnership"
OWNERSHIP_TEST_BASE = f"ou={OWNERSHIP_TEST_OU},{DEFAULT_SUFFIX}"
OWNERSHIP_EMPTY_RANGE_ATTR = "lfEmptyRange"
OWNERSHIP_AUX_OC = "lfOwnershipAux"
OWNERSHIP_ENTRY_COUNT = 16
NOT_FIRST_EMPTY_RANGE_FILTER = (
    f"(&(!(uid=lf-ownership-absent))(objectClass=person)"
    f"({OWNERSHIP_EMPTY_RANGE_ATTR}>=0))"
)


ATTRIBUTE_SPECS = (
    {"name": OUTER_A, "oid": "1.3.6.1.4.1.2312.999.2026.1",
     "desc": "large filter outer selector A",
     "equality": "caseIgnoreMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.15"},
    {"name": OUTER_B, "oid": "1.3.6.1.4.1.2312.999.2026.2",
     "desc": "large filter outer selector B",
     "equality": "caseIgnoreMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.15"},
    {"name": DN_ATTR, "oid": "1.3.6.1.4.1.2312.999.2026.3",
     "desc": "large filter DN equality value",
     "equality": "distinguishedNameMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.12"},
    {"name": SUBSTRING_ATTR, "oid": "1.3.6.1.4.1.2312.999.2026.4",
     "desc": "large filter substring value", "equality": "caseIgnoreMatch",
     "substr": "caseIgnoreSubstringsMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.15"},
    {"name": APPROX_ATTR, "oid": "1.3.6.1.4.1.2312.999.2026.5",
     "desc": "large filter approximate value",
     "equality": "caseIgnoreMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.15"},
    {"name": FALLBACK_ATTR, "oid": "1.3.6.1.4.1.2312.999.2026.6",
     "desc": "large filter absent fallback", "equality": "caseIgnoreMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.15"},
    {"name": GUARD_ATTR, "oid": "1.3.6.1.4.1.2312.999.2026.7",
     "desc": "large filter fallback guard", "equality": "caseIgnoreMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.15"},
    {"name": EXCLUDED_ONE, "oid": "1.3.6.1.4.1.2312.999.2026.8",
     "desc": "large filter unindexed presence sentinel one",
     "equality": "caseIgnoreMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.15"},
    {"name": EXCLUDED_TWO, "oid": "1.3.6.1.4.1.2312.999.2026.9",
     "desc": "large filter unindexed presence sentinel two",
     "equality": "caseIgnoreMatch",
     "syntax": "1.3.6.1.4.1.1466.115.121.1.15"},
)

OBJECTCLASS_MAY = (
    OUTER_A, OUTER_B, DN_ATTR, SUBSTRING_ATTR, APPROX_ATTR, FALLBACK_ATTR,
    GUARD_ATTR, EXCLUDED_ONE, EXCLUDED_TWO,
)

INDEX_DEFINITIONS = (
    (OUTER_A, ["eq"]),
    (OUTER_B, ["eq"]),
    (DN_ATTR, ["eq"]),
    (SUBSTRING_ATTR, ["eq", "sub"]),
    (APPROX_ATTR, ["eq", "approx"]),
)


class LargeFilterEntry(DSLdapObject):
    """One deterministic synthetic interaction entry."""

    def __init__(self, instance, dn=None):
        super(LargeFilterEntry, self).__init__(instance, dn)
        self._rdn_attribute = "uid"
        self._must_attributes = ["uid", "cn", "sn"]
        self._create_objectclasses = [
            "top", "person", "organizationalPerson", "inetOrgPerson", AUX_OC
        ]
        self._protected = False


class LargeFilterEntries(DSLdapObjects):
    """Collection used to create interaction entries through lib389."""

    def __init__(self, instance, basedn):
        super(LargeFilterEntries, self).__init__(instance)
        self._objectclasses = [AUX_OC]
        self._filterattrs = ["uid"]
        self._childobject = LargeFilterEntry
        self._basedn = basedn


class OwnershipEntry(DSLdapObject):
    """One person used by the NOT-first ownership workload."""

    def __init__(self, instance, dn=None):
        super(OwnershipEntry, self).__init__(instance, dn)
        self._rdn_attribute = "uid"
        self._must_attributes = ["uid", "cn", "sn"]
        self._create_objectclasses = [
            "top", "person", "organizationalPerson", "inetOrgPerson",
            OWNERSHIP_AUX_OC,
        ]
        self._protected = False


class OwnershipEntries(DSLdapObjects):
    """Collection used to create NOT-first ownership entries."""

    def __init__(self, instance, basedn):
        super(OwnershipEntries, self).__init__(instance)
        self._objectclasses = [OWNERSHIP_AUX_OC]
        self._filterattrs = ["uid"]
        self._childobject = OwnershipEntry
        self._basedn = basedn


def entry_uid(index):
    return f"lf-interaction-{index:05d}"


def entry_dn(index):
    return f"uid={entry_uid(index)},{TEST_BASE}"


def ghost_dn(index):
    return f"cn=lf-ghost-{index:04d},ou=References,{DEFAULT_SUFFIX}"


@contextmanager
def lookup_disabled(inst):
    """Disable the equality lookup when the server exposes it.

    On servers without the attribute the context is a no-op and the caller
    just repeats the same search.
    """
    old_value = inst.config.get_attr_val_utf8(OR_LOOKUP_ATTR)
    if old_value is None:
        yield
        return
    inst.config.replace(OR_LOOKUP_ATTR, "off")
    try:
        yield
    finally:
        inst.config.replace(OR_LOOKUP_ATTR, old_value)


def search_dns(inst, filterstr, base=TEST_BASE, scope=ldap.SCOPE_SUBTREE):
    """Run a complete search and return the LDAP result type and exact DNs."""
    msgid = inst.search_ext(base, scope, filterstr, ["1.1"])
    result_type, result_data, _, _ = inst.result3(msgid)
    dns = sorted(ensure_str(dn).lower() for dn, _ in result_data if dn)
    return result_type, dns


@contextmanager
def ownership_workload(inst, remove_on_exit=True):
    """Create the exact NOT-first workload and optionally remove it on exit."""
    schema = Schema(inst)
    backend = Backends(inst).get("userRoot")
    attr_added = False
    objectclass_added = False
    index_added = False
    setup_succeeded = False
    container = None

    try:
        attr_params = OBJECT_MODEL_PARAMS[AttributeType].copy()
        attr_params.update({
            "names": (OWNERSHIP_EMPTY_RANGE_ATTR,),
            "oid": "1.3.6.1.4.1.2312.999.2026.20",
            "desc": "emptied indexed range for NOT-first ownership test",
            "equality": "integerMatch",
            "ordering": "integerOrderingMatch",
            "syntax": "1.3.6.1.4.1.1466.115.121.1.27",
            "single_value": 1,
            "x_origin": ("389-ds large filter tests",),
        })
        schema.add_attributetype(attr_params)
        attr_added = True

        oc_params = OBJECT_MODEL_PARAMS[ObjectClass].copy()
        oc_params.update({
            "names": (OWNERSHIP_AUX_OC,),
            "oid": "1.3.6.1.4.1.2312.999.2026.21",
            "desc": "auxiliary class for NOT-first ownership test",
            "kind": ObjectclassKind.AUXILIARY.value,
            "sup": ("top",),
            "may": (OWNERSHIP_EMPTY_RANGE_ATTR,),
            "x_origin": ("389-ds large filter tests",),
        })
        schema.add_objectclass(oc_params)
        objectclass_added = True

        backend.add_index(
            OWNERSHIP_EMPTY_RANGE_ATTR, ["eq"],
            matching_rules=["integerOrderingMatch"]
        )
        index_added = True
        assert Tasks(inst).reindex(
            benamebase="userRoot",
            attrname=[OWNERSHIP_EMPTY_RANGE_ATTR],
            args={TASK_WAIT: True},
        ) == 0
        index = backend.get_index(OWNERSHIP_EMPTY_RANGE_ATTR)
        assert index is not None
        assert index.get_attr_vals_utf8_l("nsIndexType") == ["eq"]
        assert index.get_attr_vals_utf8_l("nsMatchingRule") == [
            "integerorderingmatch"
        ]

        container = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(
            properties={"ou": OWNERSHIP_TEST_OU}
        )
        entries = OwnershipEntries(inst, OWNERSHIP_TEST_BASE)
        created = []
        for entry_index in range(OWNERSHIP_ENTRY_COUNT):
            uid = f"lf-ownership-{entry_index:03d}"
            created.append(entries.create(properties={
                "uid": uid,
                "cn": f"Large Filter Ownership {entry_index:03d}",
                "sn": f"Ownership{entry_index:03d}",
            }))

        result_type, dns = search_dns(
            inst, f"(objectClass={OWNERSHIP_AUX_OC})",
            base=OWNERSHIP_TEST_BASE, scope=ldap.SCOPE_ONELEVEL
        )
        assert result_type == ldap.RES_SEARCH_RESULT
        assert dns == sorted(entry.dn.lower() for entry in created)

        created[0].add(OWNERSHIP_EMPTY_RANGE_ATTR, "0")
        created[0].remove_all(OWNERSHIP_EMPTY_RANGE_ATTR)
        result_type, dns = search_dns(
            inst, f"({OWNERSHIP_EMPTY_RANGE_ATTR}>=0)",
            base=OWNERSHIP_TEST_BASE, scope=ldap.SCOPE_SUBTREE
        )
        assert result_type == ldap.RES_SEARCH_RESULT
        assert dns == []

        setup_succeeded = True
        yield {"backend": backend, "entries": created}
    finally:
        if remove_on_exit or not setup_succeeded:
            try:
                if container is not None:
                    container.delete(recursive=True)
            finally:
                try:
                    if index_added:
                        backend.del_index(OWNERSHIP_EMPTY_RANGE_ATTR)
                finally:
                    if objectclass_added:
                        schema.remove_objectclass(OWNERSHIP_AUX_OC)
                    if attr_added:
                        schema.remove_attributetype(OWNERSHIP_EMPTY_RANGE_ATTR)


def dn_or(assertions):
    return "(|%s)" % "".join(
        f"({DN_ATTR}={escape_filter_chars(value)})" for value in assertions
    )


def miss_assertions():
    return [ghost_dn(i) for i in range(DN_BRANCHES)]


def hit_assertions(position):
    """Put the sole live assertion at a requested table-walk position."""
    assertions = miss_assertions()
    assertions[position] = LIVE_DNS[HIT_POSITIONS.index(position)]
    return assertions


def combined_assertions():
    """Return 355 branches whose three live values cover all 612 selected."""
    assertions = miss_assertions()
    for position, value in zip(HIT_POSITIONS, LIVE_DNS):
        assertions[position] = value
    return assertions


def combined_filter(cost_component):
    """A 612-entry bound, a 355-branch DN OR, and one costly component."""
    return (f"(&({OUTER_A}={OUTER_VALUE})"
            f"{dn_or(combined_assertions())}"
            f"{cost_component})")


@pytest.fixture(scope="module")
def interaction_data(topo):
    """Create schema, indexes, and all entries through live lib389 APIs."""
    inst = topo.standalone
    schema = Schema(inst)
    backend = Backends(inst).get("userRoot")
    added_attrs = []
    added_indexes = []
    objectclass_added = False
    container = None

    try:
        for spec in ATTRIBUTE_SPECS:
            params = OBJECT_MODEL_PARAMS[AttributeType].copy()
            params.update({
                "names": (spec["name"],),
                "oid": spec["oid"],
                "desc": spec["desc"],
                "equality": spec["equality"],
                "substr": spec.get("substr"),
                "syntax": spec["syntax"],
                "single_value": 1,
                "x_origin": ("389-ds large filter tests",),
            })
            schema.add_attributetype(params)
            added_attrs.append(spec["name"])

        oc_params = OBJECT_MODEL_PARAMS[ObjectClass].copy()
        oc_params.update({
            "names": (AUX_OC,),
            "oid": "1.3.6.1.4.1.2312.999.2026.10",
            "desc": "auxiliary class for large filter interaction tests",
            "kind": ObjectclassKind.AUXILIARY.value,
            "sup": ("top",),
            "may": OBJECTCLASS_MAY,
            "x_origin": ("389-ds large filter tests",),
        })
        schema.add_objectclass(oc_params)
        objectclass_added = True

        for attr, index_types in INDEX_DEFINITIONS:
            backend.add_index(attr, index_types)
            added_indexes.append(attr)
        backend.reindex(attrs=added_indexes, wait=True)

        container = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(
            properties={"ou": TEST_OU}
        )
        entries = LargeFilterEntries(inst, TEST_BASE)
        for i in range(TOTAL_ENTRIES):
            props = {
                "uid": entry_uid(i),
                "cn": f"Large Filter Common Xanadu Entry {i:05d}",
                "sn": f"Interaction{i:05d}",
                SUBSTRING_ATTR: f"Common Xanadu indexed value {i:05d}",
                APPROX_ATTR: "Xanadu",
            }
            if i < SELECTED_ENTRIES:
                props.update({
                    OUTER_A: OUTER_VALUE,
                    OUTER_B: OUTER_VALUE,
                    DN_ATTR: LIVE_DNS[i // HIT_COHORT_SIZE],
                    GUARD_ATTR: GUARD_VALUE,
                    # Makes the complex fallback branch false; the second
                    # excluded attribute stays absent and neither has a
                    # presence index.
                    EXCLUDED_ONE: "present",
                })
            entries.create(properties=props)

        yield {
            "backend": backend,
            "selected_dns": sorted(
                entry_dn(i).lower() for i in range(SELECTED_ENTRIES)
            ),
            "hit_dns": {
                position: sorted(
                    entry_dn(i).lower()
                    for i in range(cohort * HIT_COHORT_SIZE,
                                   (cohort + 1) * HIT_COHORT_SIZE)
                )
                for cohort, position in enumerate(HIT_POSITIONS)
            },
        }
    finally:
        try:
            if container is not None:
                container.delete(recursive=True)
        finally:
            try:
                for attr in reversed(added_indexes):
                    backend.del_index(attr)
            finally:
                if objectclass_added:
                    schema.remove_objectclass(AUX_OC)
                for attr in reversed(added_attrs):
                    schema.remove_attributetype(attr)
