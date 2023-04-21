# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

import pytest, os

import logging
import ldap
from lib389.backend import Backends, Backend
from lib389.mappingTree import MappingTrees
from lib389.configurations.sample import create_base_domain
from  ldap.extop import ExtendedRequest
from pyasn1.type import namedtype, univ
from pyasn1.codec.ber import encoder, decoder
from lib389.utils import ensure_bytes, get_plugin_dir
from ldap.extop import ExtendedRequest, ExtendedResponse
from pyasn1.type import namedtype, univ
from pyasn1.codec.ber import encoder, decoder
from lib389 import Entry

from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.topologies import topology_st as topo

from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Accounts

pytestmark = pytest.mark.tier0
log = logging.getLogger(__name__)


class SlapiMemberofRequestValue(univ.Sequence):
    pass

class SlapiMemberofRequest(ExtendedRequest):
    def __init__(self, requestValidLifeTime=0):
        self.requestName = '2.3.4.5.113730.6.7.1'

    def encodedRequestValue(self):
        v = SlapiMemberofRequestValue()
        return encoder.encode(v)

@pytest.fixture(scope="module")
def install_test_plugin(topo):
    import subprocess
    import shutil
    import os
    import sys
    import re
    import pdb
    import random

    current_dir = os.getcwd()
    # create a build directory
    build_dir="/tmp/build.%d" % (random.randint(1, 10000))
    os.makedirs(build_dir, exist_ok=True)
    cmd_str="chmod 755 %s" % build_dir
    subprocess.run(cmd_str, shell=True)
    os.chdir(build_dir)

    # Retrieve the path of the workspace (from the path of the test)
    workspace = None
    for i in range(0, len(sys.argv)):
        if sys.argv[i].find("slapi_memberof") > -1:
            log.info("Workspace is: %s" % sys.argv[i])
            workspace=re.sub("dirsrvtest.*$", "", sys.argv[i])
        else:
            log.info("Workspace is not: %s" % sys.argv[i])

    if not workspace:
        log.info("Fail to Retrieve from the repos containing slapi_memberof test plugin source")
        log.info("using the current directory as workspace")
        workspace=current_dir

    # Gather the include files from 'ldap/include' and 'ldap/servers/slapd'
    for the_include in ["portable.h", "avl.h", "ldaprot.h"]:
        include_file="%s/ldap/include/%s" % (workspace, the_include)
        log.info("Retrieved from the repos: %s" % include_file)
        file_path="%s/%s" % (build_dir, the_include)
        shutil.copy(include_file, file_path)
        cmd_str="chmod 666 %s" %  file_path
        subprocess.run(cmd_str, shell=True)

    for the_include in ["slap.h", "slapi-private.h", "slapi-plugin.h",\
                        "slapi_pal.h", "csngen.h", "uuid.h", "disconnect_errors.h",\
                        "pw.h", "filter.h", "proto-slap.h", "intrinsics.h", "slapi-plugin-compat4.h"]:
        include_file="%s/ldap/servers/slapd/%s" % (workspace, the_include)
        log.info("Retrieve from the repos: %s" % include_file)
        file_path="%s/%s" % (build_dir, the_include)
        shutil.copy(include_file, file_path)
        cmd_str="chmod 666 %s" %  file_path
        subprocess.run(cmd_str, shell=True)

    # retrieve the test plugin source
    log.info("use the default location")
    src_file="%s/ldap/servers/slapd/test-plugins/test_slapi_memberof.c" % (workspace)
    dst_file="%s/%s" % (build_dir, "test_slapi_memberof.c")
    log.info("Retrieve from the repos: %s" % src_file)
    shutil.copy(src_file, dst_file)
    cmd_str="chmod 666 %s" %  dst_file
    subprocess.run(cmd_str, shell=True)
    test_plugin_location=dst_file

    #
    # If needed (if PR not pushed yet) to craft slapi-plugin.h
    #
    file_path_old = "%s/slapi-plugin.h" % (build_dir)
    file_path_new = "%s/slapi-plugin.h.new" % (build_dir)
    slapi_plugin_old = open(file_path_old)

    # before crafting check if slapi_memberof defs are present
    need_to_craft = True
    for line in slapi_plugin_old:
        if "Slapi_MemberOfConfig" in line:
            need_to_craft = False
            break

    if need_to_craft:
        log.info("Need to craft slapi-plugin.h")
        slapi_plugin_old.seek(0, 0)
        slapi_plugin_new = open(file_path_new, "w")

        # definitions that were missing, add them
        struct_slapi_memberof = """

#include <plhash.h>
typedef enum {
    MEMBEROF_REUSE_ONLY,
    MEMBEROF_REUSE_IF_POSSIBLE,
    MEMBEROF_RECOMPUTE
} memberof_flag_t;

typedef struct _slapi_memberofresult {
    Slapi_ValueSet *nsuniqueid_vals;
    Slapi_ValueSet *dn_vals;
    PRBool maxgroups_reached; /* flag is true if the number of groups hit the max limit */
} Slapi_MemberOfResult;

typedef struct _slapi_memberofconfig
{
    char **groupattrs;
    PRBool subtree_search;
    int allBackends;
    Slapi_DN **entryScopes;
    Slapi_DN **entryScopeExcludeSubtrees;
    PRBool recurse;
    int maxgroups;
    memberof_flag_t flag;
    char *error_msg;
    int errot_msg_lenght;
    int entryScopeCount;          /* private to slapi_memberof */
    int entryExcludeScopeCount;   /* private to slapi_memberof */
    PRBool maxgroups_reached;     /* private to slapi_memberof */
    const char *memberof_attr;    /* private to slapi_memberof */
    Slapi_Attr *dn_syntax_attr;   /* private to slapi_memberof */
    PLHashTable *ancestors_cache; /* private to slapi_memberof */
    int current_maxgroup;         /* private to slapi_memberof */
} Slapi_MemberOfConfig;

"""
        for line in slapi_plugin_old:
            if re.search(r"^#endif.*SLAPIPLUGIN_H_.*$", line):
                slapi_plugin_new.write(struct_slapi_memberof)
                slapi_plugin_new.write("\n")
            slapi_plugin_new.write(line)

        slapi_plugin_old.close()
        slapi_plugin_new.close()
        os.remove(file_path_old)
        shutil.move(file_path_new, file_path_old)

    #
    # If needed (if PR not pushed yet) to craft slapi-private.h
    #
    file_path_old = "%s/slapi-private.h" % (build_dir)
    file_path_new = "%s/slapi-private.h.new" % (build_dir)
    slapi_private_old = open(file_path_old)

    # before crafting check if slapi_memberof defs are present
    need_to_craft = True
    for line in slapi_private_old:
        if "slapi_memberof" in line:
            need_to_craft = False
            break

    if need_to_craft:
        log.info("Need to craft slapi-private.h")
        slapi_private_old.seek(0, 0)
        slapi_private_new = open(file_path_new, "w")

        # definitions that were missing, add them
        struct_slapi_memberof = """
int slapi_memberof(Slapi_MemberOfConfig *config, Slapi_DN *member_sdn, Slapi_MemberOfResult *result);
void slapi_memberof_free_memberof_plugin_config();
int slapi_memberof_load_memberof_plugin_config();

"""

        for line in slapi_private_old:
            if re.search(r"^void dup_ldif_line.*$", line):
                slapi_private_new.write(struct_slapi_memberof)
                slapi_private_new.write("\n")
            slapi_private_new.write(line)

        slapi_private_old.close()
        slapi_private_new.close()
        os.remove(file_path_old)
        shutil.move(file_path_new, file_path_old)

    # build the plugin into a shared library
    test_plugin_object="%s/test_slapi_memberof.o" % build_dir
    test_plugin_sharedlib="%s/libtest_slapi_memberof-plugin.so" % build_dir
    cmd_str="/usr/bin/gcc -I./ldap/include -I./ldap/servers/slapd -I./include -I. -I/usr/include -I/usr/include/nss3 -I%s -I/usr/include/nspr4 -g -O2 -Wall -c %s -fPIC -DPIC -o %s" % (build_dir, test_plugin_location, test_plugin_object)
    subprocess.run(cmd_str, shell=True)
    cmd_str="/usr/bin/gcc -shared  -fPIC -DPIC  %s  -Wl,-rpath -Wl,/usr/lib64/dirsrv -L/usr/lib64/dirsrv/ /usr/lib64/dirsrv/libslapd.so.0 -lldap -llber -lc -Wl,-z,now -g -O2 -O2 -m64 -Wl,-z -Wl,relro -Wl,--as-needed -Wl,-z -Wl,now   -Wl,-soname -Wl,libtest_slapi_memberof-plugin.so -o %s" % (test_plugin_object, test_plugin_sharedlib)
    subprocess.run(cmd_str, shell=True)

    # install the test plugin
    cmd_str="chmod 755 %s" % test_plugin_sharedlib
    subprocess.run(cmd_str, shell=True)
    shutil.copy(test_plugin_sharedlib, topo.standalone.get_plugin_dir())


def _check_res_vs_expected(msg, res, expected):
    log.info("Checking %s expecting %d entries" % (msg, len(expected)))
    assert len(expected) == len(res)
    expected_str_lower = []
    for i in expected:
        expected_str_lower.append(str(i).lower())

    res_str_lower = []
    for i in res:
        res_str_lower.append(str(i).lower())

    for i in expected_str_lower:
        log.info("Check that %s is present" % (i))
        assert i in res_str_lower

EMPTY_RESULT="no error msg"

def _extop_test_slapi_member(server, dn, relation):
    value = univ.OctetString(dn)
    value_encoded = encoder.encode(value)

    extop = ExtendedRequest(requestName = '2.3.4.5.113730.6.7.1', requestValue=value_encoded)
    (oid_response, res) = server.extop_s(extop)
    d1, d2 = decoder.decode(res)
    log.info("The entries refering to %s as %s are:" % (dn, relation))
    for i in d1:
        log.info(" - %s" % i)
    return d1


def replace_manager(server, dn, managers):
    mod = [(ldap.MOD_REPLACE, 'manager', managers)]
    server.modify_s(dn, mod)

def add_entry(server, uid, manager=None, subtree=None):
    if (subtree):
        dn = 'uid=%s,ou=%s,ou=People,%s' % (uid, subtree, DEFAULT_SUFFIX)
    else:
        dn = 'uid=%s,ou=People,%s' % (uid, DEFAULT_SUFFIX)
    server.add_s(Entry((dn, {'objectclass': 'top person extensibleObject'.split(),
                             'uid': uid,
                             'cn':  uid,
                             'sn': uid})))
    if manager:
        replace_manager(server, dn, manager)
    return dn

def test_slapi_memberof_simple(topo, request, install_test_plugin):
    """
    Test that management hierarchy (manager) is computed with slapi_member
    with following parameters
    - membership attribute: 'manager'
    - span over all backends: 'on'
    - skip nesting membership: 'off'
    - computation mode: recompute
    - Scope: DEFAULT_SUFFIX
    - ExcludeScope: None
    - Maximum return entries: None

    :id: 4c2595eb-a947-4c0b-996c-e499db67d11a
    :setup: Standalone instance
    :steps:
        1. provision a set of entry
        2. configure test_slapi_memberof as described above
        3. check computed membership vs expected result
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed

    DIT is :
    e_1_parent_0
    - e_1_parent_1_0
    -- e_1_parent_1_1_0
    --- e_1_parent_1_1_1_0
    --- e_2_parent_1_1_1_0
    --- e_3_parent_1_1_1_0
    --- e_4_parent_1_1_1_0
    --- e_5_parent_1_1_1_0
    -- e_2_parent_1_1_0
    - e_2_parent_1_0
    -- e_1_parent_2_1_0
    -- e_2_parent_2_1_0
    --- e_1_parent_2_2_1_0
    -- e_3_parent_2_1_0
    -- e_4_parent_2_1_0
    e_2_parent_0
    - e_1_parent_2_0
    - e_2_parent_2_0
    - e_3_parent_2_0
    - e_4_parent_2_0
    e_3_parent_0
    - e_1_parent_3_0
    -- e_1_parent_1_3_0
    --- e_1_parent_1_1_3_0
    ---- e_1_parent_1_1_1_3_0
    """
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    # First subtree
    e_1_parent_0 = add_entry(topo.standalone, uid="e_1_parent_0")

    e_1_parent_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_1_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_2_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_3_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_3_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_4_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_4_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_5_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_5_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])

    e_2_parent_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_2_parent_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_2_parent_2_1_0 = add_entry(topo.standalone, uid="e_2_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_1_parent_2_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_2_1_0", manager=[ensure_bytes(e_2_parent_2_1_0)])
    e_3_parent_2_1_0 = add_entry(topo.standalone, uid="e_3_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_4_parent_2_1_0 = add_entry(topo.standalone, uid="e_4_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])

    # 2nd subtree
    e_2_parent_0 = add_entry(topo.standalone, uid="e_2_parent_0")

    e_1_parent_2_0 = add_entry(topo.standalone, uid="e_1_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_2_parent_2_0 = add_entry(topo.standalone, uid="e_2_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_3_parent_2_0 = add_entry(topo.standalone, uid="e_3_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_4_parent_2_0 = add_entry(topo.standalone, uid="e_4_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])

    # third subtree
    e_3_parent_0 = add_entry(topo.standalone, uid="e_3_parent_0")

    e_1_parent_3_0 = add_entry(topo.standalone, uid="e_1_parent_3_0", manager=[ensure_bytes(e_3_parent_0)])

    e_1_parent_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_3_0", manager=[ensure_bytes(e_1_parent_3_0)])

    e_1_parent_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_3_0)])

    e_1_parent_1_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_1_3_0)])

    dn_config = 'cn=test_slapi_memberof,cn=plugins,cn=config'
    topo.standalone.add_s(Entry((dn_config, {'objectclass': 'top nsSlapdPlugin extensibleObject'.split(),
                             'cn': 'test_slapi_memberof',
                             'nsslapd-pluginPath': 'libtest_slapi_memberof-plugin',
                             'nsslapd-pluginInitfunc': 'test_slapi_memberof_init',
                             'nsslapd-pluginType': 'extendedop',
                             'nsslapd-pluginEnabled': 'on',
                             'nsslapd-plugin-depends-on-type': 'database',
                             'nsslapd-pluginId': 'test_slapi_memberof-plugin',
                             'slapimemberOfMemberDN': 'uid=test_user_11,ou=People,dc=example,dc=com',
                             'slapimemberOfGroupAttr': 'manager',
                             'slapimemberOfAttr': 'memberof',
                             'slapimemberOfAllBackends': 'on',
                             'slapimemberOfSkipNested': 'off',
                             'slapimemberOfEntryScope': DEFAULT_SUFFIX,
                             'slapimemberOfMaxGroup': '0',
                             'nsslapd-pluginVersion': '2.3.2.202302131418git0e190fc3d',
                             'nsslapd-pluginVendor': '389 Project',
                             'nsslapd-pluginDescription': 'test_slapi_memberof extended operation plugin'})))
    topo.standalone.restart()

    # Check the first subtree
    expected = [ e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_0, relation="manager")
    _check_res_vs_expected("first subtree", res, expected)

    # Check the second subtree
    expected = [e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_0, relation="manager")
    _check_res_vs_expected("second subtree", res, expected)

    # Check the third subtree
    expected = [e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_3_parent_0, relation="manager")
    _check_res_vs_expected("third subtree", res, expected)

    # check e_1_parent_1_0
    expected = [e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_0", res, expected)

    # check e_1_parent_1_1_0
    expected = [e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_0", res, expected)

    # check e_2_parent_1_1_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_1_0", res, expected)

    # check e_2_parent_1_0
    expected = [e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_0", res, expected)

    # check e_2_parent_2_1_0
    expected = [e_1_parent_2_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_2_1_0", res, expected)

    # Check e_1_parent_3_0
    expected = [e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_3_0", res, expected)

    # Check e_1_parent_1_3_0
    expected = [e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_3_0", res, expected)

    # Check e_1_parent_1_1_3_0
    expected = [e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_3_0", res, expected)

    # Check e_1_parent_1_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_1_3_0", res, expected)

    def fin():
        entries = [e_1_parent_0, e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0, e_2_parent_0, e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0, e_3_parent_0, e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
        for entry in entries:
            topo.standalone.delete_s(entry)
        topo.standalone.delete_s(dn_config)

    request.addfinalizer(fin)

def test_slapi_memberof_allbackends_on(topo, request, install_test_plugin):
    """
    Test that management hierarchy (manager) is computed with slapi_member
    It exists several backends and manager relationship cross those backends
    with following parameters
    - membership attribute: 'manager'
    - span over all backends: 'on'  <----
    - skip nesting membership: 'off'
    - computation mode: recompute
    - Scope: DEFAULT_SUFFIX
    - ExcludeScope: None
    - Maximum return entries: None

    :id: 910c43a0-04ae-48f1-9e3c-6d97ba5bcb71
    :setup: Standalone instance
    :steps:
        1. create a second backend with foo_bar entry
        2. provision a set of entries in default backend with foo_bar being
           manager of entry e_1_parent_1_1_1_3_0 that is in default backend
        3. configure test_slapi_memberof as described above
        4. check computed membership vs expected result
           slapi_memberof(foo_bar, "manager") -> e_1_parent_1_1_1_3_0
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed
        4. Operation should  succeed

    DIT is :
    e_1_parent_0
    - e_1_parent_1_0
    -- e_1_parent_1_1_0
    --- e_1_parent_1_1_1_0
    --- e_2_parent_1_1_1_0
    --- e_3_parent_1_1_1_0
    --- e_4_parent_1_1_1_0
    --- e_5_parent_1_1_1_0
    -- e_2_parent_1_1_0
    - e_2_parent_1_0
    -- e_1_parent_2_1_0
    -- e_2_parent_2_1_0
    --- e_1_parent_2_2_1_0
    -- e_3_parent_2_1_0
    -- e_4_parent_2_1_0
    e_2_parent_0
    - e_1_parent_2_0
    - e_2_parent_2_0
    - e_3_parent_2_0
    - e_4_parent_2_0
    e_3_parent_0
    - e_1_parent_3_0
    -- e_1_parent_1_3_0
    --- e_1_parent_1_1_3_0
    ---- e_1_parent_1_1_1_3_0
    """
    # create a second backend
    second_suffix='dc=foo,dc=bar'
    be_name='fooBar'
    be1 = Backend(topo.standalone)
    be1.create(properties={
            'cn': be_name,
            'nsslapd-suffix': second_suffix,
        },
    )
    # Create the domain entry
    create_base_domain(topo.standalone, second_suffix)
    rdn='foo_bar'
    dn_entry_foo_bar='uid=%s,%s' % (rdn, second_suffix)
    topo.standalone.add_s(Entry((dn_entry_foo_bar, {'objectclass': 'top person extensibleObject'.split(),
                             'uid': rdn,
                             'cn':  rdn,
                             'sn': rdn})))

    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    # First subtree
    e_1_parent_0 = add_entry(topo.standalone, uid="e_1_parent_0")

    e_1_parent_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_1_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_2_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_3_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_3_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_4_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_4_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_5_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_5_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])

    e_2_parent_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_2_parent_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_2_parent_2_1_0 = add_entry(topo.standalone, uid="e_2_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_1_parent_2_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_2_1_0", manager=[ensure_bytes(e_2_parent_2_1_0)])
    e_3_parent_2_1_0 = add_entry(topo.standalone, uid="e_3_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_4_parent_2_1_0 = add_entry(topo.standalone, uid="e_4_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])

    # 2nd subtree
    e_2_parent_0 = add_entry(topo.standalone, uid="e_2_parent_0")

    e_1_parent_2_0 = add_entry(topo.standalone, uid="e_1_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_2_parent_2_0 = add_entry(topo.standalone, uid="e_2_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_3_parent_2_0 = add_entry(topo.standalone, uid="e_3_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_4_parent_2_0 = add_entry(topo.standalone, uid="e_4_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])

    # third subtree
    e_3_parent_0 = add_entry(topo.standalone, uid="e_3_parent_0")

    e_1_parent_3_0 = add_entry(topo.standalone, uid="e_1_parent_3_0", manager=[ensure_bytes(e_3_parent_0)])

    e_1_parent_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_3_0", manager=[ensure_bytes(e_1_parent_3_0)])

    e_1_parent_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_3_0)])

    e_1_parent_1_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_1_3_0)])

    # make foo_bar entry manager of e_1_parent_1_1_1_3_0
    replace_manager(topo.standalone, e_1_parent_1_1_1_3_0, [ensure_bytes(dn_entry_foo_bar)])

    dn_config = 'cn=test_slapi_memberof,cn=plugins,cn=config'
    topo.standalone.add_s(Entry((dn_config, {'objectclass': 'top nsSlapdPlugin extensibleObject'.split(),
                             'cn': 'test_slapi_memberof',
                             'nsslapd-pluginPath': 'libtest_slapi_memberof-plugin',
                             'nsslapd-pluginInitfunc': 'test_slapi_memberof_init',
                             'nsslapd-pluginType': 'extendedop',
                             'nsslapd-pluginEnabled': 'on',
                             'nsslapd-plugin-depends-on-type': 'database',
                             'nsslapd-pluginId': 'test_slapi_memberof-plugin',
                             'slapimemberOfMemberDN': 'uid=test_user_11,ou=People,dc=example,dc=com',
                             'slapimemberOfGroupAttr': 'manager',
                             'slapimemberOfAttr': 'memberof',
                             'slapimemberOfAllBackends': 'on',
                             'slapimemberOfSkipNested': 'off',
                             'slapimemberOfEntryScope': [DEFAULT_SUFFIX, second_suffix],
                             'slapimemberOfMaxGroup': '0',
                             'nsslapd-pluginVersion': '2.3.2.202302131418git0e190fc3d',
                             'nsslapd-pluginVendor': '389 Project',
                             'nsslapd-pluginDescription': 'test_slapi_memberof extended operation plugin'})))
    topo.standalone.restart()

    # Check the first subtree
    expected = [ e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_0, relation="manager")
    _check_res_vs_expected("first subtree", res, expected)

    # Check the second subtree
    expected = [e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_0, relation="manager")
    _check_res_vs_expected("second subtree", res, expected)

    # Check the third subtree
    expected = [e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_3_parent_0, relation="manager")
    _check_res_vs_expected("third subtree", res, expected)

    # check e_1_parent_1_0
    expected = [e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_0", res, expected)

    # check e_1_parent_1_1_0
    expected = [e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_0", res, expected)

    # check e_2_parent_1_1_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_1_0", res, expected)

    # check e_2_parent_1_0
    expected = [e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_0", res, expected)

    # check e_2_parent_2_1_0
    expected = [e_1_parent_2_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_2_1_0", res, expected)

    # Check e_1_parent_3_0
    expected = [e_1_parent_1_3_0, e_1_parent_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_3_0", res, expected)

    # Check e_1_parent_1_3_0
    expected = [e_1_parent_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_3_0", res, expected)

    # Check e_1_parent_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_3_0", res, expected)

    # Check e_1_parent_1_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_1_3_0", res, expected)

    # Check dn_entry_foo_bar
    expected = [e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=dn_entry_foo_bar, relation="manager")
    _check_res_vs_expected("organisation reporting to dn_entry_foo_bar", res, expected)


    def fin():
        entries = [e_1_parent_0, e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0, e_2_parent_0, e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0, e_3_parent_0, e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
        for entry in entries:
            topo.standalone.delete_s(entry)
        topo.standalone.delete_s(dn_config)
        topo.standalone.delete_s(dn_entry_foo_bar)
        be1.delete()

    request.addfinalizer(fin)

def test_slapi_memberof_allbackends_off(topo, request, install_test_plugin):
    """
    Test that management hierarchy (manager) is computed with slapi_member
    It exists several backends and manager relationship cross those backends
    with following parameters
    - membership attribute: 'manager'
    - span over all backends: 'off'  <----
    - skip nesting membership: 'off'
    - computation mode: recompute
    - Scope: DEFAULT_SUFFIX
    - ExcludeScope: None
    - Maximum return entries: None

    :id: 56fb0c16-8086-429b-adf0-fff0eb8e121e
    :setup: Standalone instance
    :steps:
        1. create a second backend with foo_bar entry
        2. provision a set of entries in default backend with foo_bar being
           manager of entry e_1_parent_1_1_1_3_0 that is in default backend
        3. configure test_slapi_memberof as described above
        4. check computed membership vs expected result
           slapi_memberof(foo_bar, "manager") NOT -> e_1_parent_1_1_1_3_0
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed
        4. Operation should  succeed

    DIT is :
    e_1_parent_0
    - e_1_parent_1_0
    -- e_1_parent_1_1_0
    --- e_1_parent_1_1_1_0
    --- e_2_parent_1_1_1_0
    --- e_3_parent_1_1_1_0
    --- e_4_parent_1_1_1_0
    --- e_5_parent_1_1_1_0
    -- e_2_parent_1_1_0
    - e_2_parent_1_0
    -- e_1_parent_2_1_0
    -- e_2_parent_2_1_0
    --- e_1_parent_2_2_1_0
    -- e_3_parent_2_1_0
    -- e_4_parent_2_1_0
    e_2_parent_0
    - e_1_parent_2_0
    - e_2_parent_2_0
    - e_3_parent_2_0
    - e_4_parent_2_0
    e_3_parent_0
    - e_1_parent_3_0
    -- e_1_parent_1_3_0
    --- e_1_parent_1_1_3_0
    ---- e_1_parent_1_1_1_3_0
    """
    # Create second backend
    second_suffix='dc=foo,dc=bar'
    be_name='fooBar'
    be1 = Backend(topo.standalone)
    be1.create(properties={
            'cn': be_name,
            'nsslapd-suffix': second_suffix,
        },
    )
    # Create the domain entry
    create_base_domain(topo.standalone, second_suffix)
    rdn='foo_bar'
    dn_entry_foo_bar='uid=%s,%s' % (rdn, second_suffix)
    topo.standalone.add_s(Entry((dn_entry_foo_bar, {'objectclass': 'top person extensibleObject'.split(),
                             'uid': rdn,
                             'cn':  rdn,
                             'sn': rdn})))

    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    # First subtree
    e_1_parent_0 = add_entry(topo.standalone, uid="e_1_parent_0")

    e_1_parent_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_1_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_2_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_3_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_3_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_4_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_4_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_5_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_5_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])

    e_2_parent_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_2_parent_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_2_parent_2_1_0 = add_entry(topo.standalone, uid="e_2_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_1_parent_2_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_2_1_0", manager=[ensure_bytes(e_2_parent_2_1_0)])
    e_3_parent_2_1_0 = add_entry(topo.standalone, uid="e_3_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_4_parent_2_1_0 = add_entry(topo.standalone, uid="e_4_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])

    # 2nd subtree
    e_2_parent_0 = add_entry(topo.standalone, uid="e_2_parent_0")

    e_1_parent_2_0 = add_entry(topo.standalone, uid="e_1_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_2_parent_2_0 = add_entry(topo.standalone, uid="e_2_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_3_parent_2_0 = add_entry(topo.standalone, uid="e_3_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_4_parent_2_0 = add_entry(topo.standalone, uid="e_4_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])

    # third subtree
    e_3_parent_0 = add_entry(topo.standalone, uid="e_3_parent_0")

    e_1_parent_3_0 = add_entry(topo.standalone, uid="e_1_parent_3_0", manager=[ensure_bytes(e_3_parent_0)])

    e_1_parent_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_3_0", manager=[ensure_bytes(e_1_parent_3_0)])

    e_1_parent_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_3_0)])

    e_1_parent_1_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_1_3_0)])

    # make foo_bar entry manager of e_1_parent_1_1_1_3_0
    replace_manager(topo.standalone, e_1_parent_1_1_1_3_0, [ensure_bytes(dn_entry_foo_bar)])

    dn_config = 'cn=test_slapi_memberof,cn=plugins,cn=config'
    topo.standalone.add_s(Entry((dn_config, {'objectclass': 'top nsSlapdPlugin extensibleObject'.split(),
                             'cn': 'test_slapi_memberof',
                             'nsslapd-pluginPath': 'libtest_slapi_memberof-plugin',
                             'nsslapd-pluginInitfunc': 'test_slapi_memberof_init',
                             'nsslapd-pluginType': 'extendedop',
                             'nsslapd-pluginEnabled': 'on',
                             'nsslapd-plugin-depends-on-type': 'database',
                             'nsslapd-pluginId': 'test_slapi_memberof-plugin',
                             'slapimemberOfMemberDN': 'uid=test_user_11,ou=People,dc=example,dc=com',
                             'slapimemberOfGroupAttr': 'manager',
                             'slapimemberOfAttr': 'memberof',
                             'slapimemberOfAllBackends': 'off',
                             'slapimemberOfSkipNested': 'off',
                             'slapimemberOfEntryScope': [DEFAULT_SUFFIX, second_suffix],
                             'slapimemberOfMaxGroup': '0',
                             'nsslapd-pluginVersion': '2.3.2.202302131418git0e190fc3d',
                             'nsslapd-pluginVendor': '389 Project',
                             'nsslapd-pluginDescription': 'test_slapi_memberof extended operation plugin'})))
    topo.standalone.restart()

    # Check the first subtree
    expected = [ e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_0, relation="manager")
    _check_res_vs_expected("first subtree", res, expected)

    # Check the second subtree
    expected = [e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_0, relation="manager")
    _check_res_vs_expected("second subtree", res, expected)

    # Check the third subtree
    expected = [e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_3_parent_0, relation="manager")
    _check_res_vs_expected("third subtree", res, expected)

    # check e_1_parent_1_0
    expected = [e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_0", res, expected)

    # check e_1_parent_1_1_0
    expected = [e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_0", res, expected)

    # check e_2_parent_1_1_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_1_0", res, expected)

    # check e_2_parent_1_0
    expected = [e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_0", res, expected)

    # check e_2_parent_2_1_0
    expected = [e_1_parent_2_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_2_1_0", res, expected)

    # Check e_1_parent_3_0
    expected = [e_1_parent_1_3_0, e_1_parent_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_3_0", res, expected)

    # Check e_1_parent_1_3_0
    expected = [e_1_parent_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_3_0", res, expected)

    # Check e_1_parent_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_3_0", res, expected)

    # Check e_1_parent_1_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_1_3_0", res, expected)

    # Check dn_entry_foo_bar is not manager of e_1_parent_1_1_1_3_0 because slapimemberOfAllBackends=off
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=dn_entry_foo_bar, relation="manager")
    _check_res_vs_expected("organisation reporting to dn_entry_foo_bar", res, expected)


    def fin():
        entries = [e_1_parent_0, e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0, e_2_parent_0, e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0, e_3_parent_0, e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
        for entry in entries:
            topo.standalone.delete_s(entry)
        topo.standalone.delete_s(dn_config)
        topo.standalone.delete_s(dn_entry_foo_bar)
        be1.delete()

    request.addfinalizer(fin)


def test_slapi_memberof_memberattr(topo, request, install_test_plugin):
    """
    Test that membership hierarchy (member) is computed with slapi_member
    the membership is done with 'manager' attribute but slapi_memberof
    called with 'member' attribute. As there is no 'member' then
    membership returns empty_results
    with following parameters
    - membership attribute: 'member'  <----
    - span over all backends: 'on'
    - skip nesting membership: 'off'
    - computation mode: recompute
    - Scope: DEFAULT_SUFFIX
    - ExcludeScope: None
    - Maximum return entries: None

    :id: 373f7f65-185f-4b06-a0a5-3e23692b87f1
    :setup: Standalone instance
    :steps:
        1. provision a set of entries in default backend
           with membership using 'manager'
        2. configure test_slapi_memberof as described above
           so checking membership using 'member'
        3. check computed membership vs expected result
           all empty_result because no entry has 'member'
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed

    DIT is :
    e_1_parent_0
    - e_1_parent_1_0
    -- e_1_parent_1_1_0
    --- e_1_parent_1_1_1_0
    --- e_2_parent_1_1_1_0
    --- e_3_parent_1_1_1_0
    --- e_4_parent_1_1_1_0
    --- e_5_parent_1_1_1_0
    -- e_2_parent_1_1_0
    - e_2_parent_1_0
    -- e_1_parent_2_1_0
    -- e_2_parent_2_1_0
    --- e_1_parent_2_2_1_0
    -- e_3_parent_2_1_0
    -- e_4_parent_2_1_0
    e_2_parent_0
    - e_1_parent_2_0
    - e_2_parent_2_0
    - e_3_parent_2_0
    - e_4_parent_2_0
    e_3_parent_0
    - e_1_parent_3_0
    -- e_1_parent_1_3_0
    --- e_1_parent_1_1_3_0
    ---- e_1_parent_1_1_1_3_0
    """
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    # First subtree
    e_1_parent_0 = add_entry(topo.standalone, uid="e_1_parent_0")

    e_1_parent_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_1_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_2_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_3_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_3_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_4_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_4_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_5_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_5_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])

    e_2_parent_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_2_parent_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_2_parent_2_1_0 = add_entry(topo.standalone, uid="e_2_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_1_parent_2_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_2_1_0", manager=[ensure_bytes(e_2_parent_2_1_0)])
    e_3_parent_2_1_0 = add_entry(topo.standalone, uid="e_3_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_4_parent_2_1_0 = add_entry(topo.standalone, uid="e_4_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])

    # 2nd subtree
    e_2_parent_0 = add_entry(topo.standalone, uid="e_2_parent_0")

    e_1_parent_2_0 = add_entry(topo.standalone, uid="e_1_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_2_parent_2_0 = add_entry(topo.standalone, uid="e_2_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_3_parent_2_0 = add_entry(topo.standalone, uid="e_3_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_4_parent_2_0 = add_entry(topo.standalone, uid="e_4_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])

    # third subtree
    e_3_parent_0 = add_entry(topo.standalone, uid="e_3_parent_0")

    e_1_parent_3_0 = add_entry(topo.standalone, uid="e_1_parent_3_0", manager=[ensure_bytes(e_3_parent_0)])

    e_1_parent_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_3_0", manager=[ensure_bytes(e_1_parent_3_0)])

    e_1_parent_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_3_0)])

    e_1_parent_1_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_1_3_0)])

    dn_config = 'cn=test_slapi_memberof,cn=plugins,cn=config'
    topo.standalone.add_s(Entry((dn_config, {'objectclass': 'top nsSlapdPlugin extensibleObject'.split(),
                             'cn': 'test_slapi_memberof',
                             'nsslapd-pluginPath': 'libtest_slapi_memberof-plugin',
                             'nsslapd-pluginInitfunc': 'test_slapi_memberof_init',
                             'nsslapd-pluginType': 'extendedop',
                             'nsslapd-pluginEnabled': 'on',
                             'nsslapd-plugin-depends-on-type': 'database',
                             'nsslapd-pluginId': 'test_slapi_memberof-plugin',
                             'slapimemberOfMemberDN': 'uid=test_user_11,ou=People,dc=example,dc=com',
                             'slapimemberOfGroupAttr': 'member',
                             'slapimemberOfAttr': 'memberof',
                             'slapimemberOfAllBackends': 'on',
                             'slapimemberOfSkipNested': 'off',
                             'slapimemberOfEntryScope': DEFAULT_SUFFIX,
                             'slapimemberOfMaxGroup': '0',
                             'nsslapd-pluginVersion': '2.3.2.202302131418git0e190fc3d',
                             'nsslapd-pluginVendor': '389 Project',
                             'nsslapd-pluginDescription': 'test_slapi_memberof extended operation plugin'})))
    topo.standalone.restart()

    # Check the first subtree
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_0, relation="manager")
    _check_res_vs_expected("first subtree", res, expected)

    # Check the second subtree
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_0, relation="manager")
    _check_res_vs_expected("second subtree", res, expected)

    # Check the third subtree
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_3_parent_0, relation="manager")
    _check_res_vs_expected("third subtree", res, expected)

    # check e_1_parent_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_0", res, expected)

    # check e_1_parent_1_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_0", res, expected)

    # check e_2_parent_1_1_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_1_0", res, expected)

    # check e_2_parent_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_0", res, expected)

    # check e_2_parent_2_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_2_1_0", res, expected)

    # Check e_1_parent_3_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_3_0", res, expected)

    # Check e_1_parent_1_3_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_3_0", res, expected)

    # Check e_1_parent_1_1_3_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_3_0", res, expected)

    # Check e_1_parent_1_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_1_3_0", res, expected)

    def fin():
        entries = [e_1_parent_0, e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0, e_2_parent_0, e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0, e_3_parent_0, e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
        for entry in entries:
            topo.standalone.delete_s(entry)
        topo.standalone.delete_s(dn_config)

    request.addfinalizer(fin)


def test_slapi_memberof_scope(topo, request, install_test_plugin):
    """
    Test that membership hierarchy (member) is computed with slapi_member
    Only entries in the subtree scope (e_2_parent_1_0) gets valid
    computation of the membership
    with following parameters
    - membership attribute: 'manager'
    - span over all backends: 'on'
    - skip nesting membership: 'off'
    - computation mode: recompute
    - Scope: ou=subtree,ou=People,dc=example,dc=com  <----
    - ExcludeScope: None
    - Maximum return entries: None

    :id: 6c7587e0-0bc4-4847-b403-773d7314aa31
    :setup: Standalone instance
    :steps:
        1. provision a set of entries in default backend
        2. configure test_slapi_memberof as described above
           so only entries under e_2_parent_1_0 are taken into
           consideration
        3. check computed membership vs expected result
           Only entries under e_2_parent_1_0 get no empty results
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed

    DIT is :
    e_1_parent_0
    - e_1_parent_1_0
    -- e_1_parent_1_1_0
    --- e_1_parent_1_1_1_0
    --- e_2_parent_1_1_1_0
    --- e_3_parent_1_1_1_0
    --- e_4_parent_1_1_1_0
    --- e_5_parent_1_1_1_0
    -- e_2_parent_1_1_0
    - e_2_parent_1_0 (subtree)                <----
    -- e_1_parent_2_1_0 (subtree)             <----
    -- e_2_parent_2_1_0 (subtree)             <----
    --- e_1_parent_2_2_1_0 (subtree)          <----
    -- e_3_parent_2_1_0 (subtree)             <----
    -- e_4_parent_2_1_0 (subtree)             <----
    e_2_parent_0
    - e_1_parent_2_0
    - e_2_parent_2_0
    - e_3_parent_2_0
    - e_4_parent_2_0
    e_3_parent_0
    - e_1_parent_3_0
    -- e_1_parent_1_3_0
    --- e_1_parent_1_1_3_0
    ---- e_1_parent_1_1_1_3_0
    """

    subtree="subtree"
    dn_subtree = 'ou=%s,ou=People,%s' % (subtree, DEFAULT_SUFFIX)
    topo.standalone.add_s(Entry((dn_subtree, {'objectclass': 'top organizationalunit'.split(),
                                              'ou': subtree})))
    # First subtree
    e_1_parent_0 = add_entry(topo.standalone, uid="e_1_parent_0")

    e_1_parent_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_1_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_2_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_3_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_3_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_4_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_4_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_5_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_5_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])

    e_2_parent_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_2_parent_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_0", manager=[ensure_bytes(e_1_parent_0)], subtree=subtree)

    e_1_parent_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)], subtree=subtree)
    e_2_parent_2_1_0 = add_entry(topo.standalone, uid="e_2_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)], subtree=subtree)
    e_1_parent_2_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_2_1_0", manager=[ensure_bytes(e_2_parent_2_1_0)], subtree=subtree)
    e_3_parent_2_1_0 = add_entry(topo.standalone, uid="e_3_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)], subtree=subtree)
    e_4_parent_2_1_0 = add_entry(topo.standalone, uid="e_4_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)], subtree=subtree)

    # 2nd subtree
    e_2_parent_0 = add_entry(topo.standalone, uid="e_2_parent_0")

    e_1_parent_2_0 = add_entry(topo.standalone, uid="e_1_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_2_parent_2_0 = add_entry(topo.standalone, uid="e_2_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_3_parent_2_0 = add_entry(topo.standalone, uid="e_3_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_4_parent_2_0 = add_entry(topo.standalone, uid="e_4_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])

    # third subtree
    e_3_parent_0 = add_entry(topo.standalone, uid="e_3_parent_0")

    e_1_parent_3_0 = add_entry(topo.standalone, uid="e_1_parent_3_0", manager=[ensure_bytes(e_3_parent_0)])

    e_1_parent_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_3_0", manager=[ensure_bytes(e_1_parent_3_0)])

    e_1_parent_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_3_0)])

    e_1_parent_1_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_1_3_0)])

    dn_config = 'cn=test_slapi_memberof,cn=plugins,cn=config'
    topo.standalone.add_s(Entry((dn_config, {'objectclass': 'top nsSlapdPlugin extensibleObject'.split(),
                             'cn': 'test_slapi_memberof',
                             'nsslapd-pluginPath': 'libtest_slapi_memberof-plugin',
                             'nsslapd-pluginInitfunc': 'test_slapi_memberof_init',
                             'nsslapd-pluginType': 'extendedop',
                             'nsslapd-pluginEnabled': 'on',
                             'nsslapd-plugin-depends-on-type': 'database',
                             'nsslapd-pluginId': 'test_slapi_memberof-plugin',
                             'slapimemberOfMemberDN': 'uid=test_user_11,ou=People,dc=example,dc=com',
                             'slapimemberOfGroupAttr': 'manager',
                             'slapimemberOfAttr': 'memberof',
                             'slapimemberOfAllBackends': 'on',
                             'slapimemberOfSkipNested': 'off',
                             'slapimemberOfEntryScope': dn_subtree,
                             'slapimemberOfMaxGroup': '0',
                             'nsslapd-pluginVersion': '2.3.2.202302131418git0e190fc3d',
                             'nsslapd-pluginVendor': '389 Project',
                             'nsslapd-pluginDescription': 'test_slapi_memberof extended operation plugin'})))
    topo.standalone.restart()

    # Check the first subtree
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_0, relation="manager")
    _check_res_vs_expected("first subtree", res, expected)

    # Check the second subtree
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_0, relation="manager")
    _check_res_vs_expected("second subtree", res, expected)

    # Check the third subtree
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_3_parent_0, relation="manager")
    _check_res_vs_expected("third subtree", res, expected)

    # check e_1_parent_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_0", res, expected)

    # check e_1_parent_1_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_0", res, expected)

    # check e_2_parent_1_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_1_0", res, expected)

    # check e_2_parent_1_0
    expected = [e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_0", res, expected)

    # Check e_1_parent_2_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_2_1_0", res, expected)

    # check e_2_parent_2_1_0
    expected = [ e_1_parent_2_2_1_0 ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_2_1_0", res, expected)

    # Check e_1_parent_3_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_3_0", res, expected)

    # Check e_1_parent_1_3_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_3_0", res, expected)

    # Check e_1_parent_1_1_3_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_3_0", res, expected)

    # Check e_1_parent_1_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_1_3_0", res, expected)

    def fin():
        entries = [e_1_parent_0, e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0, e_2_parent_0, e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0, e_3_parent_0, e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
        for entry in entries:
            topo.standalone.delete_s(entry)
        topo.standalone.delete_s(dn_config)
        topo.standalone.delete_s(dn_subtree)

    request.addfinalizer(fin)

def test_slapi_memberof_excludescope(topo, request, install_test_plugin):
    """
    Test that membership hierarchy (member) is computed with slapi_member
    Entries in the subtree excludeescope (e_2_parent_1_0) are ignored
    computation of the membership
    with following parameters
    - membership attribute: 'manager'
    - span over all backends: 'on'
    - skip nesting membership: 'off'
    - computation mode: recompute
    - Scope: DEFAULT_SUFFIX
    - ExcludeScope: ou=subtree,ou=People,dc=example,dc=com  <----
    - Maximum return entries: None

    :id: bdb17e7e-289c-4b56-83d5-0eb54d0c660e
    :setup: Standalone instance
    :steps:
        1. provision a set of entries in default backend
        2. configure test_slapi_memberof as described above
           so entries under e_2_parent_1_0 are ignored
        3. check computed membership vs expected result
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed

    DIT is :
    e_1_parent_0
    - e_1_parent_1_0
    -- e_1_parent_1_1_0
    --- e_1_parent_1_1_1_0
    --- e_2_parent_1_1_1_0
    --- e_3_parent_1_1_1_0
    --- e_4_parent_1_1_1_0
    --- e_5_parent_1_1_1_0
    -- e_2_parent_1_1_0
    - e_2_parent_1_0 (subtree)                <----
    -- e_1_parent_2_1_0 (subtree)             <----
    -- e_2_parent_2_1_0 (subtree)             <----
    --- e_1_parent_2_2_1_0 (subtree)          <----
    -- e_3_parent_2_1_0 (subtree)             <----
    -- e_4_parent_2_1_0 (subtree)             <----
    e_2_parent_0
    - e_1_parent_2_0
    - e_2_parent_2_0
    - e_3_parent_2_0
    - e_4_parent_2_0
    e_3_parent_0
    - e_1_parent_3_0
    -- e_1_parent_1_3_0
    --- e_1_parent_1_1_3_0
    ---- e_1_parent_1_1_1_3_0
    """

    subtree="subtree"
    dn_subtree = 'ou=%s,ou=People,%s' % (subtree, DEFAULT_SUFFIX)
    topo.standalone.add_s(Entry((dn_subtree, {'objectclass': 'top organizationalunit'.split(),
                                              'ou': subtree})))
    # First subtree
    e_1_parent_0 = add_entry(topo.standalone, uid="e_1_parent_0")

    e_1_parent_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_1_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_2_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_3_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_3_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_4_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_4_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_5_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_5_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])

    e_2_parent_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_2_parent_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_0", manager=[ensure_bytes(e_1_parent_0)], subtree=subtree)

    e_1_parent_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)], subtree=subtree)
    e_2_parent_2_1_0 = add_entry(topo.standalone, uid="e_2_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)], subtree=subtree)
    e_1_parent_2_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_2_1_0", manager=[ensure_bytes(e_2_parent_2_1_0)], subtree=subtree)
    e_3_parent_2_1_0 = add_entry(topo.standalone, uid="e_3_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)], subtree=subtree)
    e_4_parent_2_1_0 = add_entry(topo.standalone, uid="e_4_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)], subtree=subtree)

    # 2nd subtree
    e_2_parent_0 = add_entry(topo.standalone, uid="e_2_parent_0")

    e_1_parent_2_0 = add_entry(topo.standalone, uid="e_1_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_2_parent_2_0 = add_entry(topo.standalone, uid="e_2_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_3_parent_2_0 = add_entry(topo.standalone, uid="e_3_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_4_parent_2_0 = add_entry(topo.standalone, uid="e_4_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])

    # third subtree
    e_3_parent_0 = add_entry(topo.standalone, uid="e_3_parent_0")

    e_1_parent_3_0 = add_entry(topo.standalone, uid="e_1_parent_3_0", manager=[ensure_bytes(e_3_parent_0)])

    e_1_parent_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_3_0", manager=[ensure_bytes(e_1_parent_3_0)])

    e_1_parent_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_3_0)])

    e_1_parent_1_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_1_3_0)])

    dn_config = 'cn=test_slapi_memberof,cn=plugins,cn=config'
    topo.standalone.add_s(Entry((dn_config, {'objectclass': 'top nsSlapdPlugin extensibleObject'.split(),
                             'cn': 'test_slapi_memberof',
                             'nsslapd-pluginPath': 'libtest_slapi_memberof-plugin',
                             'nsslapd-pluginInitfunc': 'test_slapi_memberof_init',
                             'nsslapd-pluginType': 'extendedop',
                             'nsslapd-pluginEnabled': 'on',
                             'nsslapd-plugin-depends-on-type': 'database',
                             'nsslapd-pluginId': 'test_slapi_memberof-plugin',
                             'slapimemberOfMemberDN': 'uid=test_user_11,ou=People,dc=example,dc=com',
                             'slapimemberOfGroupAttr': 'manager',
                             'slapimemberOfAttr': 'memberof',
                             'slapimemberOfAllBackends': 'on',
                             'slapimemberOfSkipNested': 'off',
                             'slapimemberOfEntryScopeExcludeSubtree': dn_subtree,
                             'slapimemberOfEntryScope': DEFAULT_SUFFIX,
                             'slapimemberOfMaxGroup': '0',
                             'nsslapd-pluginVersion': '2.3.2.202302131418git0e190fc3d',
                             'nsslapd-pluginVendor': '389 Project',
                             'nsslapd-pluginDescription': 'test_slapi_memberof extended operation plugin'})))
    topo.standalone.restart()

    # Check the first subtree
    expected = [ e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_0, relation="manager")
    _check_res_vs_expected("first subtree", res, expected)

    # Check the second subtree
    expected = [e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_0, relation="manager")
    _check_res_vs_expected("second subtree", res, expected)

    # Check the third subtree
    expected = [e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_3_parent_0, relation="manager")
    _check_res_vs_expected("third subtree", res, expected)

    # check e_1_parent_1_0
    expected = [e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_0", res, expected)

    # check e_1_parent_1_1_0
    expected = [e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_0", res, expected)

    # check e_2_parent_1_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_1_0", res, expected)

    # check e_2_parent_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_0", res, expected)

    # Check e_1_parent_2_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_2_1_0", res, expected)

    # check e_2_parent_2_1_0
    expected = [ EMPTY_RESULT ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_2_1_0", res, expected)

    # Check e_1_parent_3_0
    expected = [ e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0 ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_3_0", res, expected)

    # Check e_1_parent_1_3_0
    expected = [ e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0 ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_3_0", res, expected)

    # Check e_1_parent_1_1_3_0
    expected = [ e_1_parent_1_1_1_3_0 ]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_3_0", res, expected)

    # Check e_1_parent_1_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_1_3_0", res, expected)

    def fin():
        entries = [e_1_parent_0, e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0, e_2_parent_0, e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0, e_3_parent_0, e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
        for entry in entries:
            topo.standalone.delete_s(entry)
        topo.standalone.delete_s(dn_config)
        topo.standalone.delete_s(dn_subtree)

    request.addfinalizer(fin)

def test_slapi_memberof_skip_nested(topo, request, install_test_plugin):
    """
    When searching the management (manager) hierarchy it stops at the first level
    no recursion
    Test that management hierarchy is computed with slapi_member
    It is done stopping at the first level, so the direct subordinate
    with following parameters
    - membership attribute: 'manager'
    - span over all backends: 'on'
    - skip nesting membership: 'on'  <----
    - computation mode: recompute
    - Scope: DEFAULT_SUFFIX
    - ExcludeScope: ou=subtree,ou=People,dc=example,dc=com
    - Maximum return entries: None

    :id: c9b5617f-9058-40f5-bdd6-a560bc67b30d
    :setup: Standalone instance
    :steps:
        1. provision a set of entries in default backend
        2. configure test_slapi_memberof as described above
        3. check computed membership vs expected result
           only direct subordinate are returned
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed

    DIT is :
    e_1_parent_0
    - e_1_parent_1_0
    -- e_1_parent_1_1_0
    --- e_1_parent_1_1_1_0
    --- e_2_parent_1_1_1_0
    --- e_3_parent_1_1_1_0
    --- e_4_parent_1_1_1_0
    --- e_5_parent_1_1_1_0
    -- e_2_parent_1_1_0
    - e_2_parent_1_0
    -- e_1_parent_2_1_0
    -- e_2_parent_2_1_0
    --- e_1_parent_2_2_1_0
    -- e_3_parent_2_1_0
    -- e_4_parent_2_1_0
    e_2_parent_0
    - e_1_parent_2_0
    - e_2_parent_2_0
    - e_3_parent_2_0
    - e_4_parent_2_0
    e_3_parent_0
    - e_1_parent_3_0
    -- e_1_parent_1_3_0
    --- e_1_parent_1_1_3_0
    ---- e_1_parent_1_1_1_3_0
    """

    subtree="subtree"
    dn_subtree = 'ou=%s,ou=People,%s' % (subtree, DEFAULT_SUFFIX)
    topo.standalone.add_s(Entry((dn_subtree, {'objectclass': 'top organizationalunit'.split(),
                                              'ou': subtree})))
    # First subtree
    e_1_parent_0 = add_entry(topo.standalone, uid="e_1_parent_0")

    e_1_parent_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_1_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_2_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_3_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_3_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_4_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_4_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_5_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_5_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])

    e_2_parent_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_2_parent_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_2_parent_2_1_0 = add_entry(topo.standalone, uid="e_2_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_1_parent_2_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_2_1_0", manager=[ensure_bytes(e_2_parent_2_1_0)])
    e_3_parent_2_1_0 = add_entry(topo.standalone, uid="e_3_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_4_parent_2_1_0 = add_entry(topo.standalone, uid="e_4_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])

    # 2nd subtree
    e_2_parent_0 = add_entry(topo.standalone, uid="e_2_parent_0")

    e_1_parent_2_0 = add_entry(topo.standalone, uid="e_1_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_2_parent_2_0 = add_entry(topo.standalone, uid="e_2_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_3_parent_2_0 = add_entry(topo.standalone, uid="e_3_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_4_parent_2_0 = add_entry(topo.standalone, uid="e_4_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])

    # third subtree
    e_3_parent_0 = add_entry(topo.standalone, uid="e_3_parent_0")

    e_1_parent_3_0 = add_entry(topo.standalone, uid="e_1_parent_3_0", manager=[ensure_bytes(e_3_parent_0)])

    e_1_parent_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_3_0", manager=[ensure_bytes(e_1_parent_3_0)])

    e_1_parent_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_3_0)])

    e_1_parent_1_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_1_3_0)])

    dn_config = 'cn=test_slapi_memberof,cn=plugins,cn=config'
    topo.standalone.add_s(Entry((dn_config, {'objectclass': 'top nsSlapdPlugin extensibleObject'.split(),
                             'cn': 'test_slapi_memberof',
                             'nsslapd-pluginPath': 'libtest_slapi_memberof-plugin',
                             'nsslapd-pluginInitfunc': 'test_slapi_memberof_init',
                             'nsslapd-pluginType': 'extendedop',
                             'nsslapd-pluginEnabled': 'on',
                             'nsslapd-plugin-depends-on-type': 'database',
                             'nsslapd-pluginId': 'test_slapi_memberof-plugin',
                             'slapimemberOfMemberDN': 'uid=test_user_11,ou=People,dc=example,dc=com',
                             'slapimemberOfGroupAttr': 'manager',
                             'slapimemberOfAttr': 'memberof',
                             'slapimemberOfAllBackends': 'on',
                             'slapimemberOfSkipNested': 'on',
                             'slapimemberOfEntryScope': DEFAULT_SUFFIX,
                             'slapimemberOfMaxGroup': '0',
                             'nsslapd-pluginVersion': '2.3.2.202302131418git0e190fc3d',
                             'nsslapd-pluginVendor': '389 Project',
                             'nsslapd-pluginDescription': 'test_slapi_memberof extended operation plugin'})))
    topo.standalone.restart()
    # Check the first subtree
    expected = [ e_1_parent_1_0, e_2_parent_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_0, relation="manager")
    _check_res_vs_expected("first subtree", res, expected)

    # Check the second subtree
    expected = [e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_0, relation="manager")
    _check_res_vs_expected("second subtree", res, expected)

    # Check the third subtree
    expected = [e_1_parent_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_3_parent_0, relation="manager")
    _check_res_vs_expected("third subtree", res, expected)

    # check e_1_parent_1_0
    expected = [e_1_parent_1_1_0, e_2_parent_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_0", res, expected)

    # check e_1_parent_1_1_0
    expected = [e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_0", res, expected)

    # check e_2_parent_1_1_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_1_0", res, expected)

    # check e_2_parent_1_0
    expected = [e_1_parent_2_1_0, e_2_parent_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_0", res, expected)

    # check e_2_parent_2_1_0
    expected = [e_1_parent_2_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_2_1_0", res, expected)

    # Check e_1_parent_3_0
    expected = [e_1_parent_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_3_0", res, expected)

    # Check e_1_parent_1_3_0
    expected = [e_1_parent_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_3_0", res, expected)

    # Check e_1_parent_1_1_3_0
    expected = [e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_3_0", res, expected)

    # Check e_1_parent_1_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_1_3_0", res, expected)

    def fin():
        entries = [e_1_parent_0, e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0, e_2_parent_0, e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0, e_3_parent_0, e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
        for entry in entries:
            topo.standalone.delete_s(entry)
        topo.standalone.delete_s(dn_config)
        topo.standalone.delete_s(dn_subtree)

    request.addfinalizer(fin)

def test_slapi_memberof_maxgroup(topo, request, install_test_plugin):
    """
    When searching the management (manager) hierarchy it stops when
    a maximum subordinates are retrieved
    Test that management hierarchy is computed with slapi_member
    with following parameters
    - membership attribute: 'manager'
    - span over all backends: 'on'
    - skip nesting membership: 'off'  <----
    - computation mode: recompute
    - Scope: DEFAULT_SUFFIX
    - ExcludeScope: ou=subtree,ou=People,dc=example,dc=com
    - Maximum return entries: 3      <--

    :id: 83a4c668-99d0-4f47-ac89-a7f7fc620340
    :setup: Standalone instance
    :steps:
        1. provision a set of entries in default backend
        2. configure test_slapi_memberof as described above
        3. check computed membership vs expected result
           only direct subordinate are returned
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed

    DIT is :
    e_1_parent_0
    - e_1_parent_1_0
    -- e_1_parent_1_1_0
    --- e_1_parent_1_1_1_0
    --- e_2_parent_1_1_1_0
    --- e_3_parent_1_1_1_0
    --- e_4_parent_1_1_1_0
    --- e_5_parent_1_1_1_0
    -- e_2_parent_1_1_0
    - e_2_parent_1_0
    -- e_1_parent_2_1_0
    -- e_2_parent_2_1_0
    --- e_1_parent_2_2_1_0
    -- e_3_parent_2_1_0
    -- e_4_parent_2_1_0
    e_2_parent_0
    - e_1_parent_2_0
    - e_2_parent_2_0
    - e_3_parent_2_0
    - e_4_parent_2_0
    e_3_parent_0
    - e_1_parent_3_0
    -- e_1_parent_1_3_0
    --- e_1_parent_1_1_3_0
    ---- e_1_parent_1_1_1_3_0
    """
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

    # First subtree
    e_1_parent_0 = add_entry(topo.standalone, uid="e_1_parent_0")

    e_1_parent_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_1_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_2_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_3_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_3_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_4_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_4_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])
    e_5_parent_1_1_1_0 = add_entry(topo.standalone, uid="e_5_parent_1_1_1_0", manager=[ensure_bytes(e_1_parent_1_1_0)])

    e_2_parent_1_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_1_0", manager=[ensure_bytes(e_1_parent_1_0)])

    e_2_parent_1_0 = add_entry(topo.standalone, uid="e_2_parent_1_0", manager=[ensure_bytes(e_1_parent_0)])

    e_1_parent_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_2_parent_2_1_0 = add_entry(topo.standalone, uid="e_2_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_1_parent_2_2_1_0 = add_entry(topo.standalone, uid="e_1_parent_2_2_1_0", manager=[ensure_bytes(e_2_parent_2_1_0)])
    e_3_parent_2_1_0 = add_entry(topo.standalone, uid="e_3_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])
    e_4_parent_2_1_0 = add_entry(topo.standalone, uid="e_4_parent_2_1_0", manager=[ensure_bytes(e_2_parent_1_0)])

    # 2nd subtree
    e_2_parent_0 = add_entry(topo.standalone, uid="e_2_parent_0")

    e_1_parent_2_0 = add_entry(topo.standalone, uid="e_1_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_2_parent_2_0 = add_entry(topo.standalone, uid="e_2_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_3_parent_2_0 = add_entry(topo.standalone, uid="e_3_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])
    e_4_parent_2_0 = add_entry(topo.standalone, uid="e_4_parent_2_0", manager=[ensure_bytes(e_2_parent_0)])

    # third subtree
    e_3_parent_0 = add_entry(topo.standalone, uid="e_3_parent_0")

    e_1_parent_3_0 = add_entry(topo.standalone, uid="e_1_parent_3_0", manager=[ensure_bytes(e_3_parent_0)])

    e_1_parent_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_3_0", manager=[ensure_bytes(e_1_parent_3_0)])

    e_1_parent_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_3_0)])

    e_1_parent_1_1_1_3_0 = add_entry(topo.standalone, uid="e_1_parent_1_1_1_3_0", manager=[ensure_bytes(e_1_parent_1_1_3_0)])

    dn_config = 'cn=test_slapi_memberof,cn=plugins,cn=config'
    topo.standalone.add_s(Entry((dn_config, {'objectclass': 'top nsSlapdPlugin extensibleObject'.split(),
                             'cn': 'test_slapi_memberof',
                             'nsslapd-pluginPath': 'libtest_slapi_memberof-plugin',
                             'nsslapd-pluginInitfunc': 'test_slapi_memberof_init',
                             'nsslapd-pluginType': 'extendedop',
                             'nsslapd-pluginEnabled': 'on',
                             'nsslapd-plugin-depends-on-type': 'database',
                             'nsslapd-pluginId': 'test_slapi_memberof-plugin',
                             'slapimemberOfMemberDN': 'uid=test_user_11,ou=People,dc=example,dc=com',
                             'slapimemberOfGroupAttr': 'manager',
                             'slapimemberOfAttr': 'memberof',
                             'slapimemberOfAllBackends': 'on',
                             'slapimemberOfSkipNested': 'off',
                             'slapimemberOfEntryScope': DEFAULT_SUFFIX,
                             'slapimemberOfMaxGroup': '3',
                             'nsslapd-pluginVersion': '2.3.2.202302131418git0e190fc3d',
                             'nsslapd-pluginVendor': '389 Project',
                             'nsslapd-pluginDescription': 'test_slapi_memberof extended operation plugin'})))
    topo.standalone.restart()

    # Check the first subtree
    expected = [ e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_0, relation="manager")
    _check_res_vs_expected("first subtree", res, expected)

    # Check the second subtree
    expected = [e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_0, relation="manager")
    _check_res_vs_expected("second subtree", res, expected)

    # Check the third subtree
    expected = [e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_3_parent_0, relation="manager")
    _check_res_vs_expected("third subtree", res, expected)

    # check e_1_parent_1_0
    expected = [e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_0", res, expected)

    # check e_1_parent_1_1_0
    expected = [e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_0", res, expected)

    # check e_2_parent_1_1_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_1_0", res, expected)

    # check e_2_parent_1_0
    expected = [e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_1_0", res, expected)

    # check e_2_parent_2_1_0
    expected = [e_1_parent_2_2_1_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_2_parent_2_1_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_2_parent_2_1_0", res, expected)

    # Check e_1_parent_3_0
    expected = [e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_3_0", res, expected)

    # Check e_1_parent_1_3_0
    expected = [e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_3_0", res, expected)

    # Check e_1_parent_1_1_3_0
    expected = [e_1_parent_1_1_1_3_0]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_3_0", res, expected)

    # Check e_1_parent_1_1_1_3_0
    expected = [EMPTY_RESULT]
    res = _extop_test_slapi_member(server=topo.standalone, dn=e_1_parent_1_1_1_3_0, relation="manager")
    _check_res_vs_expected("organisation reporting to e_1_parent_1_1_1_3_0", res, expected)

    def fin():
        entries = [e_1_parent_0, e_1_parent_1_0, e_1_parent_1_1_0, e_1_parent_1_1_1_0, e_2_parent_1_1_1_0, e_3_parent_1_1_1_0, e_4_parent_1_1_1_0, e_5_parent_1_1_1_0, e_2_parent_1_1_0, e_2_parent_1_0, e_1_parent_2_1_0, e_2_parent_2_1_0, e_1_parent_2_2_1_0, e_3_parent_2_1_0, e_4_parent_2_1_0, e_2_parent_0, e_1_parent_2_0, e_2_parent_2_0, e_3_parent_2_0, e_4_parent_2_0, e_3_parent_0, e_1_parent_3_0, e_1_parent_1_3_0, e_1_parent_1_1_3_0, e_1_parent_1_1_1_3_0]
        for entry in entries:
            topo.standalone.delete_s(entry)
        topo.standalone.delete_s(dn_config)

    request.addfinalizer(fin)

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)

