# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

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
    """Parse policy target DN from a user-local policy cn value.

    :id: fd5fd30b-2b39-4643-a9fe-47ceba93a563
    :setup: N/A (unit test)
    :steps:
        1. Call _parse_policy_target_from_cn with a user policy cn
    :expectedresults:
        1. The decoded user DN is returned
    """
    cn = r'cn=nsPwPolicyEntry_user,uid\3Dmark,ou\3Dpeople,dc\3Dexample,dc\3Dcom'
    assert _parse_policy_target_from_cn(cn) == 'uid=mark,ou=people,dc=example,dc=com'


def test_parse_policy_target_subtree():
    """Parse policy target DN from a subtree-local policy cn value.

    :id: 306e0f97-f740-44f6-af3f-9f361fb7ff2b
    :setup: N/A (unit test)
    :steps:
        1. Call _parse_policy_target_from_cn with a subtree policy cn
    :expectedresults:
        1. The decoded subtree DN is returned
    """
    cn = r'cn=nsPwPolicyEntry_subtree,ou\3Dpeople,dc\3Dexample,dc\3Dcom'
    assert _parse_policy_target_from_cn(cn) == 'ou=people,dc=example,dc=com'


def test_parse_policy_target_invalid():
    """Invalid policy cn values do not yield a target DN.

    :id: 2614d72b-8169-481e-b25a-58c962024059
    :setup: N/A (unit test)
    :steps:
        1. Call _parse_policy_target_from_cn with a non-policy cn
    :expectedresults:
        1. None is returned
    """
    assert _parse_policy_target_from_cn('cn=not-a-policy,dc=example,dc=com') is None


def test_classify_user_policy():
    """Classify a user-local policy from pwdpolicysubentry metadata.

    :id: c0cdd29f-1dc2-4fd3-b3ae-2602a9401bdd
    :setup: N/A (unit test)
    :steps:
        1. Classify a policy entry whose cn contains nsPwPolicyEntry_user
    :expectedresults:
        1. Policy type is User Policy and the target DN is the user DN
    """
    cn = r'cn=nsPwPolicyEntry_user,uid\3Dmark,ou\3Dpeople,dc\3Dexample,dc\3Dcom'
    entry = _FakePwEntry(cn)
    pwp_dn = 'cn=pwp,cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
    policy_type, source, target = _classify_policy_from_pwdpolicysubentry(pwp_dn, entry)
    assert policy_type == POLICY_TYPE_USER
    assert source == pwp_dn
    assert target == 'uid=mark,ou=people,dc=example,dc=com'


def test_classify_subtree_policy():
    """Classify a subtree-local policy from pwdpolicysubentry metadata.

    :id: 1ae92b82-0004-46bb-9cf2-318698884096
    :setup: N/A (unit test)
    :steps:
        1. Classify a policy entry whose cn contains nsPwPolicyEntry_subtree
    :expectedresults:
        1. Policy type is Subtree Policy and the target DN is the subtree DN
    """
    cn = r'cn=nsPwPolicyEntry_subtree,ou\3Dpeople,dc\3Dexample,dc\3Dcom'
    entry = _FakePwEntry(cn)
    pwp_dn = 'cn=pwp,cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
    policy_type, source, target = _classify_policy_from_pwdpolicysubentry(pwp_dn, entry)
    assert policy_type == POLICY_TYPE_SUBTREE
    assert target == 'ou=people,dc=example,dc=com'


def test_syntax_inheritance_merge():
    """Local effective attrs merge inherited global syntax settings.

    :id: 8b83b215-84ab-4f65-92cc-064325c6fd40
    :setup: N/A (unit test)
    :steps:
        1. Verify on/off and inherited-value helpers
        2. Build local effective attrs with inheritance enabled
    :expectedresults:
        1. Helper results match expected on/off and display rules
        2. Explicit local attrs and inherited syntax attrs are returned
    """
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
    """Local effective attrs include only explicitly stored policy settings.

    :id: a9ee339e-b35b-4b3b-ac80-eef098a84ab4
    :setup: N/A (unit test)
    :steps:
        1. Build local effective attrs from an entry with explicit settings
    :expectedresults:
        1. Only explicit password policy attributes are returned
    """
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
    """CamelCase LDAP attribute names are normalized for local policy output.

    :id: 2c2027b7-54cb-4036-ae6f-9845de7d17a8
    :setup: N/A (unit test)
    :steps:
        1. Build local effective attrs from camelCase attribute names
    :expectedresults:
        1. Attributes are normalized to lowercase keys with correct values
    """
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
    """Inherited syntax works when global attrs use schema camelCase names.

    :id: 892f5c84-83e5-41da-bdf1-c49a837b53f7
    :setup: N/A (unit test)
    :steps:
        1. Build local effective attrs with camelCase global syntax attributes
    :expectedresults:
        1. Meaningful inherited syntax values are merged; empty/zero values are skipped
    """
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
    """Report attribute order lists local settings before inherited ones.

    :id: a0e7a32b-29b8-47ea-90a1-639c29d59c10
    :setup: N/A (unit test)
    :steps:
        1. Request report attribute names for a mix of local and inherited attrs
    :expectedresults:
        1. Local attrs are alphabetical first, then inherited attrs
    """
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
