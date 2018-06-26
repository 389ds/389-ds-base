# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)


def test_ticket47937(topology_st):
    """
        Test that DNA plugin only accepts valid attributes for "dnaType"
    """

    log.info("Creating \"ou=people\"...")
    try:
        topology_st.standalone.add_s(Entry(('ou=people,' + SUFFIX, {
            'objectclass': 'top organizationalunit'.split(),
            'ou': 'people'
        })))

    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.error('Failed to add ou=people org unit: error ' + e.args[0]['desc'])
        assert False

    log.info("Creating \"ou=ranges\"...")
    try:
        topology_st.standalone.add_s(Entry(('ou=ranges,' + SUFFIX, {
            'objectclass': 'top organizationalunit'.split(),
            'ou': 'ranges'
        })))

    except ldap.LDAPError as e:
        log.error('Failed to add ou=ranges org unit: error ' + e.args[0]['desc'])
        assert False

    log.info("Creating \"cn=entry\"...")
    try:
        topology_st.standalone.add_s(Entry(('cn=entry,ou=people,' + SUFFIX, {
            'objectclass': 'top groupofuniquenames'.split(),
            'cn': 'entry'
        })))

    except ldap.LDAPError as e:
        log.error('Failed to add test entry: error ' + e.args[0]['desc'])
        assert False

    log.info("Creating DNA shared config entry...")
    try:
        topology_st.standalone.add_s(Entry(('dnaHostname=localhost.localdomain+dnaPortNum=389,ou=ranges,%s' % SUFFIX, {
            'objectclass': 'top dnaSharedConfig'.split(),
            'dnaHostname': 'localhost.localdomain',
            'dnaPortNum': '389',
            'dnaSecurePortNum': '636',
            'dnaRemainingValues': '9501'
        })))

    except ldap.LDAPError as e:
        log.error('Failed to add shared config entry: error ' + e.args[0]['desc'])
        assert False

    log.info("Add dna plugin config entry...")
    try:
        topology_st.standalone.add_s(
            Entry(('cn=dna config,cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config', {
                'objectclass': 'top dnaPluginConfig'.split(),
                'dnaType': 'description',
                'dnaMaxValue': '10000',
                'dnaMagicRegen': '0',
                'dnaFilter': '(objectclass=top)',
                'dnaScope': 'ou=people,%s' % SUFFIX,
                'dnaNextValue': '500',
                'dnaSharedCfgDN': 'ou=ranges,%s' % SUFFIX
            })))

    except ldap.LDAPError as e:
        log.error('Failed to add DNA config entry: error ' + e.args[0]['desc'])
        assert False

    log.info("Enable the DNA plugin...")
    try:
        topology_st.standalone.plugins.enable(name=PLUGIN_DNA)
    except e:
        log.error("Failed to enable DNA Plugin: error " + e.args[0]['desc'])
        assert False

    log.info("Restarting the server...")
    topology_st.standalone.stop(timeout=120)
    time.sleep(1)
    topology_st.standalone.start(timeout=120)
    time.sleep(3)

    log.info("Apply an invalid attribute to the DNA config(dnaType: foo)...")

    try:
        topology_st.standalone.modify_s('cn=dna config,cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config',
                                        [(ldap.MOD_REPLACE, 'dnaType', b'foo')])
    except ldap.LDAPError as e:
        log.info('Operation failed as expected (error: %s)' % e.args[0]['desc'])
    else:
        log.error('Operation incorectly succeeded!  Test Failed!')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
