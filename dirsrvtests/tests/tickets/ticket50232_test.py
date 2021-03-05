# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
# from lib389.tasks import *
# from lib389.utils import *
from lib389.topologies import topology_st
from lib389.replica import ReplicationManager,Replicas

from lib389._constants import DEFAULT_SUFFIX, BACKEND_NAME

from lib389.idm.user import UserAccounts
from lib389.idm.organization import Organization
from lib389.idm.organizationalunit import OrganizationalUnit

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

NORMAL_SUFFIX = 'o=normal'
NORMAL_BACKEND_NAME = 'normal'
REVERSE_SUFFIX = 'o=reverse'
REVERSE_BACKEND_NAME = 'reverse'

def _enable_replica(instance, suffix):

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl._ensure_changelog(instance)
    replicas = Replicas(instance)
    replicas.create(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': suffix,
        'nsDS5ReplicaId': '1',
        'nsDS5Flags': '1',
        'nsDS5ReplicaType': '3'
        })

def _populate_suffix(instance, suffixname):

    o = Organization(instance, 'o={}'.format(suffixname))
    o.create(properties={
        'o': suffixname,
        'description': 'test'
    })
    ou = OrganizationalUnit(instance, 'ou=people,o={}'.format(suffixname))
    ou.create(properties={
        'ou': 'people'
    })

def _get_replica_generation(instance, suffix):

    replicas = Replicas(instance)
    replica = replicas.get(suffix)
    ruv = replica.get_ruv()
    return ruv._data_generation

def _test_export_import(instance, suffix, backend):

    before_generation = _get_replica_generation(instance, suffix)

    instance.stop()
    instance.db2ldif(
        bename=backend,
        suffixes=[suffix],
        excludeSuffixes=[],
        encrypt=False,
        repl_data=True,
        outputfile="/tmp/output_file",
    )
    instance.ldif2db(
        bename=None,
        excludeSuffixes=None,
        encrypt=False,
        suffixes=[suffix],
        import_file="/tmp/output_file",
    )
    instance.start()
    after_generation = _get_replica_generation(instance, suffix)

    assert (before_generation == after_generation)

def test_ticket50232_normal(topology_st):
    """
    The fix for ticket 50232


    The test sequence is:
    - create suffix
    - add suffix entry and some child entries
    - "normally" done after populating suffix: enable replication
    - get RUV and database generation
    - export -r
    - import
    - get RUV and database generation
    - assert database generation has not changed
    """

    log.info('Testing Ticket 50232 - export creates not imprtable ldif file, normal creation order')

    topology_st.standalone.backend.create(NORMAL_SUFFIX, {BACKEND_NAME: NORMAL_BACKEND_NAME})
    topology_st.standalone.mappingtree.create(NORMAL_SUFFIX, bename=NORMAL_BACKEND_NAME, parent=None)

    _populate_suffix(topology_st.standalone, NORMAL_BACKEND_NAME)

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl._ensure_changelog(topology_st.standalone)
    replicas = Replicas(topology_st.standalone)
    replicas.create(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': NORMAL_SUFFIX,
        'nsDS5ReplicaId': '1',
        'nsDS5Flags': '1',
        'nsDS5ReplicaType': '3'
        })

    _test_export_import(topology_st.standalone, NORMAL_SUFFIX, NORMAL_BACKEND_NAME)

def test_ticket50232_reverse(topology_st):
    """
    The fix for ticket 50232


    The test sequence is:
    - create suffix
    - enable replication before suffix enztry is added
    - add suffix entry and some child entries
    - get RUV and database generation
    - export -r
    - import
    - get RUV and database generation
    - assert database generation has not changed
    """

    log.info('Testing Ticket 50232 - export creates not imprtable ldif file, normal creation order')

    #
    # Setup Replication
    #
    log.info('Setting up replication...')
    repl = ReplicationManager(DEFAULT_SUFFIX)
    # repl.create_first_supplier(topology_st.standalone)
    #
    # enable dynamic plugins, memberof and retro cl plugin
    #
    topology_st.standalone.backend.create(REVERSE_SUFFIX, {BACKEND_NAME: REVERSE_BACKEND_NAME})
    topology_st.standalone.mappingtree.create(REVERSE_SUFFIX, bename=REVERSE_BACKEND_NAME, parent=None)

    _enable_replica(topology_st.standalone, REVERSE_SUFFIX)

    _populate_suffix(topology_st.standalone, REVERSE_BACKEND_NAME)

    _test_export_import(topology_st.standalone, REVERSE_SUFFIX, REVERSE_BACKEND_NAME)

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
