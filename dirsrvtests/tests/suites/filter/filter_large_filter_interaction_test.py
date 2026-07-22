# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Functional interactions between large equality ORs and costly reads.

4,200 entries share the substring and approximate keys, an indexed AND
selects exactly 612 of them, and a 355-branch DN equality family appears
in every filter.
"""

import ldap
import logging
import os
import pytest

from ldap.controls import SimplePagedResultsControl
from lib389.utils import ensure_str
from test389.topologies import topology_st as topo

from .filter_large_filter_support import (
    APPROX_ATTR,
    EXCLUDED_ONE,
    EXCLUDED_TWO,
    FALLBACK_ATTR,
    GUARD_ATTR,
    GUARD_VALUE,
    HIT_POSITIONS,
    OUTER_A,
    OUTER_B,
    OUTER_VALUE,
    SUBSTRING_ATTR,
    TEST_BASE,
    combined_assertions,
    combined_filter,
    dn_or,
    hit_assertions,
    interaction_data,
    lookup_disabled,
    miss_assertions,
    search_dns,
)


pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_large_nested_dn_or_true_root_all_miss(topo, interaction_data):
    """A true-root all-miss OR keeps the complex fallback exact.

    :id: 143f667f-75f9-44e7-a835-29cf1ea4a250
    :setup: Standalone instance with synthetic schema and 4,200 entries
    :steps:
        1. Search an indexed outer AND containing a 355-branch missing DN OR,
           a missing equality, and a guarded two-NOT fallback
        2. Repeat with the optional equality lookup disabled when supported
        3. Run a base-object health search
    :expectedresults:
        1. LDAP succeeds with no DNs
        2. LDAP succeeds with the identical empty result
        3. LDAP succeeds and returns exactly the test container
    """
    inst = topo.standalone
    large_miss = dn_or(miss_assertions())
    filterstr = (
        f"(&({OUTER_A}={OUTER_VALUE})({OUTER_B}={OUTER_VALUE})"
        f"(|{large_miss}({FALLBACK_ATTR}=absent)"
        f"(&({GUARD_ATTR}={GUARD_VALUE})"
        f"(!({EXCLUDED_ONE}=*))(!({EXCLUDED_TWO}=*)))))"
    )

    result_type, dns = search_dns(inst, filterstr)
    assert result_type == ldap.RES_SEARCH_RESULT
    assert dns == []

    with lookup_disabled(inst):
        disabled_type, disabled_dns = search_dns(inst, filterstr)
    assert disabled_type == ldap.RES_SEARCH_RESULT
    assert disabled_dns == []

    health_type, health_dns = search_dns(
        inst, "(objectClass=*)", base=TEST_BASE,
        scope=ldap.SCOPE_BASE
    )
    assert health_type == ldap.RES_SEARCH_RESULT
    assert health_dns == [TEST_BASE.lower()]


@pytest.mark.parametrize(
    "position", HIT_POSITIONS,
    ids=["early", "middle", "late"],
)
def test_large_dn_or_early_middle_late_hits(topo, interaction_data, position):
    """The same normalized DN hit is exact at every table-walk position.

    :id: d563ea60-aacd-4949-a0ef-b2f29fd994aa
    :parametrized: yes
    :setup: Standalone instance with a 612-entry selected cohort
    :steps:
        1. Put the live DN assertion at the beginning, middle, or end of a
           355-branch family and search under the indexed outer AND
        2. Repeat with the optional equality lookup disabled when supported
    :expectedresults:
        1. LDAP succeeds with exactly the independently recorded 204 DNs
        2. The disabled path returns the same exact DNs
    """
    inst = topo.standalone
    expected = interaction_data["hit_dns"][position]
    filterstr = (f"(&({OUTER_A}={OUTER_VALUE})"
                 f"({OUTER_B}={OUTER_VALUE})"
                 f"{dn_or(hit_assertions(position))})")

    result_type, dns = search_dns(inst, filterstr)
    assert result_type == ldap.RES_SEARCH_RESULT
    assert dns == expected

    with lookup_disabled(inst):
        disabled_type, disabled_dns = search_dns(inst, filterstr)
    assert disabled_type == ldap.RES_SEARCH_RESULT
    assert disabled_dns == expected


@pytest.mark.parametrize(
    "cost_component",
    [
        f"({SUBSTRING_ATTR}=*xanadu*)",
        f"({APPROX_ATTR}~=Xanadu)",
    ],
    ids=["substring", "approximate"],
)
def test_large_dn_or_with_costly_component(
        topo, interaction_data, cost_component):
    """A large DN OR combines exactly with a broad costly component.

    :id: ad85665d-9c66-4aa1-a52d-438eb3672692
    :parametrized: yes
    :setup: 4,200 broad postings, a 612-entry equality bound, and a 355 DN OR
    :steps:
        1. Search the combined filter
        2. Repeat with the optional equality lookup disabled when supported
    :expectedresults:
        1. LDAP succeeds with exactly 612 DNs
        2. LDAP succeeds with the same DNs
    """
    inst = topo.standalone
    expected = interaction_data["selected_dns"]
    filterstr = combined_filter(cost_component)

    result_type, dns = search_dns(inst, filterstr)
    assert result_type == ldap.RES_SEARCH_RESULT
    assert dns == expected

    with lookup_disabled(inst):
        disabled_type, disabled_dns = search_dns(inst, filterstr)
    assert disabled_type == ldap.RES_SEARCH_RESULT
    assert disabled_dns == expected


def test_root_or_with_costly_and_stays_exact(
        topo, interaction_data):
    """A root OR containing a costly AND returns the exact union.

    :id: 4cf11d97-4a79-42e7-b188-363646b5af12
    :setup: Standalone instance with the reusable interaction dataset
    :steps:
        1. Put the 355-branch DN family beside a substring-bearing AND under
           a root OR and run the search
        2. Repeat with the optional equality lookup disabled when supported
    :expectedresults:
        1. LDAP succeeds with exactly 612 DNs
        2. LDAP succeeds with the identical exact DN set
    """
    inst = topo.standalone
    expected = interaction_data["selected_dns"]
    filterstr = (
        f"(|{dn_or(combined_assertions())}"
        f"(&({OUTER_A}={OUTER_VALUE})({OUTER_B}={OUTER_VALUE})"
        f"({SUBSTRING_ATTR}=*xanadu*)))"
    )

    result_type, dns = search_dns(inst, filterstr)
    assert result_type == ldap.RES_SEARCH_RESULT
    assert dns == expected

    with lookup_disabled(inst):
        disabled_type, disabled_dns = search_dns(inst, filterstr)
    assert disabled_type == ldap.RES_SEARCH_RESULT
    assert disabled_dns == expected


def test_complete_paging_large_dn_or_with_substring(topo, interaction_data):
    """Complete paging preserves the combined filter's exact DN set.

    :id: 9e47b6aa-8954-452f-90cd-b7c3a694aa67
    :setup: Standalone instance with the reusable interaction dataset
    :steps:
        1. Page through the combined large-DN-OR and substring filter
           with a page size of 97 until the server returns an empty cookie
    :expectedresults:
        1. Every page succeeds and the union of all pages is exactly the
           independently recorded 612 DNs
    """
    inst = topo.standalone
    expected = interaction_data["selected_dns"]
    filterstr = combined_filter(f"({SUBSTRING_ATTR}=*xanadu*)")
    request = SimplePagedResultsControl(True, size=97, cookie="")
    collected = []
    pages = 0

    while True:
        msgid = inst.search_ext(
            TEST_BASE, ldap.SCOPE_SUBTREE, filterstr, ["1.1"],
            serverctrls=[request]
        )
        result_type, result_data, _, response_controls = inst.result3(msgid)
        assert result_type == ldap.RES_SEARCH_RESULT
        collected.extend(
            ensure_str(dn).lower() for dn, _ in result_data if dn
        )
        pages += 1

        paged_controls = [
            control for control in response_controls
            if control.controlType == SimplePagedResultsControl.controlType
        ]
        assert paged_controls
        if not paged_controls[0].cookie:
            break
        request.cookie = paged_controls[0].cookie

    assert pages >= 7
    assert sorted(collected) == expected


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
