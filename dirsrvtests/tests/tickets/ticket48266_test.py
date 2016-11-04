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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

NEW_ACCOUNT = "new_account"
MAX_ACCOUNTS = 20


class TopologyReplication(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating master 1...
    master1 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
    args_instance[SER_HOST] = HOST_MASTER_1
    args_instance[SER_PORT] = PORT_MASTER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master1.allocate(args_master)
    instance_master1 = master1.exists()
    if instance_master1:
        master1.delete()
    master1.create()
    master1.open()
    master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_1)

    # Creating master 2...
    master2 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
    args_instance[SER_HOST] = HOST_MASTER_2
    args_instance[SER_PORT] = PORT_MASTER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_2
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master2.allocate(args_master)
    instance_master2 = master2.exists()
    if instance_master2:
        master2.delete()
    master2.create()
    master2.open()
    master2.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_2)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to master 2
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Delete each instance in the end
    def fin():
        master1.delete()
        master2.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2)


def pattern_accesslog(file, log_pattern):
    try:
        pattern_accesslog.last_pos += 1
    except AttributeError:
        pattern_accesslog.last_pos = 0


    found = None
    file.seek(pattern_accesslog.last_pos)

    # Use a while true iteration because 'for line in file: hit a
    # python bug that break file.tell()
    while True:
        line = file.readline()
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break

    pattern_accesslog.last_pos = file.tell()
    if found:
        return line
    else:
        return None


@pytest.fixture(scope="module")
def entries(topology):
    # add dummy entries in the staging DIT
    for cpt in range(MAX_ACCOUNTS):
        name = "%s%d" % (NEW_ACCOUNT, cpt)
        topology.master1.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
                                            'objectclass': "top person".split(),
                                            'sn': name,
                                            'cn': name})))
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '8192'),
           (ldap.MOD_REPLACE, 'nsslapd-accesslog-level', str(256+4))]
    topology.master1.modify_s(DN_CONFIG, mod)
    topology.master2.modify_s(DN_CONFIG, mod)


def test_ticket48266_fractional(topology, entries):
    ents = topology.master1.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1

    mod = [(ldap.MOD_REPLACE, 'nsDS5ReplicatedAttributeList', ['(objectclass=*) $ EXCLUDE telephonenumber']),
           (ldap.MOD_REPLACE, 'nsds5ReplicaStripAttrs', ['modifiersname modifytimestamp'])]
    ents = topology.master1.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    m1_m2_agmt = ents[0].dn
    topology.master1.modify_s(ents[0].dn, mod)

    ents = topology.master2.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology.master2.modify_s(ents[0].dn, mod)

    topology.master1.restart(timeout=10)
    topology.master2.restart(timeout=10)

    topology.master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    topology.master1.waitForReplInit(m1_m2_agmt)


def test_ticket48266_check_repl_desc(topology, entries):
    name = "cn=%s1,%s" % (NEW_ACCOUNT, SUFFIX)
    value = 'check repl. description'
    mod = [(ldap.MOD_REPLACE, 'description', value)]
    topology.master1.modify_s(name, mod)

    loop = 0
    while loop <= 10:
        ent = topology.master2.getEntry(name, ldap.SCOPE_BASE, "(objectclass=*)")
        if ent.hasAttr('description') and ent.getValue('description') == value:
            break
        time.sleep(1)
        loop += 1
    assert loop <= 10


# will use this CSN as a starting point on error log
# after this is one 'Skipped' then the first csn _get_first_not_replicated_csn
# should no longer be Skipped in the error log
def _get_last_not_replicated_csn(topology):
    name = "cn=%s5,%s" % (NEW_ACCOUNT, SUFFIX)

    # read the first CSN that will not be replicated
    mod = [(ldap.MOD_REPLACE, 'telephonenumber', str(123456))]
    topology.master1.modify_s(name, mod)
    msgid = topology.master1.search_ext(name, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    rtype, rdata, rmsgid = topology.master1.result2(msgid)
    attrs = None
    for dn, raw_attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        if 'nscpentrywsi' in raw_attrs:
            attrs = raw_attrs['nscpentrywsi']
    assert attrs
    for attr in attrs:
        if attr.lower().startswith('telephonenumber'):
            break
    assert attr

    # now retrieve the CSN of the operation we are looking for
    csn = None
    topology.master1.stop(timeout=10)
    file_obj = open(topology.master1.accesslog, "r")

    # First the conn/op of the operation
    regex = re.compile("MOD dn=\"%s\"" % name)
    found_op = pattern_accesslog(file_obj, regex)
    assert found_op
    if found_op:
        conn_op_pattern = '.* (conn=[0-9]* op=[0-9]*) .*'
        conn_op_re= re.compile(conn_op_pattern)
        conn_op_match  = conn_op_re.match(found_op)
        conn_op = conn_op_match.group(1)

        # now the related CSN
        regex = re.compile("%s RESULT" % conn_op)
        found_result = pattern_accesslog(file_obj, regex)
        csn_pattern = '.* csn=(.*)'
        csn_re = re.compile(csn_pattern)
        csn_match = csn_re.match(found_result)
        csn = csn_match.group(1)

    topology.master1.start(timeout=10)
    return csn


def _get_first_not_replicated_csn(topology):
    name = "cn=%s2,%s" % (NEW_ACCOUNT, SUFFIX)

    # read the first CSN that will not be replicated
    mod = [(ldap.MOD_REPLACE, 'telephonenumber', str(123456))]
    topology.master1.modify_s(name, mod)
    msgid = topology.master1.search_ext(name, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    rtype, rdata, rmsgid = topology.master1.result2(msgid)
    attrs = None
    for dn, raw_attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        if 'nscpentrywsi' in raw_attrs:
            attrs = raw_attrs['nscpentrywsi']
    assert attrs
    for attr in attrs:
        if attr.lower().startswith('telephonenumber'):
            break
    assert attr

    # now retrieve the CSN of the operation we are looking for
    csn = None
    topology.master1.stop(timeout=10)
    file_obj = open(topology.master1.accesslog, "r")

    # First the conn/op of the operation
    regex = re.compile("MOD dn=\"%s\"" % name)
    found_op = pattern_accesslog(file_obj, regex)
    assert found_op
    if found_op:
        conn_op_pattern = '.* (conn=[0-9]* op=[0-9]*) .*'
        conn_op_re= re.compile(conn_op_pattern)
        conn_op_match  = conn_op_re.match(found_op)
        conn_op = conn_op_match.group(1)

        # now the related CSN
        regex = re.compile("%s RESULT" % conn_op)
        found_result = pattern_accesslog(file_obj, regex)
        csn_pattern = '.* csn=(.*)'
        csn_re = re.compile(csn_pattern)
        csn_match = csn_re.match(found_result)
        csn = csn_match.group(1)

    topology.master1.start(timeout=10)
    return csn


def _count_full_session(topology):
    #
    # compute the number of 'No more updates'
    #
    file_obj = open(topology.master1.errlog, "r")
    # pattern to find
    pattern = ".*No more updates to send.*"
    regex = re.compile(pattern)
    no_more_updates = 0

    # check initiation number of 'No more updates
    while True:
        line = file_obj.readline()
        found = regex.search(line)
        if (found):
            no_more_updates = no_more_updates + 1
        if (line == ''):
            break
    file_obj.close()

    return no_more_updates


def test_ticket48266_count_csn_evaluation(topology, entries):
    ents = topology.master1.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    first_csn = _get_first_not_replicated_csn(topology)
    name = "cn=%s3,%s" % (NEW_ACCOUNT, SUFFIX)
    NB_SESSION = 102

    no_more_update_cnt = _count_full_session(topology)
    topology.master1.agreement.pause(ents[0].dn)
    # now do a set of updates that will NOT be replicated
    for telNumber in range(NB_SESSION):
        mod = [(ldap.MOD_REPLACE, 'telephonenumber', str(telNumber))]
        topology.master1.modify_s(name, mod)

    topology.master1.agreement.resume(ents[0].dn)

    # let's wait all replication session complete
    MAX_LOOP = 10
    cnt = 0
    current_no_more_update = _count_full_session(topology)
    while (current_no_more_update == no_more_update_cnt):
        cnt = cnt + 1
        if (cnt > MAX_LOOP):
            break
        time.sleep(5)
        current_no_more_update = _count_full_session(topology)

    log.info('after %d MODs we have completed %d replication sessions' % (NB_SESSION, (current_no_more_update - no_more_update_cnt)))
    no_more_update_cnt = current_no_more_update


    # At this point, with the fix a dummy update was made BUT may be not sent it
    # make sure it was sent so that the consumer CSN will be updated
    last_csn = _get_last_not_replicated_csn(topology)

    # let's wait all replication session complete
    MAX_LOOP = 10
    cnt = 0
    current_no_more_update = _count_full_session(topology)
    while (current_no_more_update == no_more_update_cnt):
        cnt = cnt + 1
        if (cnt > MAX_LOOP):
            break
        time.sleep(5)
        current_no_more_update = _count_full_session(topology)

    log.info('This MODs %s triggered the send of the dummy update completed %d replication sessions' % (last_csn, (current_no_more_update - no_more_update_cnt)))
    no_more_update_cnt = current_no_more_update

    # so we should no longer see the first_csn in the log
    # Let's create a new csn (last_csn) and check there is no longer first_csn
    topology.master1.agreement.pause(ents[0].dn)
    last_csn = _get_last_not_replicated_csn(topology)
    topology.master1.agreement.resume(ents[0].dn)

    # let's wait for the session to complete
    MAX_LOOP = 10
    cnt = 0
    while (current_no_more_update == no_more_update_cnt):
        cnt = cnt + 1
        if (cnt > MAX_LOOP):
            break
        time.sleep(5)
        current_no_more_update = _count_full_session(topology)

    log.info('This MODs %s  completed in %d replication sessions, should be sent without evaluating %s' % (last_csn, (current_no_more_update - no_more_update_cnt), first_csn))
    no_more_update_cnt = current_no_more_update

    # Now determine how many times we have skipped 'csn'
    # no need to stop the server to check the error log
    file_obj = open(topology.master1.errlog, "r")

    # find where the last_csn operation was processed
    pattern = ".*ruv_add_csn_inprogress: successfully inserted csn %s.*" % last_csn
    regex = re.compile(pattern)
    cnt = 0

    while True:
        line = file_obj.readline()
        found = regex.search(line)
        if ((line == '') or (found)):
            break
    if (found):
        log.info('last operation was found at %d' % file_obj.tell())
        log.info(line)
    log.info('Now check the we can not find the first csn %s in the log'% first_csn)

    pattern = ".*Skipping update operation.*CSN %s.*" % first_csn
    regex = re.compile(pattern)
    found = False
    while True:
        line = file_obj.readline()
        found = regex.search(line)
        if ((line == '') or (found)):
            break
    if (found):
        log.info('Unexpected found %s' % line)
    assert not found


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode

    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
