# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import shutil
import logging
import subprocess
import threading
import pytest
import ldap

from test389.topologies import topology_m2
from lib389._mapped_object import DSLdapObject
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager
from lib389.utils import get_default_db_lib
from lib389.config import BDB_LDBMConfig, LMDB_LDBMConfig

DEBUGGING = os.getenv('DEBUGGING', default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

PLUGIN_SOURCE = r"""
/*
 * dn-hash-corruption plugin
 *
 *   For every ADD under the configured container (default ou=test),
 *   prepends 'x' to the uid RDN value:
 *   uid=w1u42,ou=test,dc=example,dc=com -> uid=xw1u42,ou=test,dc=example,dc=com
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "slapi-plugin.h"

#define PLUGIN_NAME "dn-hash-corruption"

/* TRIGGER_PARENT_RDN is the parent RDN prefix that selects which
 * entries the plugin modifies.
 */
#ifndef TRIGGER_PARENT_RDN
#define TRIGGER_PARENT_RDN "ou=test"
#endif
#define TRIGGER_PARENT_RDN_LEN (sizeof(TRIGGER_PARENT_RDN) - 1)

static Slapi_PluginDesc plugin_desc = {
    PLUGIN_NAME, "reproducer", "1.0",
    "Triggers entry cache DN hash corruption"
};
static Slapi_ComponentId *plugin_id = NULL;

/*
 * betxn pre-add callback.
 * Runs after `cache_add_tentative()` inserted the entry into `c_dntable`
 * under the original DN, but before `CACHE_ADD()` re-inserts it.
 * Changing the DN here puts the entry in two different hash slots.
 */
static int dnhashcorr_betxn_pre_add(Slapi_PBlock *pb)
{
    Slapi_Entry *entry = NULL;
    const char *dn = NULL;
    char *parent_dn = NULL;
    char *old_uid = NULL;
    char *new_uid = NULL;
    char *new_dn = NULL;
    Slapi_DN *sdn = NULL;

    slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &entry);
    if (!entry) return 0;

    dn = slapi_entry_get_dn_const(entry);
    if (!dn) return 0;

    /* Only target entries under the configured container */
    parent_dn = slapi_dn_parent(dn);
    if (!parent_dn) return 0;
    if (strncasecmp(parent_dn, TRIGGER_PARENT_RDN, TRIGGER_PARENT_RDN_LEN) != 0) {
        slapi_ch_free_string(&parent_dn);
        return 0;
    }

    /* Get the current uid attribute value */
    old_uid = slapi_entry_attr_get_charptr(entry, "uid");
    if (!old_uid) {
        slapi_ch_free_string(&parent_dn);
        return 0;
    }

    /*
     * Build new DN by prepending 'x' to the uid value.
     * This changes the NDN hash -> entry ends up in two hash slots.
     */
    new_dn = slapi_ch_smprintf("uid=x%s,%s", old_uid, parent_dn);
    slapi_ch_free_string(&parent_dn);
    if (!new_dn) {
        slapi_ch_free_string(&old_uid);
        return 0;
    }

    /*
     * Replace the entry's SDN in-place.
     * The entry is still linked in c_dntable under the old NDN hash (slot A).
     * After this CACHE_ADD will insert it under the new NDN hash (slot B).
     */
    sdn = slapi_entry_get_sdn(entry);
    slapi_sdn_set_dn_passin(sdn, new_dn);
    slapi_sdn_get_ndn(sdn);

    /* Update uid attribute to match new RDN to avoid naming violation */
    new_uid = slapi_ch_smprintf("x%s", old_uid);
    slapi_entry_attr_set_charptr(entry, "uid", new_uid);
    slapi_ch_free_string(&new_uid);
    slapi_ch_free_string(&old_uid);

    return 0;
}

static int dnhashcorr_start(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_FATAL, PLUGIN_NAME, "DN hash corruption reproducer plugin started\n");
    return 0;
}

int dnhashcorr_init(Slapi_PBlock *pb)
{
    int rc = 0;
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_03);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&plugin_desc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *)dnhashcorr_start);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN, (void *)dnhashcorr_betxn_pre_add);
    rc |= slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_id);
    if (rc) return -1;
    return 0;
}
"""

CONTAINER_OU = 'test'
WORKLOAD_DURATION = 300
NUM_SEED = 100
NUM_ADD_WORKERS = 4
NUM_SEARCH_WORKERS = 4
NUM_DELETE_WORKERS = 2

PLUGIN_DN = 'cn=DN Hash Corruption,cn=plugins,cn=config'


def _compile_and_install_plugin(inst, tmp_dir):
    """Compile the reproducer plugin and install it into the instance's
    plugin directory.
    """
    src_file = os.path.join(tmp_dir, 'dn_hash_corruption_plugin.c')
    obj_file = os.path.join(tmp_dir, 'dn_hash_corruption_plugin.o')
    so_file = os.path.join(tmp_dir, 'libdn-hash-corruption-plugin.so')

    with open(src_file, 'w') as f:
        f.write(PLUGIN_SOURCE)

    cmd = (
        f'/usr/bin/gcc -I/usr/include/dirsrv -I/usr/include/nspr4 '
        f'-I/usr/include/nss3 -g -O2 -Wall '
        f'-DTRIGGER_PARENT_RDN=\'"ou={CONTAINER_OU}"\' '
        f'-c {src_file} -fPIC -DPIC -o {obj_file}'
    )
    subprocess.run(cmd, shell=True, check=True)

    cmd = (
        f'/usr/bin/gcc -shared -fPIC -DPIC {obj_file} '
        f'-Wl,-rpath -Wl,/usr/lib64/dirsrv -L/usr/lib64/dirsrv/ '
        f'/usr/lib64/dirsrv/libslapd.so.0 -lldap -llber -lc '
        f'-Wl,-z,now -Wl,-soname -Wl,libdn-hash-corruption-plugin.so '
        f'-o {so_file}'
    )
    subprocess.run(cmd, shell=True, check=True)

    plugin_dir = inst.get_plugin_dir()
    installed_path = os.path.join(plugin_dir,
                                  'libdn-hash-corruption-plugin.so')
    shutil.copy(so_file, installed_path)
    os.chmod(installed_path, 0o755)
    subprocess.run(['restorecon', installed_path], check=False)


def _load_plugin(inst):
    """Load the DN hash corruption plugin."""
    plugin = DSLdapObject(inst, PLUGIN_DN)
    plugin._rdn_attribute = 'cn'
    plugin._create_objectclasses = [
        'top',
        'nsSlapdPlugin',
        'extensibleObject',
    ]
    plugin._protected = False
    plugin.create(properties={
        'cn': 'DN Hash Corruption',
        'nsslapd-pluginPath': 'libdn-hash-corruption-plugin',
        'nsslapd-pluginInitfunc': 'dnhashcorr_init',
        'nsslapd-pluginType': 'betxnpreoperation',
        'nsslapd-pluginEnabled': 'on',
        'nsslapd-plugin-depends-on-type': 'database',
        'nsslapd-pluginId': 'dn-hash-corruption',
        'nsslapd-pluginVersion': '1.0',
        'nsslapd-pluginVendor': '389 Project',
        'nsslapd-pluginDescription': 'Triggers entry cache DN hash corruption',
    })


def _shrink_entry_cache(inst):
    """Shrink entry cache to create pressure."""
    if get_default_db_lib() == 'bdb':
        config_ldbm = BDB_LDBMConfig(inst)
    else:
        config_ldbm = LMDB_LDBMConfig(inst)
    config_ldbm.set('nsslapd-cache-autosize', '0')

    inst.restart()

    userroot = DSLdapObject(inst, 'cn=userRoot,cn=ldbm database,cn=plugins,cn=config')
    userroot.replace('nsslapd-cachememsize', '512000')


def _add_worker(inst, suffix, worker_id, stop_event, error_list):
    """Continuously adds entries under the test container."""
    users = UserAccounts(inst, suffix, rdn=f'ou={CONTAINER_OU}')
    n = 0
    while not stop_event.is_set():
        n += 1
        uid = f'w{worker_id}u{n}'
        try:
            users.create(properties={
                'uid': uid,
                'cn': uid,
                'sn': uid,
                'uidNumber': str(10000 + worker_id * 10000 + n),
                'gidNumber': '1000',
                'homeDirectory': f'/home/{uid}',
            })
        except (ldap.ALREADY_EXISTS, ldap.LDAPError):
            pass
        except ldap.SERVER_DOWN:
            error_list.append(f'SERVER_DOWN during add on {inst.serverid}')
            return


def _search_worker(inst, suffix, stop_event, error_list):
    """Continuously searches entries to walk the DN hash chains."""
    users = UserAccounts(inst, suffix, rdn=f'ou={CONTAINER_OU}')
    while not stop_event.is_set():
        try:
            users.filter('(uid=w*)')
        except ldap.SERVER_DOWN:
            error_list.append(f'SERVER_DOWN during search on {inst.serverid}')
            return
        except ldap.LDAPError:
            pass

        try:
            users.filter('(uid=x*)')
        except ldap.SERVER_DOWN:
            error_list.append(f'SERVER_DOWN during search on {inst.serverid}')
            return
        except ldap.LDAPError:
            pass


def _delete_worker(inst, suffix, stop_event, error_list):
    """Finds and deletes plugin-renamed entries (uid=x*).
    """
    users = UserAccounts(inst, suffix, rdn=f'ou={CONTAINER_OU}')
    while not stop_event.is_set():
        try:
            results = users.filter('(uid=x*)')
            for entry in results[:50]:
                try:
                    entry.delete()
                except (ldap.NO_SUCH_OBJECT, ldap.LDAPError):
                    pass
                except ldap.SERVER_DOWN:
                    error_list.append(
                        f'SERVER_DOWN during delete on {inst.serverid}')
                    return
        except ldap.SERVER_DOWN:
            error_list.append(
                f'SERVER_DOWN during delete-search on {inst.serverid}')
            return
        except ldap.LDAPError:
            pass
        time.sleep(0.1)


@pytest.fixture(scope="function")
def setup_plugin(topology_m2, request):
    """Compile, install and load the reproducer plugin on both suppliers."""
    S1 = topology_m2.ms["supplier1"]
    S2 = topology_m2.ms["supplier2"]

    tmp_dir = f'/tmp/dn_hash_corruption_test_{os.getpid()}'
    os.makedirs(tmp_dir, exist_ok=True)

    log.info("Compiling and installing reproducer plugin")
    for inst in [S1, S2]:
        _compile_and_install_plugin(inst, tmp_dir)

    log.info("Shrinking entry cache")
    for inst in [S1, S2]:
        _shrink_entry_cache(inst)

    log.info(f"Creating test container ou={CONTAINER_OU}")
    ous = OrganizationalUnits(S1, DEFAULT_SUFFIX)
    ous.create(properties={'ou': CONTAINER_OU})

    log.info(f"Creating {NUM_SEED} seed entries")
    users = UserAccounts(S1, DEFAULT_SUFFIX, rdn=f'ou={CONTAINER_OU}')
    for i in range(1, NUM_SEED + 1):
        uid = f'seed{i}'
        users.create(properties={
            'uid': uid,
            'cn': uid,
            'sn': uid,
            'uidNumber': str(i),
            'gidNumber': '1000',
            'homeDirectory': f'/home/{uid}',
        })

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(S1, S2, timeout=300)

    log.info("Loading reproducer plugin and restarting")
    for inst in [S1, S2]:
        _load_plugin(inst)
        inst.restart()
        time.sleep(2)

    for inst in [S1, S2]:
        plugin = DSLdapObject(inst, PLUGIN_DN)
        assert plugin.exists(), f"Plugin not found on {inst.serverid}"
        log.info(f"Plugin loaded on {inst.serverid}")

    def fin():
        log.info("Cleaning up plugin test...")
        if os.path.isdir(tmp_dir):
            shutil.rmtree(tmp_dir)
        for inst in [S1, S2]:
            inst.restart()

    request.addfinalizer(fin)
    return S1, S2


def test_entrycache_dn_hash_corruption(setup_plugin):
    """Test that entry cache DN hash corruption via in-place DN change
    does not crash the server.

    :id: 7d1c0af5-1f0f-454a-8114-ea419a93a410
    :setup: Two supplier replication topology with reproducer plugin
    :steps:
        1. Run concurrent add/search/delete workload for WORKLOAD_DURATION seconds
        2. Verify both servers survived without crashing
    :expectedresults:
        1. Workload runs without SERVER_DOWN errors
        2. Both servers are alive and responding
    """
    S1, S2 = setup_plugin

    log.info(f"Starting concurrent workload for {WORKLOAD_DURATION}s")
    stop_event = threading.Event()
    errors = []
    threads = []

    for inst in [S1, S2]:
        for w in range(1, NUM_ADD_WORKERS + 1):
            t = threading.Thread(
                target=_add_worker,
                args=(inst, DEFAULT_SUFFIX, w, stop_event, errors),
                daemon=True)
            t.start()
            threads.append(t)

        for _ in range(NUM_SEARCH_WORKERS):
            t = threading.Thread(
                target=_search_worker,
                args=(inst, DEFAULT_SUFFIX, stop_event, errors),
                daemon=True)
            t.start()
            threads.append(t)

        for _ in range(NUM_DELETE_WORKERS):
            t = threading.Thread(
                target=_delete_worker,
                args=(inst, DEFAULT_SUFFIX, stop_event, errors),
                daemon=True)
            t.start()
            threads.append(t)

    log.info(f"Started {len(threads)} worker threads")

    check_interval = 5
    elapsed = 0
    while elapsed < WORKLOAD_DURATION:
        time.sleep(check_interval)
        elapsed += check_interval

        if errors:
            log.error(f"SERVER_DOWN detected after ~{elapsed}s: {errors}")
            break

        for inst in [S1, S2]:
            try:
                inst.rootdse.get_attr_val_utf8('supportedLDAPVersion')
            except ldap.SERVER_DOWN:
                errors.append(f'{inst.serverid} crashed after ~{elapsed}s')
                break
            except ldap.LDAPError:
                pass
        if errors:
            break

        if elapsed % 30 == 0:
            log.info(f"  {elapsed}s elapsed - servers still running...")

    stop_event.set()
    for t in threads:
        t.join(timeout=10)

    assert not errors, (f"Server crashed during workload! Errors: {errors}")

    for inst in [S1, S2]:
        assert inst.status(), f"{inst.serverid} is not responding"
        log.info(f"{inst.serverid} survived the workload")

    log.info(f"Test PASSED: Both servers survived {WORKLOAD_DURATION}s workload")
