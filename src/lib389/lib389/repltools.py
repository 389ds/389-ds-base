import os
import os.path
import re
import subprocess
import ldap
from lib389._constants import *
from lib389.properties import *


# Helper functions
def _alphanum_key(s):
    """Turn the string into a list of string and number parts.
    """
    return [int(c) if c.isdigit() else c for c in
            re.split('([0-9]+)', s)]


def smart_sort(str_list):
    """Sort the given list in the way that humans expect.
    @param str_list - a list of strings to sort
    """
    str_list.sort(key=_alphanum_key)


def _getCSNTime(inst, csn):
    """Take a CSN and get the access log timestamp in seconds

    @param inst - A DirSrv object to check access log
    @param csn - A "csn" string that is used to find when the csn was logged in
                 the access log, and what time in seconds it was logged.
    @return - The time is seconds that the operation was logged.
    """

    op_line = inst.ds_access_log.match('.*csn=%s' % csn)
    if op_line:
        #vals = inst.ds_access_log.parse_line(op_line[0])
        return inst.ds_access_log.get_time_in_secs(op_line[0])
    else:
        return None


def _getCSNandTime(inst, line):
    """Take the line and find the CSN from the inst's access logs

    @param inst - A DirSrv object to check access log
    @param line - A "RESULT" line from the access log that contains a "csn"
    @return - a tuple containing the "csn" value and the time in seconds when
              it was logged.
    """

    op_line = inst.ds_access_log.match('.*%s.*' % line)
    if op_line:
        vals = inst.ds_access_log.parse_line(op_line[0])
        op = vals['op']
        conn = vals['conn']

        # Now find the result line and CSN
        result_line = inst.ds_access_log.match_archive(
            '.*conn=%s op=%s RESULT.*' % (conn, op))

        if result_line:
            vals = inst.ds_access_log.parse_line(result_line[0])
            if 'csn' in vals:
                ts = inst.ds_access_log.get_time_in_secs(result_line[0])
                return (vals['csn'], ts)

    return (None, None)


class ReplTools(object):
    """Replication tools
    """

    @staticmethod
    def checkCSNs(dirsrv_replicas):
        """Gather all the CSN strings from the access and verify all of those
        CSNs exist on all the other replicas.

        @param dirsrv_replicas - a list of DirSrv objects.  The list must begin
                                 with master replicas

        @return - True if all the CSNs are present, otherwise False
        """

        csn_logs = []
        csn_log_count = 0

        for replica in dirsrv_replicas:
            logdir = '%s*' % replica.ds_access_log._get_log_path()
            outfile = '/tmp/csn' + str(csn_log_count)
            csn_logs.append(outfile)
            csn_log_count += 1
            cmd = ("grep csn= " + logdir +
                   " | awk '{print $10}' | sort -u > " + outfile)
            os.system(cmd)

        # Set a side the first master log - we use this for our "diffing"
        main_log = csn_logs[0]
        csn_logs.pop(0)

        # Now process the remaining csn logs
        for csnlog in csn_logs:
            cmd = 'diff %s %s' % (main_log, csnlog)
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True)
            line = proc.stdout.readline()
            if line != "" and line != "\n":
                if not line.startswith("\\"):
                    log.fatal("We have a CSN mismatch between (%s vs %s): %s" %
                              (main_log, csn_log, line))
                    return False

        return True

    @staticmethod
    def replConvReport(suffix, ops, replica, all_replicas):
        """Find and measure the convergence of entries from a replica, and
        print a report on how fast all the "ops" replicated to the other
        replicas.

        @param suffix - Replicated suffix
        @param ops -  a list of "operations" to search for in the access logs
        @param replica - Dirsrv object where the entries originated
        @param all_replicas - A list of Dirsrv replicas:
                              (suppliers, hubs, consumers)
        @return - None
        """

        print('Convergence Report for replica: %s (%s)' %
              (replica.serverid, suffix))
        print('-' * 80)

        # Loop through each operation checking all the access logs
        for op in ops:
            csnstr, csntime = _getCSNandTime(replica, op)
            if csnstr is None and csntime is None:
                # Didn't find a csn, move on
                continue

            conv_time = []
            longest_time = 0
            for inst in all_replicas:
                replObj = inst.replicas.get(suffix)
                if replObj is None:
                    inst.log.warn('(%s) not setup for replication of (%s)' %
                                   (inst.serverid, suffix))
                    continue
                ctime = _getCSNTime(inst, csnstr)
                if ctime:
                    role = replObj.get_role()
                    if role == REPLICAROLE_MASTER:
                        txt = ' Master (%s)' % (inst.serverid)
                    elif role == REPLICAROLE_HUB:
                        txt = ' Hub (%s)' % (inst.serverid)
                    elif role == REPLICAROLE_CONSUMER:
                        txt = ' Consumer (%s)' % (inst.serverid)
                    else:
                        txt = '?'
                    ctime = ctime - csntime
                    conv_time.append(str(ctime) + txt)
                    if ctime > longest_time:
                        longest_time = ctime

            smart_sort(conv_time)
            print('\n    Operation: %s\n    %s' % (op, '-' * 40))
            print('\n      Convergence times:')
            for line in conv_time:
                parts = line.split(' ', 1)
                print('        %8s secs - %s' % (parts[0], parts[1]))
            print('\n      Longest Convergence Time: ' +
                  str(longest_time))

        print('\nEnd of Convergence Report for: %s\n' % (replica.serverid))

    @staticmethod
    def replIdle(replicas, suffix=DEFAULT_SUFFIX):
        """Take a list of DirSrv Objects and check to see if all of the present
        replication agreements are idle for a particular backend

        @param replicas - List of DirSrv objects that are using replication
        @param suffix - The replicated suffix
        @raise LDAPError - if unable to search for the replication agreements
        @return - True if all the agreements are idle, otherwise False
        """

        IDLE_MSG = ('Replica acquired successfully: Incremental ' +
                    'update succeeded')
        STATUS_ATTR = 'nsds5replicaLastUpdateStatus'
        FILTER = ('(&(nsDS5ReplicaRoot=' + suffix +
                  ')(objectclass=nsds5replicationAgreement))')
        repl_idle = True

        for inst in replicas:
            try:
                entries = inst.search_s("cn=config",
                    ldap.SCOPE_SUBTREE, FILTER, [STATUS_ATTR])
                if entries:
                    for entry in entries:
                        if IDLE_MSG not in entry.getValue(STATUS_ATTR):
                            repl_idle = False
                            break

                if not repl_idle:
                    break

            except ldap.LDAPError as e:
                log.fatal('Failed to search the repl agmts on ' +
                          '%s - Error: %s' % (inst.serverid, str(e)))
                assert False
        return repl_idle

    @staticmethod
    def createReplManager(server, repl_manager_dn=None, repl_manager_pw=None):
        '''Create an entry that will be used to bind as replication manager.

        @param server - A DirSrv object to connect to
        @param repl_manager_dn - DN of the bind entry. If not provided use
                                 the default one
        @param repl_manager_pw - Password of the entry. If not provide use
                                 the default one
        @return None
        @raise KeyError - if can not find valid values of Bind DN and Pwd
               LDAPError - if we fail to add the replication manager
        '''

        # check the DN and PW
        try:
            repl_manager_dn = repl_manager_dn or \
                defaultProperties[REPLICATION_BIND_DN]
            repl_manager_pw = repl_manager_pw or \
                defaultProperties[REPLICATION_BIND_PW]
            if not repl_manager_dn or not repl_manager_pw:
                raise KeyError
        except KeyError:
            if not repl_manager_pw:
                server.log.warning("replica_createReplMgr: bind DN password " +
                                   "not specified")
            if not repl_manager_dn:
                server.log.warning("replica_createReplMgr: bind DN not " +
                                   "specified")
            raise

        # If the replication manager entry already exists, just return
        try:
            entries = server.search_s(repl_manager_dn, ldap.SCOPE_BASE,
                                              "objectclass=*")
            if entries:
                # it already exist, fine
                return
        except ldap.NO_SUCH_OBJECT:
            pass

        # ok it does not exist, create it
        attrs = {'nsIdleTimeout': '0',
                 'passwordExpirationTime': '20381010000000Z'}
        server.setupBindDN(repl_manager_dn, repl_manager_pw, attrs)

