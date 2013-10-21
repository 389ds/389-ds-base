"""The dsadmin module.


    IMPORTANT: Ternary operator syntax is unsupported on RHEL5
        x if cond else y #don't!

    The DSAdmin functionalities are split in various classes
        defined in brookers.py

    TODO: reorganize method parameters according to SimpleLDAPObject
        naming: filterstr, attrlist
"""
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
import socket
import ldif
import re
import ldap
import cStringIO
import time
import operator
import shutil
import datetime
import select
import logging

from ldap.ldapobject import SimpleLDAPObject
from ldapurl import LDAPUrl
from ldap.cidict import cidict
from ldap import LDAPError
# file in this package

from lib389._constants import *
from lib389._entry import Entry
from lib389._replication import CSN, RUV
from lib389._ldifconn import LDIFConn
from lib389.utils import (
    isLocalHost, 
    is_a_dn, 
    normalizeDN, 
    suffixfilt, 
    escapeDNValue
    )

# mixin
#from lib389.tools import DSAdminTools

RE_DBMONATTR = re.compile(r'^([a-zA-Z]+)-([1-9][0-9]*)$')
RE_DBMONATTRSUN = re.compile(r'^([a-zA-Z]+)-([a-zA-Z]+)$')



# My logger
log = logging.getLogger(__name__)


class Error(Exception):
    pass


class InvalidArgumentError(Error):
    pass

class AlreadyExists(ldap.ALREADY_EXISTS):
    pass

class NoSuchEntryError(ldap.NO_SUCH_OBJECT):
    pass


class MissingEntryError(NoSuchEntryError):
    """When just added entries are missing."""
    pass


class  DsError(Error):
    """Generic DS Error."""
    pass




def wrapper(f, name):
    """Wrapper of all superclass methods using lib389.Entry.
        @param f - DSAdmin method inherited from SimpleLDAPObject
        @param name - method to call
        
    This seems to need to be an unbound method, that's why it's outside of DSAdmin.  Perhaps there
    is some way to do this with the new classmethod or staticmethod of 2.4.
    
    We replace every call to a method in SimpleLDAPObject (the superclass
    of DSAdmin) with a call to inner.  The f argument to wrapper is the bound method
    of DSAdmin (which is inherited from the superclass).  Bound means that it will implicitly
    be called with the self argument, it is not in the args list.  name is the name of
    the method to call.  If name is a method that returns entry objects (e.g. result),
    we wrap the data returned by an Entry class.  If name is a method that takes an entry
    argument, we extract the raw data from the entry object to pass in."""
    def inner(*args, **kargs):
        if name == 'result':
            objtype, data = f(*args, **kargs)
            # data is either a 2-tuple or a list of 2-tuples
            # print data
            if data:
                if isinstance(data, tuple):
                    return objtype, Entry(data)
                elif isinstance(data, list):
                    # AD sends back these search references
#                     if objtype == ldap.RES_SEARCH_RESULT and \
#                        isinstance(data[-1],tuple) and \
#                        not data[-1][0]:
#                         print "Received search reference: "
#                         pprint.pprint(data[-1][1])
#                         data.pop() # remove the last non-entry element

                    return objtype, [Entry(x) for x in data]
                else:
                    raise TypeError("unknown data type %s returned by result" %
                                    type(data))
            else:
                return objtype, data
        elif name.startswith('add'):
            # the first arg is self
            # the second and third arg are the dn and the data to send
            # We need to convert the Entry into the format used by
            # python-ldap
            ent = args[0]
            if isinstance(ent, Entry):
                return f(ent.dn, ent.toTupleList(), *args[2:])
            else:
                return f(*args, **kargs)
        else:
            return f(*args, **kargs)
    return inner



class DSAdmin(SimpleLDAPObject):

    def getDseAttr(self, attrname):
        """Return a given attribute from dse.ldif.
            TODO can we take it from "cn=config" ?
        """
        conffile = self.confdir + '/dse.ldif'
        try:
            dse_ldif = LDIFConn(conffile)
            cnconfig = dse_ldif.get(DN_CONFIG)
            if cnconfig:
                return cnconfig.getValue(attrname)
            return None
        except IOError, err: # except..as.. doedn't work on python 2.4 
            log.error("could not read dse config file")
            raise err

    def __initPart2(self):
        """Initialize the DSAdmin structure filling various fields, like:
            - dbdir
            - errlog
            - confdir

        """
        if self.binddn and len(self.binddn) and not hasattr(self, 'sroot'):
            try:
                # XXX this fields are stale and not continuously updated
                # do they have sense?
                ent = self.getEntry(DN_CONFIG, attrlist=[
                    'nsslapd-instancedir', 
                    'nsslapd-errorlog',
                    'nsslapd-certdir', 
                    'nsslapd-schemadir'])
                self.errlog = ent.getValue('nsslapd-errorlog')
                self.confdir = ent.getValue('nsslapd-certdir')
                
                if self.isLocal:
                    if not self.confdir or not os.access(self.confdir + '/dse.ldif', os.R_OK):
                        self.confdir = ent.getValue('nsslapd-schemadir')
                        if self.confdir:
                            self.confdir = os.path.dirname(self.confdir)
                instdir = ent.getValue('nsslapd-instancedir')
                if not instdir and self.isLocal:
                    # get instance name from errorlog
                    # move re outside
                    self.inst = re.match(
                        r'(.*)[\/]slapd-([^/]+)/errors', self.errlog).group(2)
                    if self.isLocal and self.confdir:
                        instdir = self.getDseAttr('nsslapd-instancedir')
                    else:
                        instdir = re.match(r'(.*/slapd-.*)/logs/errors',
                                           self.errlog).group(1)
                if not instdir:
                    instdir = self.confdir
                if self.verbose:
                    log.debug("instdir=%r" % instdir)
                    log.debug("Entry: %r" % ent)
                match = re.match(r'(.*)[\/]slapd-([^/]+)$', instdir)
                if match:
                    self.sroot, self.inst = match.groups()
                else:
                    self.sroot = self.inst = ''
                ent = self.getEntry('cn=config,' + DN_LDBM,
                    attrlist=['nsslapd-directory'])
                self.dbdir = os.path.dirname(ent.getValue('nsslapd-directory'))
            except (ldap.INSUFFICIENT_ACCESS, ldap.CONNECT_ERROR, NoSuchEntryError):
                log.exception("Skipping exception during initialization")
            except ldap.OPERATIONS_ERROR, e:
                log.exception("Skipping exception: Probably Active Directory")
            except ldap.LDAPError, e:
                log.exception("Error during initialization")
                raise

    def __localinit__(self):
        uri = self.toLDAPURL()

        SimpleLDAPObject.__init__(self, uri)

        # see if binddn is a dn or a uid that we need to lookup
        if self.binddn and not is_a_dn(self.binddn):
            self.simple_bind_s("", "")  # anon
            ent = self.getEntry(CFGSUFFIX, ldap.SCOPE_SUBTREE,
                                "(uid=%s)" % self.binddn,
                                ['uid'])
            if ent:
                self.binddn = ent.dn
            else:
                log.error("Error: could not find %s under %s" % (
                    self.binddn, CFGSUFFIX))
        if not self.nobind:
            needtls = False
            while True:
                try:
                    if needtls:
                        self.start_tls_s()
                    try:
                        self.simple_bind_s(self.binddn, self.bindpw)
                    except ldap.SERVER_DOWN, e:
                        # TODO add server info in exception
                        log.error("Cannot connect to %r" % uri)
                        raise e
                    break
                except ldap.CONFIDENTIALITY_REQUIRED:
                    needtls = True
            self.__initPart2()

    def rebind(self):
        """Reconnect to the DS
        
            @raise ldap.CONFIDENTIALITY_REQUIRED - missing TLS: 
        """
        SimpleLDAPObject.__init__(self, self.toLDAPURL())
        #self.start_tls_s()
        self.simple_bind_s(self.binddn, self.bindpw)

    def __add_brookers__(self):
        from lib389.brooker import (
            Replica,
            Backend,
            Config)
        self.replica = Replica(self)
        self.backend = Backend(self)
        self.config = Config(self)
    
    def __init__(self, host='localhost', port=389, binddn='', bindpw='', serverId=None, nobind=False, sslport=0, verbose=False):  # default to anon bind
        """We just set our instance variables and wrap the methods.
            The real work is done in the following methods, reused during
            instance creation & co.
                * __localinit__
                * __initPart2

            e.g. when using the start command, we just need to reconnect,
             not create a new instance"""
        log.info("Initializing %s with %s:%s" % (self.__class__,
                 host, sslport or port))
        self.__wrapmethods()
        self.verbose = verbose
        self.port = port
        self.sslport = sslport
        self.host = host
        self.binddn = binddn
        self.bindpw = bindpw
        self.nobind = nobind
        self.isLocal = isLocalHost(host)
        self.serverId = serverId
        
        #
        # dict caching DS structure
        #
        self.suffixes = {}
        self.agmt = {}
        # the real init
        self.__localinit__()
        self.log = log
        # add brookers
        self.__add_brookers__()

        
    def __str__(self):
        """XXX and in SSL case?"""
        return self.host + ":" + str(self.port)

    def toLDAPURL(self):
        """Return the uri ldap[s]://host:[ssl]port."""
        if self.sslport:
            return "ldaps://%s:%d/" % (self.host, self.sslport)
        else:
            return "ldap://%s:%d/" % (self.host, self.port)
        
    def getServerId(self):
        return self.serverId

    #
    # Get entries
    #
    def getEntry(self, *args, **kwargs):
        """Wrapper around SimpleLDAPObject.search. It is common to just get one entry.
            @param  - entry dn
            @param  - search scope, in ldap.SCOPE_BASE (default), ldap.SCOPE_SUB, ldap.SCOPE_ONE
            @param filterstr - filterstr, default '(objectClass=*)' from SimpleLDAPObject
            @param attrlist - list of attributes to retrieve. eg ['cn', 'uid']
            @oaram attrsonly - default None from SimpleLDAPObject
            eg. getEntry(dn, scope, filter, attributes)

            XXX This cannot return None
        """
        log.debug("Retrieving entry with %r" % [args])
        if len(args) == 1 and 'scope' not in kwargs:
            args += (ldap.SCOPE_BASE, )
            
        res = self.search(*args, **kwargs)
        restype, obj = self.result(res)
        # TODO: why not test restype?
        if not obj:
            raise NoSuchEntryError("no such entry for %r" % [args])

        log.info("Retrieved entry %r" % obj)
        if isinstance(obj, Entry):
            return obj
        else:  # assume list/tuple
            if obj[0] is None:
                raise NoSuchEntryError("Entry is None")
            return obj[0]

    def _test_entry(self, dn, scope=ldap.SCOPE_BASE):
        try:
            entry = self.getEntry(dn, scope)
            log.info("Found entry %s" % entry)
            return entry
        except NoSuchEntryError:
            log.exception("Entry %s was added successfully, but I cannot search it" % dn)
            raise MissingEntryError("Entry %s was added successfully, but I cannot search it" % dn)

    def getMTEntry(self, suffix, attrs=None):
        """Given a suffix, return the mapping tree entry for it.  If attrs is
        given, only fetch those attributes, otherwise, get all attributes.
        """
        attrs = attrs or []
        filtr = suffixfilt(suffix)
        try:
            entry = self.getEntry(
                DN_MAPPING_TREE, ldap.SCOPE_ONELEVEL, filtr, attrs)
            return entry
        except NoSuchEntryError:
            raise NoSuchEntryError(
                "Cannot find suffix in mapping tree: %r " % suffix)
        except ldap.FILTER_ERROR, e:
            log.error("Error searching for %r" % filtr)
            raise e

    def __wrapmethods(self):
        """This wraps all methods of SimpleLDAPObject, so that we can intercept
        the methods that deal with entries.  Instead of using a raw list of tuples
        of lists of hashes of arrays as the entry object, we want to wrap entries
        in an Entry class that provides some useful methods"""
        for name in dir(self.__class__.__bases__[0]):
            attr = getattr(self, name)
            if callable(attr):
                setattr(self, name, wrapper(attr, name))

    def startTask(self, entry, verbose=False):
        # start the task
        dn = entry.dn
        self.add_s(entry)

        if verbose:
            self._test_entry(dn, ldap.SCOPE_BASE)

        return True

    def checkTask(self, entry, dowait=False, verbose=False):
        '''check task status - task is complete when the nsTaskExitCode attr is set
        return a 2 tuple (true/false,code) first is false if task is running, true if
        done - if true, second is the exit code - if dowait is True, this function
        will block until the task is complete'''
        attrlist = ['nsTaskLog', 'nsTaskStatus', 'nsTaskExitCode',
                    'nsTaskCurrentItem', 'nsTaskTotalItems']
        done = False
        exitCode = 0
        dn = entry.dn
        while not done:
            entry = self.getEntry(dn, attrlist=attrlist)
            log.debug("task entry %r" % entry)

            if entry.nsTaskExitCode:
                exitCode = int(entry.nsTaskExitCode)
                done = True
            if dowait:
                time.sleep(1)
            else:
                break
        return (done, exitCode)

    def startTaskAndWait(self, entry, verbose=False):
        self.startTask(entry, verbose)
        (done, exitCode) = self.checkTask(entry, True, verbose)
        return exitCode

    def importLDIF(self, ldiffile, suffix, be=None, verbose=False):
        cn = "import" + str(int(time.time()))
        dn = "cn=%s,cn=import,cn=tasks,cn=config" % cn
        entry = Entry(dn)
        entry.setValues('objectclass', 'top', 'extensibleObject')
        entry.setValues('cn', cn)
        entry.setValues('nsFilename', ldiffile)
        if be:
            entry.setValues('nsInstance', be)
        else:
            entry.setValues('nsIncludeSuffix', suffix)

        rc = self.startTaskAndWait(entry, verbose)

        if rc:
            if verbose:
                log.error("Error: import task %s for file %s exited with %d" % (
                    cn, ldiffile, rc))
        else:
            if verbose:
                log.info("Import task %s for file %s completed successfully" % (
                    cn, ldiffile))
        return rc

    def exportLDIF(self, ldiffile, suffix, be=None, forrepl=False, verbose=False):
        cn = "export%d" % time.time()
        dn = "cn=%s,cn=export,cn=tasks,cn=config" % cn
        entry = Entry(dn)
        entry.update({
            'objectclass': ['top', 'extensibleObject'],
            'cn': cn,
            'nsFilename': ldiffile
        })
        if be:
            entry.setValues('nsInstance', be)
        else:
            entry.setValues('nsIncludeSuffix', suffix)
        if forrepl:
            entry.setValues('nsExportReplica', 'true')

        rc = self.startTaskAndWait(entry, verbose)

        if rc:
            if verbose:
                log.error("Error: export task %s for file %s exited with %d" % (
                    cn, ldiffile, rc))
        else:
            if verbose:
                log.info("Export task %s for file %s completed successfully" % (
                    cn, ldiffile))
        return rc

    def createIndex(self, suffix, attr, verbose=False):
        entries_backend = self.getBackendsForSuffix(suffix, ['cn'])
        cn = "index%d" % time.time()
        dn = "cn=%s,cn=index,cn=tasks,cn=config" % cn
        entry = Entry(dn)
        entry.update({
            'objectclass': ['top', 'extensibleObject'],
            'cn': cn,
            'nsIndexAttribute': attr,
            'nsInstance': entries_backend[0].cn
        })
        # assume 1 local backend
        rc = self.startTaskAndWait(entry, verbose)

        if rc:
            log.error("Error: index task %s for file %s exited with %d" % (
                    cn, ldiffile, rc))
        else:
            log.info("Index task %s for file %s completed successfully" % (
                    cn, ldiffile))
        return rc

    def fixupMemberOf(self, suffix, filt=None, verbose=False):
        cn = "fixupmemberof%d" % time.time()
        dn = "cn=%s,cn=memberOf task,cn=tasks,cn=config" % cn
        entry = Entry(dn)
        entry.setValues('objectclass', 'top', 'extensibleObject')
        entry.setValues('cn', cn)
        entry.setValues('basedn', suffix)
        if filt:
            entry.setValues('filter', filt)
        rc = self.startTaskAndWait(entry, verbose)

        if rc:
            if verbose:
                log.error("Error: fixupMemberOf task %s for basedn %s exited with %d" % (cn, suffix, rc))
        else:
            if verbose:
                log.info("fixupMemberOf task %s for basedn %s completed successfully" % (cn, suffix))
        return rc

    def addLDIF(self, input_file, cont=False):
        class LDIFAdder(ldif.LDIFParser):
            def __init__(self, input_file, conn, cont=False,
                         ignored_attr_types=None, max_entries=0, process_url_schemes=None
                         ):
                myfile = input_file
                if isinstance(input_file, basestring):
                    myfile = open(input_file, "r")
                self.conn = conn
                self.cont = cont
                ldif.LDIFParser.__init__(self, myfile, ignored_attr_types,
                                         max_entries, process_url_schemes)
                self.parse()
                if isinstance(input_file, basestring):
                    myfile.close()

            def handle(self, dn, entry):
                if not dn:
                    dn = ''
                newentry = Entry((dn, entry))
                try:
                    self.conn.add_s(newentry)
                except ldap.LDAPError, e:
                    if not self.cont:
                        raise e
                    log.exception("Error: could not add entry %s" % dn)

        adder = LDIFAdder(input_file, self, cont)

    def getSuffixes(self):
        """@return a list of cn suffixes"""
        ents = self.search_s(DN_MAPPING_TREE, ldap.SCOPE_ONELEVEL)
        sufs = []
        for ent in ents:
            unquoted = None
            quoted = None
            for val in ent.getValues('cn'):
                if val.find('"') < 0:  # prefer the one that is not quoted
                    unquoted = val
                else:
                    quoted = val
            if unquoted:  # preferred
                sufs.append(unquoted)
            elif quoted:  # strip
                sufs.append(quoted.strip('"'))
            else:
                raise Exception(
                    "Error: mapping tree entry %r has no suffix" % ent.dn)
        return sufs

    def setupBackend(self, suffix, binddn=None, bindpw=None, urls=None, attrvals=None, benamebase='localdb', verbose=False):
        """Setup a backend and return its dn. Blank on error
        
            NOTE This won't create a suffix nor its related entry in 
                the tree!!!
                
            XXX Deprecated! @see lib389.brooker.Backend.add
            
        """
        return self.backend.add(suffix=suffix, binddn=binddn, bindpw=bindpw,
            urls=urls, attrvals=attrvals, benamebase=benamebase, 
            setupmt=False, parent=None)
            

    def setupSuffix(self, suffix, bename, parent="", verbose=False):
        """Setup a suffix with the given backend-name.

            XXX Deprecated! @see lib389.brooker.Backend.setup_mt

        """
        return self.backend.setup_mt(suffix, bename, parent)
        

    def getBackendsForSuffix(self, suffix, attrs=None):
        # TESTME removed try..except and raise if NoSuchEntryError
        attrs = attrs or []
        nsuffix = normalizeDN(suffix)
        entries = self.search_s("cn=plugins,cn=config", ldap.SCOPE_SUBTREE,
                                "(&(objectclass=nsBackendInstance)(|(nsslapd-suffix=%s)(nsslapd-suffix=%s)))" % (suffix, nsuffix),
                                attrs)
        return entries

    def getSuffixForBackend(self, bename, attrs=None):
        """Return the mapping tree entry of `bename` or None if not found"""
        attrs = attrs or []
        try:
            entry = self.getEntry("cn=plugins,cn=config", ldap.SCOPE_SUBTREE,
                                  "(&(objectclass=nsBackendInstance)(cn=%s))" % bename,
                                  ['nsslapd-suffix'])
            suffix = entry.getValue('nsslapd-suffix')
            return self.getMTEntry(suffix, attrs)
        except NoSuchEntryError:
            log.warning("Could not find an entry for backend %r" % bename)
            return None

    def findParentSuffix(self, suffix):
        """see if the given suffix has a parent suffix"""
        rdns = ldap.explode_dn(suffix)
        del rdns[0]

        while len(rdns) > 0:
            suffix = ','.join(rdns)
            try:
                mapent = self.getMTEntry(suffix)
                return suffix
            except NoSuchEntryError:
                del rdns[0]

        return ""

    def addSuffix(self, suffix, binddn=None, bindpw=None, urls=None, bename=None, beattrs=None):
        """Create and return a suffix and its backend.

            @param  suffix
            @param  urls
            @param  bename  - name of the backed (eventually created)
            @param  beattrs - parametes to create the backend
            @param  binddn
            @param  bindpw
            Uses: setupBackend and SetupSuffix
            Requires: adding a matching entry in the tree
            TODO: test return values and error codes
            
            `beattrs`: see setupBacked
            
        """
        benames = []
        entries_backend = self.getBackendsForSuffix(suffix, ['cn'])
        # no backends for this suffix yet - create one
        if not entries_backend:
            # if not bename, self.setupBackend raises
            bename = self.setupBackend(
                suffix, binddn, bindpw, urls, benamebase=bename, attrvals=beattrs)
        else:  # use existing backend(s)
            benames = [entry.cn for entry in entries_backend]
            bename = benames.pop(0)  # do I need to modify benames

        try:
            parent = self.findParentSuffix(suffix)
            return self.setupSuffix(suffix, bename, parent)
        except NoSuchEntryError:
            log.exception(
                "Couldn't create suffix for %s %s" % (bename, suffix))
            raise

    def getDBStats(self, suffix, bename=''):
        if bename:
            dn = ','.join(("cn=monitor,cn=%s" % bename, DN_LDBM))
        else:
            entries_backend = self.getBackendsForSuffix(suffix)
            dn = "cn=monitor," + entries_backend[0].dn
        dbmondn = "cn=monitor," + DN_LDBM
        dbdbdn = "cn=database,cn=monitor," + DN_LDBM
        try:
            # entrycache and dncache stats
            ent = self.getEntry(dn, ldap.SCOPE_BASE)
            monent = self.getEntry(dbmondn, ldap.SCOPE_BASE)
            dbdbent = self.getEntry(dbdbdn, ldap.SCOPE_BASE)
            ret = "cache   available ratio    count unitsize\n"
            mecs = ent.maxentrycachesize or "0"
            cecs = ent.currententrycachesize or "0"
            rem = int(mecs) - int(cecs)
            ratio = ent.entrycachehitratio or "0"
            ratio = int(ratio)
            count = ent.currententrycachecount or "0"
            count = int(count)
            if count:
                size = int(cecs) / count
            else:
                size = 0
            ret += "entry % 11d   % 3d % 8d % 5d" % (rem, ratio, count, size)
            if ent.maxdncachesize:
                mdcs = ent.maxdncachesize or "0"
                cdcs = ent.currentdncachesize or "0"
                rem = int(mdcs) - int(cdcs)
                dct = ent.dncachetries or "0"
                tries = int(dct)
                if tries:
                    ratio = (100 * int(ent.dncachehits)) / tries
                else:
                    ratio = 0
                count = ent.currentdncachecount or "0"
                count = int(count)
                if count:
                    size = int(cdcs) / count
                else:
                    size = 0
                ret += "\ndn    % 11d   % 3d % 8d % 5d" % (
                    rem, ratio, count, size)

            if ent.hasAttr('entrycache-hashtables'):
                ret += "\n\n" + ent.getValue('entrycache-hashtables')

            # global db stats
            ret += "\n\nglobal db stats"
            dbattrs = 'dbcachehits dbcachetries dbcachehitratio dbcachepagein dbcachepageout dbcacheroevict dbcacherwevict'.split(' ')
            cols = {'dbcachehits': [len('cachehits'), 'cachehits'], 'dbcachetries': [10, 'cachetries'],
                    'dbcachehitratio': [5, 'ratio'], 'dbcachepagein': [6, 'pagein'],
                    'dbcachepageout': [7, 'pageout'], 'dbcacheroevict': [7, 'roevict'],
                    'dbcacherwevict': [7, 'rwevict']}
            dbrec = {}
            for attr, vals in monent.iterAttrs():
                if attr.startswith('dbcache'):
                    val = vals[0]
                    dbrec[attr] = val
                    vallen = len(val)
                    if vallen > cols[attr][0]:
                        cols[attr][0] = vallen
            # construct the format string based on the field widths
            fmtstr = ''
            ret += "\n"
            for attr in dbattrs:
                fmtstr += ' %%(%s)%ds' % (attr, cols[attr][0])
                ret += ' %*s' % tuple(cols[attr])
            ret += "\n" + (fmtstr % dbrec)

            # other db stats
            skips = {'nsslapd-db-cache-hit': 'nsslapd-db-cache-hit', 'nsslapd-db-cache-try': 'nsslapd-db-cache-try',
                     'nsslapd-db-page-write-rate': 'nsslapd-db-page-write-rate',
                     'nsslapd-db-page-read-rate': 'nsslapd-db-page-read-rate',
                     'nsslapd-db-page-ro-evict-rate': 'nsslapd-db-page-ro-evict-rate',
                     'nsslapd-db-page-rw-evict-rate': 'nsslapd-db-page-rw-evict-rate'}

            hline = ''  # header line
            vline = ''  # val line
            for attr, vals in dbdbent.iterAttrs():
                if attr in skips:
                    continue
                if attr.startswith('nsslapd-db-'):
                    short = attr.replace('nsslapd-db-', '')
                    val = vals[0]
                    width = max(len(short), len(val))
                    if len(hline) + width > 70:
                        ret += "\n" + hline + "\n" + vline
                        hline = vline = ''
                    hline += ' %*s' % (width, short)
                    vline += ' %*s' % (width, val)

            # per file db stats
            ret += "\n\nper file stats"
            # key is number
            # data is dict - key is attr name without the number - val is the attr val
            dbrec = {}
            dbattrs = ['dbfilename', 'dbfilecachehit',
                       'dbfilecachemiss', 'dbfilepagein', 'dbfilepageout']
            # cols maps dbattr name to column header and width
            cols = {'dbfilename': [len('dbfilename'), 'dbfilename'], 'dbfilecachehit': [9, 'cachehits'],
                    'dbfilecachemiss': [11, 'cachemisses'], 'dbfilepagein': [6, 'pagein'],
                    'dbfilepageout': [7, 'pageout']}
            for attr, vals in ent.iterAttrs():
                match = RE_DBMONATTR.match(attr)
                if match:
                    name = match.group(1)
                    num = match.group(2)
                    val = vals[0]
                    if name == 'dbfilename':
                        val = val.split('/')[-1]
                    dbrec.setdefault(num, {})[name] = val
                    vallen = len(val)
                    if vallen > cols[name][0]:
                        cols[name][0] = vallen
                match = RE_DBMONATTRSUN.match(attr)
                if match:
                    name = match.group(1)
                    if name == 'entrycache':
                        continue
                    num = match.group(2)
                    val = vals[0]
                    if name == 'dbfilename':
                        val = val.split('/')[-1]
                    dbrec.setdefault(num, {})[name] = val
                    vallen = len(val)
                    if vallen > cols[name][0]:
                        cols[name][0] = vallen
            # construct the format string based on the field widths
            fmtstr = ''
            ret += "\n"
            for attr in dbattrs:
                fmtstr += ' %%(%s)%ds' % (attr, cols[attr][0])
                ret += ' %*s' % tuple(cols[attr])
            for dbf in dbrec.itervalues():
                ret += "\n" + (fmtstr % dbf)
            return ret
        except Exception, e:
            print "caught exception", str(e)
        return ''

    def waitForEntry(self, dn, timeout=7200, attr='', quiet=True):
        scope = ldap.SCOPE_BASE
        filt = "(objectclass=*)"
        attrlist = []
        if attr:
            filt = "(%s=*)" % attr
            attrlist.append(attr)
        timeout += int(time.time())

        if isinstance(dn, Entry):
            dn = dn.dn

        # wait for entry and/or attr to show up
        if not quiet:
            sys.stdout.write("Waiting for %s %s:%s " % (self, dn, attr))
            sys.stdout.flush()
        entry = None
        while not entry and int(time.time()) < timeout:
            try:
                entry = self.getEntry(dn, scope, filt, attrlist)
            except NoSuchEntryError:
                pass  # found entry, but no attr
            except ldap.NO_SUCH_OBJECT:
                pass  # no entry yet
            except ldap.LDAPError, e:  # badness
                print "\nError reading entry", dn, e
                break
            if not entry:
                if not quiet:
                    sys.stdout.write(".")
                    sys.stdout.flush()
                time.sleep(1)

        if not entry and int(time.time()) > timeout:
            print "\nwaitForEntry timeout for %s for %s" % (self, dn)
        elif entry:
            if not quiet:
                print "\nThe waited for entry is:", entry
        else:
            print "\nError: could not read entry %s from %s" % (dn, self)

        return entry

    def addIndex(self, suffix, attr, indexTypes, *matchingRules):
        """Specify the suffix (should contain 1 local database backend),
            the name of the attribute to index, and the types of indexes
            to create e.g. "pres", "eq", "sub"
        """
        entries_backend = self.getBackendsForSuffix(suffix, ['cn'])
        # assume 1 local backend
        dn = "cn=%s,cn=index,%s" % (attr, entries_backend[0].dn)
        entry = Entry(dn)
        entry.setValues('objectclass', 'top', 'nsIndex')
        entry.setValues('cn', attr)
        entry.setValues('nsSystemIndex', "false")
        entry.setValues('nsIndexType', indexTypes)
        if matchingRules:
            entry.setValues('nsMatchingRule', matchingRules)
        try:
            self.add_s(entry)
        except ldap.ALREADY_EXISTS:
            print "Index for attr %s for backend %s already exists" % (
                attr, dn)

    def modIndex(self, suffix, attr, mod):
        """just a wrapper around a plain old ldap modify, but will
        find the correct index entry based on the suffix and attribute"""
        entries_backend = self.getBackendsForSuffix(suffix, ['cn'])
        # assume 1 local backend
        dn = "cn=%s,cn=index,%s" % (attr, entries_backend[0].dn)
        self.modify_s(dn, mod)

    def requireIndex(self, suffix):
        entries_backend = self.getBackendsForSuffix(suffix, ['cn'])
        # assume 1 local backend
        dn = entries_backend[0].dn
        replace = [(ldap.MOD_REPLACE, 'nsslapd-require-index', 'on')]
        self.modify_s(dn, replace)

    def addSchema(self, attr, val):
        dn = "cn=schema"
        self.modify_s(dn, [(ldap.MOD_ADD, attr, val)])

    def addAttr(self, *attributes):
        return self.addSchema('attributeTypes', attributes)

    def addObjClass(self, *objectclasses):
        return self.addSchema('objectClasses', objectclasses)





    def setupChainingIntermediate(self):
        confdn = ','.join(("cn=config", DN_CHAIN))
        try:
            self.modify_s(confdn, [(ldap.MOD_ADD, 'nsTransmittedControl',
                                   ['2.16.840.1.113730.3.4.12', '1.3.6.1.4.1.1466.29539.12'])])
        except ldap.TYPE_OR_VALUE_EXISTS:
            log.error("chaining backend config already has the required controls")

    def setupChainingMux(self, suffix, isIntermediate, binddn, bindpw, urls):
        self.addSuffix(suffix, binddn, bindpw, urls)
        if isIntermediate:
            self.setupChainingIntermediate()

    def setupChainingFarm(self, suffix, binddn, bindpw):
        # step 1 - create the bind dn to use as the proxy
        self.setupBindDN(binddn, bindpw)
        self.addSuffix(suffix)  # step 2 - create the suffix
        # step 3 - add the proxy ACI to the suffix
        try:
            acival = "(targetattr = \"*\")(version 3.0; acl \"Proxied authorization for database links\"" + \
                "; allow (proxy) userdn = \"ldap:///%s\";)" % binddn
            self.modify_s(suffix, [(ldap.MOD_ADD, 'aci', [acival])])
        except ldap.TYPE_OR_VALUE_EXISTS:
            log.error("proxy aci already exists in suffix %s for %s" % (
                suffix, binddn))

    def setupChaining(self, to, suffix, isIntermediate):
        """Setup chaining from self to to - self is the mux, to is the farm
        if isIntermediate is set, this server will chain requests from another server to to
        """
        bindcn = "chaining user"
        binddn = "cn=%s,cn=config" % bindcn
        bindpw = "chaining"

        to.setupChainingFarm(suffix, binddn, bindpw)
        self.setupChainingMux(
            suffix, isIntermediate, binddn, bindpw, to.toLDAPURL())


    def enableChainOnUpdate(self, suffix, bename):
        # first, get the mapping tree entry to modify
        mtent = self.getMTEntry(suffix, ['cn'])
        dn = mtent.dn

        # next, get the path of the replication plugin
        e_plugin = self.getEntry(
            "cn=Multimaster Replication Plugin,cn=plugins,cn=config",
            attrlist=['nsslapd-pluginPath'])
        path = e_plugin.getValue('nsslapd-pluginPath')

        mod = [(ldap.MOD_REPLACE, 'nsslapd-state', 'backend'),
               (ldap.MOD_ADD, 'nsslapd-backend', bename),
               (ldap.MOD_ADD, 'nsslapd-distribution-plugin', path),
               (ldap.MOD_ADD, 'nsslapd-distribution-funct', 'repl_chain_on_update')]

        try:
            self.modify_s(dn, mod)
        except ldap.TYPE_OR_VALUE_EXISTS:
            print "chainOnUpdate already enabled for %s" % suffix

    def setupConsumerChainOnUpdate(self, suffix, isIntermediate, binddn, bindpw, urls, beargs=None):
        beargs = beargs or {}
        # suffix should already exist
        # we need to create a chaining backend
        if not 'nsCheckLocalACI' in beargs:
            beargs['nsCheckLocalACI'] = 'on'  # enable local db aci eval.
        chainbe = self.setupBackend(suffix, binddn, bindpw, urls, beargs)
        # do the stuff for intermediate chains
        if isIntermediate:
            self.setupChainingIntermediate()
        # enable the chain on update
        return self.enableChainOnUpdate(suffix, chainbe)


    def setupBindDN(self, binddn, bindpw, attrs=None):
        """ Return - eventually creating - a person entry with the given dn and pwd.

            binddn can be a lib389.Entry
        """
        try:
            assert binddn
            if isinstance(binddn, Entry):
                assert binddn.dn
                binddn = binddn.dn
        except AssertionError:
            raise AssertionError("Error: entry dn should be set!" % binddn)

        ent = Entry(binddn)
        ent.setValues('objectclass', "top", "person")
        ent.setValues('userpassword', bindpw)
        ent.setValues('sn', "bind dn pseudo user")
        ent.setValues('cn', "bind dn pseudo user")
        
        # support for uid
        attribute, value = binddn.split(",")[0].split("=", 1)
        if attribute == 'uid':
            ent.setValues('objectclass', "top", "person", 'inetOrgPerson')
            ent.setValues('uid', value)

        if attrs:
            ent.update(attrs)
            
        try:
            self.add_s(ent)
        except ldap.ALREADY_EXISTS:
            log.warn("Entry %s already exists" % binddn)

        try:
            entry = self._test_entry(binddn, ldap.SCOPE_BASE)
            return entry
        except MissingEntryError:
            log.exception("This entry should exist!")
            raise

    def setupWinSyncAgmt(self, args, entry):
        if 'winsync' not in args:
            return

        suffix = args['suffix']
        entry.setValues("objectclass", "nsDSWindowsReplicationAgreement")
        entry.setValues("nsds7WindowsReplicaSubtree",
                        args.get("win_subtree",
                                 "cn=users," + suffix))
        entry.setValues("nsds7DirectoryReplicaSubtree",
                        args.get("ds_subtree",
                                 "ou=People," + suffix))
        entry.setValues(
            "nsds7NewWinUserSyncEnabled", args.get('newwinusers', 'true'))
        entry.setValues(
            "nsds7NewWinGroupSyncEnabled", args.get('newwingroups', 'true'))
        windomain = ''
        if 'windomain' in args:
            windomain = args['windomain']
        else:
            windomain = '.'.join(ldap.explode_dn(suffix, 1))
        entry.setValues("nsds7WindowsDomain", windomain)
        if 'interval' in args:
            entry.setValues("winSyncInterval", args['interval'])
        if 'onewaysync' in args:
            if args['onewaysync'].lower() == 'fromwindows' or \
                    args['onewaysync'].lower() == 'towindows':
                entry.setValues("oneWaySync", args['onewaysync'])
            else:
                raise Exception("Error: invalid value %s for oneWaySync: must be fromWindows or toWindows" % args['onewaysync'])

    # args - DSAdmin consumer (repoth), suffix, binddn, bindpw, timeout
    # also need an auto_init argument
    def setupAgreement(self, consumer, args, cn_format=r'meTo_%s:%s', description_format=r'me to %s:%s'):
        """Create (and return) a replication agreement from self to consumer.
            - self is the supplier,
            - consumer is a DSAdmin object (consumer can be a master)
            - cn_format - use this string to format the agreement name

        consumer:
            * a DSAdmin object if chaining
            * an object with attributes: host, port, sslport, __str__

        args =  {
        'suffix': "dc=example,dc=com",
        'bename': "userRoot",
        'binddn': "cn=replrepl,cn=config",
        'bindcn': "replrepl", # so I need it?
        'bindpw': "replrepl",
        'bindmethod': 'simple',
        'log'   : True.
        'timeout': 120
        }

            self.suffixes is of the form {
                'o=suffix1': 'ldaps://consumer.example.com:636',
                'o=suffix2': 'ldap://consumer.example.net:3890'
            }
        """
        assert args.get('binddn') and args.get('bindpw')
        suffix = args['suffix']
        binddn = args.get('binddn')
        bindpw = args.get('bindpw')

        nsuffix = normalizeDN(suffix)
        othhost, othport, othsslport = (
            consumer.host, consumer.port, consumer.sslport)
        othport = othsslport or othport

        # adding agreement to previously created replica
        # eventually setting self.suffixes dict.
        if not nsuffix in self.suffixes:
            replica_entries = self.replica.list(suffix)
            if not replica_entries:
                raise NoSuchEntryError(
                    "Error: no replica set up for suffix " + suffix)
            replent = replica_entries[0]
            self.suffixes[nsuffix] = {
                'dn': replent.dn,
                'type': int(replent.nsds5replicatype)
            }
        # define agreement entry
        cn = cn_format % (othhost, othport)
        dn_agreement = "cn=%s,%s" % (cn, self.suffixes[nsuffix]['dn'])
        try:
            entry = self.getEntry(dn_agreement, ldap.SCOPE_BASE)
        except ldap.NO_SUCH_OBJECT:
            entry = None
        if entry:
            log.warn("Agreement exists:", dn_agreement)
            self.suffixes.setdefault(nsuffix, {})[str(consumer)] = dn_agreement
            return dn_agreement
        if (nsuffix in self.agmt) and (consumer in self.agmt[nsuffix]):
            log.warn("Agreement exists:", dn_agreement)
            return dn_agreement

        # In a separate function in this scope?
        entry = Entry(dn_agreement)
        entry.update({
            'objectclass': ["top", "nsds5replicationagreement"],
            'cn': cn,
            'nsds5replicahost': othhost,
            'nsds5replicatimeout': str(args.get('timeout', 120)),
            'nsds5replicabinddn': binddn,
            'nsds5replicacredentials': bindpw,
            'nsds5replicabindmethod': args.get('bindmethod', 'simple'),
            'nsds5replicaroot': nsuffix,
            'nsds5replicaupdateschedule': '0000-2359 0123456',
            'description': description_format % (othhost, othport)
        })
        if 'starttls' in args:
            entry.setValues('nsds5replicatransportinfo', 'TLS')
            entry.setValues('nsds5replicaport', str(othport))
        elif othsslport:
            entry.setValues('nsds5replicatransportinfo', 'SSL')
            entry.setValues('nsds5replicaport', str(othsslport))
        else:
            entry.setValues('nsds5replicatransportinfo', 'LDAP')
            entry.setValues('nsds5replicaport', str(othport))
        if 'fractional' in args:
            entry.setValues('nsDS5ReplicatedAttributeList', args['fractional'])
        if 'auto_init' in args:
            entry.setValues('nsds5BeginReplicaRefresh', 'start')
        if 'fractional' in args:
            entry.setValues('nsDS5ReplicatedAttributeList', args['fractional'])
        if 'stripattrs' in args:
            entry.setValues('nsds5ReplicaStripAttrs', args['stripattrs'])

        if 'winsync' in args:  # state it clearly!
            self.setupWinSyncAgmt(args, entry)

        try:
            log.debug("Adding replica agreement: [%s]" % entry)
            self.add_s(entry)
        except:
            #  TODO check please!
            raise
        entry = self.waitForEntry(dn_agreement)
        if entry:
            self.suffixes.setdefault(nsuffix, {})[str(consumer)] = dn_agreement
            # More verbose but shows what's going on
            if 'chain' in args:
                chain_args = {
                    'suffix': suffix,
                    'binddn': binddn,
                    'bindpw': bindpw
                }
                # Work on `self` aka producer
                if self.suffixes[nsuffix]['type'] == MASTER_TYPE:
                    self.setupChainingFarm(**chain_args)
                # Work on `consumer`
                # TODO - is it really required?
                if consumer.suffixes[nsuffix]['type'] == LEAF_TYPE:
                    chain_args.update({
                        'isIntermediate': 0,
                        'urls': self.toLDAPURL(),
                        'args': args['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)
                elif consumer.suffixes[nsuffix]['type'] == HUB_TYPE:
                    chain_args.update({
                        'isIntermediate': 1,
                        'urls': self.toLDAPURL(),
                        'args': args['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)
        self.agmt.setdefault(nsuffix, {})[consumer] = dn_agreement
        return dn_agreement




    # moved to Replica
    def setupReplica(self, args):
        """Deprecated, use replica.add
        """
        return self.replica.add(**args)

    def startReplication_async(self, agmtdn):
        return self.replica.start_async(agmtdn)

    def checkReplInit(self, agmtdn):
        return self.replica.check_init(agmtdn)

    def waitForReplInit(self, agmtdn):
        return self.replica.wait_init(agmtdn)

    def startReplication(self, agmtdn):
        return self.replica.start_and_wait(agmtdn)


    def replicaSetupAll(self, repArgs):
        """setup everything needed to enable replication for a given suffix.
            1- eventually create the suffix
            2- enable replication logging
            3- create changelog
            4- create replica user
            repArgs is a dict with the following fields:
                {
                suffix - suffix to set up for replication (eventually create)
                            optional fields and their default values
                bename - name of backend corresponding to suffix, otherwise
                    it will use the *first* backend found (isn't that dangerous?)
                parent - parent suffix if suffix is a sub-suffix - default is undef
                ro - put database in read only mode - default is read write
                type - replica type (MASTER_TYPE, HUB_TYPE, LEAF_TYPE) - default is master
                legacy - make this replica a legacy consumer - default is no

                binddn - bind DN of the replication manager user - default is REPLBINDDN
                bindpw - bind password of the repl manager - default is REPLBINDPW

                log - if true, replication logging is turned on - default false
                id - the replica ID - default is an auto incremented number
                }

            TODO: passing the repArgs as an object or as a **repArgs could be
                a better documentation choiche
                eg. replicaSetupAll(self, suffix, type=MASTER_TYPE, log=False, ...)
        """

        repArgs.setdefault('type', MASTER_TYPE)
        user = repArgs.get('binddn'), repArgs.get('bindpw')

        # eventually create the suffix (Eg. o=userRoot)
        # TODO should I check the addSuffix output as it doesn't raise
        self.addSuffix(repArgs['suffix'])
        if 'bename' not in repArgs:
            entries_backend = self.getBackendsForSuffix(
                repArgs['suffix'], ['cn'])
            # just use first one
            repArgs['bename'] = entries_backend[0].cn
        if repArgs.get('log', False):
            self.enableReplLogging()

        # enable changelog for master and hub
        if repArgs['type'] != LEAF_TYPE:
            self.replica.changelog()
        # create replica user without timeout and expiration issues 
        try:            
            attrs = list(user)
            attrs.append({
                'nsIdleTimeout': '0',
                'passwordExpirationTime': '20381010000000Z'
                })
            self.setupBindDN(*attrs)
        except ldap.ALREADY_EXISTS:
            log.warn("User already exists: %r " % user)

        # setup replica
        repArgs['rtype'], repArgs['rid'] = repArgs['type'], repArgs['id']
        
        # remove invalid arguments from replica.add
        for invalid_arg in 'type id bename'.split():
            del repArgs[invalid_arg]
        if 'log' in repArgs:
            del repArgs['log']
        
        ret = self.replica.add(**repArgs)
        if 'legacy' in repArgs:
            self.setupLegacyConsumer(*user)

        return ret

    def subtreePwdPolicy(self, basedn, pwdpolicy, verbose=False, **pwdargs):
        args = {'basedn': basedn, 'escdn': escapeDNValue(
            normalizeDN(basedn))}
        condn = "cn=nsPwPolicyContainer,%(basedn)s" % args
        poldn = "cn=cn\\=nsPwPolicyEntry\\,%(escdn)s,cn=nsPwPolicyContainer,%(basedn)s" % args
        temdn = "cn=cn\\=nsPwTemplateEntry\\,%(escdn)s,cn=nsPwPolicyContainer,%(basedn)s" % args
        cosdn = "cn=nsPwPolicy_cos,%(basedn)s" % args
        conent = Entry(condn)
        conent.setValues('objectclass', 'nsContainer')
        polent = Entry(poldn)
        polent.setValues('objectclass', ['ldapsubentry', 'passwordpolicy'])
        tement = Entry(temdn)
        tement.setValues('objectclass', ['extensibleObject',
                         'costemplate', 'ldapsubentry'])
        tement.setValues('cosPriority', '1')
        tement.setValues('pwdpolicysubentry', poldn)
        cosent = Entry(cosdn)
        cosent.setValues('objectclass', ['ldapsubentry',
                         'cosSuperDefinition', 'cosPointerDefinition'])
        cosent.setValues('cosTemplateDn', temdn)
        cosent.setValues(
            'cosAttribute', 'pwdpolicysubentry default operational-default')
        for ent in (conent, polent, tement, cosent):
            try:
                self.add_s(ent)
                if verbose:
                    print "created subtree pwpolicy entry", ent.dn
            except ldap.ALREADY_EXISTS:
                print "subtree pwpolicy entry", ent.dn, "already exists - skipping"
        self.setPwdPolicy({'nsslapd-pwpolicy-local': 'on'})
        self.setDNPwdPolicy(poldn, pwdpolicy, **pwdargs)

    def userPwdPolicy(self, user, pwdpolicy, verbose=False, **pwdargs):
        ary = ldap.explode_dn(user)
        par = ','.join(ary[1:])
        escuser = escapeDNValue(normalizeDN(user))
        args = {'par': par, 'udn': user, 'escudn': escuser}
        condn = "cn=nsPwPolicyContainer,%(par)s" % args
        poldn = "cn=cn\\=nsPwPolicyEntry\\,%(escudn)s,cn=nsPwPolicyContainer,%(par)s" % args
        conent = Entry(condn)
        conent.setValues('objectclass', 'nsContainer')
        polent = Entry(poldn)
        polent.setValues('objectclass', ['ldapsubentry', 'passwordpolicy'])
        for ent in (conent, polent):
            try:
                self.add_s(ent)
                if verbose:
                    print "created user pwpolicy entry", ent.dn
            except ldap.ALREADY_EXISTS:
                print "user pwpolicy entry", ent.dn, "already exists - skipping"
        mod = [(ldap.MOD_REPLACE, 'pwdpolicysubentry', poldn)]
        self.modify_s(user, mod)
        self.setPwdPolicy({'nsslapd-pwpolicy-local': 'on'})
        self.setDNPwdPolicy(poldn, pwdpolicy, **pwdargs)

    def setPwdPolicy(self, pwdpolicy, **pwdargs):
        self.setDNPwdPolicy(DN_CONFIG, pwdpolicy, **pwdargs)

    def setDNPwdPolicy(self, dn, pwdpolicy, **pwdargs):
        """input is dict of attr/vals"""
        mods = []
        for (attr, val) in pwdpolicy.iteritems():
            mods.append((ldap.MOD_REPLACE, attr, str(val)))
        if pwdargs:
            for (attr, val) in pwdargs.iteritems():
                mods.append((ldap.MOD_REPLACE, attr, str(val)))
        self.modify_s(dn, mods)

    
    # Moved to config
    # replaced by loglevel
    def enableReplLogging(self):
        """Enable logging of replication stuff (1<<13)"""
        val = LOG_REPLICA
        return self.config.loglevel([val])

    def disableReplLogging(self):
        return self.config.loglevel()

    def setLogLevel(self, *vals):
        """Set nsslapd-errorlog-level and return its value."""
        return self.config.loglevel(vals)

    def setAccessLogLevel(self, *vals):
        """Set nsslapd-accesslog-level and return its value."""
        return self.config.loglevel(vals, level='access')

    def configSSL(self, secport=636, secargs=None):
        """Configure SSL support into cn=encryption,cn=config.

            secargs is a dict like {
                'nsSSLPersonalitySSL': 'Server-Cert'
            }
            
            XXX moved to brooker.Config
        """
        return self.config.enable_ssl(secport, secargs)
        
