# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


"""
Result correctness for large equality OR filters (issue #6275).  These tests
pin complete result sets across matching-rule normalization, multi-valued
entries, paging, scope, referrals, ACLs, controls, CoS, tombstones, and RUV
sentinels.  They run on builds that may optionally expose
``nsslapd-enable-or-filter-lookup``; when that switch is
present, searches are repeated with it disabled to assert behavioral parity.
"""

import ldap
import logging
import os
import pytest
import time

from contextlib import contextmanager
from ldap.controls import SimplePagedResultsControl
from ldap.controls.simple import ManageDSAITControl, ProxyAuthzControl
from ldap.controls.sss import SSSRequestControl
from ldap.controls.vlv import VLVRequestControl
from ldap.filter import escape_filter_chars
from ldap.schema.models import AttributeType
from lib389._constants import DEFAULT_SUFFIX, REPLICA_RUV_UUID
from lib389.cos import CosPointerDefinition, CosTemplate
from lib389.extensibleobject import UnsafeExtensibleObjects
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.domain import Domain
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccount, UserAccounts
from lib389.schema import OBJECT_MODEL_PARAMS, Schema
from lib389.tombstone import Tombstones
from lib389.utils import ensure_bytes, ensure_str
from test389.topologies import topology_m2, topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

TOTAL_USERS = 400
GROUPS = 20            # sn=OlSn<i % GROUPS>
SMALL_GROUPS = 30      # 3 members each; even-numbered groups store the
                       # member DNs with an UPPERCASE uid (case-cross data)
BIG_GROUPS = 3         # 200 members each, exercising large multivalued
                       # distinguished-name attributes
BIG_GROUP_MEMBERS = 200
PW = 'olpassword'

OR_LOOKUP_ATTR = 'nsslapd-enable-or-filter-lookup'

PEOPLE = f'ou=People,{DEFAULT_SUFFIX}'
GROUPS_OU = f'ou=Groups,{DEFAULT_SUFFIX}'


def user_uid(i):
    return f'oluser{i:05d}'


def user_dn(i, case_mangled=False):
    uid = user_uid(i).upper() if case_mangled else user_uid(i)
    return f'uid={uid},{PEOPLE}'


@pytest.fixture(scope="module")
def create_data(topo):
    """Import users, groups, and a subentry; return the uid list."""
    inst = topo.standalone
    ldif_dir = inst.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'filter_or_lookup.ldif')
    with open(ldif_file, 'w') as f:
        # offline import replaces the backend contents, so the LDIF must
        # carry the suffix root and containers too; the aci restores read
        # access for the non-Directory-Manager binds some tests use
        f.write(f'dn: {DEFAULT_SUFFIX}\n'
                'objectClass: top\n'
                'objectClass: domain\n'
                'dc: example\n'
                'aci: (targetattr="*")(version 3.0; acl "filter test read"; '
                'allow (read, search, compare)(userdn="ldap:///all");)\n\n'
                f'dn: {PEOPLE}\n'
                'objectClass: top\n'
                'objectClass: organizationalUnit\n'
                'ou: People\n\n'
                f'dn: {GROUPS_OU}\n'
                'objectClass: top\n'
                'objectClass: organizationalUnit\n'
                'ou: Groups\n\n')
        for i in range(TOTAL_USERS):
            uid = user_uid(i)
            extra = ''
            if i in (350, 351, 352):
                extra += (f'employeeNumber: olemp{i:05d}\n'
                          f'mail: {uid}@olmail.example.com\n')
            if i in (360, 361):
                extra += 'cn: olduptest\n'
            if i == 370:
                extra += 'cn;lang-en: olsubtypelang\n'
            if i == 371:
                extra += 'cn: OlCase MIXED Value\n'
            f.write(f'dn: uid={uid},{PEOPLE}\n'
                    'objectClass: top\n'
                    'objectClass: person\n'
                    'objectClass: organizationalPerson\n'
                    'objectClass: inetOrgPerson\n'
                    'objectClass: posixAccount\n'
                    f'uid: {uid}\n'
                    f'cn: OL User {i:05d}\n'
                    f'cn: olalt{i:05d}\n'
                    f'sn: OlSn{i % GROUPS}\n'
                    f'uidNumber: {20000 + i}\n'
                    f'gidNumber: {20000 + i}\n'
                    f'homeDirectory: /home/{uid}\n'
                    f'userPassword: {PW}\n'
                    f'{extra}\n')
        # multi-valued uid entry
        f.write(f'dn: uid=olmultia,{PEOPLE}\n'
                'objectClass: top\n'
                'objectClass: person\n'
                'objectClass: organizationalPerson\n'
                'objectClass: inetOrgPerson\n'
                'objectClass: posixAccount\n'
                'uid: olmultia\n'
                'uid: olmultib\n'
                'cn: OL Multi\n'
                'sn: OlMultiSn\n'
                'uidNumber: 29999\n'
                'gidNumber: 29999\n'
                'homeDirectory: /home/olmultia\n'
                f'userPassword: {PW}\n\n')
        # an LDAP subentry: excluded from ordinary search results unless
        # the filter names objectclass=ldapsubentry
        f.write(f'dn: cn=olsubentry,{DEFAULT_SUFFIX}\n'
                'objectClass: top\n'
                'objectClass: ldapsubentry\n'
                'objectClass: extensibleObject\n'
                'cn: olsubentry\n'
                'uid: olsubentryuid\n\n')
        # small groups; even-numbered ones store their member DNs with an
        # UPPERCASE uid RDN value (the referenced users exist lowercase)
        for g in range(SMALL_GROUPS):
            members = ''.join(
                f'member: {user_dn(3 * g + j, case_mangled=(g % 2 == 0))}\n'
                for j in range(3))
            f.write(f'dn: cn=olgroup{g:03d},{GROUPS_OU}\n'
                    'objectClass: top\n'
                    'objectClass: groupOfNames\n'
                    f'cn: olgroup{g:03d}\n'
                    f'{members}\n')
        # big groups exercise large multivalued DN attributes
        for g in range(BIG_GROUPS):
            members = ''.join(f'member: {user_dn(j)}\n'
                              for j in range(BIG_GROUP_MEMBERS))
            f.write(f'dn: cn=olbig{g},{GROUPS_OU}\n'
                    'objectClass: top\n'
                    'objectClass: groupOfNames\n'
                    f'cn: olbig{g}\n'
                    f'{members}\n')
    os.chmod(ldif_file, 0o644)
    inst.stop()
    assert inst.ldif2db('userRoot', None, None, None, ldif_file)
    inst.start()
    return [user_uid(i) for i in range(TOTAL_USERS)]


def or_of(attr, values):
    return '(|%s)' % ''.join(f'({attr}={v})' for v in values)


def escaped_or_of(attr, values):
    """Build an equality OR while preserving filter-special characters."""
    return '(|%s)' % escaped_equalities(attr, values)


def escaped_equalities(attr, values):
    """Build equality components without an enclosing Boolean node."""
    return ''.join(
        f'({attr}={escape_filter_chars(ensure_str(value))})' for value in values)


def ghosts(n, prefix='olghost'):
    """Generate deterministic absent-but-well-formed OR values."""
    return [f'{prefix}{i:05d}' for i in range(n)]


def search_uids(topo, filterstr, base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE):
    entries = topo.standalone.search_s(base, scope, filterstr, ['uid'])
    return sorted(ensure_str(e.getValue('uid')) for e in entries)


def search_dns(conn, filterstr, base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE):
    entries = conn.search_s(base, scope, filterstr, ['1.1'])
    return sorted(e.dn.lower() for e in entries)


def search_dns_result(conn, filterstr, base=DEFAULT_SUFFIX,
                      scope=ldap.SCOPE_SUBTREE, serverctrls=None):
    """Run one asynchronous search and assert its final LDAP result."""
    msgid = conn.search_ext(base, scope, filterstr, ['1.1'],
                            serverctrls=serverctrls)
    rtype, rdata, _, rctrls = conn.result3(msgid)
    assert rtype == ldap.RES_SEARCH_RESULT
    dns = []
    for dn, _ in rdata:
        assert dn is not None
        dns.append(ensure_str(dn).lower())
    return sorted(dns), rctrls


def paged_search_dns(conn, filterstr, page_size, base=DEFAULT_SUFFIX,
                     scope=ldap.SCOPE_SUBTREE):
    """Collect a complete simple-paged search, asserting each result."""
    req = SimplePagedResultsControl(True, size=page_size, cookie='')
    dns = []
    while True:
        page_dns, rctrls = search_dns_result(
            conn, filterstr, base=base, scope=scope, serverctrls=[req])
        dns.extend(page_dns)
        pctrls = [c for c in rctrls
                  if c.controlType == SimplePagedResultsControl.controlType]
        assert len(pctrls) == 1
        if not pctrls[0].cookie:
            break
        req.cookie = pctrls[0].cookie
    return sorted(dns)


def assert_health(conn):
    """Assert a simple base search completes successfully and exactly."""
    dns, _ = search_dns_result(
        conn, '(objectClass=*)', base=DEFAULT_SUFFIX, scope=ldap.SCOPE_BASE)
    assert dns == [DEFAULT_SUFFIX.lower()]


def _instance(target):
    return target.standalone if hasattr(target, 'standalone') else target


@contextmanager
def or_lookup_disabled(target):
    """Disable optional lookup support, or act as a no-op when unavailable."""
    inst = _instance(target)
    original = inst.config.get_attr_val_utf8(OR_LOOKUP_ATTR)
    if original is None:
        yield
        return
    inst.config.set(OR_LOOKUP_ATTR, 'off')
    try:
        yield
    finally:
        inst.config.set(OR_LOOKUP_ATTR, original)


def assert_parity(topo, conn, filterstr, base=DEFAULT_SUFFIX,
                  scope=ldap.SCOPE_SUBTREE, serverctrls=None):
    """Require parity with optional lookup disabled when the switch exists."""
    def run():
        try:
            if serverctrls:
                res = conn.search_ext_s(base, scope, filterstr, ['1.1'],
                                        serverctrls=serverctrls)
            else:
                res = conn.search_s(base, scope, filterstr, ['1.1'])
            # lib389 connections return Entry objects
            return sorted(e.dn.lower() for e in res if e.dn)
        except ldap.LDAPError as e:
            return type(e).__name__

    default_result = run()
    with or_lookup_disabled(topo):
        disabled_result = run()
    assert default_result == disabled_result
    return default_result


SYNTH_ATTRS = (
    {'name': 'olLookupA', 'equality': 'caseIgnoreMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.15'},
    {'name': 'olLookupB', 'equality': 'caseIgnoreMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.15'},
    {'name': 'olLookupC', 'equality': 'caseIgnoreMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.15'},
    {'name': 'olCaseIgnore', 'equality': 'caseIgnoreMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.15'},
    {'name': 'olCaseExact', 'equality': 'caseExactMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.15'},
    {'name': 'olCaseExactIA5', 'equality': 'caseExactIA5Match',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.26'},
    {'name': 'olCaseIgnoreIA5', 'equality': 'caseIgnoreIA5Match',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.26'},
    {'name': 'olInteger', 'equality': 'integerMatch',
     'ordering': 'integerOrderingMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.27'},
    {'name': 'olNumeric', 'equality': 'numericStringMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.36'},
    {'name': 'olTelephone', 'equality': 'telephoneNumberMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.50'},
    {'name': 'olDistinguishedName', 'equality': 'distinguishedNameMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.12'},
    {'name': 'olGeneralizedTime', 'equality': 'generalizedTimeMatch',
     'ordering': 'generalizedTimeOrderingMatch',
     'syntax': '1.3.6.1.4.1.1466.115.121.1.24'},
)

MATCH_RULE_CASES = (
    {'id': 'case-ignore-directory-string', 'attr': 'olCaseIgnore',
     'stored': 'Alpha  Value', 'assertion': ' alpha value '},
    {'id': 'case-exact-directory-string', 'attr': 'olCaseExact',
     'stored': 'Exact  Value', 'assertion': ' Exact Value '},
    {'id': 'case-exact-ia5', 'attr': 'olCaseExactIA5',
     'stored': 'Exact  IA5', 'assertion': ' Exact IA5 '},
    {'id': 'case-ignore-ia5', 'attr': 'olCaseIgnoreIA5',
     'stored': 'MiXeD  IA5', 'assertion': ' mixed ia5 '},
    {'id': 'integer', 'attr': 'olInteger',
     'stored': '42', 'assertion': '00042'},
    {'id': 'numeric-string', 'attr': 'olNumeric',
     'stored': '12 34 5', 'assertion': '12345'},
    {'id': 'telephone-number', 'attr': 'olTelephone',
     'stored': '+1 604-555 0100', 'assertion': '+16045550100'},
    {'id': 'distinguished-name', 'attr': 'olDistinguishedName',
     'stored': f'uid=OLUSER00001,{PEOPLE}',
     'assertion': f'UID=oluser00001, OU=people, {DEFAULT_SUFFIX.upper()}'},
)


def matching_rule_absent_values(case_id, count=14):
    if case_id == 'integer':
        return [str(1000 + i) for i in range(count)]
    if case_id == 'numeric-string':
        return [f'900{i:03d}' for i in range(count)]
    if case_id == 'telephone-number':
        return [f'+1 999 555 {i:04d}' for i in range(count)]
    if case_id == 'distinguished-name':
        return [f'cn=ol-missing-{i:02d},{DEFAULT_SUFFIX}' for i in range(count)]
    return [f'ol-missing-{case_id}-{i:02d}' for i in range(count)]


@pytest.fixture(scope='module')
def lookup_schema_data(topo, create_data):
    """Create synthetic matching-rule attributes and LDAP entries."""
    inst = topo.standalone
    schema = Schema(inst)
    added_attrs = []
    created = []
    container = None
    expected_dns = {}
    try:
        for idx, spec in enumerate(SYNTH_ATTRS, start=1):
            params = OBJECT_MODEL_PARAMS[AttributeType].copy()
            params.update({
                'names': (spec['name'],),
                'oid': f'2.16.840.1.113730.3.8.999.6275.{idx}',
                'desc': 'large equality OR lookup functional test',
                'equality': spec['equality'],
                'ordering': spec.get('ordering'),
                'syntax': spec['syntax'],
                'x_origin': ('large equality OR lookup test',),
            })
            schema.add_attributetype(params)
            added_attrs.append(spec['name'])

        container = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(
            properties={'ou': 'olLookupData'})
        objects = UnsafeExtensibleObjects(inst, container.dn)

        def add_entry(key, properties):
            entry = objects.create(properties={
                'cn': f'ol-{key}',
                **properties,
            })
            expected_dns[key] = f'cn=ol-{key},{container.dn}'.lower()
            created.append(entry)
            return entry

        add_entry('family-a', {'olLookupA': 'a-hit'})
        add_entry('family-b', {'olLookupB': 'b-hit'})
        add_entry('family-both', {'olLookupA': 'a-hit',
                                  'olLookupB': 'b-hit'})
        add_entry('family-c', {'olLookupC': 'c-hit'})
        add_entry('root-simple', {'olLookupC': 'simple-match'})
        add_entry('root-complex', {'olLookupC': 'guard-hit'})

        for case in MATCH_RULE_CASES:
            add_entry(f'match-{case["id"]}', {case['attr']: case['stored']})

        add_entry('unicode-case-ignore', {
            'olCaseIgnore': 'ÇÉLINE ÅNGSTRÖM',
        })
        add_entry('outside-generalized-time', {
            'olGeneralizedTime': '20240101010203Z',
        })
        add_entry('escaped-dn', {
            'olDistinguishedName': f'cn=Smith\\, Alice,{PEOPLE}',
        })

        count_assertions = [
            f'cn=ol-count-{i:02d},{DEFAULT_SUFFIX}' for i in range(16)]
        add_entry('dn-count-less', {
            'olDistinguishedName': [
                count_assertions[0],
                f'cn=ol-less-extra,{DEFAULT_SUFFIX}',
            ],
        })
        add_entry('dn-count-equal', {
            'olDistinguishedName': [count_assertions[0]] + [
                f'cn=ol-equal-extra-{i:02d},{DEFAULT_SUFFIX}' for i in range(15)],
        })
        add_entry('dn-count-more', {
            'olDistinguishedName': [count_assertions[0]] + [
                f'cn=ol-more-extra-{i:02d},{DEFAULT_SUFFIX}' for i in range(16)],
        })

        yield {
            'base': container.dn,
            'dns': expected_dns,
            'all_dns': sorted(expected_dns.values()),
            'dn_count_assertions': count_assertions,
        }
    finally:
        for entry in reversed(created):
            if entry.exists():
                entry.delete()
        if container is not None and container.exists():
            container.delete()
        for attr_name in reversed(added_attrs):
            schema.remove_attributetype(attr_name)


def test_or_big_same_attr_exact(topo, create_data):
    """Verify a large OR of uid equalities (live values, verbatim
    duplicates, and absent values) returns the exact result set.

    :id: 7c1de2a8-40f6-4b91-9c35-d82e61f7a904
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search an OR of 120 live uids, 20 of them repeated, 60 absent
        2. Compare the complete result set
    :expectedresults:
        1. Search succeeds
        2. Exactly the 120 named users are returned, each once
    """
    live = create_data[100:220]
    values = live + live[:20] + ghosts(60)
    assert search_uids(topo, or_of('uid', values)) == sorted(live)


def test_or_duplicate_branches(topo, create_data):
    """Verify an OR dominated by one duplicated component returns the
    exact result set.

    :id: 2f4bb9e0-a913-45c7-8d62-03cf17e8ba56
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search an OR of 300 verbatim copies of (cn=olduptest) plus a
           few live cn values
    :expectedresults:
        1. Exactly the two olduptest users and the named users return
    """
    values = ['olduptest'] * 300 + ['olalt00005', 'olalt00006']
    expected = sorted([user_uid(360), user_uid(361),
                       user_uid(5), user_uid(6)])
    assert search_uids(topo, or_of('cn', values)) == expected


def test_or_case_and_trim(topo, create_data):
    """Verify assertion values differing in case or padded with spaces
    use the attribute matching rule and return the exact expected entries.

    :id: 5a0c47d1-6e28-4f9b-a1d5-92c8e04b7f13
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search a large uid OR whose live values are UPPERCASED
        2. Search a large uid OR with space-padded live values
        3. Search a large cn OR asserting a lowercase form of a stored
           mixed-case value
    :expectedresults:
        1. Exactly the named users are returned
        2. Exactly the named users are returned
        3. Exactly the mixed-case-value user is returned
    """
    live = create_data[10:40]
    upper = [v.upper() for v in live]
    assert search_uids(topo, or_of('uid', upper + ghosts(20))) == sorted(live)
    padded = [f' {v} ' for v in live]
    assert search_uids(topo, or_of('uid', padded + ghosts(20))) == sorted(live)
    values = ['olcase mixed value'] + [f'olalt{i:05d}' for i in range(20, 35)]
    expected = sorted([user_uid(371)] + [user_uid(i) for i in range(20, 35)])
    assert search_uids(topo, or_of('cn', values)) == expected


def test_or_multivalued_entry(topo, create_data):
    """Verify entries match through a non-first attribute value, both on
    a multi-valued cn and on a multi-valued uid

    :id: e93d05c2-71b8-49a4-bd06-4f1a28c9e750
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search a large cn OR naming only second cn values (olaltNNNNN)
        2. Search a large uid OR naming only the second uid value of the
           two-uid entry
    :expectedresults:
        1. Exactly the named users are returned
        2. The two-uid entry is returned
    """
    picked = list(range(200, 230))
    values = [f'olalt{i:05d}' for i in picked]
    assert search_uids(topo, or_of('cn', values)) == sorted(
        user_uid(i) for i in picked)
    result = topo.standalone.search_s(
        DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
        or_of('uid', ['olmultib'] + ghosts(20)), ['cn'])
    assert [e.dn for e in result] == [f'uid=olmultia,{PEOPLE}']


def test_or_integer_attr(topo, create_data):
    """Verify a large OR on an INTEGER-syntax attribute (uidNumber)
    matches through integer normalization, including leading zeros

    :id: b8e61f30-2c95-4da7-8e14-70d3a9c6f582
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search an OR of 40 live uidNumbers, some written with leading
           zeros, plus absent negative values
    :expectedresults:
        1. Exactly the named users are returned
    """
    picked = list(range(120, 160))
    values = []
    for n, i in enumerate(picked):
        values.append(f'000{20000 + i}' if n % 3 == 0 else f'{20000 + i}')
    values += ['-1', '-00042', '99999999']
    assert search_uids(topo, or_of('uidNumber', values)) == sorted(
        user_uid(i) for i in picked)


def test_or_dn_member(topo, create_data):
    """Verify a large OR on a DN-syntax attribute matches across case
    differences in either direction and tolerates an unparseable DN
    assertion among the components

    :id: 91c25e84-d0b7-4f36-a29c-58e1f4d07b63
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search an OR of member values: a lowercase assertion of a
           member stored UPPERCASE, an UPPERCASE assertion of a member
           stored lowercase, one unparseable-DN value, and 16 absent
           well-formed DNs
    :expectedresults:
        1. Exactly the groups holding those two members are returned
    """
    values = ([user_dn(6), user_dn(4, case_mangled=True), 'not a dn at all']
              + [f'uid=olghost{i:03d},{PEOPLE}' for i in range(16)])
    filt = or_of('member', values)
    entries = topo.standalone.search_s(GROUPS_OU, ldap.SCOPE_SUBTREE,
                                       filt, ['cn'])
    got = sorted(ensure_str(e.getValue('cn')) for e in entries)
    # user 6 is in olgroup002 (stored UPPERCASE) and every big group;
    # user 4 is in olgroup001 (stored lowercase) and every big group
    expected = sorted(['olgroup001', 'olgroup002',
                       'olbig0', 'olbig1', 'olbig2'])
    assert got == expected


def test_or_inside_and(topo, create_data):
    """Verify an OR evaluated under an enclosing AND (candidates broader
    than the OR's own matches) returns the exact result set

    :id: 0d7f92c6-3ab1-48e5-bc70-61e94d28a5f7
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search (&(sn=OlSn0)(|...30 uids across all sn groups...))
    :expectedresults:
        1. Exactly the OR's group-0 members are returned
    """
    in_group = [uid for i, uid in enumerate(create_data) if i % GROUPS == 0][:6]
    other = [uid for i, uid in enumerate(create_data) if i % GROUPS == 7][:24]
    filt = f'(&(sn=OlSn0){or_of("uid", in_group + other)})'
    assert search_uids(topo, filt) == sorted(in_group)


def test_or_referral_wrapper(topo, create_data):
    """Verify a large OR on a suffix containing a referral entry (the
    executed filter gains the (|(f)(objectclass=referral)) wrapper)
    returns the exact result set with and without ManageDsaIT

    :id: 4e8a1c59-f723-4b06-9d84-c2571e0af938
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Add a referral entry with ManageDsaIT and restart (the
           referral-presence flag is read at backend start)
        2. Search a large uid OR with ManageDsaIT
        3. Search the same OR without ManageDsaIT, collecting entries
        4. Remove the referral entry and restart
    :expectedresults:
        1. Referral entry is added
        2. Exactly the named users are returned
        3. Exactly the named users are returned among the entries
        4. Cleanup succeeds
    """
    inst = topo.standalone
    ref_dn = f'ou=OlRefOU,{DEFAULT_SUFFIX}'
    live = create_data[40:80]
    filt = or_of('uid', live + ghosts(20))
    dsait = ManageDSAITControl()
    inst.add_ext_s(ref_dn, [
        ('objectClass', [b'top', b'referral', b'extensibleObject']),
        ('ou', [b'OlRefOU']),
        ('ref', [b'ldap://example.invalid/ou=Elsewhere']),
    ], serverctrls=[dsait])
    inst.restart()
    try:
        res = inst.search_ext_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filt,
                                ['uid'], serverctrls=[dsait])
        got = sorted(ensure_str(e.getValue('uid')) for e in res
                     if e.dn and e.getValue('uid'))
        assert got == sorted(live)
        # without ManageDsaIT the referral is returned separately; the
        # entry result set must be identical
        inst.set_option(ldap.OPT_REFERRALS, 0)
        try:
            res = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filt, ['uid'])
            got = sorted(ensure_str(e.getValue('uid')) for e in res
                         if e.dn and e.getValue('uid'))
            assert got == sorted(live)
        finally:
            inst.set_option(ldap.OPT_REFERRALS, 1)
    finally:
        inst.delete_ext_s(ref_dn, serverctrls=[dsait])
        inst.restart()


def test_or_onelevel_nonroot(topo, create_data):
    """Verify a one-level large OR as a regular user returns the exact
    result set: the executed filter carries injected parentid and
    referral components which must not take part in access checking

    :id: a67b30d9-84e2-4c15-bf08-93d7f61c2ae4
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Bind as a regular user
        2. Run a one-level search under ou=People with an OR of 40 live
           uids and 20 absent ones
    :expectedresults:
        1. Bind succeeds
        2. Exactly the named users are returned
    """
    live = create_data[300:340]
    conn = UserAccount(topo.standalone, user_dn(0)).bind(PW)
    try:
        entries = conn.search_s(PEOPLE, ldap.SCOPE_ONELEVEL,
                                or_of('uid', live + ghosts(20)), ['uid'])
        got = sorted(ensure_str(e.getValue('uid')) for e in entries)
        assert got == sorted(live)
    finally:
        conn.unbind_s()


def test_or_paged(topo, create_data):
    """Verify a paged large OR returns the exact union of pages.

    :id: c50e94f7-16d3-4ba8-92c6-e08a5d7b31f9
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Run an OR of 120 live uids as a paged search, page size 30
        2. Repeat the complete paged search with lookup disabled
    :expectedresults:
        1. Every page succeeds and the union is exactly the 120 named DNs
        2. Optional lookup-disabled evaluation returns the same DN set
    """
    inst = topo.standalone
    picked = create_data[150:270]
    filt = or_of('uid', picked)
    expected = sorted(f'uid={uid},{PEOPLE}'.lower() for uid in picked)
    assert paged_search_dns(inst, filt, 30) == expected
    with or_lookup_disabled(inst):
        assert paged_search_dns(inst, filt, 30) == expected


def test_or_after_online_mod(topo, create_data):
    """Verify a value added over LDAP (as opposed to offline import) is
    found by a large OR immediately, through the cached entry

    :id: 6b2df8a1-59c0-4e73-8f1d-a45e90c3d267
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Add a new cn value to a user over LDAP
        2. Search a large cn OR naming only that new value
        3. Repeat with lookup disabled
        4. Remove the value again
    :expectedresults:
        1. Modify succeeds
        2. Exactly that user is returned
        3. Optional lookup-disabled evaluation returns the identical DN
        4. Cleanup succeeds
    """
    user = UserAccount(topo.standalone, user_dn(42))
    user.add('cn', 'olfreshvalue')
    try:
        values = ['olfreshvalue'] + ghosts(20, prefix='olstale')
        filterstr = or_of('cn', values)
        expected = [user.dn.lower()]
        got, _ = search_dns_result(topo.standalone, filterstr)
        assert got == expected
        with or_lookup_disabled(topo.standalone):
            got_off, _ = search_dns_result(topo.standalone, filterstr)
        assert got_off == expected
    finally:
        user.remove('cn', 'olfreshvalue')


def test_or_empty_assertion_value(topo, create_data):
    """Verify an empty assertion value among the components neither
    matches nor disturbs the other components

    :id: d1c8f2b5-07e9-4a64-b3d8-52c96e01a7f4
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search a large uid OR including a (uid=) component
    :expectedresults:
        1. Exactly the named users are returned
    """
    live = create_data[80:100]
    filt = '(|%s(uid=))' % ''.join(f'(uid={v})' for v in live + ghosts(10))
    assert search_uids(topo, filt) == sorted(live)


def test_or_mixed_remainder(topo, create_data):
    """Verify non-equality and other-attribute components mixed into a
    large uid OR still match: presence, substring, extensible, and mail.

    :id: 3fa96d07-b2e4-4c81-a9f5-16d80c73e2b9
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search an OR of 30 live uids plus (employeeNumber=*),
           (mail=oluser00351@olmail.example.com), (cn=ol user 0001*), and
           an extensible dn-syntax component naming no entry
    :expectedresults:
        1. The uid matches, the three employeeNumber owners, the mail
           owner, and the cn=OL User 0001N users are all returned exactly
    """
    live = list(range(0, 30))
    filt = ('(|'
            + ''.join(f'(uid={user_uid(i)})' for i in live)
            + '(employeeNumber=*)'
            + '(mail=oluser00351@olmail.example.com)'
            + '(cn=ol user 0001*)'
            + f'(member:distinguishedNameMatch:=uid=olghost1,{PEOPLE})'
            + ')')
    expected = sorted(set(
        [user_uid(i) for i in live]
        + [user_uid(i) for i in (350, 351, 352)]   # employeeNumber owners
        + [user_uid(351)]                          # mail owner
        + [user_uid(i) for i in range(10, 20)]))   # cn=OL User 0001N
    assert search_uids(topo, filt) == expected


def test_or_subtyped_branch_and_value(topo, create_data):
    """Verify subtype handling on both sides: a subtyped assertion
    component (cn;lang-en=...) is matched, and a value
    stored under a subtyped description (cn;lang-en) is matched by plain
    cn components.

    :id: 84d05b1e-c976-4f28-a1d3-670e29c8f5a1
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search a large cn OR that includes (cn;lang-en=olsubtypelang)
        2. Search a large cn OR naming olsubtypelang as a plain cn value
    :expectedresults:
        1. The lang-en user is returned along with the plain matches
        2. The lang-en user is returned (base-type components match
           subtyped values)
    """
    plain = [f'olalt{i:05d}' for i in range(240, 260)]
    filt = ('(|' + ''.join(f'(cn={v})' for v in plain)
            + '(cn;lang-en=olsubtypelang))')
    expected = sorted([user_uid(i) for i in range(240, 260)] + [user_uid(370)])
    assert search_uids(topo, filt) == expected
    values = plain + ['olsubtypelang']
    assert search_uids(topo, or_of('cn', values)) == expected


def test_or_acl_deny_attr_no_leak(topo, create_data):
    """Verify a user denied read on uid gets no entries from a large uid
    OR: every component is undefined without access and no match may leak
    past the ACL boundary (CVE-2022-1949 class).

    :id: f2b74e09-8ad5-4c31-96e7-d03b58f1c2a6
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Add an aci denying uid read/search to one user and bind as it
        2. Search a large uid OR of live values
        3. Assert parity with optional lookup disabled when available
    :expectedresults:
        1. Bind succeeds
        2. No entries are returned
        3. Optional lookup-disabled evaluation returns the same empty set
    """
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    deny = ('(targetattr="uid")(version 3.0; acl "ol deny uid"; '
            'deny (read, search, compare)'
            f'(userdn="ldap:///{user_dn(1)}");)')
    suffix.add('aci', deny)
    conn = UserAccount(topo.standalone, user_dn(1)).bind(PW)
    try:
        filt = or_of('uid', create_data[100:160])
        entries = conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filt, ['1.1'])
        assert entries == []
        assert assert_parity(topo, conn, filt) == []
    finally:
        conn.unbind_s()
        suffix.remove('aci', deny)


def test_or_acl_multi_hit_denied_parity(topo, create_data):
    """Verify an entry whose two attribute values hit two components of
    the OR while its attribute is denied for the bound user is excluded,
    while every independently named accessible entry is returned.

    :id: 07e3c1f8-65a9-4d02-bc84-91f5e2d70a35
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Deny uid read on the two-uid entry only (targetfilter scoped)
           for the bind user
        2. Search an OR naming both of its uid values plus live values
        3. Repeat with optional lookup disabled when available
    :expectedresults:
        1. ACI is added
        2. Exactly the live users return; the two-uid entry does not
        3. Optional lookup-disabled evaluation returns the same exact set
    """
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    deny = ('(targetattr="uid")(targetfilter="(cn=OL Multi)")'
            '(version 3.0; acl "ol deny multi"; '
            'deny (read, search, compare)'
            f'(userdn="ldap:///{user_dn(2)}");)')
    suffix.add('aci', deny)
    conn = UserAccount(topo.standalone, user_dn(2)).bind(PW)
    try:
        live = create_data[20:40]
        filt = or_of('uid', ['olmultia', 'olmultib'] + live)
        got = assert_parity(topo, conn, filt)
        expected = sorted(f'uid={uid},{PEOPLE}'.lower() for uid in live)
        assert got == expected
    finally:
        conn.unbind_s()
        suffix.remove('aci', deny)


def test_or_acl_mixed_allowed_denied(topo, create_data):
    """Verify ticket 48275 semantics for a large OR: entries matching
    an accessible component of the OR are returned even though another
    component's attribute is denied, and entries matching only the denied
    component are not returned

    :id: 58c1a9d4-3f70-4e26-8b95-d217e60c4af8
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Deny employeeNumber read for a bind user
        2. Search a large uid OR that also names an existing
           employeeNumber value, as that user
        3. Repeat with optional lookup disabled when available
    :expectedresults:
        1. ACI is added
        2. Exactly the uid matches are returned: the employeeNumber
           owner (not named by any uid component) is absent
        3. Optional lookup-disabled evaluation returns the same exact set
    """
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    deny = ('(targetattr="employeeNumber")(version 3.0; acl "ol deny emp"; '
            'deny (read, search, compare)'
            f'(userdn="ldap:///{user_dn(3)}");)')
    suffix.add('aci', deny)
    conn = UserAccount(topo.standalone, user_dn(3)).bind(PW)
    try:
        live = create_data[200:240]
        filt = ('(|' + ''.join(f'(uid={v})' for v in live)
                + '(employeeNumber=olemp00350))')
        got = assert_parity(topo, conn, filt)
        assert f'uid={user_uid(350)},{PEOPLE}'.lower() not in got
        assert sorted(got) == sorted(
            f'uid={v},{PEOPLE}'.lower() for v in live)
    finally:
        conn.unbind_s()
        suffix.remove('aci', deny)


def test_or_acl_all_denied_all_miss(topo, create_data):
    """Verify an all-miss OR over an unindexed attribute denied to the
    bound user returns nothing: the classic walk scores every component
    undefined (no access), the lookup path decides non-match, and both
    are the same empty result at the protocol boundary.

    :id: 16e18b56-ab5c-437e-8673-34750cdc39a0
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Add an aci denying l read/search to one user and bind as it
        2. Search a 16-branch all-miss l OR
        3. Assert parity with optional lookup disabled when available
    :expectedresults:
        1. Bind succeeds
        2. No entries are returned
        3. Optional lookup-disabled evaluation returns the same empty set
    """
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    deny = ('(targetattr="l")(version 3.0; acl "ol deny l"; '
            'deny (read, search, compare)'
            f'(userdn="ldap:///{user_dn(4)}");)')
    suffix.add('aci', deny)
    conn = UserAccount(topo.standalone, user_dn(4)).bind(PW)
    try:
        filt = or_of('l', [f'ol-absent-loc-{i:02d}' for i in range(16)])
        assert assert_parity(topo, conn, filt) == []
    finally:
        conn.unbind_s()
        suffix.remove('aci', deny)


def test_not_of_or_complement(topo, create_data):
    """Verify NOT of a large OR returns the exact complement: an absent
    assertion is defined-false so the NOT can negate it, never undefined.

    :id: 19f7d3c0-b485-4a62-9e17-c30d86f2e5b9
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search (&(objectClass=posixAccount)(!(|...20 live uids...)))
    :expectedresults:
        1. Exactly all posixAccount entries except the 20 are returned
    """
    named = create_data[60:80]
    filt = f'(&(objectClass=posixAccount)(!{or_of("uid", named)}))'
    expected = sorted((set(create_data) - set(named)) | {'olmultia'})
    assert search_uids(topo, filt) == expected


def test_not_of_or_denied_user(topo, create_data):
    """Verify NOT of a large OR evaluates to undefined, not to a match,
    when the bound user has no access to the OR's attribute

    :id: 62a84f1b-90dc-4753-a6e8-1f4c72d09b3e
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Deny uid read for a bind user
        2. Search (&(objectClass=posixAccount)(!(|...20 live uids...)))
           as that user
        3. Repeat with optional lookup disabled when available
    :expectedresults:
        1. ACI is added
        2. No entries are returned (undefined components do not negate
           into matches)
        3. Optional lookup-disabled evaluation returns the same empty set
    """
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    deny = ('(targetattr="uid")(version 3.0; acl "ol deny uid not"; '
            'deny (read, search, compare)'
            f'(userdn="ldap:///{user_dn(4)}");)')
    suffix.add('aci', deny)
    conn = UserAccount(topo.standalone, user_dn(4)).bind(PW)
    try:
        named = create_data[60:80]
        filt = f'(&(objectClass=posixAccount)(!{or_of("uid", named)}))'
        got = assert_parity(topo, conn, filt)
        assert got == []
    finally:
        conn.unbind_s()
        suffix.remove('aci', deny)


def test_or_cos_vattr_fallback(topo, create_data):
    """Verify a CoS-served attribute in a large OR still matches through
    its virtual values while an unrelated large uid OR remains exact.

    :id: 47d92e6a-1b05-4f83-bc29-8e60d1f74c52
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Enable virtual-attribute evaluation
        2. Create a CoS pointer definition and wait until its value is visible
        3. Search a large postalCode OR naming the virtual value
        4. Run a large uid OR
        5. Remove the CoS entries and restore the configuration
    :expectedresults:
        1. Virtual attributes are evaluated
        2. CoS is observable before the large-OR assertion begins
        3. Exactly the People container and all user DNs are returned
        4. Exactly the named uid entries are returned
        5. Cleanup and restoration succeed
    """
    inst = topo.standalone
    ignore_vattrs = inst.config.get_attr_val_utf8(
        'nsslapd-ignore-virtual-attrs')
    template = CosTemplate(inst, f'cn=olcostemplate,{DEFAULT_SUFFIX}')
    definition = CosPointerDefinition(inst, f'cn=olcosdef,{PEOPLE}')
    template_created = False
    definition_created = False

    def postal_values():
        entry = inst.search_s(user_dn(0), ldap.SCOPE_BASE,
                              '(objectClass=*)', ['postalCode'])[0]
        return sorted(ensure_str(value)
                      for value in entry.getValues('postalCode'))

    try:
        inst.config.set('nsslapd-ignore-virtual-attrs', 'off')
        template.create(properties={'cn': 'olcostemplate',
                                    'postalCode': 'olvirtualzip'})
        template_created = True
        definition.create(properties={
            'cn': 'olcosdef',
            'cosTemplateDn': f'cn=olcostemplate,{DEFAULT_SUFFIX}',
            # Use the established pointer-CoS qualifier form.  "default
            # operational" is not a valid combined qualifier and silently
            # leaves this ordinary user attribute unserved on current builds.
            'cosAttribute': 'postalCode default'})
        definition_created = True
        deadline = time.monotonic() + 15
        while postal_values() != ['olvirtualzip']:
            assert time.monotonic() < deadline
            time.sleep(0.25)
        values = ['olvirtualzip'] + ghosts(20, prefix='olzip')
        got = search_dns(inst, or_of('postalCode', values), base=PEOPLE)
        expected = sorted(
            [PEOPLE.lower()]
            + [user_dn(i).lower() for i in range(TOTAL_USERS)]
            + [f'uid=olmultia,{PEOPLE}'.lower()])
        assert got == expected
        live = create_data[0:30]
        assert search_uids(topo, or_of('uid', live)) == sorted(live)
    finally:
        if definition_created:
            definition.delete()
        if template_created:
            template.delete()
        inst.config.set('nsslapd-ignore-virtual-attrs', ignore_vattrs)


def test_or_dn_big_group_m_guard(topo, create_data):
    """Verify groups with more member values than the OR has components
    still match exactly.

    :id: 76e08b52-4dc9-4f17-a3b6-29c58d10e7f4
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search an OR of 20 member DNs that all appear in the big
           groups (200 members each) and in some small groups
    :expectedresults:
        1. Exactly the big groups and the covering small groups return
    """
    values = [user_dn(i) for i in range(20)]
    entries = topo.standalone.search_s(GROUPS_OU, ldap.SCOPE_SUBTREE,
                                       or_of('member', values), ['cn'])
    got = sorted(ensure_str(e.getValue('cn')) for e in entries)
    # users 0..19 live in small groups 0..6 (3 members each) and every big
    expected = sorted([f'olgroup{g:03d}' for g in range(7)]
                      + [f'olbig{g}' for g in range(BIG_GROUPS)])
    assert got == expected


def test_or_ldapsubentry(topo, create_data):
    """Verify a large OR does not return LDAP subentries unless the
    filter names objectclass=ldapsubentry (issue #5170's subentry
    regression class)

    :id: 3b61e8d7-52f0-4c94-b8a3-06d97c2e14f5
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search a large uid OR including the subentry's uid value
        2. Repeat with an (objectclass=ldapsubentry) component added
    :expectedresults:
        1. Only the regular users are returned
        2. The subentry is returned too
    """
    live = create_data[0:20]
    values = live + ['olsubentryuid']
    assert search_uids(topo, or_of('uid', values)) == sorted(live)
    filt = ('(|' + ''.join(f'(uid={v})' for v in values)
            + '(objectclass=ldapsubentry))')
    assert search_uids(topo, filt) == sorted(live + ['olsubentryuid'])


def test_or_unknown_attr_branch(topo, create_data):
    """Verify an attribute type unknown to the schema among the
    components leaves the other components' matches intact (RFC 4511:
    unknown assertions are undefined, not errors)

    :id: 88f04a2c-97d6-4be1-a5c8-3e19d20b6f47
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search a large uid OR including (olnosuchattribute=x)
    :expectedresults:
        1. Exactly the named users are returned
    """
    live = create_data[130:160]
    filt = ('(|' + ''.join(f'(uid={v})' for v in live)
            + '(olnosuchattribute=x))')
    assert search_uids(topo, filt) == sorted(live)


def test_or_small_large_and_optional_toggle(topo, create_data):
    """Verify small and large ORs stay exact with the optional switch.

    :id: 90b5c7e2-64af-4d18-92e0-7c3f5a8d1b06
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Search a 10-component uid OR
        2. Search a 40-component uid OR
        3. Repeat the large OR with optional lookup disabled
    :expectedresults:
        1. The exact 10-DN result is returned
        2. The exact 40-DN result is returned
        3. The exact 40-DN result is unchanged
    """
    small = create_data[0:10]
    big = create_data[310:350]
    assert search_uids(topo, or_of('uid', small)) == sorted(small)
    assert search_uids(topo, or_of('uid', big)) == sorted(big)
    with or_lookup_disabled(topo):
        assert search_uids(topo, or_of('uid', big)) == sorted(big)


def test_or_vlv_sort_parity(topo, create_data):
    """Verify exact large-OR results under server-side sort plus VLV for
    Directory Manager and for a user denied read on the sorted attribute.

    :id: e5a9d013-7fc2-4368-9b04-d61e82c7f0a9
    :setup: Standalone instance with 400 users, groups, and a subentry
    :steps:
        1. Run the OR with SSS(uid)+VLV controls as Directory Manager,
           asserting optional-switch parity
        2. Deny uid read for a bind user and repeat as that user
    :expectedresults:
        1. The first ten live DNs in uid sort order return exactly, with
           optional lookup-disabled parity when available
        2. The denied user receives an exact empty result, with optional
           lookup-disabled parity when available
    """
    inst = topo.standalone
    live = create_data[100:140]
    filt = or_of('uid', live + ghosts(20))
    ctrls = [SSSRequestControl(criticality=True, ordering_rules=['uid']),
             VLVRequestControl(criticality=True, before_count=0,
                               after_count=9, offset=1, content_count=0)]
    expected = sorted(user_dn(i).lower() for i in range(100, 110))
    got = assert_parity(topo, inst, filt, base=DEFAULT_SUFFIX,
                        serverctrls=ctrls)
    assert got == expected
    suffix = Domain(inst, DEFAULT_SUFFIX)
    deny = ('(targetattr="uid")(version 3.0; acl "ol deny uid vlv"; '
            'deny (read, search, compare)'
            f'(userdn="ldap:///{user_dn(5)}");)')
    suffix.add('aci', deny)
    conn = UserAccount(inst, user_dn(5)).bind(PW)
    try:
        denied = assert_parity(topo, conn, filt, base=DEFAULT_SUFFIX,
                               serverctrls=ctrls)
        assert denied == []
    finally:
        conn.unbind_s()
        suffix.remove('aci', deny)


def test_or_15_16_exact_and_optional_toggle(topo, lookup_schema_data):
    """Verify equivalent 15- and 16-branch ORs return exact results.

    :id: e7b88ea2-c876-41db-92c0-69e411a749d1
    :setup: Standalone instance with synthetic case-ignore attributes
    :steps:
        1. Search equivalent 15- and 16-branch OR filters
        2. Repeat the 16-branch operation with optional lookup disabled
    :expectedresults:
        1. Both searches return the same exact DN set
        2. Optional lookup-disabled evaluation returns the same DN set
    """
    inst = topo.standalone
    base = lookup_schema_data['base']
    dns = lookup_schema_data['dns']
    expected = sorted([
        dns['family-a'],
        dns['family-both'],
    ])
    values15 = ['a-hit'] + [f'a-threshold-miss-{i:02d}' for i in range(14)]
    values16 = values15 + ['a-threshold-miss-14']
    filter15 = escaped_or_of('olLookupA', values15)
    filter16 = escaped_or_of('olLookupA', values16)
    got15, _ = search_dns_result(
        inst, filter15, base=base, scope=ldap.SCOPE_ONELEVEL)
    assert got15 == expected
    got16, _ = search_dns_result(
        inst, filter16, base=base, scope=ldap.SCOPE_ONELEVEL)
    assert got16 == expected
    with or_lookup_disabled(inst):
        got_off, _ = search_dns_result(
            inst, filter16, base=base, scope=ldap.SCOPE_ONELEVEL)
    assert got_off == expected


def test_or_two_large_families_order_independent(topo, lookup_schema_data):
    """Return exact results for two large families in either source order.

    :id: 150c2e44-a436-4a0a-b3ed-b60e7ddbf3a2
    :setup: Standalone instance with three synthetic case-ignore attributes
    :steps:
        1. Search one OR containing 16 A and 64 B equality branches
        2. Repeat with the A and B source runs reversed
    :expectedresults:
        1. Exact A-or-B DNs return
        2. Reversing source order leaves the result unchanged
    """
    inst = topo.standalone
    base = lookup_schema_data['base']
    dns = lookup_schema_data['dns']
    expected = sorted([
        dns['family-a'], dns['family-b'], dns['family-both'],
    ])
    a_values = ['a-hit'] + [f'a-dominant-miss-{i:02d}' for i in range(15)]
    b_values = ['b-hit'] + [f'b-dominant-miss-{i:02d}' for i in range(63)]
    a_run = escaped_equalities('olLookupA', a_values)
    b_run = escaped_equalities('olLookupB', b_values)

    for filterstr in (f'(|{a_run}{b_run})', f'(|{b_run}{a_run})'):
        got, _ = search_dns_result(
            inst, filterstr, base=base, scope=ldap.SCOPE_ONELEVEL)
        assert got == expected


def test_or_third_family_after_distractors(topo, lookup_schema_data):
    """Return a large third attribute family after two distractors.

    :id: 35f3cd38-59c7-41b6-b02d-5e925c844e97
    :setup: Standalone instance with three synthetic case-ignore attributes
    :steps:
        1. Search an OR with one absent A, one absent B, and 64 C branches
    :expectedresults:
        1. Exactly the live C entry is returned
    """
    inst = topo.standalone
    base = lookup_schema_data['base']
    dns = lookup_schema_data['dns']
    c_values = ['c-hit'] + [f'c-third-miss-{i:02d}' for i in range(63)]
    filterstr = ('(|(olLookupA=a-distractor)(olLookupB=b-distractor)'
                 f'{escaped_equalities("olLookupC", c_values)})')
    got, _ = search_dns_result(
        inst, filterstr, base=base, scope=ldap.SCOPE_ONELEVEL)
    assert got == [dns['family-c']]


def test_or_two_large_nodes_in_and(topo, lookup_schema_data):
    """Intersect two large OR nodes in one AND.

    :id: b6c49e82-060a-4eef-a128-c6a50d537b6f
    :setup: Standalone instance with intersecting synthetic A and B values
    :steps:
        1. Search one 16-branch A OR
        2. Search an AND of separate 16-branch A and B OR nodes
        3. Repeat with optional lookup disabled
    :expectedresults:
        1. Exactly the A entries return
        2. Exactly the A-and-B entry returns
        3. Optional lookup-disabled evaluation returns the identical DN set
    """
    inst = topo.standalone
    base = lookup_schema_data['base']
    dns = lookup_schema_data['dns']
    a_values = ['a-hit'] + [f'a-two-node-miss-{i:02d}' for i in range(15)]
    b_values = ['b-hit'] + [f'b-two-node-miss-{i:02d}' for i in range(15)]
    filterstr = f'(&{escaped_or_of("olLookupA", a_values)}' \
                f'{escaped_or_of("olLookupB", b_values)})'
    expected = [dns['family-both']]

    baseline_got, _ = search_dns_result(
        inst, escaped_or_of('olLookupA', a_values), base=base,
        scope=ldap.SCOPE_ONELEVEL)
    assert baseline_got == sorted([dns['family-a'], dns['family-both']])
    got, _ = search_dns_result(
        inst, filterstr, base=base, scope=ldap.SCOPE_ONELEVEL)
    assert got == expected
    with or_lookup_disabled(inst):
        got_off, _ = search_dns_result(
            inst, filterstr, base=base, scope=ldap.SCOPE_ONELEVEL)
    assert got_off == expected


@pytest.mark.parametrize('case', MATCH_RULE_CASES,
                         ids=[case['id'] for case in MATCH_RULE_CASES])
def test_or_matching_rule_families(topo, lookup_schema_data, case):
    """Exercise equality matching-rule families with large ORs.

    :id: 38d8e03c-4f36-48d5-9211-cc33cda08d8a
    :parametrized: yes
    :setup: Standalone instance with one synthetic attribute per allowlisted rule
    :steps:
        1. Search 16 assertions containing a duplicate normalized live value
           and 14 valid absent values
        2. Repeat with optional lookup disabled
    :expectedresults:
        1. Exactly the independently named matching entry is returned
        2. Optional lookup-disabled evaluation returns the identical DN set
    """
    inst = topo.standalone
    values = ([case['assertion'], case['assertion']]
              + matching_rule_absent_values(case['id']))
    filterstr = escaped_or_of(case['attr'], values)
    expected = [lookup_schema_data['dns'][f'match-{case["id"]}']]

    got, _ = search_dns_result(
        inst, filterstr, base=lookup_schema_data['base'],
        scope=ldap.SCOPE_ONELEVEL)
    assert got == expected
    with or_lookup_disabled(inst):
        got_off, _ = search_dns_result(
            inst, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
    assert got_off == expected


def test_or_non_ascii_case_ignore(topo, lookup_schema_data):
    """Normalize non-ASCII DirectoryString values in a large OR.

    :id: 41273979-fcf8-445d-a0ab-04206bb01677
    :setup: Standalone instance with a synthetic caseIgnoreMatch attribute
    :steps:
        1. Search 16 branches using a lower-case accented live assertion
        2. Repeat with optional lookup disabled
    :expectedresults:
        1. Exactly the upper-case accented stored value matches
        2. Optional lookup-disabled evaluation returns the identical DN set
    """
    inst = topo.standalone
    assertion = 'çéline ångström'
    values = [assertion, assertion] + [f'ø-absent-{i:02d}' for i in range(14)]
    filterstr = escaped_or_of('olCaseIgnore', values)
    expected = [lookup_schema_data['dns']['unicode-case-ignore']]

    got, _ = search_dns_result(
        inst, filterstr, base=lookup_schema_data['base'],
        scope=ldap.SCOPE_ONELEVEL)
    assert got == expected
    with or_lookup_disabled(inst):
        got_off, _ = search_dns_result(
            inst, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
    assert got_off == expected


def test_or_dn_escaped_comma(topo, lookup_schema_data):
    """Normalize a DN assertion whose RDN value contains an escaped comma.

    :id: 66c4dc6a-595c-4624-941b-17463bd41b91
    :setup: Standalone instance with a synthetic distinguishedNameMatch attribute
    :steps:
        1. Search 16 DN branches including a case-mangled escaped-comma DN
        2. Repeat with optional lookup disabled
    :expectedresults:
        1. Exactly the entry storing the equivalent DN is returned
        2. Optional lookup-disabled evaluation returns the identical DN set
    """
    inst = topo.standalone
    assertion = f'CN=SMITH\\, ALICE, OU=PEOPLE, {DEFAULT_SUFFIX.upper()}'
    values = [assertion, assertion] + [
        f'cn=ol-dn-absent-{i:02d},{DEFAULT_SUFFIX}' for i in range(14)]
    filterstr = escaped_or_of('olDistinguishedName', values)
    expected = [lookup_schema_data['dns']['escaped-dn']]

    got, _ = search_dns_result(
        inst, filterstr, base=lookup_schema_data['base'],
        scope=ldap.SCOPE_ONELEVEL)
    assert got == expected
    with or_lookup_disabled(inst):
        got_off, _ = search_dns_result(
            inst, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
    assert got_off == expected


def test_or_dn_value_count_boundaries(topo, lookup_schema_data):
    """Cover DN entries containing 2, 16, and 17 attribute values.

    :id: e480c416-53af-46bd-9650-117ba02ac17f
    :setup: Three entries with 2, 16, and 17 DN values respectively
    :steps:
        1. Search a 16-assertion DN equality family matching all three entries
        2. Repeat with optional lookup disabled
    :expectedresults:
        1. The complete three-DN set returns
        2. Optional lookup-disabled evaluation returns the identical DN set
    """
    inst = topo.standalone
    filterstr = escaped_or_of(
        'olDistinguishedName', lookup_schema_data['dn_count_assertions'])
    expected = sorted(
        lookup_schema_data['dns'][key]
        for key in ('dn-count-less', 'dn-count-equal', 'dn-count-more'))

    got, _ = search_dns_result(
        inst, filterstr, base=lookup_schema_data['base'],
        scope=ldap.SCOPE_ONELEVEL)
    assert got == expected
    with or_lookup_disabled(inst):
        got_off, _ = search_dns_result(
            inst, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
    assert got_off == expected


def test_or_generalized_time_matching_rule(topo, lookup_schema_data):
    """Evaluate a large OR using generalizedTimeMatch.

    :id: bfe96ed0-f0b5-4920-8908-829d80fd337e
    :setup: Standalone instance with a generalizedTimeMatch synthetic attribute
    :steps:
        1. Search 16 generalized-time equality branches
        2. Repeat with optional lookup disabled
    :expectedresults:
        1. Exactly the live generalized-time entry is returned
        2. Optional lookup-disabled evaluation returns the same DN
    """
    inst = topo.standalone
    values = ['20240101010203Z', '20240101010203Z'] + [
        f'202402{i + 1:02d}010203Z' for i in range(14)]
    filterstr = escaped_or_of('olGeneralizedTime', values)
    expected = [lookup_schema_data['dns']['outside-generalized-time']]

    got, _ = search_dns_result(
        inst, filterstr, base=lookup_schema_data['base'],
        scope=ldap.SCOPE_ONELEVEL)
    assert got == expected
    with or_lookup_disabled(inst):
        got_off, _ = search_dns_result(
            inst, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
    assert got_off == expected


def test_or_true_root_all_miss(topo, lookup_schema_data):
    """Verify the unproxied Directory Manager all-miss result and health.

    :id: 29289b81-5f4b-44ac-8e54-ccbe047116bd
    :setup: Standalone instance and an independent Directory Manager bind
    :steps:
        1. Search a 16-branch family whose assertions are all absent
        2. Repeat with optional lookup disabled
        3. Run a base-object health search
    :expectedresults:
        1. LDAP success and an exact empty result
        2. LDAP success and the same exact empty result
        3. The suffix entry is returned exactly
    """
    inst = topo.standalone
    conn = DirectoryManager(inst).bind()
    filterstr = escaped_or_of(
        'olLookupA', [f'ol-root-miss-{i:02d}' for i in range(16)])
    try:
        got, _ = search_dns_result(
            conn, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
        assert got == []
        with or_lookup_disabled(inst):
            got_off, _ = search_dns_result(
                conn, filterstr, base=lookup_schema_data['base'],
                scope=ldap.SCOPE_ONELEVEL)
        assert got_off == []
        assert_health(conn)
    finally:
        conn.unbind_s()


def test_or_true_root_nohit_simple_fallback(topo, lookup_schema_data):
    """Evaluate a simple outer-OR fallback after a root all-miss family.

    :id: 7304fc93-a9f3-4406-926b-2400c90fa6eb
    :setup: Standalone instance and an independent Directory Manager bind
    :steps:
        1. OR a 16-branch all-miss family with one matching equality
        2. Repeat with optional lookup disabled
    :expectedresults:
        1. Exactly the independently named fallback entry is returned
        2. Optional lookup-disabled evaluation returns the identical DN set
    """
    inst = topo.standalone
    conn = DirectoryManager(inst).bind()
    large_miss = escaped_or_of(
        'olLookupA', [f'ol-simple-miss-{i:02d}' for i in range(16)])
    filterstr = f'(|{large_miss}(olLookupC=simple-match))'
    expected = [lookup_schema_data['dns']['root-simple']]
    try:
        got, _ = search_dns_result(
            conn, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
        assert got == expected
        with or_lookup_disabled(inst):
            got_off, _ = search_dns_result(
                conn, filterstr, base=lookup_schema_data['base'],
                scope=ldap.SCOPE_ONELEVEL)
        assert got_off == expected
    finally:
        conn.unbind_s()


def test_or_true_root_nohit_complex_fallback(topo, lookup_schema_data):
    """Evaluate a guarded fallback after a root all-miss family.

    :id: 287c52a2-0a19-4075-bdc7-8b6494eff9a2
    :setup: Standalone instance and an independent Directory Manager bind
    :steps:
        1. OR a 16-branch all-miss family with an AND containing two NOTs
        2. Repeat with optional lookup disabled
    :expectedresults:
        1. Exactly the guarded fallback entry is returned
        2. Optional lookup-disabled evaluation returns the identical DN set
    """
    inst = topo.standalone
    conn = DirectoryManager(inst).bind()
    large_miss = escaped_or_of(
        'olLookupA', [f'ol-complex-miss-{i:02d}' for i in range(16)])
    filterstr = (f'(|{large_miss}'
                 '(&(olLookupC=guard-hit)(!(description=*))'
                 '(!(employeeNumber=*))))')
    expected = [lookup_schema_data['dns']['root-complex']]
    try:
        got, _ = search_dns_result(
            conn, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
        assert got == expected
        with or_lookup_disabled(inst):
            got_off, _ = search_dns_result(
                conn, filterstr, base=lookup_schema_data['base'],
                scope=ldap.SCOPE_ONELEVEL)
        assert got_off == expected
    finally:
        conn.unbind_s()


def test_or_true_root_nohit_under_not(topo, lookup_schema_data):
    """Negate a root all-miss family without changing semantics.

    :id: 6153594a-c923-48c6-a49f-18de69bf8c9a
    :setup: Standalone instance and an independent Directory Manager bind
    :steps:
        1. Search the NOT of a 16-branch all-miss equality OR
        2. Repeat with optional lookup disabled
    :expectedresults:
        1. Every and only generated one-level entry is returned
        2. Optional lookup-disabled evaluation returns the same complete set
    """
    inst = topo.standalone
    conn = DirectoryManager(inst).bind()
    large_miss = escaped_or_of(
        'olLookupA', [f'ol-not-miss-{i:02d}' for i in range(16)])
    filterstr = f'(!{large_miss})'
    expected = lookup_schema_data['all_dns']
    try:
        got, _ = search_dns_result(
            conn, filterstr, base=lookup_schema_data['base'],
            scope=ldap.SCOPE_ONELEVEL)
        assert got == expected
        with or_lookup_disabled(inst):
            got_off, _ = search_dns_result(
                conn, filterstr, base=lookup_schema_data['base'],
                scope=ldap.SCOPE_ONELEVEL)
        assert got_off == expected
    finally:
        conn.unbind_s()


def test_or_root_proxy_preserves_acl_semantics(topo, create_data):
    """Apply proxied-user ACL semantics to a Directory Manager search.

    :id: 563682d4-e96d-44c3-a59f-8611fc41c229
    :setup: Standalone instance, Directory Manager bind, and uid-read deny ACI
    :steps:
        1. Search a large uid OR plus an allowed cn fallback as plain root
        2. Repeat as root with a proxy authorization control for the denied user
        3. Repeat the proxied search with optional lookup disabled
    :expectedresults:
        1. All named uid entries and the cn fallback return exactly
        2. Only the cn fallback visible to the proxied identity returns
        3. Optional lookup-disabled evaluation returns the same restricted set
    """
    inst = topo.standalone
    suffix = Domain(inst, DEFAULT_SUFFIX)
    deny = ('(targetattr="uid")(version 3.0; acl "ol proxy deny uid"; '
            'deny (read, search, compare)'
            f'(userdn="ldap:///{user_dn(0)}");)')
    conn = None
    aci_added = False
    try:
        suffix.add('aci', deny)
        aci_added = True
        live = create_data[100:120]
        filterstr = f'(|{escaped_or_of("uid", live)}(cn=olalt00350))'
        unrestricted = sorted(
            [f'uid={uid},{PEOPLE}'.lower() for uid in live]
            + [user_dn(350).lower()])
        restricted = [user_dn(350).lower()]
        proxy_ctrl = ProxyAuthzControl(
            criticality=True, authzId=ensure_bytes(f'dn: {user_dn(0)}'))
        conn = DirectoryManager(inst).bind()
        direct, _ = search_dns_result(conn, filterstr)
        assert direct == unrestricted
        proxied, _ = search_dns_result(
            conn, filterstr, serverctrls=[proxy_ctrl])
        assert proxied == restricted
        with or_lookup_disabled(inst):
            proxied_off, _ = search_dns_result(
                conn, filterstr, serverctrls=[proxy_ctrl])
        assert proxied_off == restricted
    finally:
        if conn is not None:
            conn.unbind_s()
        if aci_added:
            suffix.remove('aci', deny)


def test_or_paged_cancel_cookie_health(topo, create_data):
    """Cancel a paged large-OR search with its returned cookie.

    :id: 2a860edd-e46b-40ca-84cb-b6d5d4313d03
    :setup: Standalone instance and an independent Directory Manager bind
    :steps:
        1. Verify the complete nonpaged DN set for a 60-value uid OR
        2. Read the first ten-entry page and retain its cookie
        3. Send page size zero with that cookie
        4. Run a base-object health search on the same connection
    :expectedresults:
        1. Exactly the 60 independently named entries return
        2. Ten unique expected DNs and a nonempty cookie return
        3. LDAP success, no entries, and an empty response cookie return
        4. The suffix entry is returned exactly
    """
    inst = topo.standalone
    conn = DirectoryManager(inst).bind()
    live = create_data[120:180]
    filterstr = escaped_or_of('uid', live)
    expected = sorted(f'uid={uid},{PEOPLE}'.lower() for uid in live)
    req = SimplePagedResultsControl(True, size=10, cookie='')
    try:
        complete, _ = search_dns_result(conn, filterstr)
        assert complete == expected
        first_page, rctrls = search_dns_result(
            conn, filterstr, serverctrls=[req])
        assert len(first_page) == 10
        assert len(set(first_page)) == 10
        assert set(first_page) < set(expected)
        pctrls = [
            c for c in rctrls
            if c.controlType == SimplePagedResultsControl.controlType
        ]
        assert len(pctrls) == 1
        assert pctrls[0].cookie

        req.size = 0
        req.cookie = pctrls[0].cookie
        cancelled, rctrls = search_dns_result(
            conn, filterstr, serverctrls=[req])
        assert cancelled == []
        pctrls = [
            c for c in rctrls
            if c.controlType == SimplePagedResultsControl.controlType
        ]
        assert len(pctrls) == 1
        assert not pctrls[0].cookie
        assert_health(conn)
    finally:
        conn.unbind_s()


def test_or_paged_disconnect_reconnect_health(topo, create_data):
    """Disconnect after a first page and reconnect cleanly.

    :id: 2cb0b299-a553-4e98-843e-574880417ea8
    :setup: Standalone instance and independent Directory Manager binds
    :steps:
        1. Verify the complete nonpaged DN set for a 60-value uid OR
        2. Read one page with a nonempty cookie and disconnect
        3. Reconnect, run a base health search, and page the search to completion
    :expectedresults:
        1. Exactly the 60 independently named entries return
        2. Ten unique expected DNs and a nonempty cookie return
        3. Health succeeds and complete paging returns the exact 60-DN set
    """
    inst = topo.standalone
    live = create_data[180:240]
    filterstr = escaped_or_of('uid', live)
    expected = sorted(f'uid={uid},{PEOPLE}'.lower() for uid in live)
    first_conn = DirectoryManager(inst).bind()
    req = SimplePagedResultsControl(True, size=10, cookie='')
    try:
        complete, _ = search_dns_result(first_conn, filterstr)
        assert complete == expected
        first_page, rctrls = search_dns_result(
            first_conn, filterstr, serverctrls=[req])
        assert len(first_page) == 10
        assert len(set(first_page)) == 10
        assert set(first_page) < set(expected)
        pctrls = [
            c for c in rctrls
            if c.controlType == SimplePagedResultsControl.controlType
        ]
        assert len(pctrls) == 1
        assert pctrls[0].cookie
    finally:
        first_conn.unbind_s()

    second_conn = DirectoryManager(inst).bind()
    try:
        assert_health(second_conn)
        assert paged_search_dns(second_conn, filterstr, 10) == expected
    finally:
        second_conn.unbind_s()


def test_or_tombstone_and_ruv_sentinel_semantics(topology_m2):
    """Preserve exact live-entry results with tombstone and RUV branches.

    :id: 5d6d47f7-b512-423a-837c-d3610a2d56ca
    :setup: Standard two-supplier topology with live users and one tombstone
    :steps:
        1. Search an ordinary 20-value uid OR
        2. Add a guaranteed-miss nsTombstone branch and search
        3. Add a guaranteed-miss RUV-uniqueid branch and search
        4. Repeat both sentinel filters with optional lookup disabled
    :expectedresults:
        1. Exactly the generated live DNs return
        2. The same exact live DNs return and no tombstone leaks
        3. The same exact live DNs return and no RUV entry leaks
        4. Optional lookup-disabled evaluation returns the same exact sets
    """
    inst = topology_m2.ms['supplier1']
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    tombstones = Tombstones(inst, DEFAULT_SUFFIX)
    live_users = []

    def properties(uid, number):
        return {
            'uid': uid,
            'cn': uid,
            'sn': 'ol lookup replicated sentinel',
            'uidNumber': str(number),
            'gidNumber': '627500',
            'homeDirectory': f'/home/{uid}',
        }

    try:
        for i in range(20):
            uid = f'ol-lookup-repl-{i:02d}'
            live_users.append(users.create(
                properties=properties(uid, 627500 + i)))
        tomb_uid = 'ol-lookup-repl-tombstone'
        tomb_user = users.create(properties=properties(tomb_uid, 627599))
        tomb_user.delete()
        created_tombstones = tombstones.filter(
            f'(uid={escape_filter_chars(tomb_uid)})')
        assert len(created_tombstones) == 1

        large_or = escaped_or_of(
            'uid', [f'ol-lookup-repl-{i:02d}' for i in range(20)])
        tombstone_filter = (
            f'(|{large_or}'
            '(&(objectClass=nsTombstone)(uid=ol-special-guaranteed-miss)))')
        ruv_filter = (
            f'(|{large_or}'
            f'(&(nsuniqueid={REPLICA_RUV_UUID})'
            '(uid=ol-special-guaranteed-miss)))')
        expected = sorted(
            f'uid=ol-lookup-repl-{i:02d},{PEOPLE}'.lower()
            for i in range(20))

        ordinary, _ = search_dns_result(inst, large_or)
        assert ordinary == expected

        for filterstr in (tombstone_filter, ruv_filter):
            got, _ = search_dns_result(inst, filterstr)
            assert got == expected

        with or_lookup_disabled(inst):
            for filterstr in (tombstone_filter, ruv_filter):
                got_off, _ = search_dns_result(inst, filterstr)
                assert got_off == expected
    finally:
        for user in reversed(live_users):
            if user.exists():
                user.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
