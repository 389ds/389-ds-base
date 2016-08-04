# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Tools for creating and managing servers

    uses DirSrv
"""
import sys
import os
import os.path
import base64
import select
import time
import shutil
import subprocess
import tarfile
import re
import glob
import pwd
import grp
import logging
import ldap
import shlex
import socket
import getpass

# from .nss_ssl import nss_create_new_database
from threading import Timer
from lib389._constants import *
from lib389._ldifconn import LDIFConn
from lib389.properties import *
from lib389.utils import (
    is_a_dn,
    getcfgdsuserdn,
    getcfgdsinfo,
    getcfgdsuserdn,
    update_newhost_with_fqdn,
    get_sbin_dir,
    get_server_user,
    getdomainname,
    isLocalHost,
    formatInfData,
    getserverroot,
    update_admin_domain,
    getadminport,
    getdefaultsuffix,
    ensure_bytes,
    ensure_str,
    socket_check_open,)
from lib389.passwd import password_hash, password_generate

# The poc backend api
from lib389.backend import Backends

try:
    # There are too many issues with this on EL7
    # Out of the box, it's just outright broken ...
    import six.moves.urllib.request
    import six.moves.urllib.parse
    import six.moves.urllib.error
    import six
except ImportError:
    pass

MAJOR, MINOR, _, _, _ = sys.version_info

if MAJOR >= 3:
    import configparser
else:
    import ConfigParser as configparser

__all__ = ['DirSrvTools']
try:
    from subprocess import Popen, PIPE, STDOUT
    HASPOPEN = True
except ImportError:
    import popen2
    HASPOPEN = False


logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger(__name__)

# Private constants
PATH_SETUP_DS_ADMIN = "/setup-ds-admin.pl"
PATH_SETUP_DS = CMD_PATH_SETUP_DS
PATH_REMOVE_DS = CMD_PATH_REMOVE_DS
PATH_ADM_CONF = "/etc/dirsrv/admin-serv/adm.conf"
GROUPADD = "/usr/sbin/groupadd"
USERADD = "/usr/sbin/useradd"
NOLOGIN = "/sbin/nologin"


def kill_proc(proc, timeout):
    """Kill a process after the timeout is reached
    @param proc - The subprocess process
    @param timeout - timeout in seconds
    """
    timeout["value"] = True
    proc.kill()


def runCmd(cmd, timeout_sec):
    """Run a system command with a timeout
    @param cmd - The full system command
    @param timeout_sec - The timeoput value in seconds
    @return - The result code
    """
    proc = subprocess.Popen(shlex.split(cmd), stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    timeout = {"value": False}
    timer = Timer(timeout_sec, kill_proc, [proc, timeout])
    timer.start()
    stdout, stderr = proc.communicate()
    timer.cancel()
    return proc.returncode


class DirSrvTools(object):
    """DirSrv mix-in."""

    @staticmethod
    def cgiFake(sroot, verbose, prog, args):
        """Run the local program prog as a CGI using the POST method."""
        content = urllib.parse.urlencode(args)
        length = len(content)
        # setup CGI environment
        env = os.environ.copy()
        env['REQUEST_METHOD'] = "POST"
        env['NETSITE_ROOT'] = sroot
        env['CONTENT_LENGTH'] = str(length)
        progdir = os.path.dirname(prog)
        if HASPOPEN:
            pipe = Popen(prog, cwd=progdir, env=env,
                         stdin=PIPE, stdout=PIPE, stderr=STDOUT)
            child_stdin = pipe.stdin
            child_stdout = pipe.stdout
        else:
            saveenv = os.environ
            os.environ = env
            child_stdout, child_stdin = popen2.popen2(prog)
            os.environ = saveenv
        child_stdin.write(content)
        child_stdin.close()
        for line in child_stdout:
            if verbose:
                sys.stdout.write(line)
            ary = line.split(":")
            if len(ary) > 1 and ary[0] == 'NMC_Status':
                exitCode = ary[1].strip()
                break
        child_stdout.close()
        if HASPOPEN:
            osCode = pipe.wait()
            print("%s returned NMC code %s and OS code %s" %
                  (prog, exitCode, osCode))
        return exitCode

    @staticmethod
    def cgiPost(host, port, username, password, uri, verbose, secure,
                args=None):
        """Post the request to the admin server.

           Admin server requires authentication, so we use the auth handler
           classes.

            NOTE: the url classes in python use the deprecated
            base64.encodestring() function, which truncates lines,
            causing Apache to give us a 400 Bad Request error for the
            Authentication string.  So, we have to tell
            base64.encodestring() not to truncate."""
        args = args or {}
        prefix = 'http'
        if secure:
            prefix = 'https'
        hostport = host + ":" + port
        # construct our url
        url = '%s://%s:%s%s' % (prefix, host, port, uri)
        # tell base64 not to truncate lines
        savedbinsize = base64.MAXBINSIZE
        base64.MAXBINSIZE = 256
        # create the password manager - we don't care about the realm
        passman = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        # add our password
        passman.add_password(None, hostport, username, password)
        # create the auth handler
        authhandler = urllib.request.HTTPBasicAuthHandler(passman)
        # create our url opener that handles basic auth
        opener = urllib.request.build_opener(authhandler)
        # make admin server think we are the console
        opener.addheaders = [('User-Agent', 'Fedora-Console/1.0')]
        if verbose:
            print("requesting url", url)
            sys.stdout.flush()
        exitCode = 1
        try:
            req = opener.open(url, urllib.parse.urlencode(args))
            for line in req:
                if verbose:
                    print (line)
                ary = line.split(":")
                if len(ary) > 1 and ary[0] == 'NMC_Status':
                    exitCode = ary[1].strip()
                    break
            req.close()
#         except IOError, e:
#             print e
#             print e.code
#             print e.headers
#             raise
        finally:
            # restore binsize
            base64.MAXBINSIZE = savedbinsize
        return exitCode

    @staticmethod
    def serverCmd(self, cmd, verbose, timeout=120):
        """NOTE: this tries to open the log!
        """
        if not hasattr(self, 'sroot'):
            # If the instance was previously create, retrieve 'sroot' from
            # sys/priv config file (e.g <prefix>/etc/sysconfig/dirsrv-<serverid
            # or $HOME/.dirsrv/dirsrv-<serverid>
            props = self.list()
            if len(props) == 0:
                # the instance has not yet been created, just return
                return
            else:
                assert len(props) == 1
                self.sroot = props[0][CONF_SERVER_DIR]

        sbinDir = get_sbin_dir(prefix=self.prefix)

        if hasattr(self, 'errlog'):
            errLog = self.errlog
        else:
            errLog = os.path.join(self.prefix or "/",
                                  "var/log/dirsrv/slapd-%s/errors" %
                                  self.serverid)
        done = False
        started = True
        lastLine = ""
        cmd = cmd.lower()
        # fullCmd = instanceDir + "/" + cmd + "-slapd"
        fullCmd = None
        if cmd == 'start':
            fullCmd = os.path.join(sbinDir, 'start-dirsrv %s' % self.serverid)
            cmdPat = 'slapd started.'
        else:
            fullCmd = os.path.join(sbinDir, 'stop-dirsrv %s' % self.serverid)
            cmdPat = 'slapd stopped.'

        if "USE_GDB" in os.environ or "USE_VALGRIND" in os.environ:
            timeout = timeout * 3

        full_timeout = int(time.time()) + timeout

        if cmd == 'stop':
            log.warn("unbinding before stop")
            try:
                self.unbind()
            except:
                log.warn("Unbinding fails: Instance already down?")
                pass

        log.info("Setup error log")
        try:
            logfp = open(errLog, 'r')
            logfp.seek(0, os.SEEK_END)  # seek to end
            pos = logfp.tell()  # get current position
            logfp.seek(pos, os.SEEK_SET)  # reset the EOF flag
        except IOError as e:
            if e.errno == 2:
                # No error log - just create one and move on
                try:
                    open(errLog, 'w').close()  # Create empty file
                    logfp = open(errLog, 'r')  # now open it for reading
                except:
                    done = True
            else:
                done = True

        log.warn("Running command: %r - timeout(%d)" % (fullCmd, timeout))
        rc = runCmd("%s" % fullCmd, timeout)
        while rc == 0 and not done and int(time.time()) < full_timeout:
            line = logfp.readline()
            while not done and line:
                lastLine = line
                if verbose:
                    log.debug("current line: %r" % line.strip())
                if line.find(cmdPat) >= 0:
                    started += 1
                    if started == 2:
                        done = True
                elif line.find("Initialization Failed") >= 0:
                    # sometimes the server fails to start - try again
                    rc = runCmd("%s" % (fullCmd), timeout)
                    pos = logfp.tell()
                    break
                elif line.find("exiting.") >= 0:
                    # possible transient condition - try again
                    rc = runCmd("%s" % (fullCmd), timeout)
                    pos = logfp.tell()
                    break
                pos = logfp.tell()
                line = logfp.readline()
            if line.find("PR_Bind") >= 0:
                # server port conflicts with another one, just report and punt
                log.debug("last line: %r" % lastLine.strip())
                log.warn("This server cannot be started until the other server"
                         " on this port is shutdown")
                done = True
            if not done:
                time.sleep(2)
                logfp.seek(pos, 0)
        logfp.close()
        if started < 2:
            now = int(time.time())
            if now > timeout:
                log.warn(
                    "Probable timeout: timeout=%d now=%d" % (timeout, now))

            log.error("Error: could not %s server %s %s: %d" % (
                      cmd, self.sroot, self.inst, rc))
            return 1
        else:
            log.info("%s was successful for %s %s" % (
                     cmd, self.sroot, self.inst))
            if cmd == 'start':
                self.__localinit__()
        return 0

    @staticmethod
    def stop(self, verbose=False, timeout=120):
        """Stop server or raise."""
        if not self.isLocal and hasattr(self, 'asport'):
            log.info("stopping remote server ", self)
            self.unbind()
            log.info("closed remote server ", self)
            cgiargs = {}
            rc = DirSrvTools.cgiPost(self.host, self.asport, self.cfgdsuser,
                                     self.cfgdspwd,
                                     "/slapd-%s/Tasks/Operation/stop" %
                                     self.inst, verbose, cgiargs)
            log.info("stopped remote server %s rc = %d" % (self, rc))
            return rc
        else:
            return DirSrvTools.serverCmd(self, 'stop', verbose, timeout)

    @staticmethod
    def start(self, verbose=False, timeout=120):
        """
        Start a server
        """
        if not self.isLocal and hasattr(self, 'asport'):
            log.debug("starting remote server %s " % self)
            cgiargs = {}
            rc = DirSrvTools.cgiPost(self.host, self.asport, self.cfgdsuser,
                                     self.cfgdspwd,
                                     "/slapd-%s/Tasks/Operation/start" %
                                     self.inst, verbose, cgiargs)
            log.debug("connecting remote server %s" % self)
            if not rc:
                self.__localinit__()
            log.info("started remote server %s rc = %d" % (self, rc))
            return rc
        else:
            log.debug("Starting server %r" % self)
            return DirSrvTools.serverCmd(self, 'start', verbose, timeout)

    @staticmethod
    def _infoInstanceBackupFS(dirsrv):
        """
            Return the information to retrieve the backup file of a given
            instance
            It returns:
                - Directory name containing the backup
                  (e.g. /tmp/slapd-standalone.bck)
                - The pattern of the backup files
                  (e.g. /tmp/slapd-standalone.bck/backup*.tar.gz)
        """
        backup_dir = "%s/slapd-%s.bck" % (dirsrv.backupdir, dirsrv.inst)
        backup_pattern = os.path.join(backup_dir, "backup*.tar.gz")
        return backup_dir, backup_pattern

    @staticmethod
    def clearInstanceBackupFS(dirsrv=None, backup_file=None):
        """
            Remove a backup_file or all backup up of a given instance
        """
        if backup_file:
            if os.path.isfile(backup_file):
                try:
                    os.remove(backup_file)
                except:
                    log.info("clearInstanceBackupFS: fail to remove %s" %
                             backup_file)
                    pass
        elif dirsrv:
            backup_dir, backup_pattern = \
                DirSrvTools._infoInstanceBackupFS(dirsrv)
            list_backup_files = glob.glob(backup_pattern)
            for f in list_backup_files:
                try:
                    os.remove(f)
                except:
                    log.info("clearInstanceBackupFS: fail to remove %s" %
                             backup_file)
                    pass

    @staticmethod
    def checkInstanceBackupFS(dirsrv):
        """
            If it exits a backup file, it returns it
            else it returns None
        """

        backup_dir, backup_pattern = DirSrvTools._infoInstanceBackupFS(dirsrv)
        list_backup_files = glob.glob(backup_pattern)
        if not list_backup_files:
            return None
        else:
            # returns the first found backup
            return list_backup_files[0]

    @staticmethod
    def instanceBackupFS(dirsrv):
        """
            Saves the files of an instance under
                /tmp/slapd-<instance_name>.bck/backup_HHMMSS.tar.gz
            and return the archive file name.
            If it already exists a such file, it assums it is a valid backup
            and returns its name

            dirsrv.sroot: root of the instance  (e.g. /usr/lib64/dirsrv)
            dirsrv.inst: instance name (e.g. standalone for
                         /etc/dirsrv/slapd-standalone)
            dirsrv.confdir: root of the instance config (e.g. /etc/dirsrv)
            dirsrv.dbdir: directory where is stored the database
                          (e.g. /var/lib/dirsrv/slapd-standalone/db)
            dirsrv.changelogdir: directory where is stored the changelog
                                (e.g. /var/lib/dirsrv/slapd-master/changelogdb)
        """

        # First check it if already exists a backup file
        backup_dir, backup_pattern = DirSrvTools._infoInstanceBackupFS(dirsrv)
        backup_file = DirSrvTools.checkInstanceBackupFS(dirsrv)
        if backup_file is None:
            if not os.path.exists(backup_dir):
                os.makedirs(backup_dir)
        else:
            return backup_file

        # goes under the directory where the DS is deployed
        listFilesToBackup = []
        here = os.getcwd()
        os.chdir(dirsrv.prefix)
        prefix_pattern = "%s/" % dirsrv.prefix

        # build the list of directories to scan
        instroot = "%s/slapd-%s" % (dirsrv.sroot, dirsrv.inst)
        ldir = [instroot]
        if hasattr(dirsrv, 'confdir'):
            ldir.append(dirsrv.confdir)
        if hasattr(dirsrv, 'dbdir'):
            ldir.append(dirsrv.dbdir)
        if hasattr(dirsrv, 'changelogdir'):
            ldir.append(dirsrv.changelogdir)
        if hasattr(dirsrv, 'errlog'):
            ldir.append(os.path.dirname(dirsrv.errlog))
        if hasattr(dirsrv, 'accesslog') and \
           os.path.dirname(dirsrv.accesslog) not in ldir:
            ldir.append(os.path.dirname(dirsrv.accesslog))

        # now scan the directory list to find the files to backup
        for dirToBackup in ldir:
            for root, dirs, files in os.walk(dirToBackup):
                for file in files:
                    name = os.path.join(root, file)
                    name = re.sub(prefix_pattern, '', name)

                    if os.path.isfile(name):
                        listFilesToBackup.append(name)
                        log.debug("instanceBackupFS add = %s (%s)" %
                                  (name, dirsrv.prefix))

        # create the archive
        name = "backup_%s.tar.gz" % (time.strftime("%m%d%Y_%H%M%S"))
        backup_file = os.path.join(backup_dir, name)
        tar = tarfile.open(backup_file, "w:gz")

        for name in listFilesToBackup:
            if os.path.isfile(name):
                tar.add(name)
        tar.close()
        log.info("instanceBackupFS: archive done : %s" % backup_file)

        # return to the directory where we were
        os.chdir(here)

        return backup_file

    @staticmethod
    def instanceRestoreFS(dirsrv, backup_file):
        """
        """

        # First check the archive exists
        if backup_file is None:
            log.warning("Unable to restore the instance (missing backup)")
            return 1
        if not os.path.isfile(backup_file):
            log.warning("Unable to restore the instance (%s is not a file)" %
                        backup_file)
            return 1

        #
        # Second do some clean up
        #

        # previous db (it may exists new db files not in the backup)
        log.debug("instanceRestoreFS: remove subtree %s/*" % dirsrv.dbdir)
        for root, dirs, files in os.walk(dirsrv.dbdir):
            for d in dirs:
                if d not in ("bak", "ldif"):
                    log.debug(
                        "instanceRestoreFS: before restore remove directory "
                        "%s/%s" % (root, d))
                    shutil.rmtree("%s/%s" % (root, d))

        # previous error/access logs
        log.debug("instanceRestoreFS: remove error logs %s" % dirsrv.errlog)
        for f in glob.glob("%s*" % dirsrv.errlog):
                log.debug("instanceRestoreFS: before restore remove file %s" %
                          (f))
                os.remove(f)
        log.debug("instanceRestoreFS: remove access logs %s" %
                  dirsrv.accesslog)
        for f in glob.glob("%s*" % dirsrv.accesslog):
                log.debug("instanceRestoreFS: before restore remove file %s" %
                          (f))
                os.remove(f)

        # Then restore from the directory where DS was deployed
        here = os.getcwd()
        os.chdir(dirsrv.prefix)

        tar = tarfile.open(backup_file)
        for member in tar.getmembers():
            if os.path.isfile(member.name):
                #
                # restore only writable files
                # It could be a bad idea and preferably restore all.
                # Now it will be easy to enhance that function.
                if os.access(member.name, os.W_OK):
                    log.debug("instanceRestoreFS: restored %s" % member.name)
                    tar.extract(member.name)
                else:
                    log.debug("instanceRestoreFS: not restored %s "
                              "(no write access)" % member.name)
            else:
                log.debug("instanceRestoreFS: restored %s" % member.name)
                tar.extract(member.name)

        tar.close()

        #
        # Now be safe, triggers a recovery at restart
        #
        guardian_file = os.path.join(dirsrv.dbdir, "db/guardian")
        if os.path.isfile(guardian_file):
            try:
                log.debug("instanceRestoreFS: remove %s" % guardian_file)
                os.remove(guardian_file)
            except:
                log.warning("instanceRestoreFS: fail to remove %s" %
                            guardian_file)
                pass

        os.chdir(here)

    @staticmethod
    def setupSSL(dirsrv, secport=636, sourcedir=None, secargs=None):
        """configure and setup SSL with a given certificate and
           restart the server.

            See DirSrv.configSSL for the secargs values
        """
        e = dirsrv.configSSL(secport, secargs)
        log.info("entry is %r" % [e])
        dn_config = e.dn
        # get our cert dir
        e_config = dirsrv.getEntry(
            dn_config, ldap.SCOPE_BASE, '(objectclass=*)')
        certdir = e_config.getValue('nsslapd-certdir')
        # have to stop the server before replacing any security files
        DirSrvTools.stop(dirsrv)
        # allow secport for selinux
        if secport != 636:
            try:
                log.debug("Configuring SELinux on port: %s", str(secport))

                subprocess.check_call(["semanage", "port", "-a", "-t",
                                       "ldap_port_t", "-p", "tcp",
                                       str(secport)])
            except OSError:
                log.debug("Likely SELinux not supported")
                pass
            except subprocess.CalledProcessError:
                log.debug("SELinux fails to configure")
                pass

        # eventually copy security files from source dir to our cert dir
        if sourcedir:
            for ff in ['cert8.db', 'key3.db', 'secmod.db', 'pin.txt',
                       'certmap.conf']:
                srcf = os.path.join(sourcedir, ff)
                destf = os.path.join(certdir, ff)
                # make sure dest is writable so we can copy over it
                try:
                    log.info("Copying security files: %s to %s" %
                             (srcf, destf))
                    mode = os.stat(destf).st_mode
                    newmode = mode | 0o600
                    os.chmod(destf, newmode)
                except Exception as e:
                    print(e)
                    pass  # oh well
                # copy2 will copy the mode too
                shutil.copy2(srcf, destf)

        # now, restart the ds
        DirSrvTools.start(dirsrv, True)

    @staticmethod
    def runInfProg(prog, content, verbose, prefix=None):
        """run a program that takes an .inf style file on stdin"""
        cmd = [prog]
        if verbose:
            cmd.append('-ddd')
        else:
            cmd.extend(['-l', '/dev/null'])
        cmd.extend(['-s', '-f', '-'])
        log.debug("running: %s " % cmd)
        if HASPOPEN:
            pipe = Popen(cmd, stdin=PIPE, stdout=PIPE, stderr=STDOUT)
            child_stdin = pipe.stdin
            child_stdout = pipe.stdout
        else:
            pipe = popen2.Popen4(cmd)
            child_stdin = pipe.tochild
            child_stdout = pipe.fromchild
        child_stdin.write(ensure_bytes(content))
        child_stdin.close()
        if verbose:
            log.debug("PID %s" % pipe.pid)
        while pipe.poll() is None:
            (rr, wr, xr) = select.select([child_stdout], [], [], 1.0)
            if rr and len(rr) > 0:
                line = rr[0].readline()
                if not line:
                    break
                if verbose:
                    sys.stdout.write(ensure_str(line))
            elif verbose:
                print("timed out waiting to read from pid %s : %s " % (pipe.pid, cmd))
        child_stdout.close()
        exitCode = pipe.wait()
        # if verbose:
        log.debug("%s returned exit code %s" % (prog, exitCode))
        return exitCode

    @staticmethod
    def removeInstance(dirsrv):
        """run the remove instance command"""
        if hasattr(dirsrv, 'prefix'):
            prefix = dirsrv.prefix
        else:
            prefix = None

        prog = get_sbin_dir(None, prefix) + PATH_REMOVE_DS
        cmd = "%s -i slapd-%s" % (prog, dirsrv.serverid)
        log.debug("running: %s " % cmd)
        try:
            os.system(cmd)
        except:
            log.exception("error executing %r" % cmd)

    @staticmethod
    def _offlineDirsrv(args):
        '''
            Function to allocate an offline DirSrv instance.
            This instance is not initialized with the Directory instance
            (__localinit__() and __add_brookers__() are not called)
            The properties set are:
                instance.host
                instance.port
                instance.serverid
                instance.inst
                instance.prefix
                instance.backup
        '''
        instance = lib389.DirSrv(verbose=True)
        instance.allocate(args)

        return instance

    @staticmethod
    def existsBackup(args):
        '''
            If the backup of the instance exists, it returns it.
            Else None
        '''
        instance = DirSrvTools._offlineDirsrv(args)
        return DirSrvTools.checkInstanceBackupFS(instance)

    @staticmethod
    def existsInstance(args):
        '''
            Check if an instance exists.
            It checks if the following directories/files exist:
                <confdir>/slapd-<name>
                <errlog>
            If it exists it returns a DirSrv instance NOT initialized,
            else None
        '''
        instance = DirSrvTools._offlineDirsrv(args)
        dirname = os.path.join(instance.prefix, "etc/dirsrv/slapd-%s" %
                               instance.serverid)
        errorlog = os.path.join(instance.prefix,
                                "var/log/dirsrv/slapd-%s/errors" %
                                instance.serverid)
        sroot = os.path.join(instance.prefix, "lib/dirsrv")
        if os.path.isdir(dirname) and \
           os.path.isfile(errorlog) and \
           os.path.isdir(sroot):
            instance.sroot = sroot
            instance.errlog = errorlog
            return instance

        return None

    @staticmethod
    def createInstance(args, verbose=0):
        """Create a new instance of directory server and return a connection
        to it.

        This function:
        - guesses the hostname where to create the DS, using
        localhost by default;
        - figures out if the given hostname is the local host or not.

        @param args -  a dict with the following values {
            # new instance compulsory values
            'newinstance': 'rpolli',
            'newsuffix': 'dc=example,dc=com',
            'newhost': 'localhost.localdomain',
            'newport': 22389,
            'newrootpw': 'password',

            # optionally register instance on an admin tree
            'have_admin': True,

            # optionally directory where to store instance backup
            'backupdir': [ /tmp ]

            # you can configure a new dirsrv-admin
            'setup_admin': True,

            # or you need the dirsrv-admin to be already setup
            'cfgdshost': 'localhost.localdomain',
            'cfgdsport': 22389,
            'cfgdsuser': 'admin',
            'cfgdspwd': 'admin',

        }
        """
        cfgdn = lib389.CFGSUFFIX
        isLocal = update_newhost_with_fqdn(args)

        # use prefix if binaries are relocated
        sroot = args.get('sroot', '')
        prefix = args.setdefault('prefix', '')

        # get the backup directory to store instance backup
        backupdir = args.get('backupdir', '/tmp')

        # new style - prefix or FHS?
        args['new_style'] = not args.get('sroot')

        # do we have ds only or ds+admin?
        if 'no_admin' not in args:
            sbindir = get_sbin_dir(sroot, prefix)
            if os.path.isfile(sbindir + PATH_SETUP_DS_ADMIN):
                args['have_admin'] = True

        # set default values
        args['have_admin'] = args.get('have_admin', False)
        args['setup_admin'] = args.get('setup_admin', False)

        # get default values from adm.conf
        if args['new_style'] and args['have_admin']:
            admconf = LDIFConn(
                args['prefix'] + PATH_ADM_CONF)
            args['admconf'] = admconf.get('')

        # next, get the configuration ds host and port
        if args['have_admin']:
            args['cfgdshost'], args['cfgdsport'], cfgdn = getcfgdsinfo(args)
        #
        # if a Config DS is passed, get the userdn. This creates
        # a connection to the given DS. If you don't want to connect
        # to this server you should pass 'setup_admin' too.
        #
        if args['have_admin'] and not args['setup_admin']:
            cfgconn = getcfgdsuserdn(cfgdn, args)

        # next, get the server root if not given
        if not args['new_style']:
            getserverroot(cfgconn, isLocal, args)
        # next, get the admin domain
        if args['have_admin']:
            update_admin_domain(isLocal, args)
        # next, get the admin server port and any other information -
        # close the cfgconn
        if args['have_admin'] and not args['setup_admin']:
            asport, secure = getadminport(cfgconn, cfgdn, args)
        # next, get the posix username
        get_server_user(args)
        # fixup and verify other args
        args['newport'] = args.get('newport', 389)
        args['newrootdn'] = args.get('newrootdn', DN_DM)
        args['newsuffix'] = args.get('newsuffix',
                                     getdefaultsuffix(args['newhost']))

        if not isLocal or 'cfgdshost' in args:
            if 'admin_domain' not in args:
                args['admin_domain'] = getdomainname(args['newhost'])
            if isLocal and 'cfgdspwd' not in args:
                args['cfgdspwd'] = "dummy"
            if isLocal and 'cfgdshost' not in args:
                args['cfgdshost'] = args['newhost']
            if isLocal and 'cfgdsport' not in args:
                args['cfgdsport'] = 55555
        missing = False
        for param in ('newhost', 'newport', 'newrootdn', 'newrootpw',
                      'newinstance', 'newsuffix'):
            if param not in args:
                log.error("missing required argument: ", param)
                missing = True
        if missing:
            raise InvalidArgumentError("missing required arguments")

        # try to connect with the given parameters
        try:
            newconn = lib389.DirSrv(args['newhost'], args['newport'],
                                    args['newrootdn'], args['newrootpw'],
                                    args['newinstance'])
            newconn.prefix = prefix
            newconn.backupdir = backupdir
            newconn.isLocal = isLocal
            if args['have_admin'] and not args['setup_admin']:
                newconn.asport = asport
                newconn.cfgdsuser = args['cfgdsuser']
                newconn.cfgdspwd = args['cfgdspwd']

            host = args['newhost']
            port = args['newport']
            print("Warning: server at %s:%s " % (host, port) +
                  "already exists, returning connection to it")
            return newconn
        except ldap.SERVER_DOWN:
            pass  # not running - create new one

        if not isLocal or 'cfgdshost' in args:
            for param in ('cfgdshost', 'cfgdsport', 'cfgdsuser', 'cfgdspwd',
                          'admin_domain'):
                if param not in args:
                    print("missing required argument", param)
                    missing = True
        if not isLocal and not asport:
            print("missing required argument admin server port")
            missing = True
        if missing:
            raise InvalidArgumentError("missing required arguments")

        # construct a hash table with our CGI arguments - used with cgiPost
        # and cgiFake
        cgiargs = {
            'servname': args['newhost'],
            'servport': args['newport'],
            'rootdn': args['newrootdn'],
            'rootpw': args['newrootpw'],
            'servid': args['newinstance'],
            'suffix': args['newsuffix'],
            'servuser': args['newuserid'],
            'start_server': 1
        }
        if 'cfgdshost' in args:
            cgiargs['cfg_sspt_uid'] = args['cfgdsuser']
            cgiargs['cfg_sspt_uid_pw'] = args['cfgdspwd']
            cgiargs['ldap_url'] = "ldap://%s:%d/%s" % (
                args['cfgdshost'], args['cfgdsport'], cfgdn)
            cgiargs['admin_domain'] = args['admin_domain']

        if not isLocal:
            DirSrvTools.cgiPost(args['newhost'], asport, args['cfgdsuser'],
                                args['cfgdspwd'],
                                "/slapd/Tasks/Operation/Create",
                                verbose, secure, cgiargs)
        elif not args['new_style']:
            prog = sroot + "/bin/slapd/admin/bin/ds_create"
            if not os.access(prog, os.X_OK):
                prog = sroot + "/bin/slapd/admin/bin/ds_newinstance"
            DirSrvTools.cgiFake(sroot, verbose, prog, cgiargs)
        else:
            prog = ''
            if args['have_admin']:
                prog = get_sbin_dir(sroot, prefix) + PATH_SETUP_DS_ADMIN
            else:
                prog = get_sbin_dir(sroot, prefix) + PATH_SETUP_DS

            if not os.path.isfile(prog):
                log.error("Can't find file: %r, removing extension" % prog)
                prog = prog[:-3]

            content = formatInfData(args)
            DirSrvTools.runInfProg(prog, content, verbose)

        newconn = lib389.DirSrv(args['newhost'], args['newport'],
                                args['newrootdn'], args['newrootpw'],
                                args['newinstance'])
        newconn.prefix = prefix
        newconn.backupdir = backupdir
        newconn.isLocal = isLocal
        # Now the admin should have been created
        # but still I should have taken all the required infos
        # before.
        if args['have_admin'] and not args['setup_admin']:
            newconn.asport = asport
            newconn.cfgdsuser = args['cfgdsuser']
            newconn.cfgdspwd = args['cfgdspwd']
        return newconn

    @staticmethod
    def createAndSetupReplica(createArgs, repArgs):
        """
        Pass this sub two dicts - the first one is a dict suitable to create
        a new instance - see createInstance for more details
        the second is a dict suitable for replicaSetupAll -
        see replicaSetupAll
        """
        conn = DirSrvTools.createInstance(createArgs)
        if not conn:
            print("Error: could not create server", createArgs)
            return 0

        conn.replicaSetupAll(repArgs)
        return conn

    @staticmethod
    def makeGroup(group=DEFAULT_USER):
        """
        Create a system group
        """
        try:
            grp.getgrnam(group)
            print("OK group %s exists" % group)
        except KeyError:
            print("Adding group %s" % group)
            cmd = [GROUPADD, '-r', group]
            subprocess.Popen(cmd)

    @staticmethod
    def makeUser(user=DEFAULT_USER, group=DEFAULT_USER, home=DEFAULT_USERHOME):
        """
        Create a system user
        """
        try:
            pwd.getpwnam(user)
            print("OK user %s exists" % user)
        except KeyError:
            print("Adding user %s" % user)
            cmd = [USERADD, '-g', group, '-c', DEFAULT_USER_COMMENT, '-r',
                   '-d', home, '-s', NOLOGIN, user]
            subprocess.Popen(cmd)

    @staticmethod
    def lib389User(user=DEFAULT_USER):
        """
        Create the lib389 user/group
        """
        DirSrvTools.makeGroup(group=user)
        time.sleep(1)  # Need a little time for the group to get fully created
        DirSrvTools.makeUser(user=user, group=user, home=DEFAULT_USERHOME)

    @staticmethod
    def searchHostsFile(expectedHost, ipPattern=None):
        """
        Search the hosts file
        """
        hostFile = '/etc/hosts'

        with open(hostFile, 'r') as hostfp:
            # The with statement will automatically close the file after use

            try:
                for line in hostfp.readlines():
                    if ipPattern is None:
                        words = line.split()
                        assert(words[1] == expectedHost)
                        return True
                    else:
                        if line.find(ipPattern) >= 0:
                            words = line.split()
                            # We just want to make sure it's in there somewhere
                            assert(expectedHost in words)
                            return True
            except AssertionError:
                raise AssertionError(
                    "Error: %s should contain '%s' as first host for %s" %
                    ('/etc/hosts/', expectedHost, ipPattern))
            raise AssertionError(
                "Error: /etc/hosts does not contain '%s' as first host for %s"
                % (expectedHost, ipPattern))

    @staticmethod
    def getLocalhost():
        """Get the first host value after 127.0.0.1
        from /etc/hosts file
        """

        with open('/etc/hosts', 'r') as f:
            for line in f.readlines():
                if line.startswith('127.0.0.1'):
                    localhost = line.split()[1]
                    return localhost
        return None

    @staticmethod
    def testLocalhost():
        '''
        Checks that the 127.0.0.1 is resolved as localhost.localdomain
        This is required by DSUtil.pm:checkHostname else setup-ds.pl fails
        '''
        # One day I'll make this ::1. Ipv6 is the future. The future is now.
        loopbackIpPattern = '127.0.0.1'
        DirSrvTools.searchHostsFile(LOCALHOST, loopbackIpPattern)
        DirSrvTools.searchHostsFile(LOCALHOST_SHORT, loopbackIpPattern)

    @staticmethod
    def runUpgrade(prefix, online=True):
        '''
        Run "setup-ds.pl --update"  We simply pass in one DirSrv isntance, and
        this will update all the instances that are in this prefix.  For the
        update to work we must fix/adjust the permissions of the scripts in:

            /prefix/lib[64]/dirsrv/slapd-INSTANCE/
        '''

        if not prefix:
            prefix = ''
            # This is an RPM run - check if /lib exists, if not use /lib64
            if os.path.isdir('/usr/lib/dirsrv'):
                libdir = '/usr/lib/dirsrv/'
            else:
                if os.path.isdir('/usr/lib64/dirsrv'):
                    libdir = '/usr/lib64/dirsrv/'
                else:
                    log.fatal('runUpgrade: failed to find slapd lib dir!')
                    assert False
        else:
            # Standard prefix lib location
            if os.path.isdir('/usr/lib64/dirsrv'):
                libdir = '/usr/lib64/dirsrv/'
            else:
                libdir = '/lib/dirsrv/'

        # Gather all the instances so we can adjust the permissions, otherwise
        servers = []
        path = prefix + '/etc/dirsrv'
        for files in os.listdir(path):
            if files.startswith('slapd-') and not files.endswith('.removed'):
                servers.append(prefix + libdir + files)

        if len(servers) == 0:
            # This should not happen
            log.fatal('runUpgrade: no servers found!')
            assert False

        '''
        The setup script calls things like /lib/dirsrv/slapd-instance/db2bak,
        etc, and when we run the setup perl script it gets permission denied
        as the default permissions are 750.  Adjust the permissions to 755.
        '''
        for instance in servers:
            for files in os.listdir(instance):
                os.chmod(instance + '/' + files, 755)

        # Run the "upgrade"
        try:
            process = subprocess.Popen([prefix + '/sbin/setup-ds.pl',
                                        '--update'], shell=False,
                                       stdin=subprocess.PIPE)
            # Answer the interactive questions, as "--update" currently does
            # not work with INF files
            process.stdin.write('yes\n')
            if(online):
                process.stdin.write('online\n')
                for x in servers:
                    process.stdin.write(DN_DM + '\n')
                    process.stdin.write(PW_DM + '\n')
            else:
                process.stdin.write('offline\n')
            process.stdin.close()
            process.wait()
            if process.returncode != 0:
                log.fatal('runUpgrade failed!  Error: ' + process.returncode)
                assert False
        except:
            log.fatal('runUpgrade failed!')
            assert False

    @staticmethod
    def searchFile(filename, pattern):
        """
        Search file (or files using a wildcard) for the pattern
        """
        found = False

        file_list = glob.glob(filename)
        for file_name in file_list:
            try:
                myfile = open(file_name)
                for line in myfile:
                    result = re.search(pattern, line)
                    if result:
                        found = True
                        break
                myfile.close()
                if found:
                    break
            except IOError as e:
                log.error('Problem opening/searching file ' +
                          '(%s): I/O error(%d): %s' %
                          (file_name, e.errno, e.strerror))
        return found


class MockDirSrv(object):
    """
    Mock DirSrv Object
    """
    host = 'localhost'
    port = 22389
    sslport = 0

    def __init__(self, dict_=None):
        """
        Init object
        """
        if dict_:
            self.host = dict_['host']
            self.port = dict_['port']
            if 'sslport' in dict_:
                self.sslport = dict_['sslport']

    def __str__(self):
        """
        Return ldap URL
        """
        if self.sslport:
            return 'ldaps://%s:%s' % (self.host, self.sslport)
        else:
            return 'ldap://%s:%s' % (self.host, self.port)


class SetupDs(object):
    """
    Implements the Directory Server installer.

    This maybe subclassed, and a number of well known steps will be called.
    This allows the inf to be shared, and for other installers to work in lock
    step with this.

    If you are subclassing you want to derive:

    _validate_config_2(self, config):
    _prepare(self, extra):
    _install(self, extra):

    If you are calling this from an INF, you can pass the config in
    _validate_config, then stash the result into self.extra

    If you have anything you need passed to your install helpers, this can
    be given in create_from_args(extra) if you are calling as an api.

    If you use create_from_inf, self.extra is passed to create_from_args for
    you. You only need to over-load the three methods above.

    A logging interface is provided to self.log that you should call.
    """

    def __init__(self, verbose=False, dryrun=False):
        self.verbose = verbose
        self.extra = None
        self.dryrun = dryrun
        # Expose the logger to our children.
        self.log = log
        if self.verbose:
            log.info('Running setup with verbose')

    def _validate_config_2(self, config):
        pass

    def _prepare(self, extra):
        pass

    def _install(self, extra):
        pass

    def _set_config_fallback(self, config, group, attr, value, boolean=False, num=False):
        try:
            if boolean:
                return config.getboolean(group, attr)
            elif num:
                return config.getint(group, attr)
            else:
                return config.get(group, attr)
        except ValueError:
            return value
        except configparser.NoOptionError:
            log.info("%s not specified:setting to default - %s" % (attr, value))
            return value

    def _validate_ds_2_config(self, config):
        assert config.has_section('slapd')
        # Extract them in a way that create can understand.
        general = {}
        general['config_version'] = config.getint('general', 'config_version')
        general['full_machine_name'] = config.get('general', 'full_machine_name')
	general['strict_host_checking'] = self._set_config_fallback(config, 'general', 'strict_host_checking', True, boolean=True)
        # Change this to detect if SELinux is running
        general['selinux'] = self._set_config_fallback(config, 'general', 'selinux', False, boolean=True)

        if self.verbose:
            log.info("Configuration general %s" % general)

        # Validate that we are a config_version=2
        assert general['config_version'] >= 2

        slapd = {}
        # Can probably set these defaults out of somewhere else ...
        slapd['instance_name'] = config.get('slapd', 'instance_name')
        slapd['user'] = self._set_config_fallback(config, 'slapd', 'user', 'dirsrv')
        slapd['group'] = self._set_config_fallback(config, 'slapd', 'group', 'dirsrv')
        slapd['root_dn'] = self._set_config_fallback(config, 'slapd', 'root_dn', 'cn=Directory Manager')
        slapd['root_password'] = config.get('slapd', 'root_password')
        slapd['prefix'] = self._set_config_fallback(config, 'slapd', 'prefix', '/')
        # How do we default, defaults to the DS version.
        slapd['defaults'] = self._set_config_fallback(config, 'slapd', 'defaults', None)
        slapd['port'] = self._set_config_fallback(config, 'slapd', 'port', 389, num=True)
        slapd['secure_port'] = self._set_config_fallback(config, 'slapd', 'secure_port', 636, num=True)

        # These are all the paths for DS, that are RELATIVE to the prefix
        # This will need to change to cope with configure scripts from DS!
        # perhaps these should be read as a set of DEFAULTs from a config file?
        slapd['bin_dir'] = self._set_config_fallback(config, 'slapd', 'bin_dir', '%s/bin/' % (slapd['prefix']))
        slapd['sysconf_dir'] = self._set_config_fallback(config, 'slapd', 'sysconf_dir', '%s/etc' % (slapd['prefix']))
        slapd['data_dir'] = self._set_config_fallback(config, 'slapd', 'data_dir', '%s/share/' % (slapd['prefix']))
        slapd['local_state_dir'] = self._set_config_fallback(config, 'slapd', 'local_state_dir', '%s/var' % (slapd['prefix']))

        slapd['lib_dir'] = self._set_config_fallback(config, 'slapd', 'lib_dir', '%s/usr/lib64/dirsrv' % (slapd['prefix']))
        slapd['cert_dir'] = self._set_config_fallback(config, 'slapd', 'cert_dir', '%s/etc/dirsrv/slapd-%s/' % (slapd['prefix'], slapd['instance_name']))
        slapd['config_dir'] = self._set_config_fallback(config, 'slapd', 'config_dir', '%s/etc/dirsrv/slapd-%s/' % (slapd['prefix'], slapd['instance_name']))

        slapd['inst_dir'] = self._set_config_fallback(config, 'slapd', 'inst_dir', '%s/var/lib/dirsrv/slapd-%s' % (slapd['prefix'], slapd['instance_name']))
        slapd['backup_dir'] = self._set_config_fallback(config, 'slapd', 'backup_dir', '%s/bak' % (slapd['inst_dir']))
        slapd['db_dir'] = self._set_config_fallback(config, 'slapd', 'db_dir', '%s/db' % (slapd['inst_dir']))
        slapd['ldif_dir'] = self._set_config_fallback(config, 'slapd', 'ldif_dir', '%s/ldif' % (slapd['inst_dir']))

        slapd['lock_dir'] = self._set_config_fallback(config, 'slapd', 'lock_dir', '%s/var/lock/dirsrv/slapd-%s' % (slapd['prefix'], slapd['instance_name']))
        slapd['log_dir'] = self._set_config_fallback(config, 'slapd', 'log_dir', '%s/var/log/dirsrv/slapd-%s' % (slapd['prefix'], slapd['instance_name']))
        slapd['run_dir'] = self._set_config_fallback(config, 'slapd', 'run_dir', '%s/var/run/dirsrv' % (slapd['prefix']))
        slapd['sbin_dir'] = self._set_config_fallback(config, 'slapd', 'sbin_dir', '%s/sbin' % (slapd['prefix']))
        slapd['schema_dir'] = self._set_config_fallback(config, 'slapd', 'schema_dir', '%s/etc/dirsrv/slapd-%s/schema' % (slapd['prefix'], slapd['instance_name']))
        slapd['tmp_dir'] = self._set_config_fallback(config, 'slapd', 'tmp_dir', '/tmp')

        # Need to add all the default filesystem paths.

        if self.verbose:
            log.info("Configuration slapd %s" % slapd)

        backends = []
        for section in config.sections():
            if section.startswith('backend-'):
                be = {}
                # TODO: Add the other BACKEND_ types
                be[BACKEND_NAME] = section.replace('backend-', '')
                be[BACKEND_SUFFIX] = config.get(section, 'suffix')
                be[BACKEND_SAMPLE_ENTRIES] = config.getboolean(section, 'sample_entries')
                backends.append(be)

        if self.verbose:
            log.info("Configuration backends %s" % backends)

        return (general, slapd, backends)

    def _validate_ds_config(self, config):
        # This will move to lib389 later.
        # Check we have all the sections.
        # Make sure we have needed keys.
        assert(config.has_section('general'))
        assert(config.has_option('general', 'config_version'))
        assert(config.get('general', 'config_version') >= '2')
        if config.get('general', 'config_version') == '2':
            # Call our child api to validate itself from the inf.
            self._validate_config_2(config)
            return self._validate_ds_2_config(config)
        else:
            log.info("Failed to validate configuration version.")
            assert(False)

    def create_from_inf(self, inf_path):
        """
        Will trigger a create from the settings stored in inf_path
        """
        # Get the inf file
        if self.verbose:
            log.info("Using inf from %s" % inf_path)
        if not os.path.isfile(inf_path):
            log.error("%s is not a valid file path" % inf_path)
            return False
        config = None
        try:
            config = configparser.SafeConfigParser()
            config.read([inf_path])
        except Exception as e:
            log.error("Exception %s occured" % e)
            return False

        if self.verbose:
            log.info("Configuration %s" % config.sections())

        (general, slapd, backends) = self._validate_ds_config(config)

        # Actually do the setup now.
        self.create_from_args(general, slapd, backends, self.extra)

        return True

    def _prepare_ds(self, general, slapd, backends):
        # Validate our arguments.
        assert(slapd['user'] is not None)
        # check the user exists
        assert(pwd.getpwnam(slapd['user']))
        slapd['user_uid'] = pwd.getpwnam(slapd['user']).pw_uid
        assert(slapd['group'] is not None)
        assert(grp.getgrnam(slapd['group']))
        slapd['group_gid'] = grp.getgrnam(slapd['group']).gr_gid
        # check this group exists
        # Check that we are running as this user / group, or that we are root.
        assert(os.geteuid() == 0 or getpass.getuser() == slapd['user'])

        if self.verbose:
            log.info("PASSED: user / group checking")

        assert(general['full_machine_name'] is not None)
        assert(general['strict_host_checking'] is not None)
        if general['strict_host_checking'] is True:
            # Check it resolves with dns
            assert(socket.gethostbyname(general['full_machine_name']))
            if self.verbose:
                log.info("PASSED: Hostname strict checking")

        assert(slapd['prefix'] is not None)
        assert(os.path.exists(slapd['prefix']))
        if self.verbose:
            log.info("PASSED: prefix checking")

        # We need to know the prefix before we can do the instance checks
        assert(slapd['instance_name'] is not None)
        # Check if the instance exists or not.
        # Should I move this import? I think this prevents some recursion
        from lib389 import DirSrv
        ds = DirSrv(verbose=self.verbose)
        ds.prefix = slapd['prefix']
        insts = ds.list(serverid=slapd['instance_name'])
        assert(len(insts) == 0)

        if self.verbose:
            log.info("PASSED: instance checking")

        assert(slapd['root_dn'] is not None)
        # Assert this is a valid DN
        assert(is_a_dn(slapd['root_dn']))
        assert(slapd['root_password'] is not None)
        # Check if pre-hashed or not.
        # !!!!!!!!!!!!!!

        # Right now, the way that rootpw works on ns-slapd works, it force hashes the pw
        # see https://fedorahosted.org/389/ticket/48859
        if not re.match('^\{[A-Z0-9]+\}.*$', slapd['root_password']):
            # We need to hash it. Call pwdhash-bin.
            # slapd['root_password'] = password_hash(slapd['root_password'], prefix=slapd['prefix'])
            pass
        else:
            pass

        # Create a random string
        # Hash it.
        # This will be our temporary rootdn password so that we can do
        # live mods and setup rather than static ldif manipulations.
        self._raw_secure_password = password_generate()
        self._secure_password = password_hash(self._raw_secure_password, prefix=slapd['prefix'])

        if self.verbose:
            log.info("PASSED: root user checking")

        assert(slapd['defaults'] is not None)
        if self.verbose:
            log.info("PASSED: using config settings %s" % slapd['defaults'])

        assert(slapd['port'] is not None)
        assert(socket_check_open('::1', slapd['port']) is False)
        assert(slapd['secure_port'] is not None)
        assert(socket_check_open('::1', slapd['secure_port']) is False)
        if self.verbose:
            log.info("PASSED: network avaliability checking")

        # Make assertions of the paths?

        # Make assertions of the backends?

    def create_from_args(self, general, slapd, backends=[], extra=None):
        """
        Actually does the setup. this is what you want to call as an api.
        """
        # Check we have privs to run

        if self.verbose:
            log.info("READY: preparing installation")
        self._prepare_ds(general, slapd, backends)
        # Call our child api to prepare itself.
        self._prepare(extra)

        if self.verbose:
            log.info("READY: beginning installation")

        if self.dryrun:
            log.info("NOOP: dry run requested")
        else:
            # Actually trigger the installation.
            self._install_ds(general, slapd, backends)
            # Call the child api to do anything it needs.
            self._install(extra)
        if self.verbose:
            log.info("Directory Server is brought to you by the letter R and the number 27.")
        log.info("FINISH: completed installation")

    def _install_ds(self, general, slapd, backends):
        """
        Actually install the Ds from the dicts provided.

        You should never call this directly, as it bypasses assertions.
        """
        # register the instance to /etc/sysconfig
        # We do this first so that we can trick remove-ds.pl if needed.
        # There may be a way to create this from template like the dse.ldif ...
        initconfig = ""
        with open("%s/dirsrv/config/template-initconfig" % slapd['sysconf_dir']) as template_init:
            for line in template_init.readlines():
                initconfig += line.replace('{{', '{', 1).replace('}}', '}', 1).replace('-', '_')
        with open("%s/sysconfig/dirsrv-%s" % (slapd['sysconf_dir'], slapd['instance_name']), 'w') as f:
            f.write(initconfig.format(
                SERVER_DIR=slapd['lib_dir'],
                SERVERBIN_DIR=slapd['sbin_dir'],
                CONFIG_DIR=slapd['config_dir'],
                INST_DIR=slapd['inst_dir'],
                RUN_DIR=slapd['run_dir'],
                DS_ROOT='',
                PRODUCT_NAME='slapd',
            ))

        # Create all the needed paths
        # we should only need to make bak_dir, cert_dir, config_dir, db_dir, ldif_dir, lock_dir, log_dir, run_dir? schema_dir,
        for path in ('backup_dir', 'cert_dir', 'config_dir', 'db_dir', 'ldif_dir', 'lock_dir', 'log_dir', 'run_dir'):
            if self.verbose:
                log.info("ACTION: creating %s" % slapd[path])
            os.makedirs(slapd[path], mode=0o770, exist_ok=True)
            os.chown(slapd[path], slapd['user_uid'], slapd['group_gid'])

        # Copy correct data to the paths.
        # Copy in the schema
        #  This is a little fragile, make it better.
        shutil.copytree("%s/dirsrv/schema" % slapd['sysconf_dir'], slapd['schema_dir'])
        os.chown(slapd['schema_dir'], slapd['user_uid'], slapd['group_gid'])

        # Selinux fixups?
        # Restorecon of paths?
        # Bind sockets to our type?

        # Create certdb in sysconfidir
        if self.verbose:
            log.info("ACTION: Creating certificate database is %s" % slapd['cert_dir'])
        # nss_create_new_database(slapd['cert_dir'])

        # Create dse.ldif with a temporary root password.
        # The template is in slapd['data_dir']/dirsrv/data/template-dse.ldif
        # Variables are done with %KEY%.
        # You could cheat and read it in, do a replace of % to { and } then use format?
        if self.verbose:
            log.info("ACTION: Creating dse.ldif")
        dse = ""
        with open("%s/dirsrv/data/template-dse.ldif" % slapd['data_dir']) as template_dse:
            for line in template_dse.readlines():
                dse += line.replace('%', '{', 1).replace('%', '}', 1)

        with open("%s/dse.ldif" % slapd['config_dir'], 'w') as file_dse:
            file_dse.write(dse.format(
                schema_dir=slapd['schema_dir'],
                lock_dir=slapd['lock_dir'],
                tmp_dir=slapd['tmp_dir'],
                cert_dir=slapd['cert_dir'],
                ldif_dir=slapd['ldif_dir'],
                bak_dir=slapd['backup_dir'],
                run_dir=slapd['run_dir'],
                inst_dir="",
                log_dir=slapd['log_dir'],
                fqdn=general['full_machine_name'],
                ds_port=slapd['port'],
                ds_user=slapd['user'],
                rootdn=slapd['root_dn'],
                # ds_passwd=slapd['root_password'],
                ds_passwd=self._secure_password,  # We set our own password here, so we can connect and mod.
                ds_suffix='',
                config_dir=slapd['config_dir'],
                db_dir=slapd['db_dir'],
            ))

        # open the connection to the instance.

        # Should I move this import? I think this prevents some recursion
        from lib389 import DirSrv
        ds_instance = DirSrv(self.verbose)
        args = {
            SER_PORT: slapd['port'],
            SER_SERVERID_PROP: slapd['instance_name'],
            SER_ROOT_DN: slapd['root_dn'],
            SER_ROOT_PW: self._raw_secure_password,
            SER_DEPLOYED_DIR: slapd['prefix']
        }

        ds_instance.allocate(args)
        # Does this work?
        assert(ds_instance.exists())
        # Start the server
        ds_instance.start(timeout=60)
        ds_instance.open()

        # Create the backends as listed
        # Load example data if needed.
        for backend in backends:
            ds_instance.backends.create(properties=backend)

        # Make changes using the temp root
        # Change the root password finally

        # Complete.
        ds_instance.config.set('nsslapd-rootpw',
                               ensure_str(slapd['root_password']))

    def _remove_ds(self):
        """
        The opposite of install: Removes an instance from the system.
        This takes a backup of all relevant data, and removes the paths.
        """
        # This probably actually would need to be able to read the ldif, to
        # know what to remove ...
        for path in ('backup_dir', 'cert_dir', 'config_dir', 'db_dir',
                     'ldif_dir', 'lock_dir', 'log_dir', 'run_dir'):
            print(path)
