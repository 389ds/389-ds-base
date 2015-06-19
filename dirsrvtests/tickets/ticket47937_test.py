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
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

log = logging.getLogger(__name__)

installation_prefix = None


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


def test_ticket47937(topology):
    """
        Test that DNA plugin only accepts valid attributes for "dnaType"
    """

    log.info("Creating \"ou=people\"...")
    try:
        topology.standalone.add_s(Entry(('ou=people,' + SUFFIX, {
                                         'objectclass': 'top organizationalunit'.split(),
                                         'ou': 'people'
                                         })))

    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError, e:
        log.error('Failed to add ou=people org unit: error ' + e.message['desc'])
        assert False

    log.info("Creating \"ou=ranges\"...")
    try:
        topology.standalone.add_s(Entry(('ou=ranges,' + SUFFIX, {
                                         'objectclass': 'top organizationalunit'.split(),
                                         'ou': 'ranges'
                                         })))

    except ldap.LDAPError, e:
        log.error('Failed to add ou=ranges org unit: error ' + e.message['desc'])
        assert False

    log.info("Creating \"cn=entry\"...")
    try:
        topology.standalone.add_s(Entry(('cn=entry,ou=people,' + SUFFIX, {
                                         'objectclass': 'top groupofuniquenames'.split(),
                                         'cn': 'entry'
                                         })))

    except ldap.LDAPError, e:
        log.error('Failed to add test entry: error ' + e.message['desc'])
        assert False

    log.info("Creating DNA shared config entry...")
    try:
        topology.standalone.add_s(Entry(('dnaHostname=localhost.localdomain+dnaPortNum=389,ou=ranges,%s' % SUFFIX, {
                                         'objectclass': 'top dnaSharedConfig'.split(),
                                         'dnaHostname': 'localhost.localdomain',
                                         'dnaPortNum': '389',
                                         'dnaSecurePortNum': '636',
                                         'dnaRemainingValues': '9501'
                                         })))

    except ldap.LDAPError, e:
        log.error('Failed to add shared config entry: error ' + e.message['desc'])
        assert False

    log.info("Add dna plugin config entry...")
    try:
        topology.standalone.add_s(Entry(('cn=dna config,cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config', {
                                         'objectclass': 'top dnaPluginConfig'.split(),
                                         'dnaType': 'description',
                                         'dnaMaxValue': '10000',
                                         'dnaMagicRegen': '0',
                                         'dnaFilter': '(objectclass=top)',
                                         'dnaScope': 'ou=people,%s' % SUFFIX,
                                         'dnaNextValue': '500',
                                         'dnaSharedCfgDN': 'ou=ranges,%s' % SUFFIX
                                         })))

    except ldap.LDAPError, e:
        log.error('Failed to add DNA config entry: error ' + e.message['desc'])
        assert False

    log.info("Enable the DNA plugin...")
    try:
        topology.standalone.plugins.enable(name=PLUGIN_DNA)
    except e:
        log.error("Failed to enable DNA Plugin: error " + e.message['desc'])
        assert False

    log.info("Restarting the server...")
    topology.standalone.stop(timeout=120)
    time.sleep(1)
    topology.standalone.start(timeout=120)
    time.sleep(3)

    log.info("Apply an invalid attribute to the DNA config(dnaType: foo)...")

    try:
        topology.standalone.modify_s('cn=dna config,cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config',
                                     [(ldap.MOD_REPLACE, 'dnaType', 'foo')])
    except ldap.LDAPError, e:
        log.info('Operation failed as expected (error: %s)' % e.message['desc'])
    else:
        log.error('Operation incorectly succeeded!  Test Failed!')
        assert False


def test_ticket47937_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_ticket47937(topo)
    test_ticket47937_final(topo)


if __name__ == '__main__':
    run_isolated()
