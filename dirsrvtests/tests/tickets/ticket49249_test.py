# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
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
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

COS_BRANCH = 'ou=cos_scope,' + DEFAULT_SUFFIX
COS_DEF = 'cn=cos_definition,' + COS_BRANCH
COS_TEMPLATE = 'cn=cos_template,' + COS_BRANCH
INVALID_USER_WITH_COS = 'cn=cos_user_no_mail,' + COS_BRANCH
VALID_USER_WITH_COS = 'cn=cos_user_with_mail,' + COS_BRANCH

NO_COS_BRANCH = 'ou=no_cos_scope,' + DEFAULT_SUFFIX
INVALID_USER_WITHOUT_COS = 'cn=no_cos_user_no_mail,' + NO_COS_BRANCH
VALID_USER_WITHOUT_COS = 'cn=no_cos_user_with_mail,' + NO_COS_BRANCH

def test_ticket49249(topo):
    """Write your testcase here...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """
    # Add the branches
    try:
        topo.standalone.add_s(Entry((COS_BRANCH, {
            'objectclass': 'top extensibleObject'.split(),
            'ou': 'cos_scope'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add cos_scope: error ' + e.message['desc'])
        assert False

    try:
        topo.standalone.add_s(Entry((NO_COS_BRANCH, {
            'objectclass': 'top extensibleObject'.split(),
            'ou': 'no_cos_scope'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add no_cos_scope: error ' + e.message['desc'])
        assert False

    try:
        topo.standalone.add_s(Entry((COS_TEMPLATE, {
            'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
            'cn': 'cos_template',
            'cosPriority': '1',
            'cn': 'cn=nsPwTemplateEntry,ou=level1,dc=example,dc=com',
            'mailAlternateAddress': 'hello@world'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add cos_template: error ' + e.message['desc'])
        assert False

    try:
        topo.standalone.add_s(Entry((COS_DEF, {
            'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
            'cn': 'cos_definition',
            'costemplatedn': COS_TEMPLATE,
            'cosAttribute': 'mailAlternateAddress default'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add cos_definition: error ' + e.message['desc'])
        assert False

    try:
        # This entry is not allowed to have mailAlternateAddress
        topo.standalone.add_s(Entry((INVALID_USER_WITH_COS, {
            'objectclass': 'top person'.split(),
            'cn': 'cos_user_no_mail',
            'sn': 'cos_user_no_mail'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add cos_user_no_mail: error ' + e.message['desc'])
        assert False

    try:
        # This entry is allowed to have mailAlternateAddress
        topo.standalone.add_s(Entry((VALID_USER_WITH_COS, {
            'objectclass': 'top mailGroup'.split(),
            'cn': 'cos_user_with_mail'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add cos_user_no_mail: error ' + e.message['desc'])
        assert False

    try:
        # This entry is not allowed to have mailAlternateAddress
        topo.standalone.add_s(Entry((INVALID_USER_WITHOUT_COS, {
            'objectclass': 'top person'.split(),
            'cn': 'no_cos_user_no_mail',
            'sn': 'no_cos_user_no_mail'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add no_cos_user_no_mail: error ' + e.message['desc'])
        assert False

    try:
        # This entry is  allowed to have mailAlternateAddress
        topo.standalone.add_s(Entry((VALID_USER_WITHOUT_COS, {
            'objectclass': 'top mailGroup'.split(),
            'cn': 'no_cos_user_with_mail'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add no_cos_user_with_mail: error ' + e.message['desc'])
        assert False

    try:
        entries = topo.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, '(mailAlternateAddress=*)')
        assert len(entries) == 1
        assert entries[0].hasValue('mailAlternateAddress', 'hello@world')
    except ldap.LDAPError as e:
        log.fatal('Unable to retrieve cos_user_with_mail (only entry with mailAlternateAddress) : error %s' % (USER1_DN, e.message['desc']))
        assert False

    assert not topo.standalone.ds_error_log.match(".*cos attribute mailAlternateAddress failed schema.*")

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

