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

from lib389._constants import DEFAULT_SUFFIX, PLUGIN_WHOAMI

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket47384(topology_st):
    '''
    Test pluginpath validation: relative and absolute paths

    With the inclusion of ticket 47601 - we do allow plugin paths
    outside the default location
    '''

    if os.geteuid() != 0:
        log.warn('This script must be run as root')
        return

    os.system('setenforce 0')

    PLUGIN_DN = 'cn=%s,cn=plugins,cn=config' % PLUGIN_WHOAMI
    tmp_dir = topology_st.standalone.get_tmp_dir()
    plugin_dir = topology_st.standalone.get_plugin_dir()

    # Copy the library to our tmp directory
    try:
        shutil.copy('%s/libwhoami-plugin.so' % plugin_dir, tmp_dir)
    except IOError as e:
        log.fatal('Failed to copy %s/libwhoami-plugin.so to the tmp directory %s, error: %s' % (
        plugin_dir, tmp_dir, e.strerror))
        assert False
    try:
        shutil.copy('%s/libwhoami-plugin.la' % plugin_dir, tmp_dir)
    except IOError as e:
        log.warn('Failed to copy ' + plugin_dir +
                 '/libwhoami-plugin.la to the tmp directory, error: '
                 + e.strerror)

    #
    # Test adding valid plugin paths
    #
    # Try using the absolute path to the current library
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'nsslapd-pluginPath', '%s/libwhoami-plugin' % plugin_dir)])
    except ldap.LDAPError as e:
        log.error('Failed to set valid plugin path (%s): error (%s)' %
                  ('%s/libwhoami-plugin' % plugin_dir, e.message['desc']))
        assert False

    # Try using new remote location
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'nsslapd-pluginPath', '%s/libwhoami-plugin' % tmp_dir)])
    except ldap.LDAPError as e:
        log.error('Failed to set valid plugin path (%s): error (%s)' %
                  ('%s/libwhoami-plugin' % tmp_dir, e.message['desc']))
        assert False

    # Set plugin path back to the default
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'nsslapd-pluginPath', 'libwhoami-plugin')])
    except ldap.LDAPError as e:
        log.error('Failed to set valid relative plugin path (%s): error (%s)' %
                  ('libwhoami-plugin' % tmp_dir, e.message['desc']))
        assert False

    #
    # Test invalid path (no library present)
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'nsslapd-pluginPath', '/bin/libwhoami-plugin')])
        # No exception?! This is an error
        log.error('Invalid plugin path was incorrectly accepted by the server!')
        assert False
    except ldap.UNWILLING_TO_PERFORM:
        # Correct, operation should be rejected
        pass
    except ldap.LDAPError as e:
        log.error('Failed to set invalid plugin path (%s): error (%s)' %
                  ('/bin/libwhoami-plugin', e.message['desc']))

    #
    # Test invalid relative path (no library present)
    #
    try:
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'nsslapd-pluginPath', '../libwhoami-plugin')])
        # No exception?! This is an error
        log.error('Invalid plugin path was incorrectly accepted by the server!')
        assert False
    except ldap.UNWILLING_TO_PERFORM:
        # Correct, operation should be rejected
        pass
    except ldap.LDAPError as e:
        log.error('Failed to set invalid plugin path (%s): error (%s)' %
                  ('../libwhoami-plugin', e.message['desc']))

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
