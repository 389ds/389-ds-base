# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import json
import logging
import pytest
import ldap

from lib389 import DEFAULT_SUFFIX
from lib389._constants import PASSWORD
from lib389.cli_base import FakeArgs
from lib389.cli_idm.user import get_pwp
from lib389.config import Config
from lib389.idm.user import (
    BasicUserAccounts,
    TraditionalUserAccounts,
    nsUserAccounts,
)
from lib389.idm.services import ServiceAccounts
from lib389.pwpolicy import (
    PwPolicyManager,
    POLICY_TYPE_GLOBAL,
    POLICY_TYPE_USER,
    POLICY_TYPE_SUBTREE,
)
from lib389.utils import ds_is_older
from test389.topologies import topology_st
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PEOPLE_OU = 'ou=People'

USER_TYPES = ('posix', 'basic', 'traditional', 'service')


def _run_get_pwp(topo, selector, user_type='posix', json_out=False):
    args = FakeArgs()
    args.json = json_out
    args.user_type = user_type
    args.selector = selector
    topo.logcap.flush()
    get_pwp(topo.standalone, DEFAULT_SUFFIX, topo.logcap.log, args)
    outputs = topo.logcap.get_raw_outputs()
    return outputs[0] if outputs else ''


def _create_user(topo, uid):
    users = nsUserAccounts(topo.standalone, DEFAULT_SUFFIX)
    if users.exists(uid):
        users.get(uid).delete()
    return users.create(
        'uid={}'.format(uid),
        {
            'uid': uid,
            'cn': uid,
            'displayName': uid,
            'uidNumber': '6101',
            'gidNumber': '7101',
            'homeDirectory': '/home/{}'.format(uid),
            'userPassword': PASSWORD,
        },
    )


def _create_user_by_type(topo, user_type):
    """Create a user account for the given dsidm user type; return (entry, selector)."""
    name = 'pwp_{}_user'.format(user_type)

    if user_type == 'posix':
        accounts = nsUserAccounts(topo.standalone, DEFAULT_SUFFIX)
        if accounts.exists(name):
            accounts.get(name).delete()
        user = accounts.create(
            'uid={}'.format(name),
            {
                'uid': name,
                'cn': name,
                'displayName': name,
                'uidNumber': '6201',
                'gidNumber': '7201',
                'homeDirectory': '/home/{}'.format(name),
                'userPassword': PASSWORD,
            },
        )
        selector = name
    elif user_type == 'basic':
        accounts = BasicUserAccounts(topo.standalone, DEFAULT_SUFFIX)
        if accounts.exists(name):
            accounts.get(name).delete()
        user = accounts.create(
            'uid={}'.format(name),
            {
                'uid': name,
                'cn': name,
                'displayName': name,
                'userPassword': PASSWORD,
            },
        )
        selector = name
    elif user_type == 'traditional':
        accounts = TraditionalUserAccounts(topo.standalone, DEFAULT_SUFFIX)
        if accounts.exists(name):
            accounts.get(name).delete()
        user = accounts.create(
            'cn={}'.format(name),
            {
                'cn': name,
                'sn': name,
                'userPassword': PASSWORD,
            },
        )
        selector = name
    elif user_type == 'service':
        accounts = ServiceAccounts(topo.standalone, DEFAULT_SUFFIX)
        if accounts.exists(name):
            accounts.get(name).delete()
        user = accounts.create(
            'cn={}'.format(name),
            {
                'cn': name,
                'description': 'pwp get-pwp test service',
                'userPassword': PASSWORD,
            },
        )
        selector = name
    else:
        raise ValueError('Unsupported user type: {}'.format(user_type))

    return user, selector


def _cleanup_local_policies(topo, user_dn, subtree_dn=None):
    pwp = PwPolicyManager(topo.standalone)
    try:
        if pwp.is_user_policy(user_dn):
            pwp.delete_local_policy(user_dn)
    except ValueError:
        pass
    if subtree_dn:
        try:
            if pwp.is_subtree_policy(subtree_dn):
                pwp.delete_local_policy(subtree_dn)
        except ValueError:
            pass


@pytest.fixture
def pwp_test_user(topology_st, request):
    """Create an isolated user and remove any local policies after the test."""
    uid = 'pwp_get_test_user'
    user = _create_user(topology_st, uid)
    subtree_dn = '{},{}'.format(PEOPLE_OU, DEFAULT_SUFFIX)

    def fin():
        _cleanup_local_policies(topology_st, user.dn, subtree_dn)

    request.addfinalizer(fin)
    return user


def _user_selector(user):
    return user.get_attr_val_utf8('uid')


@pytest.mark.parametrize('user_type', USER_TYPES)
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_global_policy_by_user_type(topology_st, user_type):
    """get-pwp resolves the global effective policy for each dsidm user type."""
    user, selector = _create_user_by_type(topology_st, user_type)
    try:
        output = _run_get_pwp(topology_st, selector, user_type=user_type)
        assert 'dn: {}'.format(user.dn) in output
        assert 'passwordPolicy: {}'.format(POLICY_TYPE_GLOBAL) in output
        assert 'passwordPolicyDN: cn=config' in output
        assert 'passwordPolicyTarget: global' in output
        assert 'passwordchange:' in output
        topology_st.logcap.flush()
    finally:
        if user.exists():
            user.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_global_policy(topology_st, pwp_test_user):
    """Effective policy is global when the user has no local assignment."""
    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user))
    check_value_in_log_and_reset(
        topology_st,
        check_value='passwordPolicy: {}'.format(POLICY_TYPE_GLOBAL),
    )
    assert 'passwordPolicyDN: cn=config' in output
    assert 'passwordPolicyTarget: global' in output
    assert 'passwordchange:' in output


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_subtree_policy(topology_st, pwp_test_user):
    """Effective policy is subtree local when CoS applies a subtree policy."""
    subtree_dn = '{},{}'.format(PEOPLE_OU, DEFAULT_SUFFIX)
    pwp = PwPolicyManager(topology_st.standalone)
    pwp.create_subtree_policy(subtree_dn, {'passwordhistory': 'on', 'passwordinhistory': '10'})

    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user))
    check_value_in_log_and_reset(
        topology_st,
        check_value='passwordPolicy: {}'.format(POLICY_TYPE_SUBTREE),
    )
    assert 'passwordPolicyTarget: {}'.format(subtree_dn) in output
    assert 'passwordhistory: on' in output
    assert 'passwordinhistory: 10' in output
    assert 'passwordchange:' not in output


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_user_policy(topology_st, pwp_test_user):
    """Effective policy is user local when the user has a user policy."""
    pwp = PwPolicyManager(topology_st.standalone)
    pwp.create_user_policy(pwp_test_user.dn, {'passwordhistory': 'on', 'passwordinhistory': '6'})

    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user))
    check_value_in_log_and_reset(
        topology_st,
        check_value='passwordPolicy: {}'.format(POLICY_TYPE_USER),
    )
    assert 'passwordPolicyTarget: {}'.format(pwp_test_user.dn) in output
    assert 'passwordhistory: on' in output
    assert 'passwordchange:' not in output


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_user_policy_precedence(topology_st, pwp_test_user):
    """User local policy takes precedence over subtree policy."""
    subtree_dn = '{},{}'.format(PEOPLE_OU, DEFAULT_SUFFIX)
    pwp = PwPolicyManager(topology_st.standalone)
    pwp.create_subtree_policy(subtree_dn, {'passwordhistory': 'on', 'passwordinhistory': '10'})
    pwp.create_user_policy(pwp_test_user.dn, {'passwordhistory': 'on', 'passwordinhistory': '4'})

    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user))
    assert 'passwordPolicy: {}'.format(POLICY_TYPE_USER) in output
    assert 'passwordPolicy: {}'.format(POLICY_TYPE_SUBTREE) not in output
    assert 'passwordinhistory: 4' in output
    topology_st.logcap.flush()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_syntax_inheritance_on(topology_st, pwp_test_user):
    """Syntax settings inherit from global when enabled."""
    config = Config(topology_st.standalone)
    config.replace_many(
        ('nsslapd-pwpolicy-inherit-global', 'on'),
        ('passwordchecksyntax', 'on'),
        ('passwordminlength', '16'),
    )
    pwp = PwPolicyManager(topology_st.standalone)
    pwp.create_user_policy(pwp_test_user.dn, {'passwordhistory': 'on'})

    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user))
    assert 'passwordminlength: 16 (inherited)' in output
    topology_st.logcap.flush()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_syntax_inheritance_off(topology_st, pwp_test_user):
    """Syntax settings do not inherit when global inheritance is disabled."""
    config = Config(topology_st.standalone)
    config.replace_many(
        ('nsslapd-pwpolicy-inherit-global', 'off'),
        ('passwordchecksyntax', 'on'),
        ('passwordminlength', '16'),
    )
    pwp = PwPolicyManager(topology_st.standalone)
    pwp.create_user_policy(pwp_test_user.dn, {'passwordhistory': 'on'})

    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user))
    assert 'passwordminlength: 16 (inherited)' not in output
    assert 'passwordminlength:' not in output
    assert 'passwordhistory: on' in output
    topology_st.logcap.flush()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_json_output(topology_st, pwp_test_user):
    """JSON output includes policy metadata and attrs."""
    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user), json_out=True)
    result = json.loads(output)
    assert result['policy_type'] == POLICY_TYPE_GLOBAL
    assert result['policy_dn'] == 'cn=config'
    assert result['policy_target'] == 'global'
    assert 'passwordchange' in result['attrs']
    topology_st.logcap.flush()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_json_inherited_syntax(topology_st, pwp_test_user):
    """JSON marks inherited syntax attributes."""
    config = Config(topology_st.standalone)
    config.replace_many(
        ('nsslapd-pwpolicy-inherit-global', 'on'),
        ('passwordchecksyntax', 'on'),
        ('passwordminlength', '16'),
    )
    pwp = PwPolicyManager(topology_st.standalone)
    pwp.create_user_policy(pwp_test_user.dn, {'passwordhistory': 'on'})

    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user), json_out=True)
    result = json.loads(output)
    assert result['attrs']['passwordminlength'] == ['16', 'inherited']
    topology_st.logcap.flush()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_user_not_found(topology_st):
    """Missing user produces a clear error."""
    args = FakeArgs()
    args.json = False
    args.user_type = 'posix'
    args.selector = 'no_such_pwp_user'
    with pytest.raises(ValueError, match='not found'):
        get_pwp(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_broken_policy_reference(topology_st, pwp_test_user):
    """Broken pwdpolicysubentry reference produces a clear error."""
    pwp_test_user.replace('pwdpolicysubentry', 'cn=missing-policy,cn=config')
    args = FakeArgs()
    args.json = False
    args.user_type = 'posix'
    args.selector = _user_selector(pwp_test_user)
    with pytest.raises(ValueError, match='could not be found'):
        get_pwp(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
