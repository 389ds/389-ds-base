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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_filter_init(topology):
    '''
    Write your testcase here...
    '''
    return


def test_filter_escaped(topology):
    '''
    Test we can search for an '*' in a attribute value.
    '''

    log.info('Running test_filter_escaped...')

    USER1_DN = 'uid=test_entry,' + DEFAULT_SUFFIX
    USER2_DN = 'uid=test_entry2,' + DEFAULT_SUFFIX

    try:
        topology.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '1',
                                 'cn': 'test * me',
                                 'uid': 'test_entry',
                                 'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to add test user ' + USER1_DN + ': error ' +
                  e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '2',
                                 'cn': 'test me',
                                 'uid': 'test_entry2',
                                 'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to add test user ' + USER2_DN + ': error ' + e.message['desc'])
        assert False

    try:
        entry = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'cn=*\**')
        if not entry or len(entry) > 1:
            log.fatal('test_filter_escaped: Entry was not found using "cn=*\**"')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_filter_escaped: Failed to search for user(%s), error: %s' %
        (USER1_DN, e.message('desc')))
        assert False

    log.info('test_filter_escaped: PASSED')


def test_filter_search_original_attrs(topology):
    '''
    Search and request attributes with extra characters.  The returned entry
    should not have these extra characters:  "objectclass EXTRA"
    '''

    log.info('Running test_filter_search_original_attrs...')

    try:
        entry = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE,
                                             'objectclass=top', ['objectclass-EXTRA'])
        if entry[0].hasAttr('objectclass-EXTRA'):
            log.fatal('test_filter_search_original_attrs: Entry does not have the original attribute')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_filter_search_original_attrs: Failed to search suffix(%s), error: %s' %
                  (DEFAULT_SUFFIX, e.message('desc')))
        assert False

    log.info('test_filter_search_original_attrs: PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)

    test_filter_init(topo)
    test_filter_escaped(topo)
    test_filter_search_original_attrs(topo)


if __name__ == '__main__':
    run_isolated()

