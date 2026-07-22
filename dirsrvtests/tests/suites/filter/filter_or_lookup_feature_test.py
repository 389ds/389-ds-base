# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


"""Strict feature contracts for the large equality-OR lookup evaluator.

These tests require the lookup configuration attribute and its debug
summary, and fail on a server without the feature.  Result-only coverage
lives in ``filter_or_lookup_test.py``.
"""

import os
import re
from contextlib import contextmanager

import ldap
import pytest
from ldap.filter import escape_filter_chars
from ldap.schema.models import AttributeType

from lib389._constants import DEFAULT_SUFFIX
from lib389.extensibleobject import UnsafeExtensibleObjects
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.schema import OBJECT_MODEL_PARAMS, Schema
from lib389.utils import ensure_str
from test389.topologies import topology_st as topo


pytestmark = pytest.mark.tier1

OR_LOOKUP_ATTR = 'nsslapd-enable-or-filter-lookup'
ERRORLOG_LEVEL_BACKLDBM = '524288'
ERRORLOG_LEVEL_FILTER = '32'
ENG_LOG_PATTERN = '.*OR filter equality lookup engaged.*'
ENG_LOG_RE = re.compile(
    r'OR filter equality lookup engaged: (\d+) node\(s\), largest (\d+) branches')
AVA_LOG_RE = re.compile(
    r'^\[[^\r\n]+\]\s+-\s+DEBUG\s+-\s+test_ava_filter\s+-\s+'
    r'=>\s*AVA:\s*(?P<attr>[^~<>=\s]+)=(?P<value>[^\r\n]*)\r?$',
    re.MULTILINE | re.IGNORECASE)

FEATURE_ATTRS = ('olFeatureLookupA', 'olFeatureLookupB', 'olFeatureLookupC')
FEATURE_TIME_ATTR = 'olFeatureLookupTime'


def escaped_equalities(attr, values):
    """Build equality components without an enclosing Boolean node."""
    return ''.join(
        f'({attr}={escape_filter_chars(ensure_str(value))})' for value in values)


def escaped_or_of(attr, values):
    """Build an equality OR while preserving filter-special characters."""
    return f'(|{escaped_equalities(attr, values)})'


def search_dns_result(conn, filterstr, base=DEFAULT_SUFFIX,
                      scope=ldap.SCOPE_SUBTREE):
    """Run an asynchronous search and require an LDAP search result."""
    msgid = conn.search_ext(base, scope, filterstr, ['1.1'])
    rtype, rdata, _, _ = conn.result3(msgid)
    assert rtype == ldap.RES_SEARCH_RESULT
    return sorted(ensure_str(dn).lower() for dn, _ in rdata if dn is not None)


def eng_summaries(inst):
    """Return all stable lookup-summary ``(nodes, largest)`` pairs."""
    summaries = []
    for line in inst.ds_error_log.match(ENG_LOG_PATTERN):
        match = ENG_LOG_RE.search(line)
        assert match is not None
        summaries.append((int(match.group(1)), int(match.group(2))))
    return summaries


@contextmanager
def error_log_window(inst):
    """Capture bytes appended to one stable active error log."""
    path = inst.ds_paths.error_log
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_CLOEXEC', 0))
    window = {'text': None}
    try:
        initial = os.fstat(fd)
        identity = (initial.st_dev, initial.st_ino)
        begin = initial.st_size
        guard_at = max(0, begin - 4096)
        guard = os.pread(fd, begin - guard_at, guard_at)

        yield window

        active = os.stat(path)
        assert (active.st_dev, active.st_ino) == identity, \
            'error log rotated during strict operation window'
        assert active.st_size >= begin, \
            'error log was truncated during strict operation window'
        assert os.pread(fd, begin - guard_at, guard_at) == guard, \
            'error log prefix changed during strict operation window'

        end = active.st_size
        chunks = []
        offset = begin
        while offset < end:
            chunk = os.pread(fd, end - offset, offset)
            assert chunk, 'error log shrank while reading strict operation window'
            chunks.append(chunk)
            offset += len(chunk)

        final_fd = os.fstat(fd)
        final_active = os.stat(path)
        assert (final_fd.st_dev, final_fd.st_ino) == identity
        assert (final_active.st_dev, final_active.st_ino) == identity
        assert final_fd.st_size >= end and final_active.st_size >= end, \
            'error log shrank while closing strict operation window'
        assert os.pread(fd, begin - guard_at, guard_at) == guard, \
            'error log prefix changed while reading strict operation window'
        window['text'] = b''.join(chunks).decode('utf-8', errors='replace')
    finally:
        os.close(fd)


def window_summaries(text):
    """Parse lookup summaries from one strict operation log window."""
    return [(int(match.group(1)), int(match.group(2)))
            for match in ENG_LOG_RE.finditer(text)]


def window_avas(text):
    """Parse equality evaluations from one strict operation log window."""
    return [(match.group('attr').lower(), match.group('value'))
            for match in AVA_LOG_RE.finditer(text)]


@contextmanager
def backend_debug_log(inst):
    """Enable lookup summaries and filter AVA diagnostics."""
    level = inst.config.get_attr_val_utf8('nsslapd-errorlog-level')
    debug_level = (int(level or '0') | int(ERRORLOG_LEVEL_BACKLDBM) |
                   int(ERRORLOG_LEVEL_FILTER))
    inst.config.set('nsslapd-errorlog-level', str(debug_level))
    try:
        yield
    finally:
        if level is None:
            inst.config.remove_all('nsslapd-errorlog-level')
        else:
            inst.config.set('nsslapd-errorlog-level', level)


@contextmanager
def lookup_disabled(inst):
    """Disable the required lookup feature and restore its original value."""
    original = inst.config.get_attr_val_utf8(OR_LOOKUP_ATTR)
    assert original is not None
    try:
        inst.config.set(OR_LOOKUP_ATTR, 'off')
        assert inst.config.get_attr_val_utf8(OR_LOOKUP_ATTR) == 'off'
        yield
    finally:
        if inst.config.get_attr_val_utf8(OR_LOOKUP_ATTR) != original:
            inst.config.set(OR_LOOKUP_ATTR, original)


@pytest.fixture(scope='module')
def lookup_feature_data(topo):
    """Create the minimal schema and entries needed by feature contracts."""
    inst = topo.standalone
    schema = Schema(inst)
    added_attrs = []
    created = []
    container = None
    dns = {}
    try:
        for index, name in enumerate(FEATURE_ATTRS, start=101):
            params = OBJECT_MODEL_PARAMS[AttributeType].copy()
            params.update({
                'names': (name,),
                'oid': f'2.16.840.1.113730.3.8.999.6275.{index}',
                'desc': 'large equality OR lookup feature contract',
                'equality': 'caseIgnoreMatch',
                'syntax': '1.3.6.1.4.1.1466.115.121.1.15',
                'x_origin': ('large equality OR lookup feature test',),
            })
            schema.add_attributetype(params)
            added_attrs.append(name)

        params = OBJECT_MODEL_PARAMS[AttributeType].copy()
        params.update({
            'names': (FEATURE_TIME_ATTR,),
            'oid': '2.16.840.1.113730.3.8.999.6275.104',
            'desc': 'unsupported large equality OR lookup family',
            'equality': 'generalizedTimeMatch',
            'syntax': '1.3.6.1.4.1.1466.115.121.1.24',
            'x_origin': ('large equality OR lookup feature test',),
        })
        schema.add_attributetype(params)
        added_attrs.append(FEATURE_TIME_ATTR)

        container = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(
            properties={'ou': 'olLookupFeatureData'})
        objects = UnsafeExtensibleObjects(inst, container.dn)

        def add_entry(key, properties):
            entry = objects.create(properties={
                'cn': f'ol-feature-{key}',
                **properties,
            })
            created.append(entry)
            dns[key] = entry.dn.lower()

        add_entry('family-a', {'olFeatureLookupA': 'a-hit'})
        add_entry('family-b', {'olFeatureLookupB': 'b-hit'})
        add_entry('family-both', {
            'olFeatureLookupA': 'a-hit',
            'olFeatureLookupB': 'b-hit',
        })
        add_entry('family-c', {'olFeatureLookupC': 'c-hit'})
        add_entry('tie-probe', {
            'olFeatureLookupA': 'a-tie-hit',
            'olFeatureLookupB': 'b-tie-hit',
        })
        add_entry('fallback-probe', {
            'olFeatureLookupA': 'a-fallback-hit',
            FEATURE_TIME_ATTR: '20260101000000Z',
        })

        yield {
            'base': container.dn,
            'dns': dns,
        }
    finally:
        for entry in reversed(created):
            if entry.exists():
                entry.delete()
        if container is not None and container.exists():
            container.delete()
        for name in reversed(added_attrs):
            schema.remove_attributetype(name)


def test_or_lookup_config_threshold_and_runtime_toggle(topo,
                                                       lookup_feature_data):
    """Require the default-on switch, 15/16 threshold, and live toggling.

    :id: d89d4195-ae58-4928-84a6-dbec589baeb1
    :setup: Standalone instance with synthetic case-ignore attributes
    :steps:
        1. Search equivalent 15- and 16-branch equality OR filters
        2. Check the live default and the threshold summaries
        3. Disable lookup and repeat the 16-branch search
        4. Restore lookup and repeat without restarting
    :expectedresults:
        1. Both filters return the same exact DN set
        2. The feature is on and only 16 branches emit a largest-16 summary
        3. Results remain exact and no summary is emitted while disabled
        4. Results remain exact and the 16-branch summary returns immediately
    """
    inst = topo.standalone
    base = lookup_feature_data['base']
    dns = lookup_feature_data['dns']
    expected = sorted([dns['family-a'], dns['family-both']])
    values15 = ['a-hit'] + [f'a-threshold-miss-{i:02d}' for i in range(14)]
    values16 = values15 + ['a-threshold-miss-14']
    filter15 = escaped_or_of('olFeatureLookupA', values15)
    filter16 = escaped_or_of('olFeatureLookupA', values16)
    original = inst.config.get_attr_val_utf8(OR_LOOKUP_ATTR)

    try:
        with backend_debug_log(inst):
            before15 = len(eng_summaries(inst))
            assert search_dns_result(
                inst, filter15, base=base,
                scope=ldap.SCOPE_ONELEVEL) == expected
            after15 = len(eng_summaries(inst))

            before16 = after15
            assert search_dns_result(
                inst, filter16, base=base,
                scope=ldap.SCOPE_ONELEVEL) == expected
            summaries = eng_summaries(inst)[before16:]

            assert original == 'on'
            assert after15 == before15
            assert summaries and all(largest == 16
                                     for _, largest in summaries)

            with lookup_disabled(inst):
                before = len(eng_summaries(inst))
                assert search_dns_result(
                    inst, filter16, base=base,
                    scope=ldap.SCOPE_ONELEVEL) == expected
                assert len(eng_summaries(inst)) == before

            assert inst.config.get_attr_val_utf8(OR_LOOKUP_ATTR) == 'on'
            before = len(eng_summaries(inst))
            assert search_dns_result(
                inst, filter16, base=base,
                scope=ldap.SCOPE_ONELEVEL) == expected
            summaries = eng_summaries(inst)[before:]
            assert summaries and all(largest == 16
                                     for _, largest in summaries)
    finally:
        if (original is not None and
                inst.config.get_attr_val_utf8(OR_LOOKUP_ATTR) != original):
            inst.config.set(OR_LOOKUP_ATTR, original)


def test_or_lookup_dominant_family_order_independent(topo,
                                                     lookup_feature_data):
    """Require selection of the 64-branch family in either source order.

    :id: 8e2110e6-c1f1-435c-8e5f-7f3922dd106d
    :setup: Standalone instance with three synthetic case-ignore attributes
    :steps:
        1. Search an OR containing 16 A branches followed by 64 B branches
        2. Search the same runs in the reverse source order
    :expectedresults:
        1. The exact A-or-B DN set returns and the summary reports largest 64
        2. The exact result and selected family size are unchanged
    """
    inst = topo.standalone
    base = lookup_feature_data['base']
    dns = lookup_feature_data['dns']
    expected = sorted([dns['family-a'], dns['family-b'], dns['family-both']])
    a_values = ['a-hit'] + [f'a-dominant-miss-{i:02d}' for i in range(15)]
    b_values = ['b-hit'] + [f'b-dominant-miss-{i:02d}' for i in range(63)]
    a_run = escaped_equalities('olFeatureLookupA', a_values)
    b_run = escaped_equalities('olFeatureLookupB', b_values)

    with backend_debug_log(inst):
        for filterstr in (f'(|{a_run}{b_run})', f'(|{b_run}{a_run})'):
            with error_log_window(inst) as window:
                assert search_dns_result(
                    inst, filterstr, base=base,
                    scope=ldap.SCOPE_ONELEVEL) == expected
            summaries = window_summaries(window['text'])
            assert len(summaries) == 1
            assert summaries[0][0] > 0 and summaries[0][1] == 64


def test_or_lookup_equal_family_tie_uses_first_source(topo,
                                                       lookup_feature_data):
    """Require first source occurrence as the equal-size family tie-breaker.

    :id: 468d35ed-03be-4794-bf92-0da6cdaf10e0
    :setup: A base object that matches one branch in the A and B families
    :steps:
        1. Search equal 16-branch A and B families with A first
        2. Repeat the equivalent filter with B first
        3. Inspect only each search's appended error-log bytes
    :expectedresults:
        1. The exact base DN returns and only the A sentinel is evaluated
        2. The exact base DN returns and only the B sentinel is evaluated
        3. Each search reports one selected family of 16 usable branches
    """
    inst = topo.standalone
    probe_dn = lookup_feature_data['dns']['tie-probe']
    a_values = ['a-tie-hit'] + [f'a-tie-miss-{i:02d}' for i in range(15)]
    b_values = ['b-tie-hit'] + [f'b-tie-miss-{i:02d}' for i in range(15)]
    a_run = escaped_equalities('olFeatureLookupA', a_values)
    b_run = escaped_equalities('olFeatureLookupB', b_values)

    with backend_debug_log(inst):
        for filterstr, expected_ava in (
                (f'(|{a_run}{b_run})', ('olfeaturelookupa', 'a-tie-hit')),
                (f'(|{b_run}{a_run})', ('olfeaturelookupb', 'b-tie-hit'))):
            with error_log_window(inst) as window:
                assert search_dns_result(
                    inst, filterstr, base=probe_dn,
                    scope=ldap.SCOPE_BASE) == [probe_dn]

            summaries = window_summaries(window['text'])
            assert len(summaries) == 1
            assert summaries[0][0] > 0 and summaries[0][1] == 16
            avas = window_avas(window['text'])
            assert avas and all(ava == expected_ava for ava in avas)


def test_or_lookup_larger_unsupported_family_falls_back(topo,
                                                         lookup_feature_data):
    """Select a supported family over a larger unsupported family.

    :id: 6e01be9a-f869-45c6-b7d5-cf11f72decdb
    :setup: A base object matching generalizedTime and supported A families
    :steps:
        1. Put 64 generalizedTime branches before 16 supported A branches
        2. Search the dedicated base object
        3. Inspect only the search's appended error-log bytes
    :expectedresults:
        1. The larger family is plausible but unsupported for byte lookup
        2. The exact base DN returns through the selected A family
        3. The summary reports 16 and only the A sentinel is evaluated
    """
    inst = topo.standalone
    probe_dn = lookup_feature_data['dns']['fallback-probe']
    time_values = ['20260101000000Z'] + [
        f'202602{1 + i // 24:02d}{i % 24:02d}0000Z' for i in range(63)
    ]
    a_values = ['a-fallback-hit'] + [
        f'a-fallback-miss-{i:02d}' for i in range(15)
    ]
    filterstr = f'(|{escaped_equalities(FEATURE_TIME_ATTR, time_values)}' \
                f'{escaped_equalities("olFeatureLookupA", a_values)})'

    with backend_debug_log(inst):
        with error_log_window(inst) as window:
            assert search_dns_result(
                inst, filterstr, base=probe_dn,
                scope=ldap.SCOPE_BASE) == [probe_dn]

    summaries = window_summaries(window['text'])
    assert len(summaries) == 1
    assert summaries[0][0] > 0 and summaries[0][1] == 16
    avas = window_avas(window['text'])
    expected_ava = ('olfeaturelookupa', 'a-fallback-hit')
    assert avas and all(ava == expected_ava for ava in avas)


def test_or_lookup_third_family_after_distractors(topo,
                                                  lookup_feature_data):
    """Require discovery of a 64-branch third family after distractors.

    :id: 9c9bde22-983b-4a17-af10-8cc9461939da
    :setup: Standalone instance with three synthetic case-ignore attributes
    :steps:
        1. Search an OR with one absent A, one absent B, and 64 C branches
        2. Inspect the operation summary
    :expectedresults:
        1. Exactly the live C entry returns
        2. The summary reports a largest family of 64 branches
    """
    inst = topo.standalone
    base = lookup_feature_data['base']
    dns = lookup_feature_data['dns']
    c_values = ['c-hit'] + [f'c-third-miss-{i:02d}' for i in range(63)]
    filterstr = ('(|(olFeatureLookupA=a-distractor)'
                 '(olFeatureLookupB=b-distractor)'
                 f'{escaped_equalities("olFeatureLookupC", c_values)})')

    with backend_debug_log(inst):
        before = len(eng_summaries(inst))
        assert search_dns_result(
            inst, filterstr, base=base,
            scope=ldap.SCOPE_ONELEVEL) == [dns['family-c']]
        summaries = eng_summaries(inst)[before:]
        assert summaries and all(largest == 64 for _, largest in summaries)


def test_or_lookup_true_root_all_miss(topo, lookup_feature_data):
    """Require the all-miss fast decision for an unproxied root search.

    The classic walk emits one ``test_ava_filter`` AVA line per branch per
    entry.  An engaged search with zero family AVA lines therefore proves
    the all-miss entries were decided by the table, not the walk.

    :id: 3fd00a80-65fd-4945-b5f9-dc146ac1b3ec
    :setup: Standalone instance and an independent Directory Manager bind
    :steps:
        1. Search a 16-branch all-miss family as Directory Manager
        2. Repeat with lookup disabled
        3. Run a base-object health search on the same connection
    :expectedresults:
        1. LDAP success, an exact empty result, a largest-16 summary, and no
           family AVA evaluations in the operation window
        2. LDAP success, the same empty result, no new summary, and the
           classic walk's family AVA evaluations reappear
        3. The suffix entry returns exactly
    """
    inst = topo.standalone
    conn = DirectoryManager(inst).bind()
    filterstr = escaped_or_of(
        'olFeatureLookupA', [f'ol-root-miss-{i:02d}' for i in range(16)])
    try:
        with backend_debug_log(inst):
            before = len(eng_summaries(inst))
            with error_log_window(inst) as window:
                assert search_dns_result(
                    conn, filterstr, base=lookup_feature_data['base'],
                    scope=ldap.SCOPE_ONELEVEL) == []
            summaries = eng_summaries(inst)[before:]
            assert summaries and all(largest == 16
                                     for _, largest in summaries)
            assert not [attr for attr, _ in window_avas(window['text'])
                        if attr == 'olfeaturelookupa']

            with lookup_disabled(inst):
                before = len(eng_summaries(inst))
                with error_log_window(inst) as window:
                    assert search_dns_result(
                        conn, filterstr, base=lookup_feature_data['base'],
                        scope=ldap.SCOPE_ONELEVEL) == []
                assert len(eng_summaries(inst)) == before
                assert [attr for attr, _ in window_avas(window['text'])
                        if attr == 'olfeaturelookupa']

        assert search_dns_result(
            conn, '(objectClass=*)', base=DEFAULT_SUFFIX,
            scope=ldap.SCOPE_BASE) == [DEFAULT_SUFFIX.lower()]
    finally:
        conn.unbind_s()


def test_or_lookup_all_miss_under_not_falls_back(topo, lookup_feature_data):
    """Require the classic walk for an all-miss family under NOT.

    NOT can tell false and undefined apart, so under it the no-winner
    shortcut must not decide and the classic walk runs.

    :id: b991ad4b-13f2-4886-9204-f8e22a7e94ce
    :setup: Standalone instance with synthetic case-ignore attributes
    :steps:
        1. Search the negation of a 16-branch all-miss family
        2. Inspect the operation's engagement summary and AVA evaluations
    :expectedresults:
        1. Exactly the container's entries return (the complement)
        2. The table is built, and the classic walk's family AVA
           evaluations are present
    """
    inst = topo.standalone
    base = lookup_feature_data['base']
    dns = lookup_feature_data['dns']
    filterstr = '(!{})'.format(escaped_or_of(
        'olFeatureLookupA', [f'ol-not-miss-{i:02d}' for i in range(16)]))
    expected = sorted(dns.values())

    with backend_debug_log(inst):
        before = len(eng_summaries(inst))
        with error_log_window(inst) as window:
            assert search_dns_result(
                inst, filterstr, base=base,
                scope=ldap.SCOPE_ONELEVEL) == expected
        summaries = eng_summaries(inst)[before:]
        assert summaries and all(largest == 16 for _, largest in summaries)
        assert [attr for attr, _ in window_avas(window['text'])
                if attr == 'olfeaturelookupa']


def test_or_lookup_all_miss_rest_component_matches(topo, lookup_feature_data):
    """Require the rest walk to decide matches after a no-winner pass.

    After all table probes miss, the non-family branches are still
    evaluated; a match there returns the entry without walking the family.

    :id: 4d7fb9c3-ddce-40e3-9b81-beed18f2cd6f
    :setup: Standalone instance with synthetic case-ignore attributes
    :steps:
        1. Search a 16-branch all-miss family OR one live non-family
           equality
        2. Inspect the operation's AVA evaluations
        3. Repeat with lookup disabled
    :expectedresults:
        1. Exactly the non-family matches return
        2. The rest component was evaluated per entry while the family
           branches were not
        3. The same exact result returns
    """
    inst = topo.standalone
    base = lookup_feature_data['base']
    dns = lookup_feature_data['dns']
    filterstr = '(|{}{})'.format(
        escaped_equalities(
            'olFeatureLookupA', [f'ol-rest-miss-{i:02d}' for i in range(16)]),
        '(olFeatureLookupB=b-hit)')
    expected = sorted([dns['family-b'], dns['family-both']])

    with backend_debug_log(inst):
        before = len(eng_summaries(inst))
        with error_log_window(inst) as window:
            assert search_dns_result(
                inst, filterstr, base=base,
                scope=ldap.SCOPE_ONELEVEL) == expected
        summaries = eng_summaries(inst)[before:]
        assert summaries and all(largest == 16 for _, largest in summaries)
        avas = window_avas(window['text'])
        assert [attr for attr, _ in avas if attr == 'olfeaturelookupb']
        assert not [attr for attr, _ in avas if attr == 'olfeaturelookupa']

    with lookup_disabled(inst):
        assert search_dns_result(
            inst, filterstr, base=base,
            scope=ldap.SCOPE_ONELEVEL) == expected


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(['-s', CURRENT_FILE])
