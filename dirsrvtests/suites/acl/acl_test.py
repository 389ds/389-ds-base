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
from lib389.utils import *

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

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def add_attr(topology, attr_name):
    """Adds attribute to the schema"""

    ATTR_VALUE = """(NAME '%s' \
                    DESC 'Attribute filteri-Multi-Valued' \
                    SYNTAX 1.3.6.1.4.1.1466.115.121.1.27)""" % attr_name
    mod = [(ldap.MOD_ADD, 'attributeTypes', ATTR_VALUE)]

    try:
        topology.standalone.modify_s(DN_SCHEMA, mod)
    except ldap.LDAPError, e:
        log.fatal('Failed to add attr (%s): error (%s)' % (attr_name,
                                                           e.message['desc']))
        assert False


@pytest.fixture(params=["lang-ja", "binary", "phonetic"])
def aci_with_attr_subtype(request, topology):
    """Adds and deletes an ACI in the DEFAULT_SUFFIX"""

    TARGET_ATTR = 'protectedOperation'
    USER_ATTR = 'allowedToPerform'
    SUBTYPE = request.param

    log.info("========Executing test with '%s' subtype========" % SUBTYPE)
    log.info("        Add a target attribute")
    add_attr(topology, TARGET_ATTR)

    log.info("        Add a user attribute")
    add_attr(topology, USER_ATTR)

    ACI_TARGET = '(targetattr=%s;%s)' % (TARGET_ATTR, SUBTYPE)
    ACI_ALLOW = '(version 3.0; acl "test aci for subtypes"; allow (read) '
    ACI_SUBJECT = 'userattr = "%s;%s#GROUPDN";)' % (USER_ATTR, SUBTYPE)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    log.info("        Add an ACI with attribute subtype")
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    try:
        topology.standalone.modify_s(DEFAULT_SUFFIX, mod)
    except ldap.LDAPError, e:
        log.fatal('Failed to add ACI: error (%s)' % (e.message['desc']))
        assert False

    def fin():
        log.info("        Finally, delete an ACI with the '%s' subtype" %
                                                       SUBTYPE)
        mod = [(ldap.MOD_DELETE, 'aci', ACI_BODY)]
        try:
            topology.standalone.modify_s(DEFAULT_SUFFIX, mod)
        except ldap.LDAPError, e:
            log.fatal('Failed to delete ACI: error (%s)' % (e.message['desc']))
            assert False
    request.addfinalizer(fin)

    return ACI_BODY


def test_aci_attr_subtype_targetattr(topology, aci_with_attr_subtype):
    """Checks, that ACIs allow attribute subtypes in the targetattr keyword

    Test description:
    1. Define two attributes in the schema
        - first will be a targetattr
        - second will be a userattr
    2. Add an ACI with an attribute subtype
        - or language subtype
        - or binary subtype
        - or pronunciation subtype
    """

    log.info("        Search for the added attribute")
    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX,
                                               ldap.SCOPE_BASE,
                                               '(objectclass=*)', ['aci'])
        entry = str(entries[0])
        assert aci_with_attr_subtype in entry
        log.info("        The added attribute was found")

    except ldap.LDAPError, e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
