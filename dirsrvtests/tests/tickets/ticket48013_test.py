# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldapurl
import pytest
from ldap.ldapobject import SimpleLDAPObject
from ldap.syncrepl import SyncreplConsumer
from lib389.utils import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


class SyncObject(SimpleLDAPObject, SyncreplConsumer):
    def __init__(self, uri):
        # Init the ldap connection
        SimpleLDAPObject.__init__(self, uri)

    def sync_search(self, test_cookie):
        self.syncrepl_search('dc=example,dc=com', ldap.SCOPE_SUBTREE,
                             filterstr='(objectclass=*)', mode='refreshOnly',
                             cookie=test_cookie)

    def poll(self):
        self.syncrepl_poll(all=1)


def test_ticket48013(topology_st):
    '''
    Content Synchonization: Test that invalid cookies are caught
    '''

    cookies = ('#', '##', 'a#a#a', 'a#a#1')

    # Enable dynamic plugins
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    # Enable retro changelog
    topology_st.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    # Enbale content sync plugin
    topology_st.standalone.plugins.enable(name=PLUGIN_REPL_SYNC)

    # Set everything up
    ldap_url = ldapurl.LDAPUrl('ldap://%s:%s' % (HOST_STANDALONE,
                                                 PORT_STANDALONE))
    ldap_connection = SyncObject(ldap_url.initializeUrl())

    # Authenticate
    try:
        ldap_connection.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        print('Login to LDAP server failed: %s' % e.message['desc'])
        assert False

    # Test invalid cookies
    for invalid_cookie in cookies:
        log.info('Testing cookie: %s' % invalid_cookie)
        try:
            ldap_connection.sync_search(invalid_cookie)
            ldap_connection.poll()
            log.fatal('Invalid cookie accepted!')
            assert False
        except Exception as e:
            log.info('Invalid cookie correctly rejected: %s' % e.message['info'])
            pass

    # Success
    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
