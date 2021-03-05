import pytest
import glob
import base64
import re
from lib389.tasks import *
from lib389.rewriters import *
from lib389.idm.user import UserAccounts
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, HOST_STANDALONE, PORT_STANDALONE

samba_missing = False
try:
    from samba.dcerpc import security
    from samba.ndr import ndr_pack, ndr_unpack
except:
    samba_missing = True
    pass

log = logging.getLogger(__name__)
# Skip on versions 1.4.2 and before. Rewriters are expected in 1.4.3
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.4.3'), reason="Not implemented")]

PW = 'password'

#
# Necessary because objectcategory relies on cn=xxx RDN
# while userAccount creates uid=xxx RDN
#
def _create_user(inst, schema_container, name, salt):
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

    rewriters = AdRewriters(topology_st.standalone)
    ad_rewriter = rewriters.ensure_state(properties={"cn": "adfilter", "nsslapd-libpath": librewriters})
    ad_rewriter.add('nsslapd-filterrewriter', "adfilter_rewrite_objectCategory")
    ad_rewriter.create_containers(DEFAULT_SUFFIX)
    schema_container = ad_rewriter.get_schema_dn()

    objectcategory_attr = '( NAME \'objectCategory\' DESC \'test of objectCategory\' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )'
    topology_st.standalone.schema.add_schema('attributetypes', [ensure_bytes(objectcategory_attr)])

    topology_st.standalone.restart(60)

    # Add a user
    for i in range(0, 20):
        _create_user(topology_st.standalone, schema_container, "user_%d" % i, str(i))

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

def sid_to_objectsid(sid):
    return base64.b64encode(ndr_pack(security.dom_sid(sid))).decode('utf-8')

def objectsid_to_sid(objectsid):
    sid = ndr_unpack(security.dom_sid, base64.b64decode(objectsid))
    return str(sid)

@pytest.mark.skipif(samba_missing, reason="It is missing samba python bindings")
def test_adfilter_objectSid(topology_st):
    """
    Test adfilter objectCategory rewriter function

    :id: fc5880ff-4305-47ba-84fb-38429e264e9e

    :setup: Standalone instance

    :steps:
         1. add a objectsid rewriter (from librewriters.so)
         2. add a dummy schema definition of objectsid to prevent nsslapd-verify-filter-schema
         3. restart the server (to load the rewriter)
         4. Add "samba" container/users
         5. Searches using objectsid in string format

    :expectedresults:
         1. Add operation should PASS.
         2. Add operations should PASS.
         3. restart should PASS
         4. Add "samba" users should PASS
         5. Search returns only one entry
    """
    librewriters = os.path.join( topology_st.standalone.ds_paths.lib_dir, 'dirsrv/librewriters.so')
    assert librewriters

    rewriters = AdRewriters(topology_st.standalone)
    ad_rewriter = rewriters.ensure_state(properties={"cn": "adfilter", "nsslapd-libpath": librewriters})
    ad_rewriter.add('nsslapd-filterrewriter', "adfilter_rewrite_objectsid")
    ad_rewriter.create_containers(DEFAULT_SUFFIX)
    schema_container = ad_rewriter.get_schema_dn()

    # to prevent nsslapd-verify-filter-schema to reject searches with objectsid
    objectcategory_attr = '( NAME \'objectsid\' DESC \'test of objectsid\' SYNTAX 1.3.6.1.4.1.1466.115.121.1.40 )'
    topology_st.standalone.schema.add_schema('attributetypes', [ensure_bytes(objectcategory_attr)])

    topology_st.standalone.restart()

    # Contains a list of b64encoded SID from https://github.com/SSSD/sssd/blob/supplier/src/tests/intg/data/ad_data.ldif
    SIDs = ["AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EUAQAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8E9gEAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EAwIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EBAIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EBgIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EBwIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EBQIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EAAIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EAQIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EAgIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ECAIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EKQIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EOwIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EPAIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ECQIAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8E8gEAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ETQQAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ETgQAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EeUMBAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EekMBAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8Ee0MBAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EfEMBAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ETwQAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8EUQQAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ESUMBAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ESkMBAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ES0MBAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8ETEMBAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8E9AEAAA==",
            "AQUAAAAAAAUVAAAADcfLTVzC66zo0l8E9QEAAA=="]

    # Add a container and "samba" like users containing objectsid
    users = UserAccounts(topology_st.standalone, schema_container, rdn=None)
    i = 0
    for sid in SIDs:
        decoded = base64.b64decode(sid)
        user = users.create_test_user(uid=i)
        user.add('objectclass', 'extensibleobject')
        user.replace('objectsid', decoded)
        user.replace('objectSidString', objectsid_to_sid(sid))
        i = i + 1

    # Check that objectsid rewrite can retrieve the "samba" user
    # using either a string objectsid (i.e. S-1-5...) or a blob objectsid
    for sid_blob in SIDs:
        sid_string = objectsid_to_sid(sid_blob)
        ents_sid_string = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(objectsid=%s)' % sid_string)
        assert len(ents_sid_string) == 1

