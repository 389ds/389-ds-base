# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

from lib389.topologies import topology_st
from lib389.dbgen import dbgen_users
from lib389.ldclt import Ldclt
from lib389.tasks import ImportTask
from lib389._constants import DEFAULT_SUFFIX


def test_stress_search_simple(topology_st):
    """Test a simple stress test of searches on the directory server.

    :id: 3786d01c-ea03-4655-a4f9-450693c75863
    :setup: Standalone Instance
    :steps:
        1. Create test users
        2. Import them
        3. Stress test!
    :expectedresults:
        1. Success
        2. Success
        3. Results are written to /tmp
    """

    inst = topology_st.standalone
    inst.config.set("nsslapd-verify-filter-schema", "off")
    # Bump idllimit to test OR worst cases.
    from lib389.config import LDBMConfig
    lconfig = LDBMConfig(inst)
    # lconfig.set("nsslapd-idlistscanlimit", '20000')
    # lconfig.set("nsslapd-lookthroughlimit", '20000')

    ldif_dir = inst.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    dbgen_users(inst, 10000, import_ldif, DEFAULT_SUFFIX)

    r = ImportTask(inst)
    r.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    r.wait()

    # Run a small to warm up the server's caches ...
    l = Ldclt(inst)
    l.search_loadtest(DEFAULT_SUFFIX, "(mail=XXXX@example.com)", rounds=1)

    # Now do it for realsies!
    # l.search_loadtest(DEFAULT_SUFFIX, "(|(mail=XXXX@example.com)(nonexist=foo))", rounds=10)
    l.search_loadtest(DEFAULT_SUFFIX, "(mail=XXXX@example.com)", rounds=10)
