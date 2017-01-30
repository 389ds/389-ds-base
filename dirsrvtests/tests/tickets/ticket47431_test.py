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

# Skip on older versions
pytestmark = pytest.mark.skipif(ds_is_older('1.3'), reason="Not implemented")

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DN_7BITPLUGIN = "cn=7-bit check,%s" % DN_PLUGIN
ATTRS = ["uid", "mail", "userpassword", ",", SUFFIX, None]


def test_ticket47431_0(topology_st):
    '''
    Enable 7 bit plugin
    '''
    log.info("Ticket 47431 - 0: Enable 7bit plugin...")
    topology_st.standalone.plugins.enable(name=PLUGIN_7_BIT_CHECK)


def test_ticket47431_1(topology_st):
    '''
    nsslapd-pluginarg0: uid
    nsslapd-pluginarg1: mail
    nsslapd-pluginarg2: userpassword <== repeat 27 times
    nsslapd-pluginarg3: ,
    nsslapd-pluginarg4: dc=example,dc=com

    The duplicated values are removed by str2entry_dupcheck as follows:
    [..] - str2entry_dupcheck: 27 duplicate values for attribute type nsslapd-pluginarg2
           detected in entry cn=7-bit check,cn=plugins,cn=config. Extra values ignored.
    '''

    log.info("Ticket 47431 - 1: Check 26 duplicate values are treated as one...")
    expected = "str2entry_dupcheck - .* duplicate values for attribute type nsslapd-pluginarg2 detected in entry cn=7-bit check,cn=plugins,cn=config."

    log.debug('modify_s %s' % DN_7BITPLUGIN)
    try:
        topology_st.standalone.modify_s(DN_7BITPLUGIN,
                                        [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', "uid"),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg1', "mail"),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg2', "userpassword"),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg3', ","),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg4', SUFFIX)])
    except ValueError:
        log.error('modify failed: Some problem occured with a value that was provided')
        assert False

    arg2 = "nsslapd-pluginarg2: userpassword"
    topology_st.standalone.stop(timeout=10)
    dse_ldif = topology_st.standalone.confdir + '/dse.ldif'
    os.system('mv %s %s.47431' % (dse_ldif, dse_ldif))
    os.system(
        'sed -e "s/\\(%s\\)/\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1\\n\\1/" %s.47431 > %s' % (
        arg2, dse_ldif, dse_ldif))
    topology_st.standalone.start(timeout=10)

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


def test_ticket47431_2(topology_st):
    '''
    nsslapd-pluginarg0: uid
    nsslapd-pluginarg0: mail
    nsslapd-pluginarg1: userpassword
    nsslapd-pluginarg2: ,
    nsslapd-pluginarg3: dc=example,dc=com
    ==>
    nsslapd-pluginarg0: uid
    nsslapd-pluginarg1: mail
    nsslapd-pluginarg2: userpassword
    nsslapd-pluginarg3: ,
    nsslapd-pluginarg4: dc=example,dc=com
    Should be logged in error log:
    [..] NS7bitAttr_Init - 0: uid
    [..] NS7bitAttr_Init - 1: userpassword
    [..] NS7bitAttr_Init - 2: mail
    [..] NS7bitAttr_Init - 3: ,
    [..] NS7bitAttr_Init - 4: dc=example,dc=com
    '''

    log.info("Ticket 47431 - 2: Check two values belonging to one arg is fixed...")

    try:
        topology_st.standalone.modify_s(DN_7BITPLUGIN,
                                        [(ldap.MOD_REPLACE, 'nsslapd-pluginarg0', "uid"),
                                         (ldap.MOD_ADD, 'nsslapd-pluginarg0', "mail"),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg1', "userpassword"),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg2', ","),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg3', SUFFIX),
                                         (ldap.MOD_DELETE, 'nsslapd-pluginarg4', None)])
    except ValueError:
        log.error('modify failed: Some problem occured with a value that was provided')
        assert False

    # PLUGIN LOG LEVEL
    topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '65536')])

    topology_st.standalone.restart(timeout=10)

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


def test_ticket47431_3(topology_st):
    '''
    nsslapd-pluginarg1: uid
    nsslapd-pluginarg3: mail
    nsslapd-pluginarg5: userpassword
    nsslapd-pluginarg7: ,
    nsslapd-pluginarg9: dc=example,dc=com
    ==>
    nsslapd-pluginarg0: uid
    nsslapd-pluginarg1: mail
    nsslapd-pluginarg2: userpassword
    nsslapd-pluginarg3: ,
    nsslapd-pluginarg4: dc=example,dc=com
    Should be logged in error log:
    [..] NS7bitAttr_Init - 0: uid
    [..] NS7bitAttr_Init - 1: userpassword
    [..] NS7bitAttr_Init - 2: mail
    [..] NS7bitAttr_Init - 3: ,
    [..] NS7bitAttr_Init - 4: dc=example,dc=com
    '''

    log.info("Ticket 47431 - 3: Check missing args are fixed...")

    try:
        topology_st.standalone.modify_s(DN_7BITPLUGIN,
                                        [(ldap.MOD_DELETE, 'nsslapd-pluginarg0', None),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg1', "uid"),
                                         (ldap.MOD_DELETE, 'nsslapd-pluginarg2', None),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg3', "mail"),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg5', "userpassword"),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg7', ","),
                                         (ldap.MOD_REPLACE, 'nsslapd-pluginarg9', SUFFIX)])
    except ValueError:
        log.error('modify failed: Some problem occured with a value that was provided')
        assert False

    # PLUGIN LOG LEVEL
    topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '65536')])

    topology_st.standalone.stop(timeout=10)
    os.system('mv %s %s.47431' % (topology_st.standalone.errlog, topology_st.standalone.errlog))
    os.system('touch %s' % (topology_st.standalone.errlog))
    topology_st.standalone.start(timeout=10)

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
