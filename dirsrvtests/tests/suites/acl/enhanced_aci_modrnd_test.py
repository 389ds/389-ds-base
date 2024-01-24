# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

CONTAINER_1_OU = 'test_ou_1'
CONTAINER_2_OU = 'test_ou_2'
CONTAINER_1 = f'ou={CONTAINER_1_OU},dc=example,dc=com'
CONTAINER_2 = f'ou={CONTAINER_2_OU},dc=example,dc=com'
USER_CN = 'test_user'
USER_PWD = 'Secret123'
USER = f'cn={USER_CN},{CONTAINER_1}'


@pytest.fixture(scope="module")
def env_setup(topology_st):
    """Adds two containers, one user and two ACI rules"""

    log.info("Add a container: %s" % CONTAINER_1)
    topology_st.standalone.add_s(Entry((CONTAINER_1,
                                        {'objectclass': ['top','organizationalunit'],
                                         'ou': CONTAINER_1_OU,
                                         })))

    log.info("Add a container: %s" % CONTAINER_2)
    topology_st.standalone.add_s(Entry((CONTAINER_2,
                                        {'objectclass': ['top', 'organizationalunit'],
                                         'ou': CONTAINER_2_OU,
                                         })))

    log.info("Add a user: %s" % USER)
    topology_st.standalone.add_s(Entry((USER,
                                        {'objectclass': 'top person'.split(),
                                         'cn': USER_CN,
                                         'sn': USER_CN,
                                         'userpassword': USER_PWD
                                         })))

    ACI_TARGET = '(targetattr="*")'
    ACI_ALLOW = '(version 3.0; acl "All rights for %s"; allow (all) ' % USER
    ACI_SUBJECT = 'userdn="ldap:///%s";)' % USER
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ensure_bytes(ACI_BODY))]

    log.info("Add an ACI 'allow (all)' by %s to the %s" % (USER,
                                                           CONTAINER_1))
    topology_st.standalone.modify_s(CONTAINER_1, mod)

    log.info("Add an ACI 'allow (all)' by %s to the %s" % (USER,
                                                           CONTAINER_2))
    topology_st.standalone.modify_s(CONTAINER_2, mod)


def test_enhanced_aci_modrnd(topology_st, env_setup):
    """Tests, that MODRDN operation is allowed,
    if user has ACI right '(all)' under superior entries,
    but doesn't have '(modrdn)'

    :id: 492cf2a9-2efe-4e3b-955e-85eca61d66b9
    :setup: Standalone instance
    :steps:
         1. Create two containers
         2. Create a user within "ou=test_ou_1,dc=example,dc=com"
         3. Add an aci with a rule "cn=test_user is allowed all" within these containers
         4. Run MODRDN operation on the "cn=test_user" and set "newsuperior" to
            the "ou=test_ou_2,dc=example,dc=com"
         5. Check there is no user under container one (ou=test_ou_1,dc=example,dc=com)
         6. Check there is a user under container two (ou=test_ou_2,dc=example,dc=com)

    :expectedresults:
         1. Two containers should be created
         2. User should be added successfully
         3. This should pass
         4. This should pass
         5. User should not be found under container ou=test_ou_1,dc=example,dc=com
         6. User should be found under container ou=test_ou_2,dc=example,dc=com
    """

    log.info("Bind as %s" % USER)

    topology_st.standalone.simple_bind_s(USER, USER_PWD)

    log.info("User MODRDN operation from %s to %s" % (CONTAINER_1,
                                                      CONTAINER_2))

    topology_st.standalone.rename_s(USER, "cn=%s" % USER_CN,
                                    newsuperior=CONTAINER_2, delold=1)

    log.info("Check there is no user in %s" % CONTAINER_1)
    entries = topology_st.standalone.search_s(CONTAINER_1,
                                              ldap.SCOPE_ONELEVEL,
                                              'cn=%s' % USER_CN)
    assert not entries

    log.info("Check there is our user in %s" % CONTAINER_2)
    entries = topology_st.standalone.search_s(CONTAINER_2,
                                              ldap.SCOPE_ONELEVEL,
                                              'cn=%s' % USER_CN)
    assert entries


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    # -v for additional verbose
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
