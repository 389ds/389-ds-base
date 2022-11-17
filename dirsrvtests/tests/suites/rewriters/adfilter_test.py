# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import glob
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, HOST_STANDALONE, PORT_STANDALONE

log = logging.getLogger(__name__)
# Skip on versions 1.4.2 and before. Rewriters are expected in 1.4.3
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.4.3'), reason="Not implemented")]

PW = 'password'
configuration_container = 'cn=Configuration,%s' % DEFAULT_SUFFIX
schema_container = "cn=Schema,%s" % configuration_container

def _create_ad_objects_container(inst):
    inst.add_s(Entry((
        configuration_container, {
            'objectClass': 'top nsContainer'.split(),
            'cn': 'Configuration'
        })))
    inst.add_s(Entry((
        schema_container, {
            'objectClass': 'top nsContainer'.split(),
            'cn': 'Schema'
        })))

def _create_user(inst, name, salt):
    dn = 'cn=%s,%s' % (name, schema_container)
    inst.add_s(Entry((
        dn, {
            'objectClass': 'top person extensibleobject'.split(),
            'cn': name,
            'sn': name,
            'objectcategory': dn,
            "description" : salt,
            'userpassword': PW
        })))



def test_adfilter_objectCategory(topology_st):
    """
    Test adfilter objectCategory rewriter function
    """

    librewriters = os.path.join( topology_st.standalone.ds_paths.lib_dir, 'dirsrv/librewriters.so')
    assert librewriters
    # register objectCategory rewriter
    topology_st.standalone.add_s(Entry((
        "cn=adfilter,cn=rewriters,cn=config", {
            "objectClass": "top rewriterEntry".split(),
            "cn": "adfilter",
            "nsslapd-libpath": librewriters,
            "nsslapd-filterrewriter": "adfilter_rewrite_objectCategory",
        }
    )))

    objectcategory_attr = '( NAME \'objectCategory\' DESC \'test of objectCategory\' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )'
    topology_st.standalone.schema.add_schema('attributetypes', [ensure_bytes(objectcategory_attr)])

    topology_st.standalone.restart(60)

    # Add a user
    _create_ad_objects_container(topology_st.standalone)
    for i in range(0, 20):
        _create_user(topology_st.standalone, "user_%d" % i, str(i))

    # Check EQUALITY filter rewrite => it should match only one entry
    for i in range(0, 20):
        ents = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectCategory=user_%d)' % i)
        assert len(ents) == 1

    # Check SUBSTRING search is not replaced
    ents = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectCategory=user_*)')
    assert len(ents) == 0

    # Check PRESENCE search is not replaced so it selects all entries having objectCategory
    ents = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectCategory=*)')
    assert len(ents) == 20

    log.info('Test PASSED')

