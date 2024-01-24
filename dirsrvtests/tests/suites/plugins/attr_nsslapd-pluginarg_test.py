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

from lib389._constants import DEFAULT_SUFFIX, DN_PLUGIN, SUFFIX, PLUGIN_7_BIT_CHECK

# Skip on older versions
pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3'), reason="Not implemented")]

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DN_7BITPLUGIN = "cn=7-bit check,%s" % DN_PLUGIN
ATTRS = ["uid", "mail", "userpassword", ",", SUFFIX, None]


@pytest.fixture(scope="module")
def enable_plugin(topology_st):
    """Enabling the 7-bit plugin for the
    environment setup"""
    log.info("Ticket 47431 - 0: Enable 7bit plugin...")
    topology_st.standalone.plugins.enable(name=PLUGIN_7_BIT_CHECK)


def test_duplicate_values(topology_st, enable_plugin):
    """Check 26 duplicate values are treated as one

    :id: b23e04f1-2757-42cc-b3a2-26426c903f6d
    :setup: Standalone instance, enable 7bit plugin
    :steps:
        1. Modify the entry for cn=7-bit check,cn=plugins,cn=config as :
           nsslapd-pluginarg0 : uid
           nsslapd-pluginarg1 : mail
           nsslapd-pluginarg2 : userpassword
           nsslapd-pluginarg3 : ,
           nsslapd-pluginarg4 : dc=example,dc=com
        2. Set nsslapd-pluginarg2 to 'userpassword' for multiple time (ideally 27)
        3. Check whether duplicate values are treated as one
    :expectedresults:
        1. It should be modified successfully
        2. It should be successful
        3. It should be successful
     """

    log.info("Ticket 47431 - 1: Check 26 duplicate values are treated as one...")
    expected = "str2entry_dupcheck.* duplicate values for attribute type nsslapd-pluginarg2 detected in entry cn=7-bit check,cn=plugins,cn=config."

    log.debug('modify_s %s' % DN_7BITPLUGIN)
    topology_st.standalone.modify_s(DN_7BITPLUGIN,
                                    [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', b"uid"),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg1', b"mail"),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg2', b"userpassword"),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg3', b","),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg4', ensure_bytes(SUFFIX))])

    arg2 = "nsslapd-pluginarg2: userpassword"
    topology_st.standalone.stop()
    dse_ldif = topology_st.standalone.confdir + '/dse.ldif'
    os.system('mv %s %s.47431' % (dse_ldif, dse_ldif))
    os.system(
        'sed -e "s/\\(%s\\)/\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1/" %s.47431 > %s' % (
        arg2, dse_ldif, dse_ldif))
    topology_st.standalone.start()

    cmdline = 'egrep -i "%s" %s' % (expected, topology_st.standalone.errlog)
    p = os.popen(cmdline, "r")
    line = p.readline()
    if line == "":
        log.error('Expected error "%s" not logged in %s' % (expected, topology_st.standalone.errlog))
        assert False
    else:
        log.debug('line: %s' % line)
        log.info('Expected error "%s" logged in %s' % (expected, topology_st.standalone.errlog))

    log.info("Ticket 47431 - 1: done")


def test_multiple_value(topology_st, enable_plugin):
    """Check two values belonging to one arg is fixed

    :id: 20c802bc-332f-4e8d-bcfb-8cd28123d695
    :setup: Standalone instance, enable 7bit plugin
    :steps:
        1. Modify the entry for cn=7-bit check,cn=plugins,cn=config as :
           nsslapd-pluginarg0 : uid
           nsslapd-pluginarg0 : mail
           nsslapd-pluginarg1 : userpassword
           nsslapd-pluginarg2 : ,
           nsslapd-pluginarg3 : dc=example,dc=com
           nsslapd-pluginarg4 : None
           (Note : While modifying add two attributes entries for nsslapd-pluginarg0)

        2. Check two values belonging to one arg is fixed
    :expectedresults:
        1. Entries should be modified successfully
        2. Operation should be successful
     """

    log.info("Ticket 47431 - 2: Check two values belonging to one arg is fixed...")

    topology_st.standalone.modify_s(DN_7BITPLUGIN,
                                    [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', b"uid"),
                                     (ldap.MOD_ADD, 'nsslapd-pluginarg0', b"mail"),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg1', b"userpassword"),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg2', b","),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg3', ensure_bytes(SUFFIX)),
                                     (ldap.MOD_DELETE, 'nsslapd-pluginarg4', None)])

    # PLUGIN LOG LEVEL
    topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'65536')])

    topology_st.standalone.restart()

    cmdline = 'egrep -i %s %s' % ("NS7bitAttr_Init", topology_st.standalone.errlog)
    p = os.popen(cmdline, "r")
    i = 0
    while ATTRS[i]:
        line = p.readline()
        log.debug('line - %s' % line)
        log.debug('ATTRS[%d] %s' % (i, ATTRS[i]))
        if line == "":
            break
        elif line.find(ATTRS[i]) >= 0:
            log.debug('%s was logged' % ATTRS[i])
        else:
            log.error('%s was not logged.' % ATTRS[i])
            assert False
        i = i + 1

    log.info("Ticket 47431 - 2: done")


def test_missing_args(topology_st, enable_plugin):
    """Check missing args are fixed

    :id: b2814399-7ed2-4fe0-981d-b0bdbbe31cfb
    :setup: Standalone instance, enable 7bit plugin
    :steps:
        1. Modify the entry for cn=7-bit check,cn=plugins,cn=config as :
           nsslapd-pluginarg0 : None
           nsslapd-pluginarg1 : uid
           nsslapd-pluginarg2 : None
           nsslapd-pluginarg3 : mail
           nsslapd-pluginarg5 : userpassword
           nsslapd-pluginarg7 : ,
           nsslapd-pluginarg9 : dc=example,dc=com
           (Note: While modifying add 2 entries as None)

        2. Change the nsslapd-errorlog-level to 65536
        3. Check missing agrs are fixed
    :expectedresults:
        1. Entries should be modified successfully
        2. Operation should be successful
        3. Operation should be successful
     """

    log.info("Ticket 47431 - 3: Check missing args are fixed...")

    topology_st.standalone.modify_s(DN_7BITPLUGIN,
                                    [(ldap.MOD_DELETE, 'nsslapd-pluginarg0', None),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg1', b"uid"),
                                     (ldap.MOD_DELETE, 'nsslapd-pluginarg2', None),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg3', b"mail"),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg5', b"userpassword"),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg7', b","),
                                     (ldap.MOD_REPLACE, 'nsslapd-pluginarg9', ensure_bytes(SUFFIX))])

    # PLUGIN LOG LEVEL
    topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'65536')])

    topology_st.standalone.stop()
    os.system('mv %s %s.47431' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    topology_st.standalone.start()

    cmdline = 'egrep -i %s %s' % ("NS7bitAttr_Init", topology_st.standalone.errlog)
    p = os.popen(cmdline, "r")
    i = 0
    while ATTRS[i]:
        line = p.readline()
        if line == "":
            break
        elif line.find(ATTRS[i]) >= 0:
            log.debug('%s was logged' % ATTRS[i])
        else:
            log.error('%s was not logged.' % ATTRS[i])
            assert False
        i = i + 1

    log.info("Ticket 47431 - 3: done")
    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
