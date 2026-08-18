# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
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
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

NEW_ACCOUNT    = "new_account"
MAX_ACCOUNTS   = 100
HOMEHEAD = "/home/xyz_"

MIXED_VALUE="/home/mYhOmEdIrEcToRy"
LOWER_VALUE="/home/myhomedirectory"
HOMEDIRECTORY_INDEX = 'cn=homeDirectory,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
HOMEDIRECTORY_CN="homedirectory"
MATCHINGRULE = 'nsMatchingRule'
UIDNUMBER_INDEX = 'cn=uidnumber,cn=index,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'
UIDNUMBER_CN="uidnumber"


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
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
        #standalone.delete()
        pass
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)

def _find_notes_accesslog(file, log_pattern):
    try:
        _find_notes_accesslog.last_pos += 1
    except AttributeError:
        _find_notes_accesslog.last_pos = 0

        
    #position to the where we were last time
    found = None
    file.seek(_find_notes_accesslog.last_pos)
    
    while True:
        line = file.readline()
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break

    
    if found:
        # assuming that the result is the next line of the search
        line = file.readline()
        _find_notes_accesslog.last_pos = file.tell()
        return line
    else:
        _find_notes_accesslog.last_pos = file.tell()
        return None

def _find_next_notes(topology, Filter):
    topology.standalone.stop(timeout=10)
    file_path = topology.standalone.accesslog
    file_obj = open(file_path, "r")
    regex = re.compile("filter=\"\(%s" % Filter)
    result = _find_notes_accesslog(file_obj, regex)
    file_obj.close() 
    topology.standalone.start(timeout=10)
    
    return result

#
# find the next message showing an indexing failure
# (starting at the specified posistion)
# and return the position in the error log
# If there is not such message -> return None
def _find_next_indexing_failure(topology, pattern, position):
    file_path = topology.standalone.errlog
    file_obj = open(file_path, "r")

    try:
        file_obj.seek(position + 1)
    except:
        file_obj.close()
        return None
    
    # Check if the MR configuration failure occurs
    regex = re.compile(pattern)
    while True:
        line = file_obj.readline()
        found = regex.search(line)
        if ((line == '') or (found)):
            break
    

    
    if (found):
        log.info("The configuration of a specific MR fails")
        log.info(line)
        result = file_obj.tell()
        file_obj.close()
        return result
    else:
        file_obj.close()
        result = None
        
    return result
#
# find the first message showing an indexing failure
# and return the position in the error log
# If there is not such message -> return None
def _find_first_indexing_failure(topology, pattern):
    file_path = topology.standalone.errlog
    file_obj = open(file_path, "r")

    # Check if the MR configuration failure occurs
    regex = re.compile(pattern)
    while True:
        line = file_obj.readline()
        found = regex.search(line)
        if ((line == '') or (found)):
            break
    
    
    
    if (found):
        log.info("pattern is found: \"%s\"")
        log.info(line)
        result = file_obj.tell()
        file_obj.close()
    else:
        result = None
    
    return result

def _check_entry(topology, filterHead=None, filterValueUpper=False, entry_ext=None, found=False, indexed=False):
    # Search with CES with exact value -> find an entry + indexed
    if filterValueUpper:
        homehead = HOMEHEAD.upper()
    else:
        homehead = HOMEHEAD
    searchedHome = "%s%d" % (homehead, entry_ext)
    Filter = "(%s=%s)" % (filterHead, searchedHome)
    log.info("Search %s" % Filter)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, Filter)
    if found:
        assert len(ents) == 1
        assert ents[0].hasAttr('homedirectory')
        valueHome = ensure_bytes("%s%d" % (HOMEHEAD, entry_ext))
        assert valueHome in ents[0].getValues('homedirectory')
    else:
        assert len(ents) == 0
    
    result = _find_next_notes(topology, Filter)
    log.info("result=%s" % result)
    if indexed:
        assert not "notes=U" in result
    else:
        assert "notes=U" in result

def test_ticket48973_init(topology):
    log.info("Initialization: add dummy entries for the tests")
    for cpt in range(MAX_ACCOUNTS):
        name = "%s%d" % (NEW_ACCOUNT, cpt)
        topology.standalone.add_s(Entry(("uid=%s,%s" % (name, SUFFIX), {
                                            'objectclass': "top posixAccount".split(),
                                            'uid': name,
                                            'cn': name,
                                            'uidnumber': str(111),
                                            'gidnumber': str(222),
                                            'homedirectory': "%s%d" % (HOMEHEAD, cpt)})))

def test_ticket48973_ces_not_indexed(topology):
    """
    Check that homedirectory is not indexed
      - do a search unindexed
    """
    
    entry_ext = 0
    searchedHome = "%s%d" % (HOMEHEAD, entry_ext)
    Filter = "(homeDirectory=%s)" % searchedHome
    log.info("Search %s" % Filter)
    ents = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, Filter)
    assert len(ents) == 1
    assert ents[0].hasAttr('homedirectory')
    assert ensure_bytes(searchedHome) in ents[0].getValues('homedirectory')
    
    result = _find_next_notes(topology, Filter)
    log.info("result=%s" % result)
    assert "notes=U" in result


def test_ticket48973_homeDirectory_indexing(topology):
    """
    Check that homedirectory is indexed with syntax (ces)
      - triggers index
      - no failure on index
      - do a search indexed with exact value (ces) and no default_mr_indexer_create warning
      - do a search indexed with uppercase value (ces) and no default_mr_indexer_create warning
    """
    entry_ext = 1
    
    try:
        ent = topology.standalone.getEntry(HOMEDIRECTORY_INDEX, ldap.SCOPE_BASE)
    except ldap.NO_SUCH_OBJECT:
        topology.standalone.add_s(Entry((HOMEDIRECTORY_INDEX, {
                                            'objectclass': "top nsIndex".split(),
                                            'cn': HOMEDIRECTORY_CN,
                                            'nsSystemIndex': 'false',
                                            'nsIndexType': 'eq'})))

    args = {TASK_WAIT: True}
    topology.standalone.tasks.reindex(suffix=SUFFIX, attrname='homeDirectory', args=args)

    log.info("Check indexing succeeded with no specified matching rule")
    assert not _find_first_indexing_failure(topology, "unknown or invalid matching rule")
    assert not _find_first_indexing_failure(topology, "default_mr_indexer_create: warning")
    assert not _find_first_indexing_failure(topology, "default_mr_indexer_create - Plugin .* does not handle")

    _check_entry(topology, filterHead="homeDirectory", filterValueUpper=False, entry_ext=entry_ext,found=True, indexed=True)

    _check_entry(topology, filterHead="homeDirectory:caseExactIA5Match:", filterValueUpper=False, entry_ext=entry_ext, found=True, indexed=False)
    
    _check_entry(topology, filterHead="homeDirectory:caseIgnoreIA5Match:", filterValueUpper=False, entry_ext=entry_ext, found=True, indexed=False)
    
    _check_entry(topology, filterHead="homeDirectory", filterValueUpper=True, entry_ext=entry_ext, found=False, indexed=True)
    
    _check_entry(topology, filterHead="homeDirectory:caseExactIA5Match:", filterValueUpper=True, entry_ext=entry_ext, found=False, indexed=False)
    
    _check_entry(topology, filterHead="homeDirectory:caseIgnoreIA5Match:", filterValueUpper=True, entry_ext=entry_ext, found=True, indexed=False)

    

def test_ticket48973_homeDirectory_caseExactIA5Match_caseIgnoreIA5Match_indexing(topology):
    """
    Check that homedirectory is indexed with syntax (ces && cis)
      - triggers index
      - no failure on index
      - do a search indexed (ces) and no default_mr_indexer_create warning
      - do a search indexed (cis) and no default_mr_indexer_create warning
    """
    entry_ext = 4
    
    log.info("\n\nindex homeDirectory in caseExactIA5Match and caseIgnoreIA5Match")
    EXACTIA5_MR_NAME=b'caseExactIA5Match'
    IGNOREIA5_MR_NAME=b'caseIgnoreIA5Match'
    EXACT_MR_NAME=b'caseExactMatch'
    IGNORE_MR_NAME=b'caseIgnoreMatch'
    mod = [(ldap.MOD_REPLACE, MATCHINGRULE, (EXACT_MR_NAME, IGNORE_MR_NAME, EXACTIA5_MR_NAME, IGNOREIA5_MR_NAME))]
    topology.standalone.modify_s(HOMEDIRECTORY_INDEX, mod)

    args = {TASK_WAIT: True}
    topology.standalone.tasks.reindex(suffix=SUFFIX, attrname='homeDirectory', args=args)

    log.info("Check indexing succeeded with no specified matching rule")
    assert not _find_first_indexing_failure(topology, "unknown or invalid matching rule")
    assert not _find_first_indexing_failure(topology, "default_mr_indexer_create: warning")
    assert not _find_first_indexing_failure(topology, "default_mr_indexer_create - Plugin .* does not handle")

    _check_entry(topology, filterHead="homeDirectory", filterValueUpper=False, entry_ext=entry_ext, found=True, indexed=True)

    _check_entry(topology, filterHead="homeDirectory:caseExactIA5Match:", filterValueUpper=False, entry_ext=entry_ext, found=True, indexed=True)
    
    _check_entry(topology, filterHead="homeDirectory:caseIgnoreIA5Match:", filterValueUpper=False, entry_ext=entry_ext, found=True, indexed=True)
    
    _check_entry(topology, filterHead="homeDirectory", filterValueUpper=True, entry_ext=entry_ext, found=False, indexed=True)
    
    _check_entry(topology, filterHead="homeDirectory:caseExactIA5Match:", filterValueUpper=True, entry_ext=entry_ext, found=False, indexed=True)
    
    _check_entry(topology, filterHead="homeDirectory:caseIgnoreIA5Match:", filterValueUpper=True, entry_ext=entry_ext, found=True, indexed=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
