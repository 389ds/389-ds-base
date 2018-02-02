# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

# Performance tests look different to others, they require some extra
# environmental settings.

import ldap
import os
from lib389 import DirSrv
from lib389._constants import DEFAULT_SUFFIX

from lib389.topologies import topology_st as topology

from lib389.idm.domain import Domain
from lib389.idm.group import Groups
from lib389.idm.user import nsUserAccounts
from lib389.backend import Backends

from lib389.ldclt import Ldclt
import time

# We want to write a CSV such as:
# category,1 thread,4 thread,8 thread,16 thread
# testcategory,500,800,1000,2000
# testcategory2,500,800,1000,2000
TEST_MARKER = 'configured: search_performance_test.py'
# GROUP_MAX = 4000
# USER_MAX = 6000

GROUP_MAX = 4000
USER_MAX = 6000

TARGET_HOST = os.environ.get('PERF_TARGET_HOST', 'localhost')
TARGET_PORT = os.environ.get('PERF_TARGET_PORT', '389')

def assert_data_present(inst):
    # Do we have the backend marker?
    d = Domain(inst, DEFAULT_SUFFIX)
    try:
        desc = d.get_attr_val_utf8('description')
        if desc == TEST_MARKER:
            return
    except:
        # Just reset everything.
        pass
    # Reset the backends
    bes = Backends(inst)
    try:
        be = bes.get(DEFAULT_SUFFIX)
        be.delete()
    except:
        pass

    be = bes.create(properties={
        'nsslapd-suffix': DEFAULT_SUFFIX,
        'cn': 'userRoot',
    })
    be.create_sample_entries('001004002')

    # Load our data
    # We can't use dbgen as that relies on local access :(

    # Add 40,000 groups
    groups = Groups(inst, DEFAULT_SUFFIX)
    for i in range(1,GROUP_MAX):
        rdn = 'group_{0:07d}'.format(i)
        groups.create(properties={
            'cn': rdn,
        })

    # Add 60,000 users
    users = nsUserAccounts(inst, DEFAULT_SUFFIX)
    for i in range(1,USER_MAX):
        rdn = 'user_{0:07d}'.format(i)
        users.create(properties={
            'uid': rdn,
            'cn': rdn,
            'displayName': rdn,
            'uidNumber' : '%s' % i,
            'gidNumber' : '%s' % i,
            'homeDirectory' : '/home/%s' % rdn,
            'userPassword': rdn,
        })

    # Add the marker
    d.replace('description', TEST_MARKER)
    # Done!

# Single uid
# 1000 uid
# 4000 uid
# 5000 uid
# 10,000 uid

# & of single uid
# & of two 1000 uid sets
# & of two 4000 uid sets
# & of two 5000 uid sets
# & of two 10,000 uid sets

# | of single uid
# | of two 1000 uid sets
# | of two 4000 uid sets
# | of two 5000 uid sets
# | of two 10,000 uid sets

# & of user and group

# | of user and group

def _do_search_performance(inst, thread_count):
    # Configure thread count
    # Restart
    print("Configuring with %s threads ..." % thread_count)
    time.sleep(1)
    inst.config.set('nsslapd-threadnumber', str(thread_count))
    inst.restart()
    ld = Ldclt(inst)
    out = ld.search_loadtest(DEFAULT_SUFFIX, "(uid=user_XXXXXXX)", min=1, max=USER_MAX)
    return out

# Need a check here
def test_user_search_performance():
    inst = DirSrv(verbose=True)
    inst.remote_simple_allocate(
        f"ldaps://{TARGET_HOST}",
        password="password"
    )
    # Need a better way to set this.
    inst.host = TARGET_HOST
    inst.port = TARGET_PORT
    inst.open(reqcert=ldap.OPT_X_TLS_NEVER)
    assert_data_present(inst)
    r1 = _do_search_performance(inst, 1)
    # r2 = _do_search_performance(inst, 4)
    # r3 = _do_search_performance(inst, 6)
    # r4 = _do_search_performance(inst, 8)
    # r5 = _do_search_performance(inst, 12)
    r6 = _do_search_performance(inst, 16)
    # print("category,t1,t4,t6,t8,t12,t16")
    # print("search,%s,%s,%s,%s,%s,%s" % (r1, r2, r3, r4, r5, r6))

def test_group_search_performance():
    pass

## TODO
# Tweak cache levels
# turbo mode
# ldclt threads = 2x server?
# add perf logs to each test




