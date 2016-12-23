# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)

CHANGELOG = 'cn=changelog5,cn=config'
RETROCHANGELOG = 'cn=Retro Changelog Plugin,cn=plugins,cn=config'

MAXAGE = 'nsslapd-changelogmaxage'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'
COMPACTDBINTERVAL = 'nsslapd-changelogcompactdb-interval'

FILTER = '(cn=*)'


def test_ticket47669_init(topology_st):
    """
    Add cn=changelog5,cn=config
    Enable cn=Retro Changelog Plugin,cn=plugins,cn=config
    """
    log.info('Testing Ticket 47669 - Test duration syntax in the changelogs')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    try:
        changelogdir = os.path.join(os.path.dirname(topology_st.standalone.dbdir), 'changelog')
        topology_st.standalone.add_s(Entry((CHANGELOG,
                                            {'objectclass': 'top extensibleObject'.split(),
                                             'nsslapd-changelogdir': changelogdir})))
    except ldap.LDAPError as e:
        log.error('Failed to add ' + CHANGELOG + ': error ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(RETROCHANGELOG, [(ldap.MOD_REPLACE, 'nsslapd-pluginEnabled', 'on')])
    except ldap.LDAPError as e:
        log.error('Failed to enable ' + RETROCHANGELOG + ': error ' + e.message['desc'])
        assert False

    # restart the server
    topology_st.standalone.restart(timeout=10)


def add_and_check(topology_st, plugin, attr, val, isvalid):
    """
    Helper function to add/replace attr: val and check the added value
    """
    if isvalid:
        log.info('Test %s: %s -- valid' % (attr, val))
        try:
            topology_st.standalone.modify_s(plugin, [(ldap.MOD_REPLACE, attr, val)])
        except ldap.LDAPError as e:
            log.error('Failed to add ' + attr + ': ' + val + ' to ' + plugin + ': error ' + e.message['desc'])
            assert False
    else:
        log.info('Test %s: %s -- invalid' % (attr, val))
        if plugin == CHANGELOG:
            try:
                topology_st.standalone.modify_s(plugin, [(ldap.MOD_REPLACE, attr, val)])
            except ldap.LDAPError as e:
                log.error('Expectedly failed to add ' + attr + ': ' + val +
                          ' to ' + plugin + ': error ' + e.message['desc'])
        else:
            try:
                topology_st.standalone.modify_s(plugin, [(ldap.MOD_REPLACE, attr, val)])
            except ldap.LDAPError as e:
                log.error('Failed to add ' + attr + ': ' + val + ' to ' + plugin + ': error ' + e.message['desc'])

    try:
        entries = topology_st.standalone.search_s(plugin, ldap.SCOPE_BASE, FILTER, [attr])
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


def test_ticket47669_changelog_maxage(topology_st):
    """
    Test nsslapd-changelogmaxage in cn=changelog5,cn=config
    """
    log.info('1. Test nsslapd-changelogmaxage in cn=changelog5,cn=config')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topology_st, CHANGELOG, MAXAGE, '12345', True)
    add_and_check(topology_st, CHANGELOG, MAXAGE, '10s', True)
    add_and_check(topology_st, CHANGELOG, MAXAGE, '30M', True)
    add_and_check(topology_st, CHANGELOG, MAXAGE, '12h', True)
    add_and_check(topology_st, CHANGELOG, MAXAGE, '2D', True)
    add_and_check(topology_st, CHANGELOG, MAXAGE, '4w', True)
    add_and_check(topology_st, CHANGELOG, MAXAGE, '-123', False)
    add_and_check(topology_st, CHANGELOG, MAXAGE, 'xyz', False)


def test_ticket47669_changelog_triminterval(topology_st):
    """
    Test nsslapd-changelogtrim-interval in cn=changelog5,cn=config
    """
    log.info('2. Test nsslapd-changelogtrim-interval in cn=changelog5,cn=config')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topology_st, CHANGELOG, TRIMINTERVAL, '12345', True)
    add_and_check(topology_st, CHANGELOG, TRIMINTERVAL, '10s', True)
    add_and_check(topology_st, CHANGELOG, TRIMINTERVAL, '30M', True)
    add_and_check(topology_st, CHANGELOG, TRIMINTERVAL, '12h', True)
    add_and_check(topology_st, CHANGELOG, TRIMINTERVAL, '2D', True)
    add_and_check(topology_st, CHANGELOG, TRIMINTERVAL, '4w', True)
    add_and_check(topology_st, CHANGELOG, TRIMINTERVAL, '-123', False)
    add_and_check(topology_st, CHANGELOG, TRIMINTERVAL, 'xyz', False)


def test_ticket47669_changelog_compactdbinterval(topology_st):
    """
    Test nsslapd-changelogcompactdb-interval in cn=changelog5,cn=config
    """
    log.info('3. Test nsslapd-changelogcompactdb-interval in cn=changelog5,cn=config')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topology_st, CHANGELOG, COMPACTDBINTERVAL, '12345', True)
    add_and_check(topology_st, CHANGELOG, COMPACTDBINTERVAL, '10s', True)
    add_and_check(topology_st, CHANGELOG, COMPACTDBINTERVAL, '30M', True)
    add_and_check(topology_st, CHANGELOG, COMPACTDBINTERVAL, '12h', True)
    add_and_check(topology_st, CHANGELOG, COMPACTDBINTERVAL, '2D', True)
    add_and_check(topology_st, CHANGELOG, COMPACTDBINTERVAL, '4w', True)
    add_and_check(topology_st, CHANGELOG, COMPACTDBINTERVAL, '-123', False)
    add_and_check(topology_st, CHANGELOG, COMPACTDBINTERVAL, 'xyz', False)


def test_ticket47669_retrochangelog_maxage(topology_st):
    """
    Test nsslapd-changelogmaxage in cn=Retro Changelog Plugin,cn=plugins,cn=config
    """
    log.info('4. Test nsslapd-changelogmaxage in cn=Retro Changelog Plugin,cn=plugins,cn=config')

    # bind as directory manager
    topology_st.standalone.log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    add_and_check(topology_st, RETROCHANGELOG, MAXAGE, '12345', True)
    add_and_check(topology_st, RETROCHANGELOG, MAXAGE, '10s', True)
    add_and_check(topology_st, RETROCHANGELOG, MAXAGE, '30M', True)
    add_and_check(topology_st, RETROCHANGELOG, MAXAGE, '12h', True)
    add_and_check(topology_st, RETROCHANGELOG, MAXAGE, '2D', True)
    add_and_check(topology_st, RETROCHANGELOG, MAXAGE, '4w', True)
    add_and_check(topology_st, RETROCHANGELOG, MAXAGE, '-123', False)
    add_and_check(topology_st, RETROCHANGELOG, MAXAGE, 'xyz', False)

    topology_st.standalone.log.info("ticket47669 was successfully verified.")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
