# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.replica import Replicas
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2 as topo_m2
from . import get_repl_entries
from lib389.idm.user import UserAccount
from lib389.replica import ReplicationManager
from lib389._constants import *

pytestmark = pytest.mark.tier0

TEST_ENTRY_NAME = 'mmrepl_test'
TEST_ENTRY_DN = 'uid={},{}'.format(TEST_ENTRY_NAME, DEFAULT_SUFFIX)
NEW_SUFFIX_NAME = 'test_repl'
NEW_SUFFIX = 'o={}'.format(NEW_SUFFIX_NAME)
NEW_BACKEND = 'repl_base'

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

pytest.mark.skipif(not os.environ.get('UNSAFE_ACK', False), reason="UNSAFE tests may damage system configuration.")
def test_rfc2307compat(topo_m2):
    """ Test to verify if 10rfc2307compat.ldif does not prevent replication of schema
        - Create 2 masters and a test entry
        - Move 10rfc2307compat.ldif to be private to M1
        - Move 10rfc2307.ldif to be private to M2
        - Add 'objectCategory' to the schema of M1
        - Force a replication session
        - Check 'objectCategory' on M1 and M2
    """
    m1 = topo_m2.ms["master1"]
    m2 = topo_m2.ms["master2"]

    m1.config.loglevel(vals=(ErrorLog.DEFAULT, ErrorLog.REPLICA))
    m2.config.loglevel(vals=(ErrorLog.DEFAULT, ErrorLog.REPLICA))

    m1.add_s(Entry((
        TEST_ENTRY_DN, {
            "objectClass": "top",
            "objectClass": "extensibleObject",
            'uid': TEST_ENTRY_NAME,
            'cn': TEST_ENTRY_NAME,
            'sn': TEST_ENTRY_NAME,
        }
    )))

    entries = get_repl_entries(topo_m2, TEST_ENTRY_NAME, ["uid"])
    assert all(entries), "Entry {} wasn't replicated successfully".format(TEST_ENTRY_DN)

    # Clean the old locations (if any)
    m1_temp_schema = os.path.join(m1.get_config_dir(), 'schema')
    m2_temp_schema = os.path.join(m2.get_config_dir(), 'schema')
    m1_schema = os.path.join(m1.get_data_dir(), 'dirsrv/schema')
    m1_opt_schema = os.path.join(m1.get_data_dir(), 'dirsrv/data')
    m1_temp_backup = os.path.join(m1.get_tmp_dir(), 'schema')

    # Does the system schema exist?
    if os.path.islink(m1_schema):
        # Then we need to put the m1 schema back.
        os.unlink(m1_schema)
        shutil.copytree(m1_temp_backup, m1_schema)
    if not os.path.exists(m1_temp_backup):
        shutil.copytree(m1_schema, m1_temp_backup)

    shutil.rmtree(m1_temp_schema, ignore_errors=True)
    shutil.rmtree(m2_temp_schema, ignore_errors=True)

    # Build a new copy
    shutil.copytree(m1_schema, m1_temp_schema)
    shutil.copytree(m1_schema, m2_temp_schema)
    # Ensure 99user.ldif exists
    with open(os.path.join(m1_temp_schema, '99user.ldif'), 'w') as f:
        f.write('dn: cn=schema')

    with open(os.path.join(m2_temp_schema, '99user.ldif'), 'w') as f:
        f.write('dn: cn=schema')

    # m1 has compat, m2 has legacy.
    os.unlink(os.path.join(m2_temp_schema, '10rfc2307compat.ldif'))
    shutil.copy(os.path.join(m1_opt_schema, '10rfc2307.ldif'), m2_temp_schema)

    # Configure the instances
    # m1.config.replace('nsslapd-schemadir', m1_temp_schema)
    # m2.config.replace('nsslapd-schemadir', m2_temp_schema)

    # Now mark the system schema as empty.
    shutil.rmtree(m1_schema)
    os.symlink('/var/lib/empty', m1_schema)

    print("SETUP COMPLETE -->")

    # Stop all instances
    m1.stop()
    m2.stop()

    # udpate the schema on M1 to tag a schemacsn
    m1.start()
    objectcategory_attr = '( NAME \'objectCategory\' DESC \'test of objectCategory\' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )'
    m1.schema.add_schema('attributetypes', [ensure_bytes(objectcategory_attr)])

    # Now start M2 and trigger a replication M1->M2
    m2.start()
    m1.modify_s(TEST_ENTRY_DN, [(ldap.MOD_ADD, 'cn', [ensure_bytes('value_m1')])])

    # Now check that objectCategory is in both schema
    time.sleep(10)
    ents = m1.search_s("cn=schema", ldap.SCOPE_SUBTREE, 'objectclass=*',['attributetypes'])
    for value in ents[0].getValues('attributetypes'):
        if ensure_bytes('objectCategory') in value:
           log.info("M1: " + str(value))
           break
    assert ensure_bytes('objectCategory') in value

    ents = m2.search_s("cn=schema", ldap.SCOPE_SUBTREE, 'objectclass=*',['attributetypes'])
    for value in ents[0].getValues('attributetypes'):
        if ensure_bytes('objectCategory') in value:
           log.info("M2: " + str(value))
           break
    assert ensure_bytes('objectCategory') in value

    # Stop m2
    m2.stop()

    # "Update" it's schema,
    os.unlink(os.path.join(m2_temp_schema, '10rfc2307.ldif'))
    shutil.copy(os.path.join(m1_temp_backup, '10rfc2307compat.ldif'), m2_temp_schema)

    # Add some more to m1
    objectcategory_attr = '( NAME \'objectCategoryX\' DESC \'test of objectCategoryX\' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )'
    m1.schema.add_schema('attributetypes', [ensure_bytes(objectcategory_attr)])

    # Start m2.
    m2.start()
    m1.modify_s(TEST_ENTRY_DN, [(ldap.MOD_ADD, 'cn', [ensure_bytes('value_m2')])])

    time.sleep(10)
    ents = m1.search_s("cn=schema", ldap.SCOPE_SUBTREE, 'objectclass=*',['attributetypes'])
    for value in ents[0].getValues('attributetypes'):
        if ensure_bytes('objectCategoryX') in value:
           log.info("M1: " + str(value))
           break
    assert ensure_bytes('objectCategoryX') in value

    ents = m2.search_s("cn=schema", ldap.SCOPE_SUBTREE, 'objectclass=*',['attributetypes'])
    for value in ents[0].getValues('attributetypes'):
        if ensure_bytes('objectCategoryX') in value:
           log.info("M2: " + str(value))
           break
    assert ensure_bytes('objectCategoryX') in value

    # Success cleanup
    os.unlink(m1_schema)
    shutil.copytree(m1_temp_backup, m1_schema)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
