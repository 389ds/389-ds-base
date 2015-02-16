"""Tools for creating and managing servers

    uses DirSrv
"""
__all__ = ['DirSrvTools']
try:
    from subprocess import Popen, PIPE, STDOUT
    HASPOPEN = True
except ImportError:
    import popen2
    HASPOPEN = False

import sys
import os
import os.path
import base64
import urllib
import urllib2
import ldap
import operator
import select
import time
import shutil
import subprocess
import tarfile
import re
import glob
import pwd
import grp

import lib389
from lib389 import *
from lib389.properties import *

from lib389.utils import (
    getcfgdsuserdn,
    getcfgdsinfo,
    getcfgdsuserdn,
    update_newhost_with_fqdn,
    get_sbin_dir, get_server_user, getdomainname,
    isLocalHost, formatInfData, getserverroot,
    update_admin_domain, getadminport, getdefaultsuffix,
    )
from lib389._ldifconn import LDIFConn
from lib389._constants import DN_DM

import logging
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


class DirSrvTools(object):
    """DirSrv mix-in."""

    @staticmethod
    def cgiFake(sroot, verbose, prog, args):
        """Run the local program prog as a CGI using the POST method."""
        content = urllib.urlencode(args)
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
            print "%s returned NMC code %s and OS code %s" % (
                prog, exitCode, osCode)
        return exitCode

    @staticmethod
    def cgiPost(host, port, username, password, uri, verbose, secure, args=None):
        """Post the request to the admin server.

           Admin server requires authentication, so we use the auth handler classes.

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
        passman = urllib2.HTTPPasswordMgrWithDefaultRealm()
        # add our password
        passman.add_password(None, hostport, username, password)
        # create the auth handler
        authhandler = urllib2.HTTPBasicAuthHandler(passman)
        # create our url opener that handles basic auth
        opener = urllib2.build_opener(authhandler)
        # make admin server think we are the console
        opener.addheaders = [('User-Agent', 'Fedora-Console/1.0')]
        if verbose:
            print "requesting url", url
            sys.stdout.flush()
        exitCode = 1
        try:
            req = opener.open(url, urllib.urlencode(args))
            for line in req:
                if verbose:
                    print line
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
            # sys/priv config file (e.g <prefix>/etc/sysconfig/dirsrv-<serverid or $HOME/.dirsrv/dirsrv-<serverid>
            props = self.list()
            if len(props) == 0:
                # the instance has not yet been created, just return
                return
            else:
                assert len(props) == 1
                self.sroot = props[0][CONF_SERVER_DIR]

        instanceDir = os.path.join(self.sroot, "slapd-" + self.inst)

        if hasattr(self, 'errlog'):
            errLog = self.errlog
        else:
            errLog = os.path.join(self.prefix or "/", "var/log/dirsrv/slapd-%s/errors" % self.serverid)
        done = False
        started = True
        lastLine = ""
        cmd = cmd.lower()
        fullCmd = instanceDir + "/" + cmd + "-slapd"
        if cmd == 'start':
            cmdPat = 'slapd started.'
        else:
            cmdPat = 'slapd stopped.'

        if "USE_GDB" in os.environ or "USE_VALGRIND" in os.environ:
            timeout = timeout * 3
        timeout += int(time.time())
        if cmd == 'stop':
            log.warn("unbinding before stop")
            try:
                self.unbind()
            except:
                log.warn("Unbinding fails: Instance already down (stopped or killed) ?")
                pass

        log.info("Setup error log")
        logfp = open(errLog, 'r')
        logfp.seek(0, os.SEEK_END)  # seek to end
        pos = logfp.tell()  # get current position
        logfp.seek(pos, os.SEEK_SET)  # reset the EOF flag

        log.warn("Running command: %r" % fullCmd)
        rc = os.system(fullCmd)
        while not done and int(time.time()) < timeout:
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
                    rc = os.system(fullCmd)
                    pos = logfp.tell()
                    break
                elif line.find("exiting.") >= 0:
                    # possible transient condition - try again
                    rc = os.system(fullCmd)
                    pos = logfp.tell()
                    break
                pos = logfp.tell()
                line = logfp.readline()
            if line.find("PR_Bind") >= 0:
                # server port conflicts with another one, just report and punt
                log.debug("last line: %r" % lastLine.strip())
                log.warn("This server cannot be started until the other server on this port is shutdown")
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
                                      "/slapd-%s/Tasks/Operation/stop" % self.inst,
                                      verbose, cgiargs)
            log.info("stopped remote server %s rc = %d" % (self, rc))
            return rc
        else:
            return DirSrvTools.serverCmd(self, 'stop', verbose, timeout)

    @staticmethod
    def start(self, verbose=False, timeout=120):
        if not self.isLocal and hasattr(self, 'asport'):
            log.debug("starting remote server %s " % self)
            cgiargs = {}
            rc = DirSrvTools.cgiPost(self.host, self.asport, self.cfgdsuser,
                                      self.cfgdspwd,
                                      "/slapd-%s/Tasks/Operation/start" % self.inst,
                                      verbose, cgiargs)
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
            Return the information to retrieve the backup file of a given instance
            It returns:
                - Directory name containing the backup (e.g. /tmp/slapd-standalone.bck)
                - The pattern of the backup files (e.g. /tmp/slapd-standalone.bck/backup*.tar.gz)
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
                    log.info("clearInstanceBackupFS: fail to remove %s" % backup_file)
                    pass
        elif dirsrv:
            backup_dir, backup_pattern = DirSrvTools._infoInstanceBackupFS(dirsrv)
            list_backup_files = glob.glob(backup_pattern)
            for f in list_backup_files:
                try:
                    os.remove(f)
                except:
                    log.info("clearInstanceBackupFS: fail to remove %s" % backup_file)
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
            Saves the files of an instance under /tmp/slapd-<instance_name>.bck/backup_HHMMSS.tar.gz
            and return the archive file name.
            If it already exists a such file, it assums it is a valid backup and
            returns its name

            dirsrv.sroot : root of the instance  (e.g. /usr/lib64/dirsrv)
            dirsrv.inst  : instance name (e.g. standalone for /etc/dirsrv/slapd-standalone)
            dirsrv.confdir : root of the instance config (e.g. /etc/dirsrv)
            dirsrv.dbdir: directory where is stored the database (e.g. /var/lib/dirsrv/slapd-standalone/db)
            dirsrv.changelogdir: directory where is stored the changelog (e.g. /var/lib/dirsrv/slapd-master/changelogdb)
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
        if hasattr(dirsrv, 'accesslog') and os.path.dirname(dirsrv.accesslog) not in ldir:
            ldir.append(os.path.dirname(dirsrv.accesslog))

        # now scan the directory list to find the files to backup
        for dirToBackup in ldir:
            for root, dirs, files in os.walk(dirToBackup):
                for file in files:
                    name = os.path.join(root, file)
                    name = re.sub(prefix_pattern, '', name)

                    if os.path.isfile(name):
                        listFilesToBackup.append(name)
                        log.debug("instanceBackupFS add = %s (%s)" % (name, dirsrv.prefix))

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
            log.warning("Unable to restore the instance (%s is not a file)" % backup_file)
            return 1

        #
        # Second do some clean up
        #

        # previous db (it may exists new db files not in the backup)
        log.debug("instanceRestoreFS: remove subtree %s/*" % dirsrv.dbdir)
        for root, dirs, files in os.walk(dirsrv.dbdir):
            for d in dirs:
                if d not in ("bak", "ldif"):
                    log.debug("instanceRestoreFS: before restore remove directory %s/%s" % (root, d))
                    shutil.rmtree("%s/%s" % (root, d))

        # previous error/access logs
        log.debug("instanceRestoreFS: remove error logs %s" % dirsrv.errlog)
        for f in glob.glob("%s*" % dirsrv.errlog):
                log.debug("instanceRestoreFS: before restore remove file %s" % (f))
                os.remove(f)
        log.debug("instanceRestoreFS: remove access logs %s" % dirsrv.accesslog)
        for f in glob.glob("%s*" % dirsrv.accesslog):
                log.debug("instanceRestoreFS: before restore remove file %s" % (f))
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
                    log.debug("instanceRestoreFS: not restored %s (no write access)" % member.name)
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
                log.warning("instanceRestoreFS: fail to remove %s" % guardian_file)
                pass

        os.chdir(here)

    @staticmethod
    def setupSSL(dirsrv, secport=636, sourcedir=None, secargs=None):
        """configure and setup SSL with a given certificate and restart the server.

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

                subprocess.check_call(["semanage", "port", "-a", "-t", "ldap_port_t", "-p", "tcp", str(secport)])
            except OSError:
                log.debug("Likely SELinux not supported")
                pass
            except subprocess.CalledProcessError:
                log.debug("SELinux fails to configure")
                pass

        # eventually copy security files from source dir to our cert dir
        if sourcedir:
            for ff in ['cert8.db', 'key3.db', 'secmod.db', 'pin.txt', 'certmap.conf']:
                srcf = os.path.join(sourcedir, ff)
                destf = os.path.join(certdir, ff)
                # make sure dest is writable so we can copy over it
                try:
                    log.info("Copying security files: %s to %s" % (srcf, destf))
                    mode = os.stat(destf).st_mode
                    newmode = mode | 0600
                    os.chmod(destf, newmode)
                except Exception, e:
                    print e
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
        child_stdin.write(content)
        child_stdin.close()
        while not pipe.poll():
            (rr, wr, xr) = select.select([child_stdout], [], [], 1.0)
            if rr and len(rr) > 0:
                line = rr[0].readline()
                if not line:
                    break
                if verbose:
                    sys.stdout.write(line)
            elif verbose:
                print "timed out waiting to read from", cmd
        child_stdout.close()
        exitCode = pipe.wait()
        if verbose:
            print "%s returned exit code %s" % (prog, exitCode)
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
            If it exists it returns a DirSrv instance NOT initialized, else None
        '''
        instance = DirSrvTools._offlineDirsrv(args)
        dirname  = os.path.join(instance.prefix, "etc/dirsrv/slapd-%s" % instance.serverid)
        errorlog = os.path.join(instance.prefix, "var/log/dirsrv/slapd-%s/errors" % instance.serverid)
        sroot    = os.path.join(instance.prefix, "lib/dirsrv")
        if  os.path.isdir(dirname) and \
            os.path.isfile(errorlog) and \
            os.path.isdir(sroot):
            instance.sroot = sroot
            instance.errlog = errorlog
            return instance

        return None

    @staticmethod
    def createInstance(args, verbose=0):
        """Create a new instance of directory server and return a connection to it.

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
        # next, get the admin server port and any other information - close the cfgconn
        if args['have_admin'] and not args['setup_admin']:
            asport, secure = getadminport(cfgconn, cfgdn, args)
        # next, get the posix username
        get_server_user(args)
        # fixup and verify other args
        args['newport'] = args.get('newport', 389)
        args['newrootdn'] = args.get('newrootdn', DN_DM)
        args['newsuffix'] = args.get('newsuffix', getdefaultsuffix(args['newhost']))

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
        for param in ('newhost', 'newport', 'newrootdn', 'newrootpw', 'newinstance', 'newsuffix'):
            if param not in args:
                log.error("missing required argument: ", param)
                missing = True
        if missing:
            raise InvalidArgumentError("missing required arguments")

        # try to connect with the given parameters
        try:
            newconn = lib389.DirSrv(args['newhost'], args['newport'],
                              args['newrootdn'], args['newrootpw'], args['newinstance'])
            newconn.prefix = prefix
            newconn.backupdir = backupdir
            newconn.isLocal = isLocal
            if args['have_admin'] and not args['setup_admin']:
                newconn.asport = asport
                newconn.cfgdsuser = args['cfgdsuser']
                newconn.cfgdspwd = args['cfgdspwd']
            print "Warning: server at %s:%s already exists, returning connection to it" % \
                  (args['newhost'], args['newport'])
            return newconn
        except ldap.SERVER_DOWN:
            pass  # not running - create new one

        if not isLocal or 'cfgdshost' in args:
            for param in ('cfgdshost', 'cfgdsport', 'cfgdsuser', 'cfgdspwd', 'admin_domain'):
                if param not in args:
                    print "missing required argument", param
                    missing = True
        if not isLocal and not asport:
            print "missing required argument admin server port"
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
                                 args['cfgdspwd'], "/slapd/Tasks/Operation/Create", verbose,
                                 secure, cgiargs)
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
                          args['newrootdn'], args['newrootpw'], args['newinstance'])
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
        # pass this sub two dicts - the first one is a dict suitable to create
        # a new instance - see createInstance for more details
        # the second is a dict suitable for replicaSetupAll - see replicaSetupAll
        conn = DirSrvTools.createInstance(createArgs)
        if not conn:
            print "Error: could not create server", createArgs
            return 0

        conn.replicaSetupAll(repArgs)
        return conn

    @staticmethod
    def makeGroup(group=DEFAULT_USER):
        try:
            grp.getgrnam(group)
            print "OK group %s exists" % group
        except KeyError:
            print "Adding group %s" % group
            cmd = [GROUPADD, '-r', group]
            subprocess.Popen(cmd)

    @staticmethod
    def makeUser(user=DEFAULT_USER, group=DEFAULT_USER, home=DEFAULT_USERHOME):
        try:
            pwd.getpwnam(user)
            print "OK user %s exists" % user
        except KeyError:
            print "Adding user %s" % user
            cmd = [USERADD, '-g', group,
                   '-c', "lib389 DS user",
                    '-r',
                    '-d', home,
                    '-s', NOLOGIN,
                    user]
            subprocess.Popen(cmd)

    @staticmethod
    def lib389User(user=DEFAULT_USER):
        DirSrvTools.makeGroup(group=user)
        DirSrvTools.makeUser(user=user, group=user, home=DEFAULT_USERHOME)

    @staticmethod
    def testLocalhost():
        '''
        Checks that the 127.0.0.1 is resolved as localhost.localdomain
        This is required by DSUtil.pm:checkHostname else setup-ds.pl fails
        '''
        hostFile = '/etc/hosts'
        loopbackIpPattern = '127.0.0.1'
        expectedHost = 'localhost.localdomain'

        hostfp = open(hostFile, 'r')
        hostfp.seek(0, os.SEEK_CUR)

        done = False
        try:
            while not done:
                line = hostfp.readline()
                if line.find(loopbackIpPattern) >= 0:
                    words = line.split()
                    assert(words[1] == expectedHost)
                    done = True
        except AssertionError:
            raise AssertionError("Error: /etc/hosts should contains 'localhost.localdomain' as first host for %s" %
                                 (expectedHost, loopbackIpPattern))

    @staticmethod
    def runUpgrade(prefix, online=True):
        '''
        Run "setup-ds.pl --update"  We simply pass in one DirSrv isntance, and
        this will update all the instances that are in this prefix.  For the update
        to work we must fix/adjust the permissions of the scripts in:

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
        The setup script calls things like /lib/dirsrv/slapd-instance/db2bak, etc,
        and when we run the setup perl script it gets permission denied as the default
        permissions are 750.  Adjust the permissions to 755.
        '''
        for instance in servers:
            for files in os.listdir(instance):
                os.chmod(instance + '/' + files, 755)

        # Run the "upgrade"
        try:
            process = subprocess.Popen([prefix + '/sbin/setup-ds.pl', '--update'], shell=False, stdin=subprocess.PIPE)
            # Answer the interactive questions, as "--update" currently does not work with INF files
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
        # Open the file and read it line by line
        found = False
        try:
            myfile = open(filename)
            for line in myfile:
                if re.search(pattern, line):
                    found = True
            myfile.close()
        except IOError as e:
            log.error('Problem opening/searching file (%s): I/O error(%d): %s' % (filename, e.errno, e.strerror))

        return found


class MockDirSrv(object):
    host = 'localhost'
    port = 22389
    sslport = 0

    def __init__(self, dict_=None):
        if dict_:
            self.host = dict_['host']
            self.port = dict_['port']
            if 'sslport' in dict_:
                self.sslport = dict_['sslport']

    def __str__(self):
        if self.sslport:
            return 'ldaps://%s:%s' % (self.host, self.sslport)
        else:
            return 'ldap://%s:%s' % (self.host, self.port)


