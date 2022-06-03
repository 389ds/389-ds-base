

import ldap
import logging
import pytest
import os
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389.idm.domain import Domain
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.account import Anonymous

log = logging.getLogger(__name__)


def test_filter_access(topo):
    """Search that compound filters are correctly processed by access control

    :id: ad6a3ffc-2620-4e76-909b-926f94c1a920
    :setup: Standalone Instance
    :steps:
        1. Add anonymous aci
        2. Add ou
        2. Test good filters
        4. Test bad filters
    :expectedresults:
        1. Success
        2. Success
        3. The good filters return the OU entry
        4. The bad filters do not return the OU entry
    """

    # Add aci
    ACI_TEXT = ('(targetattr="objectclass || cn")(version 3.0; acl "Anonymous read access"; allow' +
                '(read, search, compare) userdn = "ldap:///anyone";)')
    domain = Domain(topo.standalone, DEFAULT_SUFFIX)
    domain.replace('aci', ACI_TEXT)

    # To remove noise, delete EVERYTHING else.

    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    existing_ous = ous.list()
    for eou in existing_ous:
        eou.delete(recursive=True)

    # Create restricted entry
    OU_PROPS = {
        'ou': 'restricted',
        'description': 'secret data'
    }
    ou = ous.create(properties=OU_PROPS)
    OU_DN = ou.dn

    # Do anonymous search using different filters
    GOOD_FILTERS = [
        "(|(objectClass=top)(&(objectClass=organizationalunit)(description=secret data)))",
        "(|(&(objectClass=organizationalunit)(description=secret data))(objectClass=top))",
        "(|(objectClass=organizationalunit)(description=secret data)(sn=*))",
        "(|(description=secret data)(objectClass=organizationalunit)(sn=*))",
        "(|(sn=*)(description=secret data)(objectClass=organizationalunit))",
        "(objectClass=top)",
    ]
    BAD_FILTERS = [
        "(|(objectClass=person)(&(objectClass=organizationalunit)(description=secret data)))",
        "(&(objectClass=top)(objectClass=organizationalunit)(description=secret data))",
        "(|(&(description=*)(objectClass=top))(objectClass=person))",
        "(description=secret data)",
        "(description=*)",
        "(ou=*)",
    ]
    conn = Anonymous(topo.standalone).bind()

    # These searches should return the OU
    for search_filter in GOOD_FILTERS:
        entries = conn.search_s(OU_DN, ldap.SCOPE_SUBTREE, search_filter)
        log.debug(f"Testing good filter: {search_filter} result: {len(entries)}")
        assert len(entries) == 1

    # These searches should not return the OU
    for search_filter in BAD_FILTERS:
        entries = conn.search_s(OU_DN, ldap.SCOPE_SUBTREE, search_filter)
        log.debug(f"Testing bad filter: {search_filter} result: {len(entries)}")
        assert len(entries) == 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

