# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import os

import ldap
import pytest

from lib389._entry import Entry
from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD
from test389.topologies import topology_st
from lib389.utils import ds_is_older, ensure_bytes
from lib389.idm.user import UserAccount, UserAccounts, DEFAULT_BASEDN_RDN

log = logging.getLogger(__name__)

DEBUGGING = os.getenv('DEBUGGING', False)
if DEBUGGING:
    log.setLevel(logging.DEBUG)
else:
    log.setLevel(logging.INFO)

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="addn not implemented")]

ADDN_PLUGIN_DN = 'cn=addn,cn=plugins,cn=config'
ADDN_DOMAIN_DN = f'cn=example.com,{ADDN_PLUGIN_DN}'

USER1 = 'user1'
USER1_DOMAIN = 'user1@example.com'
ADDN_FILTER = '(&(objectClass=account)(uid=%s))'


def _addn_bind_succeeds(inst, name, cred):
    """Return True if simple_bind_s succeeds, False on INVALID_CREDENTIALS; other errors propagate."""
    if DEBUGGING:
        log.debug(f'addn test BINDING AS {name}')
    try:
        inst.simple_bind_s(name, cred)
        return True
    except ldap.INVALID_CREDENTIALS:
        return False


@pytest.fixture
def addn_plugin_setup(topology_st, request):
    """
    Create the addn plugin entry and cn=example.com child (addn_base / addn_filter), then
    restart.
    """
    inst = topology_st.standalone
    inst.add_s(Entry((
        ADDN_PLUGIN_DN,
        {
            'objectClass': 'top nsSlapdPlugin extensibleObject'.split(),
            'cn': 'addn',
            'nsslapd-pluginPath': 'libaddn-plugin',
            'nsslapd-pluginInitfunc': 'addn_init',
            'nsslapd-pluginType': 'preoperation',
            'nsslapd-pluginEnabled': 'on',
            'nsslapd-pluginId': 'addn',
            'nsslapd-pluginVendor': '389 Project',
            'nsslapd-pluginVersion': '1.3.6.0',
            'nsslapd-pluginDescription': 'Allow AD DN style bind names to LDAP',
            'addn_default_domain': 'example.com',
        }
    )))

    inst.add_s(Entry((
        ADDN_DOMAIN_DN,
        {
            'objectClass': 'top extensibleObject'.split(),
            'cn': 'example.com',
            'addn_base': f'{DEFAULT_BASEDN_RDN},{DEFAULT_SUFFIX}',
            'addn_filter': ADDN_FILTER,
        }
    )))

    inst.restart(timeout=60)

    def _cleanup():
        people_user_dn = f'uid={USER1},{DEFAULT_BASEDN_RDN},{DEFAULT_SUFFIX}'
        suffix_user_dn = f'uid={USER1},{DEFAULT_SUFFIX}'

        try:
            inst.simple_bind_s(DN_DM, PASSWORD)
        except ldap.LDAPError as e:
            log.warning(f'addn_plugin_setup finalizer: could not bind as DM: {e}')
            return

        for dn in (suffix_user_dn, people_user_dn, ADDN_DOMAIN_DN, ADDN_PLUGIN_DN):
            try:
                inst.delete_s(dn)
            except ldap.NO_SUCH_OBJECT:
                pass
            except ldap.LDAPError as e:
                log.warning(f'addn_plugin_setup finalizer: could not delete {dn}: {e}')

        try:
            inst.restart(timeout=60)
        except Exception as e:
            log.warning(f'addn_plugin_setup finalizer: restart failed: {e}')

    request.addfinalizer(_cleanup)
    return inst


def test_addn_plugin_bind_names(addn_plugin_setup):
    """Addn (AD-style) bind: short name, UPN, and ambiguous addn search

    :id: 173663ab-afcf-439c-9437-5429f0df0321
    :setup: Standalone instance with addn_plugin_setup
    :steps:
        1. Add user1 under the default people subtree
        2. Check bind with full user DN, with short name user1, and with user1@example.com (unambiguous addn)
        3. Add a second user1 at the suffix root; set addn_base to the full default suffix, restart
        4. Check that simple binds with both full user DNs still succeed
        5. In a loop, attempt bind for user1 and user1@example.com
    :expectedresults:
        1. User1 is created successfully
        2. The three unambiguous bind forms all succeed
        3. Second user1 created successfully; addn_base covers the whole default suffix
        4. Both DN-based binds still succeed
        5. Short name and UPN each raise ldap.OPERATIONS_ERROR; DN-based binds remain valid
    """
    inst = addn_plugin_setup

    # Create user1 in the default suffix
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user1 = users.create(
        f'uid={USER1}',
        {
            'uid': USER1,
            'cn': USER1,
            'sn': USER1,
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': f'/home/{USER1}',
            'userPassword': PASSWORD,
        },
    )

    # Check bind
    assert _addn_bind_succeeds(inst, user1.dn, PASSWORD)
    for _ in range(10):
        assert _addn_bind_succeeds(inst, USER1, PASSWORD)
        assert _addn_bind_succeeds(inst, USER1_DOMAIN, PASSWORD)

    # Create second user1
    inst.simple_bind_s(DN_DM, PASSWORD)
    user2 = UserAccount(inst, dn=None)
    user2.create(
        f'uid={USER1}',
        {
            'uid': USER1,
            'cn': USER1,
            'sn': USER1,
            'uidNumber': '1001',
            'gidNumber': '2000',
            'homeDirectory': f'/home/{USER1}2',
            'userPassword': PASSWORD,
        },
        DEFAULT_SUFFIX,
    )

    # Modify addn_base to cover the whole default suffix
    inst.modify_s(
        ADDN_DOMAIN_DN,
        [(ldap.MOD_REPLACE, 'addn_base', ensure_bytes(DEFAULT_SUFFIX))],
    )
    inst.restart(timeout=60)

    # Check bind
    assert _addn_bind_succeeds(inst, user1.dn, PASSWORD)
    assert _addn_bind_succeeds(inst, user2.dn, PASSWORD)
    for _ in range(10):
        with pytest.raises(ldap.OPERATIONS_ERROR):
            _addn_bind_succeeds(inst, USER1, PASSWORD)
        with pytest.raises(ldap.OPERATIONS_ERROR):
            _addn_bind_succeeds(inst, USER1_DOMAIN, PASSWORD)


if __name__ == '__main__':
    pytest.main([os.path.realpath(__file__), '-s'])
