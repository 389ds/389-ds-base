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
from lib389.idm.organizationalunit import OrganizationalUnits
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
OTHER_OU = 'ou=other'
CUSTOM_OU = 'ou=custom'

USER_TYPES = ('posix', 'basic', 'traditional', 'service')


def _run_get_pwp(topo, selector, user_type='posix', json_out=False, parent_dn=None):
    args = FakeArgs()
    args.json = json_out
    args.user_type = user_type
    args.selector = selector
    args.parent_dn = parent_dn
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


def _create_user_by_type(topo, user_type, rdn=None):
    """Create a user account for the given dsidm user type; return (entry, selector).

    :param rdn: Optional container RDN under DEFAULT_SUFFIX (e.g. 'ou=custom').
                When None, each account type uses its default container.
    """
    name = 'pwp_{}_user'.format(user_type)
    kwargs = {'rdn': rdn} if rdn is not None else {}

    if user_type == 'posix':
        accounts = nsUserAccounts(topo.standalone, DEFAULT_SUFFIX, **kwargs)
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
        accounts = BasicUserAccounts(topo.standalone, DEFAULT_SUFFIX, **kwargs)
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
        accounts = TraditionalUserAccounts(topo.standalone, DEFAULT_SUFFIX, **kwargs)
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
        accounts = ServiceAccounts(topo.standalone, DEFAULT_SUFFIX, **kwargs)
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
    """get-pwp resolves the global effective policy for each dsidm user type.

    :id: 619e8040-ce33-4fa2-a8bb-75acb7b0668b
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Create a user of the given dsidm user type
        2. Run dsidm user get-pwp for that user
    :expectedresults:
        1. The user entry is created successfully
        2. get-pwp reports the global password policy
    """
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
    """Effective policy is global when the user has no local assignment.

    :id: 7ef61f72-fd0e-4074-b4c5-662951103c0d
    :setup: Standalone instance with a test user
    :steps:
        1. Run dsidm user get-pwp for a user with no local policy
    :expectedresults:
        1. get-pwp reports the global password policy
    """
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
    """Effective policy is subtree local when CoS applies a subtree policy.

    :id: d599678c-7542-4d11-bd47-0542ca89e10f
    :setup: Standalone instance with a test user
    :steps:
        1. Create a subtree password policy for ou=People
        2. Run dsidm user get-pwp for the test user
    :expectedresults:
        1. The subtree policy is created successfully
        2. get-pwp reports the subtree local policy and its settings
    """
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
def test_get_pwp_subtree_policy_other_ou(topology_st):
    """get-pwp resolves subtree local policy for a user under ou=other.

    :id: 30f7f701-4928-4313-811c-e4a5ef2857da
    :setup: Standalone instance
    :steps:
        1. Create organizational unit ou=other under the default suffix
        2. Create a subtree password policy for ou=other
        3. Create a posix user under ou=other using nsUserAccounts with rdn=ou=other
        4. Run dsidm user get-pwp for the user with --parent-dn set to ou=other
    :expectedresults:
        1. The organizational unit entry is created successfully
        2. The subtree policy is created successfully
        3. The user entry is created under ou=other
        4. get-pwp reports the subtree local policy and its settings
    """
    other_ou_dn = '{},{}'.format(OTHER_OU, DEFAULT_SUFFIX)
    uid = 'pwp_other_ou_user'
    user = None
    pwp = PwPolicyManager(topology_st.standalone)
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)

    if ous.exists('other'):
        ou = ous.get('other')
    else:
        ou = ous.create(properties={'ou': 'other'})

    try:
        pwp.create_subtree_policy(other_ou_dn, {
            'passwordhistory': 'on',
            'passwordinhistory': '8',
        })

        accounts = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX, rdn=OTHER_OU)
        if accounts.exists(uid):
            accounts.get(uid).delete()
        user = accounts.create(
            'uid={}'.format(uid),
            {
                'uid': uid,
                'cn': uid,
                'displayName': uid,
                'uidNumber': '6301',
                'gidNumber': '7301',
                'homeDirectory': '/home/{}'.format(uid),
                'userPassword': PASSWORD,
            },
        )

        output = _run_get_pwp(topology_st, uid, parent_dn=other_ou_dn)
        check_value_in_log_and_reset(
            topology_st,
            check_value='passwordPolicy: {}'.format(POLICY_TYPE_SUBTREE),
        )
        assert 'dn: {}'.format(user.dn) in output
        assert 'passwordPolicyTarget: {}'.format(other_ou_dn) in output
        assert 'passwordhistory: on' in output
        assert 'passwordinhistory: 8' in output
        assert 'passwordchange:' not in output
        topology_st.logcap.flush()
    finally:
        if user is not None and user.exists():
            _cleanup_local_policies(topology_st, user.dn, other_ou_dn)
            user.delete()
        else:
            try:
                if pwp.is_subtree_policy(other_ou_dn):
                    pwp.delete_local_policy(other_ou_dn)
            except ValueError:
                pass
        if ou.exists():
            ou.delete()


@pytest.mark.parametrize('user_type', USER_TYPES)
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_all_user_types_under_custom_ou(topology_st, user_type):
    """get-pwp resolves policy for every user type under a non-default OU.

    Users (including service accounts) live under ou=custom rather than their
    default containers. parent_dn points get-pwp at that custom location.

    :id: abc5e462-ec4a-429c-ae12-c8977d59d01b
    :parametrized: yes
    :setup: Standalone instance
    :steps:
        1. Create organizational unit ou=custom under the default suffix
        2. Create a subtree password policy for ou=custom
        3. Create a user of the given type under ou=custom
        4. Run dsidm user get-pwp with parent_dn set to ou=custom,DEFAULT_SUFFIX
    :expectedresults:
        1. The organizational unit entry is created successfully
        2. The subtree policy is created successfully
        3. The user entry is created under ou=custom
        4. get-pwp reports the subtree policy for that user DN and settings
    """
    custom_ou_dn = '{},{}'.format(CUSTOM_OU, DEFAULT_SUFFIX)
    user = None
    pwp = PwPolicyManager(topology_st.standalone)
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)

    if ous.exists('custom'):
        ou = ous.get('custom')
    else:
        ou = ous.create(properties={'ou': 'custom'})

    try:
        if not pwp.is_subtree_policy(custom_ou_dn):
            pwp.create_subtree_policy(custom_ou_dn, {
                'passwordhistory': 'on',
                'passwordinhistory': '7',
            })

        user, selector = _create_user_by_type(topology_st, user_type, rdn=CUSTOM_OU)
        assert custom_ou_dn.lower() in user.dn.lower()

        output = _run_get_pwp(
            topology_st,
            selector,
            user_type=user_type,
            parent_dn=custom_ou_dn,
        )
        assert 'dn: {}'.format(user.dn) in output
        assert 'passwordPolicy: {}'.format(POLICY_TYPE_SUBTREE) in output
        assert 'passwordPolicyTarget: {}'.format(custom_ou_dn) in output
        assert 'passwordhistory: on' in output
        assert 'passwordinhistory: 7' in output
        topology_st.logcap.flush()
    finally:
        if user is not None and user.exists():
            _cleanup_local_policies(topology_st, user.dn, custom_ou_dn)
            user.delete()
        else:
            try:
                if pwp.is_subtree_policy(custom_ou_dn):
                    pwp.delete_local_policy(custom_ou_dn)
            except ValueError:
                pass
        if ou.exists():
            # Subtree policy cleanup may leave the container; remove OU when empty.
            try:
                if pwp.is_subtree_policy(custom_ou_dn):
                    pwp.delete_local_policy(custom_ou_dn)
            except ValueError:
                pass
            try:
                ou.delete()
            except Exception:
                pass


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_user_policy(topology_st, pwp_test_user):
    """Effective policy is user local when the user has a user policy.

    :id: f1c66bc1-af96-4a31-86e9-33703b4e6c90
    :setup: Standalone instance with a test user
    :steps:
        1. Create a user-local password policy for the test user
        2. Run dsidm user get-pwp for the test user
    :expectedresults:
        1. The user policy is created successfully
        2. get-pwp reports the user local policy and its settings
    """
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
    """User local policy takes precedence over subtree policy.

    :id: 1e3cd24e-fa66-4f96-b276-8714b71fc43b
    :setup: Standalone instance with a test user
    :steps:
        1. Create a subtree password policy and a user-local policy with different values
        2. Run dsidm user get-pwp for the test user
    :expectedresults:
        1. Both policies are created successfully
        2. get-pwp reports the user local policy, not the subtree policy
    """
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
    """Syntax settings inherit from global when enabled.

    :id: ac811f41-422e-4098-9fc0-e668d662ef80
    :setup: Standalone instance with a test user
    :steps:
        1. Enable global syntax checking and inheritance, set passwordminlength
        2. Create a user-local policy without local syntax settings
        3. Run dsidm user get-pwp for the test user
    :expectedresults:
        1. Global syntax and inheritance settings are applied
        2. The user policy is created successfully
        3. get-pwp reports passwordminlength as inherited from global
    """
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
    """Syntax settings do not inherit when global inheritance is disabled.

    :id: 3d79d6ba-5d02-4e95-8156-857c85cac6c3
    :setup: Standalone instance with a test user
    :steps:
        1. Disable inheritance, enable global syntax checking, set passwordminlength
        2. Create a user-local policy without local syntax settings
        3. Run dsidm user get-pwp for the test user
    :expectedresults:
        1. Global settings are applied with inheritance off
        2. The user policy is created successfully
        3. get-pwp does not report inherited passwordminlength
    """
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
def test_get_pwp_local_off_uses_global_syntax(topology_st, pwp_test_user):
    """With local policies disabled, effective passwordMinLength is global.

    A user-local policy may still exist with a different passwordMinLength, and
    nsslapd-pwpolicy-inherit-global may be on with global syntax checking enabled.
    When nsslapd-pwpolicy-local is off, get-pwp must ignore the local assignment
    and report the global policy value.

    :id: ebaf0c48-e4f1-484a-b54b-5ca913be07ab
    :setup: Standalone instance with a test user
    :steps:
        1. Set global passwordchecksyntax on, passwordminlength to 16, and
           nsslapd-pwpolicy-inherit-global on
        2. Create a user-local policy with passwordminlength 24
        3. Set nsslapd-pwpolicy-local to off
        4. Run dsidm user get-pwp for the user
    :expectedresults:
        1. Global syntax and inheritance settings are applied
        2. User-local policy is created with a different passwordminlength
        3. Local password policies are disabled on cn=config
        4. get-pwp reports Global Policy and passwordminlength 16 (not 24)
    """
    global_minlen = '16'
    local_minlen = '24'
    config = Config(topology_st.standalone)
    config.replace_many(
        ('nsslapd-pwpolicy-inherit-global', 'on'),
        ('passwordchecksyntax', 'on'),
        ('passwordminlength', global_minlen),
    )
    pwp = PwPolicyManager(topology_st.standalone)
    pwp.create_user_policy(pwp_test_user.dn, {
        'passwordminlength': local_minlen,
    })
    config.replace('nsslapd-pwpolicy-local', 'off')

    try:
        output = _run_get_pwp(topology_st, _user_selector(pwp_test_user))
        assert 'passwordPolicy: {}'.format(POLICY_TYPE_GLOBAL) in output
        assert 'passwordPolicyDN: cn=config' in output
        assert 'passwordminlength: {}'.format(global_minlen) in output
        assert 'passwordminlength: {}'.format(local_minlen) not in output
        assert 'passwordminlength: {} (inherited)'.format(global_minlen) not in output
        topology_st.logcap.flush()
    finally:
        config.replace('nsslapd-pwpolicy-local', 'on')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_json_output(topology_st, pwp_test_user):
    """JSON output includes policy metadata and attrs.

    :id: 433b678b-15d7-4dbc-8576-ad9ff36f1049
    :setup: Standalone instance with a test user
    :steps:
        1. Run dsidm user get-pwp with JSON output enabled
    :expectedresults:
        1. JSON includes policy_type, policy_dn, policy_target, and attrs
    """
    output = _run_get_pwp(topology_st, _user_selector(pwp_test_user), json_out=True)
    result = json.loads(output)
    assert result['policy_type'] == POLICY_TYPE_GLOBAL
    assert result['policy_dn'] == 'cn=config'
    assert result['policy_target'] == 'global'
    assert 'passwordchange' in result['attrs']
    topology_st.logcap.flush()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_json_inherited_syntax(topology_st, pwp_test_user):
    """JSON marks inherited syntax attributes.

    :id: 6706a146-61b7-40ef-a4f1-72aa6421412e
    :setup: Standalone instance with a test user
    :steps:
        1. Enable global syntax checking and inheritance, set passwordminlength
        2. Create a user-local policy without local syntax settings
        3. Run dsidm user get-pwp with JSON output enabled
    :expectedresults:
        1. Global syntax and inheritance settings are applied
        2. The user policy is created successfully
        3. JSON marks passwordminlength with the inherited flag
    """
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
    """Missing user produces a clear error.

    :id: 8d8a7463-4621-48fe-871f-d0d8f989b13a
    :setup: Standalone instance
    :steps:
        1. Run dsidm user get-pwp for a non-existent user selector
    :expectedresults:
        1. get-pwp raises ValueError indicating the user was not found
    """
    args = FakeArgs()
    args.json = False
    args.user_type = 'posix'
    args.selector = 'no_such_pwp_user'
    args.parent_dn = None
    with pytest.raises(ValueError, match='not found'):
        get_pwp(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_get_pwp_broken_policy_reference(topology_st, pwp_test_user):
    """Broken pwdpolicysubentry reference produces a clear error.

    :id: 2fc47004-1ba9-47af-9c57-3aa34fae5500
    :setup: Standalone instance with a test user
    :steps:
        1. Set the user's pwdpolicysubentry to a missing policy DN
        2. Run dsidm user get-pwp for the test user
    :expectedresults:
        1. The broken policy reference is stored on the user entry
        2. get-pwp raises ValueError indicating the policy could not be found
    """
    pwp_test_user.replace('pwdpolicysubentry', 'cn=missing-policy,cn=config')
    args = FakeArgs()
    args.json = False
    args.user_type = 'posix'
    args.selector = _user_selector(pwp_test_user)
    args.parent_dn = None
    with pytest.raises(ValueError, match='could not be found'):
        get_pwp(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
