# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

CONTAINER_1_OU = 'test_ou_1'
CONTAINER_2_OU = 'test_ou_2'
CONTAINER_1 = 'ou=%s,dc=example,dc=com' % CONTAINER_1_OU
CONTAINER_2 = 'ou=%s,dc=example,dc=com' % CONTAINER_2_OU
USER_CN = 'test_user'
USER_PWD = 'Secret123'
USER = 'cn=%s,%s' % (USER_CN, CONTAINER_1)


@pytest.fixture(scope="module")
def env_setup(topology_st):
    """Adds two containers, one user and two ACI rules"""

    try:
        log.info("Add a container: %s" % CONTAINER_1)
        topology_st.standalone.add_s(Entry((CONTAINER_1,
                                            {'objectclass': 'top',
                                             'objectclass': 'organizationalunit',
                                             'ou': CONTAINER_1_OU,
                                             })))

        log.info("Add a container: %s" % CONTAINER_2)
        topology_st.standalone.add_s(Entry((CONTAINER_2,
                                            {'objectclass': 'top',
                                             'objectclass': 'organizationalunit',
                                             'ou': CONTAINER_2_OU,
                                             })))

        log.info("Add a user: %s" % USER)
        topology_st.standalone.add_s(Entry((USER,
                                            {'objectclass': 'top person'.split(),
                                             'cn': USER_CN,
                                             'sn': USER_CN,
                                             'userpassword': USER_PWD
                                             })))
    except ldap.LDAPError as e:
        log.error('Failed to add object to database: %s' % e.message['desc'])
        assert False

    ACI_TARGET = '(targetattr="*")'
    ACI_ALLOW = '(version 3.0; acl "All rights for %s"; allow (all) ' % USER
    ACI_SUBJECT = 'userdn="ldap:///%s";)' % USER
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]

    try:
        log.info("Add an ACI 'allow (all)' by %s to the %s" % (USER,
                                                               CONTAINER_1))
        topology_st.standalone.modify_s(CONTAINER_1, mod)

        log.info("Add an ACI 'allow (all)' by %s to the %s" % (USER,
                                                               CONTAINER_2))
        topology_st.standalone.modify_s(CONTAINER_2, mod)
    except ldap.LDAPError as e:
        log.fatal('Failed to add ACI: error (%s)' % (e.message['desc']))
        assert False


def test_ticket47553(topology_st, env_setup):
    """Tests, that MODRDN operation is allowed,
    if user has ACI right '(all)' under superior entries,
    but doesn't have '(modrdn)'
    """

    log.info("Bind as %s" % USER)
    try:
        topology_st.standalone.simple_bind_s(USER, USER_PWD)
    except ldap.LDAPError as e:
        log.error('Bind failed for %s, error %s' % (USER, e.message['desc']))
        assert False

    log.info("User MODRDN operation from %s to %s" % (CONTAINER_1,
                                                      CONTAINER_2))
    try:
        topology_st.standalone.rename_s(USER, "cn=%s" % USER_CN,
                                        newsuperior=CONTAINER_2, delold=1)
    except ldap.LDAPError as e:
        log.error('MODRDN failed for %s, error %s' % (USER, e.message['desc']))
        assert False

    try:
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
    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    # -v for additional verbose
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
