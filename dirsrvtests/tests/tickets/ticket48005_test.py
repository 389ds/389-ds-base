# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import re

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import (DEFAULT_SUFFIX, SUFFIX, PLUGIN_REFER_INTEGRITY, PLUGIN_AUTOMEMBER,
                              PLUGIN_MEMBER_OF, PLUGIN_USN)

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket48005_setup(topology_st):
    '''
    allow dump core
    generate a test ldif file using dbgen.pl
    import the ldif
    '''
    log.info("Ticket 48005 setup...")
    if hasattr(topology_st.standalone, 'prefix'):
        prefix = topology_st.standalone.prefix
    else:
        prefix = None
    sysconfig_dirsrv = os.path.join(topology_st.standalone.get_initconfig_dir(), 'dirsrv')
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

    topology_st.standalone.restart(timeout=10)

    ldif_file = topology_st.standalone.get_ldif_dir() + "/ticket48005.ldif"
    os.system('ls %s' % ldif_file)
    os.system('rm -f %s' % ldif_file)
    if hasattr(topology_st.standalone, 'prefix'):
        prefix = topology_st.standalone.prefix
    else:
        prefix = ""
    dbgen_prog = prefix + '/bin/dbgen.pl'
    log.info('dbgen_prog: %s' % dbgen_prog)
    os.system('%s -s %s -o %s -u -n 10000' % (dbgen_prog, SUFFIX, ldif_file))
    cmdline = 'egrep dn: %s | wc -l' % ldif_file
    p = os.popen(cmdline, "r")
    dnnumstr = p.readline()
    num = int(dnnumstr)
    log.info("We have %d entries.\n", num)

    importTask = Tasks(topology_st.standalone)
    args = {TASK_WAIT: True}
    importTask.importLDIF(SUFFIX, None, ldif_file, args)
    log.info('Importing %s complete.' % ldif_file)


def test_ticket48005_memberof(topology_st):
    '''
    Enable memberof and referint plugin
    Run fixmemberof task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 memberof test...")
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_st.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)

    topology_st.standalone.restart(timeout=10)

    try:
        # run the fixup task
        topology_st.standalone.tasks.fixupMemberOf(suffix=SUFFIX, args={TASK_WAIT: False})
    except ValueError:
        log.error('Some problem occured with a value that was provided')
        assert False

    topology_st.standalone.stop(timeout=10)

    mytmp = '/tmp'
    logdir = re.sub('errors', '', topology_st.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_memberof' % (logdir, mytmp))
        log.error('FixMemberof: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology_st.standalone.start(timeout=10)

    topology_st.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
    topology_st.standalone.plugins.disable(name=PLUGIN_MEMBER_OF)

    topology_st.standalone.restart(timeout=10)

    log.info("Ticket 48005 memberof test complete")


def test_ticket48005_automember(topology_st):
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
    topology_st.standalone.plugins.enable(name=PLUGIN_AUTOMEMBER)
    topology_st.standalone.plugins.enable(name=PLUGIN_REFER_INTEGRITY)

    # configure automember config entry
    log.info('Adding automember config')
    try:
        topology_st.standalone.add_s(Entry(('cn=group cfg,cn=Auto Membership Plugin,cn=plugins,cn=config', {
            'objectclass': 'top autoMemberDefinition'.split(),
            'autoMemberScope': 'dc=example,dc=com',
            'autoMemberFilter': 'objectclass=inetorgperson',
            'autoMemberDefaultGroup': 'cn=group0,dc=example,dc=com',
            'autoMemberGroupingAttr': 'uniquemember:dn',
            'cn': 'group cfg'})))
    except ValueError:
        log.error('Failed to add automember config')
        assert False

    topology_st.standalone.restart(timeout=10)

    try:
        # run the automember rebuild task
        topology_st.standalone.tasks.automemberRebuild(suffix=SUFFIX, args={TASK_WAIT: False})
    except ValueError:
        log.error('Automember rebuild task failed.')
        assert False

    topology_st.standalone.stop(timeout=10)

    mytmp = '/tmp'
    logdir = re.sub('errors', '', topology_st.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_automember_rebuild' % (logdir, mytmp))
        log.error('Automember_rebuld: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology_st.standalone.start(timeout=10)

    ldif_out_file = mytmp + "/ticket48005_automember_exported.ldif"
    try:
        # run the automember export task
        topology_st.standalone.tasks.automemberExport(suffix=SUFFIX, ldif_out=ldif_out_file, args={TASK_WAIT: False})
    except ValueError:
        log.error('Automember Export task failed.')
        assert False

    topology_st.standalone.stop(timeout=10)

    logdir = re.sub('errors', '', topology_st.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_automember_export' % (logdir, mytmp))
        log.error('Automember_export: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology_st.standalone.start(timeout=10)

    ldif_in_file = topology_st.standalone.get_ldif_dir() + "/ticket48005.ldif"
    ldif_out_file = mytmp + "/ticket48005_automember_map.ldif"
    try:
        # run the automember map task
        topology_st.standalone.tasks.automemberMap(ldif_in=ldif_in_file, ldif_out=ldif_out_file,
                                                   args={TASK_WAIT: False})
    except ValueError:
        log.error('Automember Map task failed.')
        assert False

    topology_st.standalone.stop(timeout=10)

    logdir = re.sub('errors', '', topology_st.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_automember_map' % (logdir, mytmp))
        log.error('Automember_map: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology_st.standalone.start(timeout=10)

    topology_st.standalone.plugins.disable(name=PLUGIN_REFER_INTEGRITY)
    topology_st.standalone.plugins.enable(name=PLUGIN_AUTOMEMBER)

    topology_st.standalone.restart(timeout=10)

    log.info("Ticket 48005 automember test complete")


def test_ticket48005_syntaxvalidate(topology_st):
    '''
    Run syntax validate task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 syntax validate test...")

    try:
        # run the fixup task
        topology_st.standalone.tasks.syntaxValidate(suffix=SUFFIX, args={TASK_WAIT: False})
    except ValueError:
        log.error('Some problem occured with a value that was provided')
        assert False

    topology_st.standalone.stop(timeout=10)

    mytmp = '/tmp'
    logdir = re.sub('errors', '', topology_st.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_syntaxvalidate' % (logdir, mytmp))
        log.error('SyntaxValidate: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology_st.standalone.start(timeout=10)

    log.info("Ticket 48005 syntax validate test complete")


def test_ticket48005_usn(topology_st):
    '''
    Enable entryusn
    Delete all user entries.
    Run USN tombstone cleanup task
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 usn test...")
    topology_st.standalone.plugins.enable(name=PLUGIN_USN)

    topology_st.standalone.restart(timeout=10)

    try:
        entries = topology_st.standalone.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=inetorgperson)")
        if len(entries) == 0:
            log.info("No user entries.")
        else:
            for i in range(len(entries)):
                # log.info('Deleting %s' % entries[i].dn)
                try:
                    topology_st.standalone.delete_s(entries[i].dn)
                except ValueError:
                    log.error('delete_s %s failed.' % entries[i].dn)
                    assert False
    except ValueError:
        log.error('search_s failed.')
        assert False

    try:
        # run the usn tombstone cleanup
        topology_st.standalone.tasks.usnTombstoneCleanup(suffix=SUFFIX, bename="userRoot", args={TASK_WAIT: False})
    except ValueError:
        log.error('Some problem occured with a value that was provided')
        assert False

    topology_st.standalone.stop(timeout=10)

    mytmp = '/tmp'
    logdir = re.sub('errors', '', topology_st.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        s.system('mv %score* %s/core.ticket48005_usn' % (logdir, mytmp))
        log.error('usnTombstoneCleanup: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology_st.standalone.start(timeout=10)

    topology_st.standalone.plugins.disable(name=PLUGIN_USN)

    topology_st.standalone.restart(timeout=10)

    log.info("Ticket 48005 usn test complete")


def test_ticket48005_schemareload(topology_st):
    '''
    Run schema reload task without waiting
    Shutdown the server
    Check if a core file was generated or not
    If no core was found, this test case was successful.
    '''
    log.info("Ticket 48005 schema reload test...")

    try:
        # run the schema reload task
        topology_st.standalone.tasks.schemaReload(args={TASK_WAIT: False})
    except ValueError:
        log.error('Schema Reload task failed.')
        assert False

    topology_st.standalone.stop(timeout=10)

    logdir = re.sub('errors', '', topology_st.standalone.errlog)
    cmdline = 'ls ' + logdir + 'core*'
    p = os.popen(cmdline, "r")
    lcore = p.readline()
    if lcore != "":
        mytmp = '/tmp'
        s.system('mv %score* %s/core.ticket48005_schema_reload' % (logdir, mytmp))
        log.error('Schema reload: Moved core file(s) to %s; Test failed' % mytmp)
        assert False
    log.info('No core files are found')

    topology_st.standalone.start(timeout=10)

    log.info("Ticket 48005 schema reload test complete")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
