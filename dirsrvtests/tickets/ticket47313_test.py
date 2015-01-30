import os
import sys
import time
import ldap
import logging
import time
import pytest
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

log = logging.getLogger(__name__)

installation_prefix = None

ENTRY_NAME = 'test_entry'


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

    # Get the status of the instance
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

    return TopologyStandalone(standalone)


def test_ticket47313_run(topology):
    """
        It adds 2 test entries
        Search with filters including subtype and !
            It deletes the added entries
    """

    # bind as directory manager
    topology.standalone.log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    # enable filter error logging
    #mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '32')]
    #topology.standalone.modify_s(DN_CONFIG, mod)

    topology.standalone.log.info("\n\n######################### ADD ######################\n")

    # Prepare the entry with cn;fr & cn;en
    entry_name_fr = '%s fr' % (ENTRY_NAME)
    entry_name_en = '%s en' % (ENTRY_NAME)
    entry_name_both = '%s both' % (ENTRY_NAME)
    entry_dn_both = 'cn=%s, %s' % (entry_name_both, SUFFIX)
    entry_both = Entry(entry_dn_both)
    entry_both.setValues('objectclass', 'top', 'person')
    entry_both.setValues('sn', entry_name_both)
    entry_both.setValues('cn', entry_name_both)
    entry_both.setValues('cn;fr', entry_name_fr)
    entry_both.setValues('cn;en', entry_name_en)

    # Prepare the entry with one member
    entry_name_en_only = '%s en only' % (ENTRY_NAME)
    entry_dn_en_only = 'cn=%s, %s' % (entry_name_en_only, SUFFIX)
    entry_en_only = Entry(entry_dn_en_only)
    entry_en_only.setValues('objectclass', 'top', 'person')
    entry_en_only.setValues('sn', entry_name_en_only)
    entry_en_only.setValues('cn', entry_name_en_only)
    entry_en_only.setValues('cn;en', entry_name_en)

    topology.standalone.log.info("Try to add Add %s: %r" % (entry_dn_both, entry_both))
    topology.standalone.add_s(entry_both)

    topology.standalone.log.info("Try to add Add %s: %r" % (entry_dn_en_only, entry_en_only))
    topology.standalone.add_s(entry_en_only)

    topology.standalone.log.info("\n\n######################### SEARCH ######################\n")

    # filter: (&(cn=test_entry en only)(!(cn=test_entry fr)))
    myfilter = '(&(sn=%s)(!(cn=%s)))' % (entry_name_en_only, entry_name_fr)
    topology.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1
    assert ents[0].sn == entry_name_en_only
    topology.standalone.log.info("Found %s" % ents[0].dn)

    # filter: (&(cn=test_entry en only)(!(cn;fr=test_entry fr)))
    myfilter = '(&(sn=%s)(!(cn;fr=%s)))' % (entry_name_en_only, entry_name_fr)
    topology.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 1
    assert ents[0].sn == entry_name_en_only
    topology.standalone.log.info("Found %s" % ents[0].dn)

    # filter: (&(cn=test_entry en only)(!(cn;en=test_entry en)))
    myfilter = '(&(sn=%s)(!(cn;en=%s)))' % (entry_name_en_only, entry_name_en)
    topology.standalone.log.info("Try to search with filter %s" % myfilter)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, myfilter)
    assert len(ents) == 0
    topology.standalone.log.info("Found none")

    topology.standalone.log.info("\n\n######################### DELETE ######################\n")

    topology.standalone.log.info("Try to delete  %s " % entry_dn_both)
    topology.standalone.delete_s(entry_dn_both)

    topology.standalone.log.info("Try to delete  %s " % entry_dn_en_only)
    topology.standalone.delete_s(entry_dn_en_only)


def test_ticket47313_final(topology):
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
    test_ticket47313_run(topo)

    test_ticket47313_final(topo)


if __name__ == '__main__':
    run_isolated()

