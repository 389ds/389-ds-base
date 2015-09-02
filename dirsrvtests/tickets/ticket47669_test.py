# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from ldap.controls import SimplePagedResultsControl
from ldap.controls.simple import GetEffectiveRightsControl

log = logging.getLogger(__name__)

installation_prefix = None

CHANGELOG = 'cn=changelog5,cn=config'
RETROCHANGELOG = 'cn=Retro Changelog Plugin,cn=plugins,cn=config'

MAXAGE = 'nsslapd-changelogmaxage'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'
COMPACTDBINTERVAL = 'nsslapd-changelogcompactdb-interval'

FILTER = '(cn=*)'


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def test_ticket47669_init(topo):
    """
    Add cn=changelog5,cn=config
    Enable cn=Retro Changelog Plugin,cn=plugins,cn=config
    """
    log.info('Testing Ticket 47669 - Test duration syntax in the changelogs')

    # bind as directory manager
    topo.standalone.log.info("Bind as %s" % DN_DM)
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    try:
        changelogdir = "%s/changelog" % topo.standalone.dbdir
        topo.standalone.add_s(Entry((CHANGELOG,
                                     {'objectclass': 'top extensibleObject'.split(),
                                      'nsslapd-changelogdir': changelogdir})))
    except ldap.LDAPError as e:
        log.error('Failed to add ' + CHANGELOG + ': error ' + e.message['desc'])
        assert False

    try:
        topo.standalone.modify_s(RETROCHANGELOG, [(ldap.MOD_REPLACE, 'nsslapd-pluginEnabled', 'on')])
    except ldap.LDAPError as e:
        log.error('Failed to enable ' + RETROCHANGELOG + ': error ' + e.message['desc'])
        assert False

    # restart the server
    topo.standalone.restart(timeout=10)


def add_and_check(topo, plugin, attr, val, isvalid):
    """
    Helper function to add/replace attr: val and check the added value
    """
    if isvalid:
        log.info('Test %s: %s -- valid' % (attr, val))
        try:
            topo.standalone.modify_s(plugin, [(ldap.MOD_REPLACE, attr, val)])
        except ldap.LDAPError as e:
            log.error('Failed to add ' + attr + ': ' + val + ' to ' + plugin + ': error ' + e.message['desc'])
            assert False
    else:
        log.info('Test %s: %s -- invalid' % (attr, val))
        if plugin == CHANGELOG:
            try:
                topo.standalone.modify_s(plugin, [(ldap.MOD_REPLACE, attr, val)])
            except ldap.LDAPError as e:
                log.error('Expectedly failed to add ' + attr + ': ' + val + ' to ' + plugin + ': error ' + e.message['desc'])
        else:
            try:
                topo.standalone.modify_s(plugin, [(ldap.MOD_REPLACE, attr, val)])
            except ldap.LDAPError as e:
                log.error('Failed to add ' + attr + ': ' + val + ' to ' + plugin + ': error ' + e.message['desc'])

    try:
        entries = topo.standalone.search_s(plugin, ldap.SCOPE_BASE, FILTER, [attr])
        if isvalid:
            if not entries[0].hasValue(attr, val):
                log.fatal('%s does not have expected (%s: %s)' % (plugin, attr, val))
                assert False
        else:
            if plugin == CHANGELOG:
                if entries[0].hasValue(attr, val):
                    log.fatal('%s has unexpected (%s: %s)' % (plugin, attr, val))
                    assert False
            else:
                if not entries[0].hasValue(attr, val):
                    log.fatal('%s does not have expected (%s: %s)' % (plugin, attr, val))
                    assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (plugin, e.message['desc']))
        assert False


def test_ticket47669_changelog_maxage(topo):
    """
    Test nsslapd-changelogmaxage in cn=changelog5,cn=config
    """
    log.info('1. Test nsslapd-changelogmaxage in cn=changelog5,cn=config')

    # bind as directory manager
    topo.standalone.log.info("Bind as %s" % DN_DM)
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topo, CHANGELOG, MAXAGE, '12345', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '10s', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '30M', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '12h', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '2D', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '4w', True)
    add_and_check(topo, CHANGELOG, MAXAGE, '-123', False)
    add_and_check(topo, CHANGELOG, MAXAGE, 'xyz', False)


def test_ticket47669_changelog_triminterval(topo):
    """
    Test nsslapd-changelogtrim-interval in cn=changelog5,cn=config
    """
    log.info('2. Test nsslapd-changelogtrim-interval in cn=changelog5,cn=config')

    # bind as directory manager
    topo.standalone.log.info("Bind as %s" % DN_DM)
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '12345', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '10s', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '30M', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '12h', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '2D', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '4w', True)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, '-123', False)
    add_and_check(topo, CHANGELOG, TRIMINTERVAL, 'xyz', False)


def test_ticket47669_changelog_compactdbinterval(topo):
    """
    Test nsslapd-changelogcompactdb-interval in cn=changelog5,cn=config
    """
    log.info('3. Test nsslapd-changelogcompactdb-interval in cn=changelog5,cn=config')

    # bind as directory manager
    topo.standalone.log.info("Bind as %s" % DN_DM)
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '12345', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '10s', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '30M', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '12h', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '2D', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '4w', True)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, '-123', False)
    add_and_check(topo, CHANGELOG, COMPACTDBINTERVAL, 'xyz', False)


def test_ticket47669_retrochangelog_maxage(topo):
    """
    Test nsslapd-changelogmaxage in cn=Retro Changelog Plugin,cn=plugins,cn=config
    """
    log.info('4. Test nsslapd-changelogmaxage in cn=Retro Changelog Plugin,cn=plugins,cn=config')

    # bind as directory manager
    topo.standalone.log.info("Bind as %s" % DN_DM)
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topo, RETROCHANGELOG, MAXAGE, '12345', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '10s', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '30M', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '12h', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '2D', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '4w', True)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, '-123', False)
    add_and_check(topo, RETROCHANGELOG, MAXAGE, 'xyz', False)

    topo.standalone.log.info("ticket47669 was successfully verified.")


def test_ticket47669_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    """
    run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
    To run isolated without py.test, you need to
    - edit this file and comment '@pytest.fixture' line before 'topology' function.
    - set the installation prefix
    - run this program
    """
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_ticket47669_init(topo)
    test_ticket47669_changelog_maxage(topo)
    test_ticket47669_changelog_triminterval(topo)
    test_ticket47669_changelog_compactdbinterval(topo)
    test_ticket47669_retrochangelog_maxage(topo)
    test_ticket47669_final(topo)

if __name__ == '__main__':
    run_isolated()

