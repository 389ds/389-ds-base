# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest

from lib389.pwpolicy import (
    POLICY_TYPE_SUBTREE,
    POLICY_TYPE_USER,
    _classify_policy_from_pwdpolicysubentry,
    _parse_policy_target_from_cn,
)


class _FakePwEntry(object):
    def __init__(self, cn):
        self._cn = cn

    def get_attr_val_utf8(self, attr):
        if attr == 'cn':
            return self._cn
        return None


def test_parse_policy_target_user():
    cn = r'cn=nsPwPolicyEntry_user,uid\3Dmark,ou\3Dpeople,dc\3Dexample,dc\3Dcom'
    assert _parse_policy_target_from_cn(cn) == 'uid=mark,ou=people,dc=example,dc=com'


def test_parse_policy_target_subtree():
    cn = r'cn=nsPwPolicyEntry_subtree,ou\3Dpeople,dc\3Dexample,dc\3Dcom'
    assert _parse_policy_target_from_cn(cn) == 'ou=people,dc=example,dc=com'


def test_parse_policy_target_invalid():
    assert _parse_policy_target_from_cn('cn=not-a-policy,dc=example,dc=com') is None


def test_classify_user_policy():
    cn = r'cn=nsPwPolicyEntry_user,uid\3Dmark,ou\3Dpeople,dc\3Dexample,dc\3Dcom'
    entry = _FakePwEntry(cn)
    pwp_dn = 'cn=pwp,cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
    policy_type, source, target = _classify_policy_from_pwdpolicysubentry(pwp_dn, entry)
    assert policy_type == POLICY_TYPE_USER
    assert source == pwp_dn
    assert target == 'uid=mark,ou=people,dc=example,dc=com'


def test_classify_subtree_policy():
    cn = r'cn=nsPwPolicyEntry_subtree,ou\3Dpeople,dc\3Dexample,dc\3Dcom'
    entry = _FakePwEntry(cn)
    pwp_dn = 'cn=pwp,cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
    policy_type, source, target = _classify_policy_from_pwdpolicysubentry(pwp_dn, entry)
    assert policy_type == POLICY_TYPE_SUBTREE
    assert target == 'ou=people,dc=example,dc=com'


def test_syntax_inheritance_merge():
    from lib389.pwpolicy import PwPolicyManager, _attr_is_on, _should_show_inherited_value

    assert _attr_is_on('on') is True
    assert _attr_is_on('off') is False
    assert _should_show_inherited_value('16') is True
    assert _should_show_inherited_value('0') is False
    assert _should_show_inherited_value('off') is False

    class _FakeInst(object):
        log = __import__('logging').getLogger('fake')

    mgr = PwPolicyManager(_FakeInst())
    global_attrs = {
        'nsslapd-pwpolicy-inherit-global': ['on'],
        'passwordchecksyntax': ['on'],
        'passwordminlength': ['16'],
        'passwordmindigits': ['0'],
    }
    policy_attrs = {'passwordhistory': ['on']}
    effective = mgr._build_local_effective_attrs(policy_attrs, global_attrs)
    assert set(effective.keys()) == {'passwordhistory', 'passwordminlength'}
    assert effective['passwordminlength']['values'] == ['16']
    assert effective['passwordminlength']['inherited'] is True
    assert effective['passwordhistory']['values'] == ['on']
    assert effective['passwordhistory']['inherited'] is False
    assert 'passwordchange' not in effective


def test_local_policy_only_explicit_attrs():
    from lib389.pwpolicy import PwPolicyManager

    class _FakeInst(object):
        log = __import__('logging').getLogger('fake')

    mgr = PwPolicyManager(_FakeInst())
    policy_attrs = {
        'passwordhistory': ['on'],
        'passwordinhistory': ['6'],
        'cn': ['cn=nsPwPolicyEntry_user,uid=mark,ou=people,dc=example,dc=com'],
        'objectClass': ['top', 'ldapsubentry', 'passwordpolicy'],
    }
    effective = mgr._build_local_effective_attrs(policy_attrs, {})
    assert set(effective.keys()) == {'passwordhistory', 'passwordinhistory'}


def test_local_policy_camelcase_attrs():
    from lib389.pwpolicy import PwPolicyManager, _normalise_attrs

    class _FakeInst(object):
        log = __import__('logging').getLogger('fake')

    mgr = PwPolicyManager(_FakeInst())
    raw_attrs = {
        'passwordHistory': ['on'],
        'passwordInHistory': ['10'],
    }
    effective = mgr._build_local_effective_attrs(_normalise_attrs(raw_attrs), {})
    assert set(effective.keys()) == {'passwordhistory', 'passwordinhistory'}
    assert effective['passwordhistory']['values'] == ['on']
    assert effective['passwordinhistory']['values'] == ['10']


def test_inheritance_with_camelcase_global():
    from lib389.pwpolicy import PwPolicyManager

    class _FakeInst(object):
        log = __import__('logging').getLogger('fake')

    mgr = PwPolicyManager(_FakeInst())
    policy_attrs = {'passwordhistory': ['on']}
    global_attrs = {
        'nsslapd-pwpolicy-inherit-global': ['on'],
        'passwordCheckSyntax': ['on'],
        'passwordMinLength': ['16'],
        'passwordMinDigits': ['0'],
    }
    effective = mgr._build_local_effective_attrs(policy_attrs, global_attrs)
    assert effective['passwordminlength']['values'] == ['16']
    assert effective['passwordminlength']['inherited'] is True
    assert 'passwordmindigits' not in effective


def test_report_attribute_order_inherited_last():
    from lib389.pwpolicy import PwPolicyManager, POLICY_TYPE_USER

    class _FakeInst(object):
        log = __import__('logging').getLogger('fake')

    mgr = PwPolicyManager(_FakeInst())
    report = {
        'policy_type': POLICY_TYPE_USER,
        'attrs': {
            'passwordminlength': {'values': ['16'], 'inherited': True},
            'passwordhistory': {'values': ['on'], 'inherited': False},
            'passwordinhistory': {'values': ['6'], 'inherited': False},
        },
    }
    assert mgr._report_attribute_names(report) == [
        'passwordhistory',
        'passwordinhistory',
        'passwordminlength',
    ]
