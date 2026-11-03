# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

"""MemberOf correctness and changelog payload after a replicated user move."""

import logging
import os
import re
import threading
import time

import ldap
import pytest

from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.backend import Backends
from lib389.idm.group import Group, Groups, UniqueGroups
from lib389.idm.organizationalunit import OrganizationalUnit, OrganizationalUnits
from lib389.idm.user import UserAccount, UserAccounts
from lib389.plugins import MemberOfPlugin, ReferentialIntegrityPlugin
from lib389.replica import Replicas
from lib389.utils import get_default_db_lib
from test389.topologies import topology_m2 as topo_m2, topology_st as topo_st


pytestmark = pytest.mark.tier1
DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG if DEBUGGING else logging.INFO)
log = logging.getLogger(__name__)

GROUP_COUNT = 32
PEOPLE_DN = "ou=memberof-growth-people," + DEFAULT_SUFFIX
DISABLED_DN = "ou=memberof-growth-disabled," + DEFAULT_SUFFIX
GROUPS_DN = "ou=memberof-growth-groups," + DEFAULT_SUFFIX
OLD_USER_DN = "uid=memberof-growth-user," + PEOPLE_DN
NEW_USER_DN = "uid=memberof-growth-user," + DISABLED_DN


def _ruv_maxima(inst):
    rows = Replicas(inst).get(DEFAULT_SUFFIX).get_ruv().format_ruv()["ruvs"]
    return {row["rid"]: row["raw_maxcsn"] for row in rows}


def _wait_ruvs(instances, timeout=180):
    """Require equal stable RUV samples before checking persisted LDAP state."""
    deadline = time.monotonic() + timeout
    previous = None
    while time.monotonic() < deadline:
        vectors = [_ruv_maxima(inst) for inst in instances]
        if vectors and all(vector == vectors[0] for vector in vectors):
            if vectors == previous:
                return
            previous = vectors
        else:
            previous = None
        time.sleep(1)
    raise AssertionError("Supplier RUVs did not converge within {} seconds".format(timeout))


def _agreements(instances):
    return [agreement for inst in instances
            for agreement in Replicas(inst).get(DEFAULT_SUFFIX).get_agreements().list()]


def _pause_agreements(agreements):
    for agreement in agreements:
        agreement.pause()
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if all(agreement.get_attr_val_utf8_l("nsds5ReplicaUpdateInProgress") != "true"
               for agreement in agreements):
            return
        time.sleep(1)
    raise AssertionError("Replication delivery did not stop within 60 seconds")


def _configure_supplier(inst):
    plugin = MemberOfPlugin(inst)
    plugin.replace_many(("nsslapd-pluginEnabled", "on"),
                        ("nsslapd-pluginType", "betxnpostoperation"),
                        ("memberOfGroupAttr", "member"),
                        ("memberOfAttr", "memberOf"))
    for attr in ("memberOfSkipNested", "memberOfAllBackends", "memberOfEntryScope",
                 "memberOfEntryScopeExcludeSubtree", "memberOfDeferredUpdate"):
        plugin.remove_all(attr)
    ReferentialIntegrityPlugin(inst).disable()
    backend = Backends(inst).get(DEFAULT_SUFFIX)
    for attr in ("member", "memberOf"):
        indexes = backend.get_indexes()
        try:
            indexes.get(attr).ensure_present("nsIndexType", "eq")
        except ldap.NO_SUCH_OBJECT:
            indexes.create(properties={"cn": attr, "nsSystemIndex": "false",
                                       "nsIndexType": "eq"})
    inst.restart()


def _changelog_records(inst):
    """Decode only operation, target DN, and memberOf value count per dbid."""
    dump = inst.dbscan(DEFAULT_BENAME, "replication_changelog", stopping=False)
    records = {}
    for block in re.split(r"(?=^dbid: )", dump.decode("utf-8", errors="replace"),
                          flags=re.MULTILINE):
        dbid = re.search(r"^dbid: (\S+)$", block, flags=re.MULTILINE)
        if dbid is None:
            continue
        operation = re.search(r"^\toperation: (\w+)$", block, flags=re.MULTILINE)
        dn = re.search(r"^\tdn: (.*)$", block, flags=re.MULTILINE)
        values = re.findall(r"^\t\tmemberof: (.*)$", block,
                            flags=re.MULTILINE | re.IGNORECASE)
        records[dbid.group(1)] = {
            "operation": operation.group(1).lower() if operation else None,
            "dn": dn.group(1).lower() if dn else None,
            "memberof_values": len(values),
        }
    assert records, "MDB changelog dbscan yielded no records"
    return records


def _assert_move_state(inst, group_dns):
    assert not UserAccount(inst, OLD_USER_DN).exists()
    user = UserAccount(inst, NEW_USER_DN)
    assert user.exists()
    expected = {dn.lower() for dn in group_dns}
    assert set(user.get_attr_vals_utf8_l("memberOf")) == expected
    for dn in group_dns:
        assert Group(inst, dn).get_attr_vals_utf8_l("member") == [NEW_USER_DN.lower()]


def _cleanup_move(inst, group_dns):
    objects = [OrganizationalUnit(inst, dn)
               for dn in (PEOPLE_DN, DISABLED_DN, GROUPS_DN)]
    objects.extend(UserAccount(inst, dn) for dn in (OLD_USER_DN, NEW_USER_DN))
    objects.extend(Group(inst, dn) for dn in group_dns)
    _cleanup_objects(objects)


def _member_set(user):
    return set(user.get_attr_vals_utf8_l("memberOf"))


def _create_user(users, name, number):
    return users.create(properties={
        "uid": name, "cn": name, "sn": name,
        "uidNumber": str(number), "gidNumber": str(number),
        "homeDirectory": "/nonexistent/" + name,
    })


def _cleanup_objects(objects):
    for obj in reversed(objects):
        try:
            if obj.exists():
                obj.delete()
        except ldap.LDAPError as exc:
            log.warning("Could not clean %s: %s", obj.dn, exc)


def test_modrdn_delivery_memberof_payload(topo_m2):
    """A replicated move rewrites group references without full memberOf payloads.

    :id: 7eb3e866-d7c6-496c-b260-13073e5743ed
    :setup: Two suppliers with transactional MemberOf and 32 flat groups
    :steps:
        1. Configure both suppliers and create indexed memberships
        2. Pause both agreements and move the user with unchanged RDN
        3. Resume delivery and wait for both supplier RUVs
        4. Check LDAP state on both suppliers and MDB delivery changelog records
    :expectedresults:
        1. All memberships converge before the move
        2. The origin has the moved user and updated group references
        3. Both suppliers reach the same RUV
        4. Membership is exact and each new user MODIFY has at most one memberOf value
    """
    instances = [topo_m2.ms["supplier1"], topo_m2.ms["supplier2"]]
    agreements = []
    group_dns = []
    paused = False
    try:
        for inst in instances:
            _configure_supplier(inst)
        agreements = _agreements(instances)
        for agreement in agreements:
            agreement.remove_all("nsDS5ReplicatedAttributeList")
            agreement.remove_all("nsDS5ReplicaStripAttrs")
            agreement.remove_all("nsDS5ReplicatedAttributeListTotal")

        ous = OrganizationalUnits(instances[0], DEFAULT_SUFFIX)
        for name in ("memberof-growth-people", "memberof-growth-disabled",
                     "memberof-growth-groups"):
            ous.create(properties={"ou": name})
        user = UserAccounts(instances[0], PEOPLE_DN, rdn=None).create(properties={
            "uid": "memberof-growth-user", "cn": "MemberOf growth user",
            "sn": "growth-user", "uidNumber": "21031", "gidNumber": "21031",
            "homeDirectory": "/nonexistent/memberof-growth-user",
        })
        groups = Groups(instances[0], GROUPS_DN, rdn=None)
        for number in range(GROUP_COUNT):
            group = groups.create(properties={
                "cn": "memberof-growth-{:03d}".format(number), "member": user.dn,
            })
            group_dns.append(group.dn)
        _wait_ruvs(instances)
        for inst in instances:
            assert UserAccount(inst, OLD_USER_DN).exists()
            assert set(UserAccount(inst, OLD_USER_DN).get_attr_vals_utf8_l("memberOf")) == {
                dn.lower() for dn in group_dns}

        _pause_agreements(agreements)
        paused = True
        _wait_ruvs(instances)
        user.rename("uid=memberof-growth-user", newsuperior=DISABLED_DN,
                    deloldrdn=False)
        _assert_move_state(instances[0], group_dns)
        before = {inst.serverid: _changelog_records(inst) for inst in instances} \
            if get_default_db_lib() == "mdb" else None

        for agreement in agreements:
            agreement.resume()
        paused = False
        _wait_ruvs(instances)
        for inst in instances:
            _assert_move_state(inst, group_dns)

        if before is not None:
            total = 0
            seen = 0
            delivery_records = 0
            for inst in instances:
                after = _changelog_records(inst)
                new_ids = set(after) - set(before[inst.serverid])
                delivery_records += len(new_ids)
                user_modifies = [after[dbid] for dbid in new_ids
                                 if after[dbid]["operation"] == "modify"
                                 and after[dbid]["dn"] in (OLD_USER_DN.lower(),
                                                            NEW_USER_DN.lower())]
                counts = [record["memberof_values"] for record in user_modifies]
                assert all(count <= 1 for count in counts), (
                    "Delivery recorded a user MODIFY with multiple memberOf values: "
                    "max={}, records={}".format(max(counts, default=0), len(counts)))
                total += sum(counts)
                seen += len(counts)
            log.info("Delivery user MODIFYs=%d memberOf values=%d", seen, total)
            assert delivery_records > 0, "No delivery changelog records were decoded"
            assert total <= 4 * GROUP_COUNT, (
                "Delivery recorded {} memberOf values, bound is {}".format(
                    total, 4 * GROUP_COUNT))
    finally:
        if paused:
            for agreement in agreements:
                try:
                    agreement.resume()
                except ldap.LDAPError as exc:
                    log.warning("Could not resume agreement %s: %s", agreement.dn, exc)
        for inst in instances:
            _cleanup_move(inst, group_dns)


def test_memberof_reconciliation_paths(topo_st):
    """Retain nested membership through another path and repair larger changes.

    :id: 93e6902a-8902-4fd6-847c-6d8af2ab3285
    :setup: Standalone server with transactional MemberOf and nested groups
    :steps:
        1. Add direct and overlapping nested memberships
        2. Remove one nested route and then the last nested route
        3. Remove two memberships in one group change and then the last membership
        4. Add and delete a mixed-case equivalent member DN
        5. Change the grouping attribute and add a uniqueMember group
    :expectedresults:
        1. The user has the exact direct and indirect set
        2. The remaining route preserves the parent without a user MODIFY
        3. The full repair and last-route removal preserve the exact set
        4. DN syntax matching yields one membership and then none
        5. The refreshed grouping attribute updates the user
    """
    inst = topo_st.standalone
    plugin = MemberOfPlugin(inst)
    plugin.replace_many(("nsslapd-pluginEnabled", "on"),
                        ("nsslapd-pluginType", "betxnpostoperation"),
                        ("memberOfGroupAttr", "member"),
                        ("memberOfAttr", "memberOf"))
    ReferentialIntegrityPlugin(inst).disable()
    inst.restart()
    objects = []
    try:
        ou = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(
            properties={"ou": "memberof-delta-branches"})
        objects.append(ou)
        users = UserAccounts(inst, ou.dn, rdn=None)
        user = _create_user(users, "memberof-delta-user", 21041)
        dummy = _create_user(users, "memberof-delta-dummy", 21042)
        objects.extend((user, dummy))
        groups = Groups(inst, ou.dn, rdn=None)
        a = groups.create(properties={"cn": "delta-a", "member": dummy.dn})
        b = groups.create(properties={"cn": "delta-b", "member": dummy.dn})
        objects.extend((a, b))

        assert _member_set(user) == set()
        a.add_member(user.dn)
        assert _member_set(user) == {a.dn.lower()}
        b.add_member(user.dn)
        assert _member_set(user) == {a.dn.lower(), b.dn.lower()}
        parent = groups.create(properties={
            "cn": "delta-parent", "member": [a.dn, b.dn, dummy.dn],
        })
        objects.append(parent)
        assert _member_set(user) == {a.dn.lower(), b.dn.lower(), parent.dn.lower()}

        unchanged_timestamp = user.get_attr_val_utf8("modifyTimestamp")
        time.sleep(1.1)
        parent.remove_member(a.dn)
        assert _member_set(user) == {a.dn.lower(), b.dn.lower(), parent.dn.lower()}
        assert user.get_attr_val_utf8("modifyTimestamp") == unchanged_timestamp
        parent.remove_member(b.dn)
        assert _member_set(user) == {a.dn.lower(), b.dn.lower()}

        stable = groups.create(properties={
            "cn": "delta-stable", "member": [user.dn, dummy.dn],
        })
        base = groups.create(properties={
            "cn": "delta-base", "member": [user.dn, dummy.dn],
        })
        branch_parent = groups.create(properties={
            "cn": "delta-branch-parent", "member": [base.dn, dummy.dn],
        })
        objects.extend((stable, base, branch_parent))
        assert _member_set(user) == {a.dn.lower(), b.dn.lower(),
                                     stable.dn.lower(), base.dn.lower(),
                                     branch_parent.dn.lower()}
        # Removing base loses both base and branch_parent, so it needs full repair.
        base.remove_member(user.dn)
        assert _member_set(user) == {a.dn.lower(), b.dn.lower(), stable.dn.lower()}
        b.remove_member(user.dn)
        stable.remove_member(user.dn)
        assert _member_set(user) == {a.dn.lower()}
        a.remove_member(user.dn)
        assert _member_set(user) == set()

        case = groups.create(properties={"cn": "delta-case", "member": dummy.dn})
        objects.append(case)
        case.add_member(user.dn.upper())
        assert _member_set(user) == {case.dn.lower()}
        case.remove_member(user.dn.lower())
        assert _member_set(user) == set()

        plugin.replace("memberOfGroupAttr", "uniqueMember")
        unique = UniqueGroups(inst, ou.dn, rdn=None).create(properties={
            "cn": "delta-unique", "uniqueMember": [user.dn, dummy.dn],
        })
        objects.append(unique)
        assert _member_set(user) == {unique.dn.lower()}
    finally:
        try:
            plugin.replace("memberOfGroupAttr", "member")
        except ldap.LDAPError as exc:
            log.warning("Could not restore MemberOf group attribute: %s", exc)
        _cleanup_objects(objects)


@pytest.mark.parametrize("parent_count", [1, 2])
@pytest.mark.parametrize("filter_attr, filter_template", [
    ("memberOfExcludeSpecificGroupFilter", "(entrydn={})"),
    ("memberOfSpecificGroupFilter", "(!(entrydn={}))"),
])
def test_excluded_nested_group_reconciliation(topo_st, parent_count,
                                            filter_attr, filter_template):
    """Group membership changes leave an excluded child's memberOf untouched.

    :id: 6f9b8302-2a59-4566-b07d-64c7f4d5b123
    :parametrized: yes
    :setup: Standalone server with transactional MemberOf and nested groups
    :steps:
        1. Add a child group to one or two parent groups
        2. Exclude the child using an exclude filter or an include filter
        3. Add the child to another parent and remove it again
        4. Remove the child from an original parent
    :expectedresults:
        1. The child has the exact original parent set in memberOf
        2. The existing memberOf values remain unchanged
        3. Both group changes succeed without changing the child's memberOf
        4. The group change succeeds without changing the child's memberOf
    """
    inst = topo_st.standalone
    plugin = MemberOfPlugin(inst)
    config = (
        ("nsslapd-pluginEnabled", "on"),
        ("nsslapd-pluginType", "betxnpostoperation"),
        ("memberOfGroupAttr", "member"),
        ("memberOfAttr", "memberOf"),
        ("memberOfDeferredUpdate", "off"),
        ("memberOfSpecificGroupOC", "groupOfNames"),
        ("memberOfSpecificGroupFilter", []),
        ("memberOfExcludeSpecificGroupFilter", []),
        ("memberOfEntryScope", []),
        ("memberOfEntryScopeExcludeSubtree", []),
    )
    saved_config = [(attr, plugin.get_attr_vals(attr)) for attr, _ in config]
    objects = []
    try:
        plugin.replace_many(*config)
        inst.restart()
        ou = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(
            properties={"ou": "memberof-excluded-child"})
        objects.append(ou)
        groups = Groups(inst, ou.dn, rdn=None)
        child = groups.create(properties={"cn": "excluded-child"})
        objects.append(child)
        parents = []
        for number in range(parent_count):
            parent = groups.create(properties={
                "cn": "original-parent-{}".format(number), "member": child.dn,
            })
            parents.append(parent)
            objects.append(parent)
        new_parent = groups.create(properties={"cn": "new-parent"})
        objects.append(new_parent)
        expected = {parent.dn.lower() for parent in parents}
        assert _member_set(child) == expected

        plugin.replace(filter_attr, filter_template.format(child.dn))
        inst.restart()
        assert _member_set(child) == expected

        new_parent.add_member(child.dn)
        assert all(parent.is_member(child.dn) for parent in parents + [new_parent])
        assert _member_set(child) == expected
        new_parent.remove_member(child.dn)
        assert not new_parent.is_member(child.dn)
        assert _member_set(child) == expected

        parents[0].remove_member(child.dn)
        assert not parents[0].is_member(child.dn)
        assert _member_set(child) == expected
    finally:
        try:
            _cleanup_objects(objects)
        finally:
            plugin.replace_many(*saved_config)
            inst.restart()


def test_reconciliation_with_missing_include_scope(topo_st):
    """An absent include scope is empty and does not block membership updates.

    :id: 146c1904-083a-490d-8bd8-20edb775ca10
    :setup: Standalone server with transactional MemberOf
    :steps:
        1. Configure an existing and an absent subtree as include scopes
        2. Add an existing in-scope user to an in-scope group
        3. Remove the user from the group
    :expectedresults:
        1. Both include scopes are accepted
        2. The group contains the user and memberOf contains exactly that group
        3. The group no longer contains the user and memberOf is empty
    """
    inst = topo_st.standalone
    plugin = MemberOfPlugin(inst)
    config = (
        ("nsslapd-pluginEnabled", "on"),
        ("nsslapd-pluginType", "betxnpostoperation"),
        ("memberOfGroupAttr", "member"),
        ("memberOfAttr", "memberOf"),
        ("memberOfDeferredUpdate", "off"),
        ("memberOfSpecificGroupFilter", []),
        ("memberOfExcludeSpecificGroupFilter", []),
        ("memberOfEntryScope", []),
        ("memberOfEntryScopeExcludeSubtree", []),
    )
    saved_config = [(attr, plugin.get_attr_vals(attr)) for attr, _ in config]
    objects = []
    try:
        plugin.replace_many(*config)
        inst.restart()
        ou = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(
            properties={"ou": "memberof-existing-scope"})
        objects.append(ou)
        user = _create_user(UserAccounts(inst, ou.dn, rdn=None),
                            "memberof-scoped-user", 21061)
        objects.append(user)
        group = Groups(inst, ou.dn, rdn=None).create(properties={"cn": "scoped-group"})
        objects.append(group)
        missing_dn = "ou=memberof-missing-scope," + DEFAULT_SUFFIX
        assert not OrganizationalUnit(inst, missing_dn).exists()
        plugin.replace("memberOfEntryScope", [ou.dn, missing_dn])

        group.add_member(user.dn)
        assert group.is_member(user.dn)
        assert _member_set(user) == {group.dn.lower()}
        group.remove_member(user.dn)
        assert not group.is_member(user.dn)
        assert _member_set(user) == set()
    finally:
        try:
            plugin.remove_all("memberOfEntryScope")
            _cleanup_objects(objects)
        finally:
            plugin.replace_many(*saved_config)
            inst.restart()


def test_concurrent_distinct_group_adds(topo_st):
    """Two successful group ADDs retain both memberships on one user.

    :id: 566af77b-2bce-4626-bcf7-ac84eb1130a5
    :setup: Standalone server with transactional MemberOf and two groups
    :steps:
        1. Open a separate LDAP connection for each worker
        2. Add the same user to distinct groups concurrently
        3. Check both group references and the user's memberOf set
    :expectedresults:
        1. Both worker connections are independent
        2. At least one pair of operations succeeds within five attempts
        3. Both memberships survive every successful pair
    """
    inst = topo_st.standalone
    plugin = MemberOfPlugin(inst)
    plugin.replace_many(("nsslapd-pluginEnabled", "on"),
                        ("nsslapd-pluginType", "betxnpostoperation"),
                        ("memberOfGroupAttr", "member"),
                        ("memberOfAttr", "memberOf"))
    inst.restart()
    objects = []
    try:
        ou = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(
            properties={"ou": "memberof-concurrent-adds"})
        objects.append(ou)
        users = UserAccounts(inst, ou.dn, rdn=None)
        user = _create_user(users, "memberof-concurrent-user", 21051)
        dummy = _create_user(users, "memberof-concurrent-dummy", 21052)
        objects.extend((user, dummy))
        groups = Groups(inst, ou.dn, rdn=None)
        group1 = groups.create(properties={"cn": "concurrent-one", "member": dummy.dn})
        group2 = groups.create(properties={"cn": "concurrent-two", "member": dummy.dn})
        objects.extend((group1, group2))

        for attempt in range(5):
            barrier = threading.Barrier(2)
            results = [None, None]

            def add_member(index, group_dn):
                conn = None
                try:
                    conn = inst.clone()
                    conn.open()
                    conn.set_option(ldap.OPT_TIMEOUT, 30.0)
                    barrier.wait(timeout=30)
                    Group(conn, group_dn).add_member(user.dn)
                    results[index] = True
                except Exception as exc:
                    results[index] = exc
                finally:
                    if conn is not None:
                        try:
                            conn.close()
                        except Exception:
                            pass

            workers = [threading.Thread(target=add_member, args=(0, group1.dn)),
                       threading.Thread(target=add_member, args=(1, group2.dn))]
            for worker in workers:
                worker.start()
            for worker in workers:
                worker.join(timeout=45)
            assert not any(worker.is_alive() for worker in workers), (
                "Concurrent MemberOf workers did not finish")
            for result in results:
                assert result is True or isinstance(result, (ldap.LDAPError,
                                                             threading.BrokenBarrierError)), (
                    "Unexpected worker failure: {}".format(result))

            if results == [True, True]:
                assert group1.is_member(user.dn)
                assert group2.is_member(user.dn)
                assert _member_set(user) == {group1.dn.lower(), group2.dn.lower()}
                break
            log.info("Concurrent attempt %d had an LDAP conflict; retrying", attempt + 1)
            for group in (group1, group2):
                if group.is_member(user.dn):
                    group.remove_member(user.dn)
            assert _member_set(user) == set()
        else:
            pytest.fail("No concurrent pair succeeded in five attempts")
    finally:
        _cleanup_objects(objects)
