"""Brooker classes to organize ldap methods.
   Stuff is split in classes, like:
   * Replica
   * Backend
   * Suffix

   You will access this from:
   DSAdmin.backend.methodName()
"""
import ldap
import os
import re
import time


from lib389._constants import *
from lib389 import Entry, DSAdmin
from lib389.utils import normalizeDN, escapeDNValue, suffixfilt
from lib389 import (
    NoSuchEntryError
)

from lib389._constants import (
    DN_CHANGELOG,
    DN_MAPPING_TREE,
    DN_CHAIN, DN_LDBM,
    MASTER_TYPE,
    HUB_TYPE,
    LEAF_TYPE,
    REPLICA_RDONLY_TYPE,
    REPLICA_RDWR_TYPE
)

from lib389._replication import RUV, CSN
from lib389._entry import FormatDict


class Replica(object):
    proxied_methods = 'search_s getEntry'.split()
    STOP = '2358-2359 0'
    START = '0000-2359 0123456'
    ALWAYS = None

    def __init__(self, conn):
        """@param conn - a DSAdmin instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Replica.proxied_methods:
            return DSAdmin.__getattr__(self.conn, name)

    def _get_mt_entry(self, suffix):
        """Return the replica dn of the given suffix."""
        mtent = self.conn.getMTEntry(suffix)
        return ','.join(("cn=replica", mtent.dn))

    def changelog(self, dbname='changelogdb'):
        """Add and return the replication changelog entry.

            If dbname starts with "/" then it's considered a full path,
            otherwise it's relative to self.dbdir
        """
        dn = DN_CHANGELOG
        dirpath = os.path.join(self.conn.dbdir, dbname)
        entry = Entry(dn)
        entry.update({
            'objectclass': ("top", "extensibleobject"),
            'cn': "changelog5",
            'nsslapd-changelogdir': dirpath
        })
        self.log.debug("adding changelog entry: %r" % entry)
        try:
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS:
            self.log.warn("entry %s already exists" % dn)

        return self.conn._test_entry(dn, ldap.SCOPE_BASE)

    def list(self, suffix=None):
        """Return a list of replica entries under the given suffix.
            @param suffix - if suffix is None, return all replicas
        """
        if suffix:
            filtr = "(&(objectclass=nsds5Replica)(nsds5replicaroot=%s))" % suffix
        else:
            filtr = "(objectclass=nsds5Replica)"
        ents = self.conn.search_s(DN_MAPPING_TREE, ldap.SCOPE_SUBTREE, filtr)
        return ents

    def check_init(self, agmtdn):
        """returns tuple - first element is done/not done, 2nd is no error/has error
            @param agmtdn - the agreement dn
        """
        done, hasError = False, 0
        attrlist = ['cn', 'nsds5BeginReplicaRefresh', 'nsds5replicaUpdateInProgress',
                    'nsds5ReplicaLastInitStatus', 'nsds5ReplicaLastInitStart',
                    'nsds5ReplicaLastInitEnd']
        try:
            entry = self.conn.getEntry(
                agmtdn, ldap.SCOPE_BASE, "(objectclass=*)", attrlist)
        except NoSuchEntryError:
            self.log.exception("Error reading status from agreement %r" % agmtdn)
            hasError = 1
        else:
            refresh = entry.nsds5BeginReplicaRefresh
            inprogress = entry.nsds5replicaUpdateInProgress
            status = entry.nsds5ReplicaLastInitStatus
            if not refresh:  # done - check status
                if not status:
                    print "No status yet"
                elif status.find("replica busy") > -1:
                    print "Update failed - replica busy - status", status
                    done = True
                    hasError = 2
                elif status.find("Total update succeeded") > -1:
                    print "Update succeeded: status ", status
                    done = True
                elif inprogress.lower() == 'true':
                    print "Update in progress yet not in progress: status ", status
                else:
                    print "Update failed: status", status
                    hasError = 1
                    done = True
            elif self.verbose:
                print "Update in progress: status", status

        return done, hasError

    def start_and_wait(self, agmtdn):
        """@param agmtdn - agreement dn"""
        rc = self.start_async(agmtdn)
        if not rc:
            rc = self.wait_init(agmtdn)
            if rc == 2:  # replica busy - retry
                rc = self.start_and_wait(agmtdn)
        return rc

    def wait_init(self, agmtdn):
        """Initialize replication and wait for completion.
        @oaram agmtdn - agreement dn
        """
        done = False
        haserror = 0
        while not done and not haserror:
            time.sleep(1)  # give it a few seconds to get going
            done, haserror = self.check_init(agmtdn)
        return haserror

    def start_async(self, agmtdn):
        """Initialize replication without waiting.
            @param agmtdn - agreement dn
        """
        self.log.info("Starting async replication %s" % agmtdn)
        mod = [(ldap.MOD_ADD, 'nsds5BeginReplicaRefresh', 'start')]
        self.conn.modify_s(agmtdn, mod)

    def stop(self, agmtdn):
        """Stop replication.
            @param agmtdn - agreement dn
        """
        self.log.info("Stopping replication %s" % agmtdn)
        mod = [(
            ldap.MOD_REPLACE, 'nsds5replicaupdateschedule', [Replica.STOP])]
        self.conn.modify_s(agmtdn, mod)

    def restart(self, agmtdn, schedule=START):
        """Schedules a new replication.
            @param agmtdn  -
            @param schedule - default START
            `schedule` allows to customize the replication instant.
                        see 389 documentation for further info
        """
        self.log.info("Restarting replication %s" % agmtdn)
        mod = [(ldap.MOD_REPLACE, 'nsds5replicaupdateschedule', [
                schedule])]
        self.modify_s(agmtdn, mod)

    def keep_in_sync(self, agmtdn):
        """
        @param agmtdn - 
        """
        self.log.info("Setting agreement for continuous replication")
        raise NotImplementedError("Check nsds5replicaupdateschedule before writing!")

    def status(self, agreement_dn):
        """Return a formatted string with the replica status.
            @param agreement_dn - 
        """

        attrlist = ['cn', 'nsds5BeginReplicaRefresh', 'nsds5replicaUpdateInProgress',
                    'nsds5ReplicaLastInitStatus', 'nsds5ReplicaLastInitStart',
                    'nsds5ReplicaLastInitEnd', 'nsds5replicaReapActive',
                    'nsds5replicaLastUpdateStart', 'nsds5replicaLastUpdateEnd',
                    'nsds5replicaChangesSentSinceStartup', 'nsds5replicaLastUpdateStatus',
                    'nsds5replicaChangesSkippedSinceStartup', 'nsds5ReplicaHost',
                    'nsds5ReplicaPort']
        try:
            ent = self.conn.getEntry(
                agreement_dn, ldap.SCOPE_BASE, "(objectclass=*)", attrlist)
        except NoSuchEntryError:
            raise NoSuchEntryError(
                "Error reading status from agreement", agreement_dn)
        else:
            retstr = (
                "Status for %(cn)s agmt %(nsDS5ReplicaHost)s:%(nsDS5ReplicaPort)s" "\n"
                "Update in progress: %(nsds5replicaUpdateInProgress)s" "\n"
                "Last Update Start: %(nsds5replicaLastUpdateStart)s" "\n"
                "Last Update End: %(nsds5replicaLastUpdateEnd)s" "\n"
                "Num. Changes Sent: %(nsds5replicaChangesSentSinceStartup)s" "\n"
                "Num. changes Skipped: %(nsds5replicaChangesSkippedSinceStartup)s" "\n"
                "Last update Status: %(nsds5replicaLastUpdateStatus)s" "\n"
                "Init in progress: %(nsds5BeginReplicaRefresh)s" "\n"
                "Last Init Start: %(nsds5ReplicaLastInitStart)s" "\n"
                "Last Init End: %(nsds5ReplicaLastInitEnd)s" "\n"
                "Last Init Status: %(nsds5ReplicaLastInitStatus)s" "\n"
                "Reap Active: %(nsds5ReplicaReapActive)s" "\n"
            )
            # FormatDict manages missing fields in string formatting
            return retstr % FormatDict(ent.data)

    def add(self, suffix, binddn, bindpw, rtype=MASTER_TYPE, rid=None, tombstone_purgedelay=None, purgedelay=None, referrals=None, legacy=False):
        """Setup a replica entry on an existing suffix.
            @param suffix - dn of suffix
            @param binddn - the replication bind dn for this replica
                            can also be a list ["cn=r1,cn=config","cn=r2,cn=config"]
            @param bindpw - used to eventually provision the replication entry

            @param rtype - master, hub, leaf (see above for values) - default is master
            @param rid - replica id or - if not given - an internal sequence number will be assigned

            # further args
            @param legacy - true or false - for legacy consumer
            @param tombstone_purgedelay
            @param purgedelay - changelog expiration time in seconds
            @param referrals

            Ex. replica.add(**{
                    'suffix': "dc=example,dc=com",
                    'type'  : lib389.MASTER_TYPE,
                    'binddn': "cn=replication manager,cn=config"
              })
             binddn
            TODO: this method does not update replica type
        """
        # set default values
        if rtype == MASTER_TYPE:
            rtype = REPLICA_RDWR_TYPE
        else:
            rtype = REPLICA_RDONLY_TYPE

        if legacy:
            legacy = 'on'
        else:
            legacy = 'off'

        # create replica entry in mapping-tree
        nsuffix = normalizeDN(suffix)
        mtent = self.conn.getMTEntry(suffix)
        dn_replica = ','.join(("cn=replica", mtent.dn))
        try:
            entry = self.conn.getEntry(dn_replica, ldap.SCOPE_BASE)
            self.log.warn("Already setup replica for suffix %r" % suffix)
            rec = self.conn.suffixes.setdefault(nsuffix, {})
            rec.update({'dn': dn_replica, 'type': rtype})
            return rec
        except ldap.NO_SUCH_OBJECT:
            entry = None

        # If a replica does not exist
        binddnlist = []
        if hasattr(binddn, '__iter__'):
            binddnlist = binddn
        else:
            binddnlist.append(binddn)

        entry = Entry(dn_replica)
        entry.update({
            'objectclass': ("top", "nsds5replica", "extensibleobject"),
            'cn': "replica",
            'nsds5replicaroot': nsuffix,
            'nsds5replicaid': str(rid),
            'nsds5replicatype': str(rtype),
            'nsds5replicalegacyconsumer': legacy,
            'nsds5replicabinddn': binddnlist
        })
        if rtype != LEAF_TYPE:
            entry.setValues('nsds5flags', "1")

        # other args
        if tombstone_purgedelay is not None:
            entry.setValues(
                'nsds5replicatombstonepurgeinterval', str(tombstone_purgedelay))
        if purgedelay is not None:
            entry.setValues('nsds5ReplicaPurgeDelay', str(purgedelay))
        if referrals:
            entry.setValues('nsds5ReplicaReferral', referrals)

        self.conn.add_s(entry)

        # check if the entry exists TODO better to raise!
        self.conn._test_entry(dn_replica, ldap.SCOPE_BASE)

        self.conn.suffixes[nsuffix] = {'dn': dn_replica, 'type': rtype}
        return {'dn': dn_replica, 'type': rtype}

    def ruv(self, suffix, tryrepl=False):
        """return a replica update vector for the given suffix.

            @param suffix - eg. 'o=netscapeRoot'

            @raises NoSuchEntryError if missing
        """
        uuid = "ffffffff-ffffffff-ffffffff-ffffffff"
        filt = "(&(nsUniqueID=%s)(objectclass=nsTombstone))" % uuid
        attrs = ['nsds50ruv', 'nsruvReplicaLastModified']
        ents = self.conn.search_s(suffix, ldap.SCOPE_SUBTREE, filt, attrs)
        ent = None
        if ents and (len(ents) > 0):
            ent = ents[0]
        elif tryrepl:
            self.log.warn("Could not get RUV from %r entry - trying cn=replica" % suffix)
            ensuffix = escapeDNValue(normalizeDN(suffix))
            dn = ','.join(("cn=replica", "cn=%s" % ensuffix, DN_MAPPING_TREE))
            ents = self.conn.search_s(dn, ldap.SCOPE_BASE, "objectclass=*", attrs)

        if ents and (len(ents) > 0):
            ent = ents[0]
            self.log.debug("RUV entry is %r" % ent)
            return RUV(ent)

        raise NoSuchEntryError("RUV not found: suffix: %r" % suffix)

    def agreements(self, filtr='', attrs=None, dn=True):
        """Return a list of agreement dn.
            @param filtr - get only agreements matching the given filter
                            eg. '(cn=*example.it*)'
            @param attrs - attributes to retrieve
                            eg. use ['*'] for all, defaul is ['cn']
            @param dn - return a list of lib389.Entry if dn=False

        """
        attrs = attrs or ['cn']
        realfiltr = "(objectclass=nsds5ReplicationAgreement)"
        if filtr:
            realfiltr = "(&%s%s)" % (realfiltr, filtr)

        ents = self.conn.search_s(
            DN_MAPPING_TREE, ldap.SCOPE_SUBTREE, realfiltr, attrs)
        if dn:
            return [ent.dn for ent in ents]
        return ents

    def agreement_add(self, consumer, suffix=None, binddn=None, bindpw=None, cn_format=r'meTo_$host:$port', description_format=r'me to $host:$port', timeout=120, auto_init=False, bindmethod='simple', starttls=False, schedule=ALWAYS, args=None):
        """Create (and return) a replication agreement from self to consumer.
            - self is the supplier,

            @param consumer: one of the following (consumer can be a master)
                    * a DSAdmin object if chaining
                    * an object with attributes: host, port, sslport, __str__
            @param suffix    - eg. 'dc=babel,dc=it'
            @param binddn    - 
            @param bindpw    -
            @param cn_format - string.Template to format the agreement name
            @param timeout   - replica timeout in seconds
            @param auto_init - start replication immediately
            @param bindmethod-  'simple'
            @param starttls  - True or False
            @param schedule  - when to schedule the replication. default: ALWAYS 
            @param args      - further args dict. Allowed keys:
                    'fractional',
                    'stripattrs',
                    'winsync'
                    
            @raise NosuchEntryError    - if a replica doesn't exist for that suffix
            @raise ALREADY_EXISTS
            @raise UNWILLING_TO_PERFORM if the database was previously
                    in read-only state. To create new agreements you
                    need to *restart* the directory server
            
            NOTE: this method doesn't cache connection entries
            
            TODO: test winsync 
            TODO: test chain
            
        """
        import string
        assert binddn and bindpw and suffix
        args = args or {}

        othhost, othport, othsslport = (
            consumer.host, consumer.port, consumer.sslport)
        othport = othsslport or othport
        nsuffix = normalizeDN(suffix)

        # adding agreement to previously created replica
        replica_entries = self.list(suffix)
        if not replica_entries:
            raise NoSuchEntryError(
                "Error: no replica set up for suffix " + suffix)
        replica = replica_entries[0]

        # define agreement entry
        cn = string.Template(cn_format).substitute({'host': othhost, 'port': othport})
        dn_agreement = ','.join(["cn=%s" % cn, replica.dn])

        # This is probably unnecessary because
        # we can just raise ALREADY_EXISTS
        try:
            entry = self.conn.getEntry(dn_agreement, ldap.SCOPE_BASE)
            self.log.warn("Agreement exists: %r" % dn_agreement)
            raise ldap.ALREADY_EXISTS
        except ldap.NO_SUCH_OBJECT:
            entry = None

        # In a separate function in this scope?
        entry = Entry(dn_agreement)
        entry.update({
            'objectclass': ["top", "nsds5replicationagreement"],
            'cn': cn,
            'nsds5replicahost': consumer.host,
            'nsds5replicatimeout': str(timeout),
            'nsds5replicabinddn': binddn,
            'nsds5replicacredentials': bindpw,
            'nsds5replicabindmethod': bindmethod,
            'nsds5replicaroot': nsuffix,
            'description': string.Template(description_format).substitute({'host': othhost, 'port': othport})
        })
        if schedule:
            if not re.match(r'\d{4}-\d{4} [0-6]{1,7}', schedule): # TODO put the regexp in a separate variable
                raise ValueError("Bad schedule format %r" % schedule)
            entry.update({'nsds5replicaupdateschedule': schedule})
        if starttls:
            entry.setValues('nsds5replicatransportinfo', 'TLS')
            entry.setValues('nsds5replicaport', str(othport))
        elif othsslport:
            entry.setValues('nsds5replicatransportinfo', 'SSL')
            entry.setValues('nsds5replicaport', str(othsslport))
        else:
            entry.setValues('nsds5replicatransportinfo', 'LDAP')
            entry.setValues('nsds5replicaport', str(othport))
            
        if auto_init:
            entry.setValues('nsds5BeginReplicaRefresh', 'start')
            
        # further arguments
        if 'fractional' in args:
            entry.setValues('nsDS5ReplicatedAttributeList', args['fractional'])
        if 'stripattrs' in args:
            entry.setValues('nsds5ReplicaStripAttrs', args['stripattrs'])
        if 'winsync' in args:  # state it clearly!
            self.conn.setupWinSyncAgmt(args, entry)

        try:
            self.log.debug("Adding replica agreement: [%s]" % entry)
            self.conn.add_s(entry)
        except:
            #  FIXME check please!
            raise

        entry = self.conn.waitForEntry(dn_agreement)
        if entry:
            # More verbose but shows what's going on
            if 'chain' in args:
                chain_args = {
                    'suffix': suffix,
                    'binddn': binddn,
                    'bindpw': bindpw
                }
                # Work on `self` aka producer
                if replica.nsds5replicatype == MASTER_TYPE:
                    self.setupChainingFarm(**chain_args)
                # Work on `consumer`
                # TODO - is it really required?
                if replica.nsds5replicatype == LEAF_TYPE:
                    chain_args.update({
                        'isIntermediate': 0,
                        'urls': self.conn.toLDAPURL(),
                        'args': args['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)
                elif replica.nsds5replicatype == HUB_TYPE:
                    chain_args.update({
                        'isIntermediate': 1,
                        'urls': self.conn.toLDAPURL(),
                        'args': args['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)

        return dn_agreement
    
    def agreement_changes(self, agmtdn):
        """Return a list of changes sent by this agreement."""
        retval = 0
        try:
            ent = self.conn.getEntry(
                agmtdn, ldap.SCOPE_BASE, "(objectclass=*)",
                ['nsds5replicaChangesSentSinceStartup'])
        except:
            raise NoSuchEntryError(
                "Error reading status from agreement", agmtdn)

        if ent.nsds5replicaChangesSentSinceStartup:
            val = ent.nsds5replicaChangesSentSinceStartup
            items = val.split(' ')
            if len(items) == 1:
                retval = int(items[0])
            else:
                for item in items:
                    ary = item.split(":")
                    if ary and len(ary) > 1:
                        retval = retval + int(ary[1].split("/")[0])
        return retval


class Config(object):
    """
        Manage "cn=config" tree, including:
        - enable SSL
        - set access and error logging
        - get and set "cn=config" attributes
    """
    def __init__(self, conn):
        """@param conn - a DSAdmin instance """
        self.conn = conn
        self.log = conn.log
        
    def set(self, key, value):
        """Set a parameter under cn=config
            @param key - the attribute name
            @param value - attribute value as string
            
            eg. set('passwordExp', 'on')
        """
        self.log.debug("set(%r, %r)" % (key, value))
        return self.conn.modify(DN_CONFIG,
            [(ldap.MOD_REPLACE, key, value)])
            
    def get(self, key):
        """Get an attribute under cn=config"""
        return self.conn.getEntry(DN_CONFIG).__getattr__(key)

    def loglevel(self, vals=(LOG_DEFAULT,), level='error', update=False):
        """Set the access or error log level.
        @param vals - a list of log level codes (eg. lib389.LOG_*) 
                      defaults to LOG_DEFAULT
        @param level   -   'access' or 'error'
        @param update  - False for replace (default), True for update
        
        ex. loglevel([lib389.LOG_DEFAULT, lib389.LOG_ENTRY_PARSER])
        """
        level = 'nsslapd-%slog-level' % level
        assert len(vals) > 0, "set at least one log level"
        tot = 0
        for v in vals:
            tot |= v

        if update:
            old = int(self.get(level))
            tot |= old
            self.log.debug("Update %s value: %r -> %r" % (level, old, tot))
        else:
            self.log.debug("Replace %s with value: %r" % (level, tot))

        self.set(level, str(tot))
        return tot

    def enable_ssl(self, secport=636, secargs=None):
        """Configure SSL support into cn=encryption,cn=config.

            secargs is a dict like {
                'nsSSLPersonalitySSL': 'Server-Cert'
            }
        """
        self.log.debug("configuring SSL with secargs:%r" % secargs)
        secargs = secargs or {}

        dn_enc = 'cn=encryption,cn=config'
        ciphers = '-rsa_null_md5,+rsa_rc4_128_md5,+rsa_rc4_40_md5,+rsa_rc2_40_md5,+rsa_des_sha,' + \
            '+rsa_fips_des_sha,+rsa_3des_sha,+rsa_fips_3des_sha,' + \
            '+tls_rsa_export1024_with_rc4_56_sha,+tls_rsa_export1024_with_des_cbc_sha'
        mod = [(ldap.MOD_REPLACE, 'nsSSL3', secargs.get('nsSSL3', 'on')),
               (ldap.MOD_REPLACE, 'nsSSLClientAuth',
                secargs.get('nsSSLClientAuth', 'allowed')),
               (ldap.MOD_REPLACE, 'nsSSL3Ciphers', secargs.get('nsSSL3Ciphers', ciphers))]
        self.conn.modify_s(dn_enc, mod)

        dn_rsa = 'cn=RSA,cn=encryption,cn=config'
        e_rsa = Entry(dn_rsa)
        e_rsa.update({
            'objectclass': ['top', 'nsEncryptionModule'],
            'nsSSLPersonalitySSL': secargs.get('nsSSLPersonalitySSL', 'Server-Cert'),
            'nsSSLToken': secargs.get('nsSSLToken', 'internal (software)'),
            'nsSSLActivation': secargs.get('nsSSLActivation', 'on')
        })
        try:
            self.conn.add_s(e_rsa)
        except ldap.ALREADY_EXISTS:
            pass

        mod = [
            (ldap.MOD_REPLACE,
                'nsslapd-security', secargs.get('nsslapd-security', 'on')),
            (ldap.MOD_REPLACE,
                'nsslapd-ssl-check-hostname', secargs.get('nsslapd-ssl-check-hostname', 'off')),
            (ldap.MOD_REPLACE,
                'nsslapd-secureport', str(secport))
        ]
        self.log.debug("trying to modify %r with %r" % (DN_CONFIG, mod))
        self.conn.modify_s(DN_CONFIG, mod)

        fields = 'nsslapd-security nsslapd-ssl-check-hostname'.split()
        return self.conn.getEntry(DN_CONFIG, attrlist=fields)




class Backend(object):
    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DSAdmin instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Replica.proxied_methods:
            return DSAdmin.__getattr__(self.conn, name)

    def list(self, name=None, suffix=None, attrs=None):
        """Get backends by name or suffix
            @param name -   backend name
            @param suffix   -   get backend for suffix
        """
        attrs = attrs or []
        
        # raise errors asap
        if name and suffix:
            raise ValueError("Can't specify both name and suffix")
        
        def _list_by_suffix(self, suffix, attrs=None):    
            if suffix:
                nsuffix = normalizeDN(suffix)
            else:
                suffix = nsuffix = '*'
                
            entries = self.conn.search_s("cn=plugins,cn=config", ldap.SCOPE_SUBTREE,
                                    "(&(objectclass=nsBackendInstance)(|(nsslapd-suffix=%s)(nsslapd-suffix=%s)))" % (suffix, nsuffix),
                                    attrs)
            return entries

        def _list_by_name(self, name, attrs=None):
            backend_dn = ','.join(('cn=' + name, DN_LDBM))
            return self.conn.search_s(backend_dn, ldap.SCOPE_BASE, attrlist=attrs)
            
        if name:
            return _list_by_name(self, name, attrs)
        elif suffix:
            return _list_by_suffix(self, suffix, attrs)
            
        return self.conn.search_s("cn=plugins,cn=config", 
            ldap.SCOPE_SUBTREE, "(objectclass=nsBackendInstance)", attrs)

        
    def readonly(self, bename=None, readonly='on', suffix=None):
        """Put a database in readonly mode
            @param  bename  -   the backend name (eg. addressbook1)
            @param  readonly-   'on' or 'off'

            NOTE: I can ldif2db to a read-only database. After the
                  import, the database will still be in readonly.
                  
            NOTE: When a db is read-only, it seems you need to restart 
                  the directory server before creating further 
                  agreements or initialize consumers
        """
        if bename and suffix:
            raise ValueError("Specify either bename or suffix")

        if suffix:
            raise NotImplementedError()

        self.conn.modify_s(','.join(('cn=' + bename, DN_LDBM)), [
            (ldap.MOD_REPLACE, 'nsslapd-readonly', readonly)
        ])


    def add(self, suffix, binddn=None, bindpw=None, urls=None, attrvals=None, benamebase='localdb', setupmt=False, parent=None):
        """Setup a backend and return its dn. Blank on error XXX should RAISE!
            @param suffix
            @param benamebase   -   the backend common name
            @param setupmt  - eventually setup Mapping Tree entry
            @param urls - a string of ldapurl - create a chaining backend
            @oaram binddn   -   chaining proxy user
            @param bindpw   -   chaining proxy password
            @param attrvals: a dict with further params like
                for ldbm    {
                                'nsslapd-cachememsize': '1073741824',
                                'nsslapd-cachesize': '-1',
                            }
                for chain   {
                                'nsmaxresponsedelay': '60',
                                'nsslapd-sizelimit': '-1'
                            }
                            
            ex.
            benamebase="Isle0-0"
            urls=[ 
                "ldaps://f0-ldap-vip.example.it:636/",
                "ldaps://ldap-18.example.it:636/",
                "ldaps://ldap-19.example.it:636/"
                ]
            
            NOTE: The suffix attribute is a mere string for the backend.
                the following action will work nicely:
                c.backend.add(suffix="foo=example,dc=com",benamebase="db1")
                    
            TODO: rename benamebase with cn
            TODO: split CHAIN and LDBM ? eg. backend.add_chain
        """
        attrvals = attrvals or {}
        dnbase = ""

        # figure out what type of be based on args
        if binddn and bindpw and urls:  # its a chaining be
            dnbase = DN_CHAIN
        else:  # its a ldbm be
            dnbase = DN_LDBM

        nsuffix = normalizeDN(suffix)
        try:
            cn = benamebase 
            self.log.debug("create backend with cn: %s" % cn)
            dn = "cn=" + cn + "," + dnbase
            entry = Entry(dn)
            entry.update({
                'objectclass': ['top', 'extensibleObject', 'nsBackendInstance'],
                'cn': cn,
                'nsslapd-suffix': nsuffix
            })

            if binddn and bindpw and urls:  # its a chaining be
                entry.update({
                             'nsfarmserverurl': urls,
                             'nsmultiplexorbinddn': binddn,
                             'nsmultiplexorcredentials': bindpw
                             })

            # set attrvals (but not cn, because it's in dn)
            # TODO do it in Entry
            if attrvals:
                entry.update(attrvals)

            self.log.debug("adding entry: %r" % entry)
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS, e:
            self.log.error("Entry already exists: %r" % dn)
            raise ldap.ALREADY_EXISTS("%s : %r" % (e, dn))
        except ldap.LDAPError, e:
            self.log.error("Could not add backend entry: %r" % dn)
            raise

        self.conn._test_entry(dn, ldap.SCOPE_BASE)
        
        #
        # if setupmt creates the suffix entry
        #
        if setupmt:
            self.log.debug("Setup Mapping Tree entry")
            mtentry = self.setup_mt(suffix=suffix, bename=cn, parent=parent) 
            self.log.info("Created Mapping Tree entry %r" % mtentry)
        return cn


    def setup_mt(self, suffix, bename, parent=None):
        """Setup a suffix with the given backend-name.

            @param suffix
            @param bename
            @param parent   - the parent suffix 
            @param verbose  - None 

            This method does not create the matching entry in the tree,
            nor the given backend. Both should be created apart.
            
            Ex. setup_mt(suffix='o=addressbook1', bename='addressbook1')
                creates:
                    - the mapping in "cn=mapping tree,cn=config"
                you have to create:
                    - the backend 
                    - the ldap entry "o=addressbook1" *after*
        """
        nsuffix = normalizeDN(suffix)
        #escapedn = escapeDNValue(nsuffix)
        if parent:
            nparent = normalizeDN(parent)
        else:
            nparent = ""
            
        filt = suffixfilt(suffix)
        # if suffix exists, return
        try:
            entry = self.conn.getEntry(
                DN_MAPPING_TREE, ldap.SCOPE_SUBTREE, filt)
            return entry
        except NoSuchEntryError:
            entry = None

        # fix me when we can actually used escaped DNs
        #dn = "cn=%s,cn=mapping tree,cn=config" % escapedn
        dn = ','.join(('cn="%s"' % nsuffix, DN_MAPPING_TREE))
        entry = Entry(dn)
        entry.update({
            'objectclass': ['top', 'extensibleObject', 'nsMappingTree'],
            'nsslapd-state': 'backend',
            # the value in the dn has to be DN escaped
            # internal code will add the quoted value - unquoted value is useful for searching
            'cn': nsuffix,
            'nsslapd-backend': bename
        })
        #entry.setValues('cn', [escapedn, nsuffix]) # the value in the dn has to be DN escaped
        # the other value can be the unescaped value
        if parent:
            entry.setValues('nsslapd-parent-suffix', nparent)
        try:
            self.log.debug("Creating entry: %r" % entry)
            self.conn.add_s(entry)
        except ldap.LDAPError, e:
            raise ldap.LDAPError("Error adding suffix entry " + dn, e)

        ret = self.conn._test_entry(dn, ldap.SCOPE_BASE)
        return ret

    def suffixes(self,unquote=False):
        """Return a list of suffixes dn
            @param unquote - remove ldap escaping
        """
        suffixes = self.search_s(DN_MAPPING_TREE, ldap.SCOPE_ONELEVEL)
        if unquote:
            raise NotImplementedError()
            return [x.dn.replace("\3D","=").replace("\2C",",") for x in suffixes]
        return [x.dn for x in suffixes]
