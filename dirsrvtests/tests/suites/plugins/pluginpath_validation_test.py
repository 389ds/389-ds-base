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


@pytest.mark.ds47384
def test_pluginpath_validation(topology_st):
    """Test pluginpath validation: relative and absolute paths
    With the inclusion of ticket 47601 - we do allow plugin paths
    outside the default location

    :id: 99f1fb2f-051d-4fd9-93d0-592dcd9b4c22
    :setup: Standalone instance
    :steps:
         1. Copy the library to a temporary directory
         2. Add valid plugin paths
                * using the absolute path to the current library
                * using new remote location
         3. Set plugin path back to the default
         4. Check invalid path (no library present)
         5. Check invalid relative path (no library present)

    :expectedresults:
         1. This should pass
         2. This should pass
         3. This should pass
         4. This should fail
         5. This should fail
    """

    if os.geteuid() != 0:
        log.warning('This script must be run as root')
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
        log.warning('Failed to copy ' + plugin_dir +
                 '/libwhoami-plugin.la to the tmp directory, error: '
                 + e.strerror)

    #
    # Test adding valid plugin paths
    #
    # Try using the absolute path to the current library
    topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                 'nsslapd-pluginPath', ensure_bytes('%s/libwhoami-plugin' % plugin_dir))])

    # Try using new remote location
    topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                 'nsslapd-pluginPath', ensure_bytes('%s/libwhoami-plugin' % tmp_dir))])

    # Set plugin path back to the default
    topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                 'nsslapd-pluginPath', b'libwhoami-plugin')])

    #
    # Test invalid path (no library present)
    #
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'nsslapd-pluginPath', b'/bin/libwhoami-plugin')])
        # No exception?! This is an error
        log.error('Invalid plugin path was incorrectly accepted by the server!')

    #
    # Test invalid relative path (no library present)
    #
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        topology_st.standalone.modify_s(PLUGIN_DN, [(ldap.MOD_REPLACE,
                                                     'nsslapd-pluginPath', b'../libwhoami-plugin')])
        # No exception?! This is an error
        log.error('Invalid plugin path was incorrectly accepted by the server!')

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

