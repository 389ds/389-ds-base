# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import ldap
import logging
import pytest
import re
from lib389 import DirSrv, Entry
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
    #request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_ticket48005_setup(topology):
    '''
    allow dump core
    generate a test ldif file using dbgen.pl
    import the ldif
    '''
    log.info("Ticket 48005 setup...")
    if hasattr(topology.standalone, 'prefix'):
        prefix = topology.standalone.prefix
    else:
        prefix = None
    sysconfig_dirsrv = prefix + ENV_SYSCONFIG_DIR + "/dirsrv"
    cmdline = 'egrep "ulimit -c unlimited" %s' % sysconfig_dirsrv
    p = os.popen(cmdline, "r")
    ulimitc = p.readline()
    if ulimitc == "":
        log.info('No ulimit -c in %s' % sysconfig_dirsrv)
        log.info('Adding it')
        cmdline = 'echo "ulimit -c unlimited" >> %s' % sysconfig_dirsrv

    sysconfig_dirsrv_systemd = sysconfig_dirsrv + ".systemd"
    cmdline = 'egrep LimitCORE=infinity %s' % sysconfig_dirsrv_systemd
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore == "":
        log.info('No LimitCORE in %s' % sysconfig_dirsrv_systemd)
        log.info('Adding it')
        cmdline = 'echo LimitCORE=infinity >> %s' % sysconfig_dirsrv_systemd

    topology.standalone.restart(timeout=10)

    ldif_file = topology.standalone.get_ldif_dir() + "/ticket48005.ldif"
    os.system('ls %s' % ldif_file)
    os.system('rm -f %s' % ldif_file)
    if hasattr(topology.standalone, 'prefix'):
        prefix = topology.standalone.prefix
    else:
        prefix = None
    dbgen_prog = prefix + '/bin/dbgen.pl'
    log.info('dbgen_prog: %s' % dbgen_prog)
    os.system('%s -s %s -o %s -u -n 10000' % (dbgen_prog, SUFFIX, ldif_file))
    cmdline = 'egrep dn: %s | wc -l' % ldif_file
    p = os.popen(cmdline, "r")
    dnnumstr = p.readline()
    num = int(dnnumstr)
    log.info("We have %d entries.\n", num)

    importTask = Tasks(topology.standalone)
    args = {TASK_WAIT: True}
    importTask.importLDIF(SUFFIX, None, ldif_file, args)
    log.info('Importing %s complete.' % ldif_file)


def test_ticket48005_memberof(topology):
    '''
    Enable memberof and referint plugin
    Run fixmemberof task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 memberof test...")
    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)

    topology.standalone.restart(timeout=10)

    try:
        # run the fixup task
        topology.standalone.tasks.fixupMemberOf(suffix=SUFFIX, args={TASK_WAIT: False})
    except ValueError:
        log.error('Some problem occured with a value that was provided')
        assert False

    topology.standalone.stop(timeout=10)

    mytmp = '/tmp'
    logdir = re.sub('errors', '', topology.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_memberof' % (logdir, mytmp))
        log.error('FixMemberof: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology.standalone.start(timeout=10)

    topology.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
    topology.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)

    topology.standalone.restart(timeout=10)

    log.info("Ticket 48005 memberof test complete")


def test_ticket48005_automember(topology):
    '''
    Enable automember and referint plugin
    1. Run automember rebuild membership task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    2. Run automember export updates task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    3. Run automember map updates task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 automember test...")
    topology.standalone.plugins.enable(name=PLUGIN_AUTOMEMBER)
    topology.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)

    # configure automember config entry
    log.info('Adding automember config')
    try:
        topology.standalone.add_s(Entry(('cn=group cfg,cn=Auto Membership Plugin,cn=plugins,cn=config', {
                                         'objectclass': 'top autoMemberDefinition'.split(),
                                         'autoMemberScope': 'dc=example,dc=com',
                                         'autoMemberFilter': 'objectclass=inetorgperson',
                                         'autoMemberDefaultGroup': 'cn=group0,dc=example,dc=com',
                                         'autoMemberGroupingAttr': 'uniquemember:dn',
                                         'cn': 'group cfg'})))
    except ValueError:
        log.error('Failed to add automember config')
        assert False

    topology.standalone.restart(timeout=10)

    try:
        # run the automember rebuild task
        topology.standalone.tasks.automemberRebuild(suffix=SUFFIX, args={TASK_WAIT: False})
    except ValueError:
        log.error('Automember rebuild task failed.')
        assert False

    topology.standalone.stop(timeout=10)

    mytmp = '/tmp'
    logdir = re.sub('errors', '', topology.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_automember_rebuild' % (logdir, mytmp))
        log.error('Automember_rebuld: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology.standalone.start(timeout=10)

    ldif_out_file = mytmp + "/ticket48005_automember_exported.ldif"
    try:
        # run the automember export task
        topology.standalone.tasks.automemberExport(suffix=SUFFIX, ldif_out=ldif_out_file, args={TASK_WAIT: False})
    except ValueError:
        log.error('Automember Export task failed.')
        assert False

    topology.standalone.stop(timeout=10)

    logdir = re.sub('errors', '', topology.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_automember_export' % (logdir, mytmp))
        log.error('Automember_export: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology.standalone.start(timeout=10)

    ldif_in_file = topology.standalone.get_ldif_dir() + "/ticket48005.ldif"
    ldif_out_file = mytmp + "/ticket48005_automember_map.ldif"
    try:
        # run the automember map task
        topology.standalone.tasks.automemberMap(ldif_in=ldif_in_file, ldif_out=ldif_out_file, args={TASK_WAIT: False})
    except ValueError:
        log.error('Automember Map task failed.')
        assert False

    topology.standalone.stop(timeout=10)

    logdir = re.sub('errors', '', topology.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_automember_map' % (logdir, mytmp))
        log.error('Automember_map: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology.standalone.start(timeout=10)

    topology.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
    topology.standalone.plugins.enable(name=PLUGIN_AUTOMEMBER)

    topology.standalone.restart(timeout=10)

    log.info("Ticket 48005 automember test complete")


def test_ticket48005_syntaxvalidate(topology):
    '''
    Run syntax validate task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 syntax validate test...")

    try:
        # run the fixup task
        topology.standalone.tasks.syntaxValidate(suffix=SUFFIX, args={TASK_WAIT: False})
    except ValueError:
        log.error('Some problem occured with a value that was provided')
        assert False

    topology.standalone.stop(timeout=10)

    mytmp = '/tmp'
    logdir = re.sub('errors', '', topology.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_syntaxvalidate' % (logdir, mytmp))
        log.error('SyntaxValidate: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology.standalone.start(timeout=10)

    log.info("Ticket 48005 syntax validate test complete")


def test_ticket48005_usn(topology):
    '''
    Enable entryusn
    Delete all user entries.
    Run USN tombstone cleanup task
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 usn test...")
    topology.standalone.plugins.enable(name=PLUGIN_USN)

    topology.standalone.restart(timeout=10)

    try:
        entries = topology.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=inetorgperson)")
        if len(entries) == 0:
            log.info("No user entries.")
        else:
            for i in range(len(entries)):
                # log.info('Deleting %s' % entries[i].dn)
                try:
                    topology.standalone.delete_s(entries[i].dn)
                except ValueError:
                    log.error('delete_s %s failed.' % entries[i].dn)
                    assert False
    except ValueError:
        log.error('search_s failed.')
        assert False

    try:
        # run the usn tombstone cleanup
        topology.standalone.tasks.usnTombstoneCleanup(suffix=SUFFIX, bename="userRoot", args={TASK_WAIT: False})
    except ValueError:
        log.error('Some problem occured with a value that was provided')
        assert False

    topology.standalone.stop(timeout=10)

    mytmp = '/tmp'
    logdir = re.sub('errors', '', topology.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_usn' % (logdir, mytmp))
        log.error('usnTombstoneCleanup: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology.standalone.start(timeout=10)

    topology.standalone.plugins.disable(name=PLUGIN_USN)

    topology.standalone.restart(timeout=10)

    log.info("Ticket 48005 usn test complete")


def test_ticket48005_schemareload(topology):
    '''
    Run schema reload task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 schema reload test...")

    try:
        # run the schema reload task
        topology.standalone.tasks.schemaReload(args={TASK_WAIT: False})
    except ValueError:
        log.error('Schema Reload task failed.')
        assert False

    topology.standalone.stop(timeout=10)

    logdir = re.sub('errors', '', topology.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        mytmp = '/tmp'
        s.system('mv %score* %s/core.ticket48005_schema_reload' % (logdir, mytmp))
        log.error('Schema reload: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology.standalone.start(timeout=10)

    log.info("Ticket 48005 schema reload test complete")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
