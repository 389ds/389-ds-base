# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import re
import time
import json
import datetime
from lib389._constants import *
from lib389.properties import *
from lib389._entry import FormatDict
from lib389.utils import normalizeDN, ensure_bytes, ensure_str, ensure_dict_str, ensure_list_str
from lib389 import Entry, DirSrv, NoSuchEntryError, InvalidArgumentError
from lib389._mapped_object import DSLdapObject, DSLdapObjects


class Agreement(DSLdapObject):
    """A replication agreement from this server instance to
    another instance of directory server.

    - must attributes: [ 'cn' ]
    - RDN attribute: 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    csnpat = r'(.{8})(.{4})(.{4})(.{4})'
    csnre = re.compile(csnpat)

    def __init__(self, instance, dn=None, winsync=False):
        super(Agreement, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = [
            'cn',
        ]
        if winsync:
            self._create_objectclasses = [
                'top',
                'nsDSWindowsReplicationAgreement',
            ]
        else:
            self._create_objectclasses = [
                'top',
                'nsds5replicationagreement',
            ]
        self._protected = False

    def begin_reinit(self):
        """Begin a total reinit of the consumer. This will send
        our data to the server we are replicating too.
        """
        self.set('nsds5BeginReplicaRefresh', 'start')

    def check_reinit(self):
        """Check the status of a reinit. Returns done, inprogress, and error text. A correct
        reinit will return (True, False, False).

        :returns: tuple(done, error), where done, error are bool.
        """
        done = False
        error = False
        inprogress = False
        status = self.get_attr_val_utf8('nsds5ReplicaLastInitStatus')
        self._log.debug('agreement tot_init status: %s' % status)
        if not status:
            pass
        elif 'replica busy' in status:
            error = status
        elif 'Total update succeeded' in status:
            done = True
            inprogress = False
        elif 'Replication error' in status:
            error = status
        elif 'Total update in progress' in status:
            inprogress = True
        elif 'LDAP error' in status:
            error = status

        return (done, inprogress, error)

    def wait_reinit(self, timeout=300):
        """Wait for a reinit to complete. Returns done and error. A correct
        reinit will return (True, False).
        :param timeout: timeout value for how long to wait for the reinit
        :type timeout: int
        :returns: tuple(done, error), where done, error are bool.
        """
        done = False
        error = False
        count = 0
        while done is False and error is False:
            (done, inprogress, error) = self.check_reinit()
            if count > timeout and not done:
                error = True
            count = count + 2
            time.sleep(2)
        return (done, error)

    def get_name(self):
        return self.get_attr_val_utf8_l('cn')

    def get_agmt_maxcsn(self):
        """Get the agreement maxcsn from the database RUV entry
        :returns: CSN string if found, otherwise None is returned
        """
        from lib389.replica import Replicas
        suffix = self.get_attr_val_utf8(REPL_ROOT)
        agmt_name = self.get_attr_val_utf8('cn')
        replicas = Replicas(self._instance)
        replica = replicas.get(suffix)
        maxcsns = replica.get_ruv_agmt_maxcsns()

        if maxcsns is None or len(maxcsns) == 0:
            self._log.debug('get_agmt_maxcsn - Failed to get agmt maxcsn from RUV')
            return None

        for csn in maxcsns:
            comps = csn.split(';')
            if agmt_name == comps[1]:
                # same replica, get maxcsn
                if len(comps) < 6:
                    return None
                else:
                    return comps[5]

        self._log.debug('get_agmt_maxcsn - did not find matching agmt maxcsn from RUV')
        return None

    def get_consumer_maxcsn(self, binddn=None, bindpw=None):
        """Attempt to get the consumer's maxcsn from its database RUV entry
        :param binddn: Specifies a specific bind DN to use when contacting the remote consumer
        :type binddn: str
        :param bindpw: Password for the bind DN
        :type bindpw: str
        :returns: CSN string if found, otherwise "Unavailable" is returned
        """
        host = self.get_attr_val_utf8(AGMT_HOST)
        port = self.get_attr_val_utf8(AGMT_PORT)
        suffix = self.get_attr_val_utf8(REPL_ROOT)
        protocol = self.get_attr_val_utf8('nsds5replicatransportinfo').lower()

        result_msg = "Unavailable"

        # If we are using LDAPI we need to provide the credentials, otherwise
        # use the existing credentials
        if binddn is None:
            binddn = self._instance.binddn
        if bindpw is None:
            bindpw = self._instance.bindpw

        # Get the replica id from supplier to compare to the consumer's rid
        from lib389.replica import Replicas
        replicas = Replicas(self._instance)
        replica = replicas.get(suffix)
        rid = replica.get_attr_val_utf8(REPL_ID)

        # Open a connection to the consumer
        consumer = DirSrv(verbose=self._instance.verbose)
        args_instance[SER_HOST] = host
        if protocol == "ssl" or protocol == "ldaps":
            args_instance[SER_SECURE_PORT] = int(port)
        else:
            args_instance[SER_PORT] = int(port)
        args_instance[SER_ROOT_DN] = binddn
        args_instance[SER_ROOT_PW] = bindpw
        args_standalone = args_instance.copy()
        consumer.allocate(args_standalone)
        try:
            consumer.open()
        except ldap.INVALID_CREDENTIALS as e:
            raise(e)
        except ldap.LDAPError as e:
            self._log.debug('Connection to consumer ({}:{}) failed, error: {}'.format(host, port, e))
            return result_msg

        # Search for the tombstone RUV entry
        try:
            entry = consumer.search_s(suffix, ldap.SCOPE_SUBTREE,
                                      REPLICA_RUV_FILTER, ['nsds50ruv'])
            if not entry:
                self._log.debug("Failed to retrieve database RUV entry from consumer")
            else:
                elements = ensure_list_str(entry[0].getValues('nsds50ruv'))
                for ruv in elements:
                    if ('replica %s ' % rid) in ruv:
                        ruv_parts = ruv.split()
                        if len(ruv_parts) == 5:
                            result_msg = ruv_parts[4]
                        break
        except ldap.INVALID_CREDENTIALS as e:
            raise(e)
        except ldap.LDAPError as e:
            self._log.debug('Failed to search for the suffix ' +
                                     '({}) consumer ({}:{}) failed, error: {}'.format(
                                         suffix, host, port, e))
        consumer.close()
        return result_msg

    def get_agmt_status(self, binddn=None, bindpw=None, return_json=False):
        """Return the status message
        :param binddn: Specifies a specific bind DN to use when contacting the remote consumer
        :type binddn: str
        :param bindpw: Password for the bind DN
        :type bindpw: str
        :returns: A status message about the replication agreement
        """
        con_maxcsn = "Unknown"
        try:
            agmt_maxcsn = self.get_agmt_maxcsn()
            agmt_status = json.loads(self.get_attr_val_utf8_l(AGMT_UPDATE_STATUS_JSON))
            if agmt_maxcsn is not None:
                try:
                    con_maxcsn = self.get_consumer_maxcsn(binddn=binddn, bindpw=bindpw)
                    if con_maxcsn:
                        if agmt_maxcsn == con_maxcsn:
                            if return_json:
                                return json.dumps({
                                    'msg': "In Synchronization",
                                    'agmt_maxcsn': agmt_maxcsn,
                                    'con_maxcsn': con_maxcsn,
                                    'state': agmt_status['state'],
                                    'reason': agmt_status['message']
                                }, indent=4)
                            else:
                                return "In Synchronization"
                except:
                    pass
            else:
                agmt_maxcsn = "Unknown"

            # Not in sync - attempt to discover the cause
            repl_msg = agmt_status['message']
            if self.get_attr_val_utf8_l(AGMT_UPDATE_IN_PROGRESS) == 'true':
                # Replication is on going - this is normal
                repl_msg = "Replication still in progress"
            elif "can't contact ldap" in agmt_status['message']:
                    # Consumer is down
                    repl_msg = "Consumer can not be contacted"

            if return_json:
                return json.dumps({
                    'msg': "Not in Synchronization",
                    'agmt_maxcsn': agmt_maxcsn,
                    'con_maxcsn': con_maxcsn,
                    'state': agmt_status['state'],
                    'reason': repl_msg
                }, indent=4)
            else:
                return ("Not in Synchronization: supplier " +
                        "(%s) consumer (%s) State (%s) Reason (%s)" %
                        (agmt_maxcsn, con_maxcsn, agmt_status['state'], repl_msg))
        except ldap.INVALID_CREDENTIALS as e:
            raise(e)
        except ldap.LDAPError as e:
            raise ValueError(str(e))

    def get_lag_time(self, suffix, agmt_name, binddn=None, bindpw=None):
        """Get the lag time between the supplier and the consumer
        :param suffix: The replication suffix
        :type suffix: str
        :param agmt_name: The name of the agreement
        :type agmt_name: str
        :param binddn: Specifies a specific bind DN to use when contacting the remote consumer
        :type binddn: str
        :param bindpw: Password for the bind DN
        :type bindpw: str
        :returns: A time-formated string of the the replication lag (HH:MM:SS).
        :raises: ValueError - if unable to get consumer's maxcsn
        """

        try:
            agmt_maxcsn = self.get_agmt_maxcsn()
            con_maxcsn = self.get_consumer_maxcsn(binddn=binddn, bindpw=bindpw)
        except ldap.LDAPError as e:
            raise ValueError("Unable to get lag time: " + str(e))

        if con_maxcsn is None:
            raise ValueError("Unable to get consumer's max csn")
        if con_maxcsn.lower() == "unavailable":
            return con_maxcsn

        # Extract the csn timstamps and compare them
        agmt_time = 0
        con_time = 0
        match = Agreement.csnre.match(agmt_maxcsn)
        if match:
            agmt_time = int(match.group(1), 16)
        match = Agreement.csnre.match(con_maxcsn)
        if match:
            con_time = int(match.group(1), 16)
        diff = con_time - agmt_time
        if diff < 0:
            lag = datetime.timedelta(seconds=-diff)
        else:
            lag = datetime.timedelta(seconds=diff)

        # Return a nice formated timestamp
        return "{:0>8}".format(str(lag))

    def status(self, winsync=False, just_status=False, use_json=False, binddn=None, bindpw=None):
        """Get the status of a replication agreement
        :param winsync: Specifies if the the agreement is a winsync replication agreement
        :type winsync: boolean
        :param just_status: Just return the status string and not all of the status attributes
        :type just_status: boolean
        :param use_json: Return the status in a JSON object
        :type use_json: boolean
        :param binddn: Specifies a specific bind DN to use when contacting the remote consumer
        :type binddn: str
        :param bindpw: Password for the bind DN
        :type bindpw: str
        :returns: A status message
        :raises: ValueError - if failing to get agmt status
        """
        status_attrs_dict = self.get_all_attrs()
        status_attrs_dict = dict((k.lower(), v) for k, v in list(status_attrs_dict.items()))

        # We need a bind DN and passwd so we can query the consumer.  If this is an LDAPI
        # connection, and the consumer does not allow anonymous access to the tombstone
        # RUV entry under the suffix, then we can't get the status.  So in this case we
        # need to provide a DN and password.
        if not winsync:
            try:
                status = self.get_agmt_status(binddn=binddn, bindpw=bindpw)
            except ldap.INVALID_CREDENTIALS as e:
                raise(e)
            except ValueError as e:
                status = str(e)
            if just_status:
                if use_json:
                    return (json.dumps(status, indent=4))
                else:
                    return status

            # Get the lag time
            suffix = ensure_str(status_attrs_dict['nsds5replicaroot'][0])
            agmt_name = ensure_str(status_attrs_dict['cn'][0])
            lag_time = self.get_lag_time(suffix, agmt_name, binddn=binddn, bindpw=bindpw)
        else:
            lag_time = "Not available for Winsync agreements"
            status = "Not available for Winsync agreements"

        # handle the attributes that are not always set in the agreement
        if 'nsds5replicaenabled' not in status_attrs_dict:
            status_attrs_dict['nsds5replicaenabled'] = ['on']
        if 'nsds5agmtmaxcsn' not in status_attrs_dict:
            status_attrs_dict['nsds5agmtmaxcsn'] = ["unavailable"]
        if 'nsds5replicachangesskippedsince' not in status_attrs_dict:
            status_attrs_dict['nsds5replicachangesskippedsince'] = ["unavailable"]
        if 'nsds5beginreplicarefresh' not in status_attrs_dict:
            status_attrs_dict['nsds5beginreplicarefresh'] = [""]
        if 'nsds5replicalastinitstatus' not in status_attrs_dict:
            status_attrs_dict['nsds5replicalastinitstatus'] = ["unavailable"]
        if 'nsds5replicachangessentsincestartup' not in status_attrs_dict:
            status_attrs_dict['nsds5replicachangessentsincestartup'] = ['0']
        if ensure_str(status_attrs_dict['nsds5replicachangessentsincestartup'][0]) == '':
            status_attrs_dict['nsds5replicachangessentsincestartup'] = ['0']

        consumer = "{}:{}".format(ensure_str(status_attrs_dict['nsds5replicahost'][0]),
                                  ensure_str(status_attrs_dict['nsds5replicaport'][0]))

        # Case sensitive?
        if use_json:
            result = {
                      'agmt-name': ensure_list_str(status_attrs_dict['cn']),
                      'replica': [consumer],
                      'replica-enabled': ensure_list_str(status_attrs_dict['nsds5replicaenabled']),
                      'update-in-progress': ensure_list_str(status_attrs_dict['nsds5replicaupdateinprogress']),
                      'last-update-start': ensure_list_str(status_attrs_dict['nsds5replicalastupdatestart']),
                      'last-update-end': ensure_list_str(status_attrs_dict['nsds5replicalastupdateend']),
                      'number-changes-sent': ensure_list_str(status_attrs_dict['nsds5replicachangessentsincestartup']),
                      'number-changes-skipped': ensure_list_str(status_attrs_dict['nsds5replicachangesskippedsince']),
                      'last-update-status': ensure_list_str(status_attrs_dict['nsds5replicalastupdatestatus']),
                      'last-init-start': ensure_list_str(status_attrs_dict['nsds5replicalastinitstart']),
                      'last-init-end': ensure_list_str(status_attrs_dict['nsds5replicalastinitend']),
                      'last-init-status': ensure_list_str(status_attrs_dict['nsds5replicalastinitstatus']),
                      'reap-active': ensure_list_str(status_attrs_dict['nsds5replicareapactive']),
                      'replication-status': [status],
                      'replication-lag-time': [lag_time]
                }
            return (json.dumps(result, indent=4))
        else:
            retstr = (
                "Status For Agreement: \"%(cn)s\" (%(nsDS5ReplicaHost)s:"
                "%(nsDS5ReplicaPort)s)" "\n"
                "Replica Enabled: %(nsds5ReplicaEnabled)s" "\n"
                "Update In Progress: %(nsds5replicaUpdateInProgress)s" "\n"
                "Last Update Start: %(nsds5replicaLastUpdateStart)s" "\n"
                "Last Update End: %(nsds5replicaLastUpdateEnd)s" "\n"
                "Number Of Changes Sent: %(nsds5replicaChangesSentSinceStartup)s"
                "\n"
                "Number Of Changes Skipped: %(nsds5replicaChangesSkippedSince"
                "Startup)s" "\n"
                "Last Update Status: %(nsds5replicaLastUpdateStatus)s" "\n"
                "Last Init Start: %(nsds5ReplicaLastInitStart)s" "\n"
                "Last Init End: %(nsds5ReplicaLastInitEnd)s" "\n"
                "Last Init Status: %(nsds5ReplicaLastInitStatus)s" "\n"
                "Reap Active: %(nsds5ReplicaReapActive)s" "\n"
            )
            # FormatDict manages missing fields in string formatting
            entry_data = ensure_dict_str(status_attrs_dict)
            result = retstr % FormatDict(entry_data)
            result += "Replication Status: {}\n".format(status)
            result += "Replication Lag Time: {}\n".format(lag_time)
            return result

    def pause(self):
        """Pause outgoing changes from this server to consumer. Note
        that this does not pause the consumer, only that changes will
        not be sent from this master to consumer: the consumer may still
        receive changes from other replication paths!
        """
        self.set('nsds5ReplicaEnabled', 'off')

    def resume(self):
        """Resume sending updates from this master to consumer directly.
        """
        self.set('nsds5ReplicaEnabled', 'on')

    def set_wait_for_async_results(self, value):
        """Set nsDS5ReplicaWaitForAsyncResults to value.

        :param value: Time in milliseconds.
        :type value: str
        """
        self.replace('nsDS5ReplicaWaitForAsyncResults', value)

    def remove_wait_for_async_results(self):
        """Reset nsDS5ReplicaWaitForAsyncResults to default.
        """
        self.remove_all('nsDS5ReplicaWaitForAsyncResults')

    def get_wait_for_async_results_utf8(self):
        """Get the current value of nsDS5ReplicaWaitForAsyncResults.

        :returns: str
        """
        return self.get_attr_val_utf8('nsDS5ReplicaWaitForAsyncResults')

    def set_flowcontrolwindow(self, value):
        """Set nsds5ReplicaFlowControlWindow to value.

        :param value: During total update Number of entries to send without waiting ack
        :type value: str
        """
        self.replace('nsds5ReplicaFlowControlWindow', value)

class WinsyncAgreement(Agreement):
    """A replication agreement from this server instance to
    another instance of directory server.

    - must attributes: [ 'cn' ]
    - RDN attribute: 'cn'

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(Agreement, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = [
            'cn',
        ]
        self._create_objectclasses = [
                'top',
                'nsDSWindowsReplicationAgreement',
            ]

        self._protected = False


class Agreements(DSLdapObjects):
    """Represents the set of agreements configured on this instance.
    There are two possible ways to use this interface.

    The first is as the set of agreements on the server for all
    replicated suffixes. IE:

    agmts = Agreements(inst).

    However, this will NOT allow new agreements to be created, as
    agreements must be related to a replica.

    The second is Agreements related to a replica. For this method
    you must use:

    replica = Replicas(inst).get(<suffix>)
    agmts = Agreements(inst, replica.dn)


    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: The base dn to search.
    :type basedn: str
    :param rdn: The rdn relative to cn=mapping tree to search.
    :type rdn: str
    """

    def __init__(self, instance, basedn=DN_MAPPING_TREE, rdn=None, winsync=False):
        super(Agreements, self).__init__(instance)
        if winsync:
            self._childobject = WinsyncAgreement
            self._objectclasses = ['nsDSWindowsReplicationAgreement']
        else:
            self._childobject = Agreement
            self._objectclasses = ['nsds5replicationagreement']
        self._filterattrs = ['cn', 'nsDS5ReplicaRoot']
        if rdn is None:
            self._basedn = basedn
        else:
            self._basedn = "%s,%s" % (rdn, basedn)

    def _validate(self, rdn, properties):
        """ An internal implementation detail of create verification. You should
        never call this directly.
        """
        if self._basedn == DN_MAPPING_TREE:
            raise ldap.UNWILLING_TO_PERFORM("Refusing to create agreement in %s" % DN_MAPPING_TREE)
        return super(Agreements, self)._validate(rdn, properties)


class AgreementLegacy(object):
    """An object that helps to work with agreement entry

    :param conn: An instance
    :type conn: lib389.DirSrv
    """

    ALWAYS = '0000-2359 0123456'
    NEVER = '2358-2359 0'

    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Agreement.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def status(self, agreement_dn, just_status=False):
        """Get a formatted string with the replica status

        :param agreement_dn: DN of the replica agreement
        :type agreement_dn: str
        :param just_status: If True, returns just status
        :type just_status: bool
        :returns: str -- See below
        :raises: NoSuchEntryError - if agreement_dn is an unknown entry

        :example:
            ::

                Status for meTo_localhost.localdomain:50389 agmt
                    localhost.localdomain:50389
                Update in progress: TRUE
                Last Update Start: 20131121132756Z
                Last Update End: 0
                Num. Changes Sent: 1:10/0
                Num. changes Skipped: None
                Last update Status: Error (0) Replica acquired successfully:
                    Incremental update started
                Init in progress: None
                Last Init Start: 0
                Last Init End: 0
                Last Init Status: None
                Reap Active: 0
        """

        attrlist = ['cn', 'nsds5BeginReplicaRefresh', 'nsds5ReplicaRoot',
                    'nsds5replicaUpdateInProgress',
                    'nsds5ReplicaLastInitStatus', 'nsds5ReplicaLastInitStart',
                    'nsds5ReplicaLastInitEnd', 'nsds5replicaReapActive',
                    'nsds5replicaLastUpdateStart', 'nsds5replicaLastUpdateEnd',
                    'nsds5replicaChangesSentSinceStartup',
                    'nsds5replicaLastUpdateStatus',
                    'nsds5replicaChangesSkippedSinceStartup',
                    'nsds5ReplicaHost', 'nsds5ReplicaPort',
                    'nsds5ReplicaEnabled', 'nsds5AgmtMaxCSN']
        try:
            ent = self.conn.getEntry(
                agreement_dn, ldap.SCOPE_BASE, "(objectclass=*)", attrlist)
        except NoSuchEntryError:
            raise NoSuchEntryError(
                "Error reading status from agreement", agreement_dn)
        else:
            status = self.conn.getReplAgmtStatus(ent)
            if just_status:
                return status

            retstr = (
                "Status for %(cn)s agmt %(nsDS5ReplicaHost)s:"
                "%(nsDS5ReplicaPort)s" "\n"
                "Replica Enabled: %(nsds5ReplicaEnabled)s" "\n"
                "Agreement maxCSN: %(nsds5AgmtMaxCSN)s" "\n"
                "Update in progress: %(nsds5replicaUpdateInProgress)s" "\n"
                "Last Update Start: %(nsds5replicaLastUpdateStart)s" "\n"
                "Last Update End: %(nsds5replicaLastUpdateEnd)s" "\n"
                "Num. Changes Sent: %(nsds5replicaChangesSentSinceStartup)s"
                "\n"
                "Num. changes Skipped: %(nsds5replicaChangesSkippedSince"
                "Startup)s" "\n"
                "Last update Status: %(nsds5replicaLastUpdateStatus)s" "\n"
                "Init in progress: %(nsds5BeginReplicaRefresh)s" "\n"
                "Last Init Start: %(nsds5ReplicaLastInitStart)s" "\n"
                "Last Init End: %(nsds5ReplicaLastInitEnd)s" "\n"
                "Last Init Status: %(nsds5ReplicaLastInitStatus)s" "\n"
                "Reap Active: %(nsds5ReplicaReapActive)s" "\n"
            )
            # FormatDict manages missing fields in string formatting
            entry_data = ensure_dict_str(ent.data)
            result = retstr % FormatDict(entry_data)
            result += "Replication Status: %s\n" % status
            return result

    def _check_interval(self, interval):
        """Check the interval for schedule replication is valid:
        HH [0..23]
        MM [0..59]
        DAYS [0-6]{1,7}

        :param interval: - 'HHMM-HHMM D+' With D=[0123456]+
                          - Agreement.ALWAYS
                          - Agreement.NEVER
        :type interval: str

        :returns: None
        :raises: ValueError - if the interval is illegal
        """

        c = re.compile(re.compile('^([0-9][0-9])([0-9][0-9])-([0-9][0-9])' +
                                  '([0-9][0-9]) ([0-6]{1,7})$'))
        if not c.match(interval):
            raise ValueError("Bad schedule format %r" % interval)
        schedule = c.split(interval, c.groups)

        # check the hours
        hour = int(schedule[1])
        if ((hour < 0) or (hour > 23)):
            raise ValueError("Bad schedule format %r: illegal hour %d" %
                             (interval, hour))
        hour = int(schedule[3])
        if ((hour < 0) or (hour > 23)):
            raise ValueError("Bad schedule format %r: illegal hour %d" %
                             (interval, hour))
        if int(schedule[1]) > int(schedule[3]):
            raise ValueError("Bad schedule (start HOUR larger than end HOUR)" +
                             " %r: illegal hour %d" %
                             (interval, int(schedule[1])))

        # check the minutes
        minute = int(schedule[2])
        if ((minute < 0) or (minute > 59)):
            raise ValueError("Bad schedule format %r: illegal minute %d" %
                             (interval, minute))
        minute = int(schedule[4])
        if ((minute < 0) or (minute > 59)):
            raise ValueError("Bad schedule format %r: illegal minute %d" %
                             (interval, minute))

    def schedule(self, agmtdn=None, interval=ALWAYS):
        """Schedule the replication agreement

        :param agmtdn: DN of the replica agreement
        :type agmtdn: str
        :param interval: - 'HHMM-HHMM D+' With D=[0123456]+
                          - Agreement.ALWAYS
                          - Agreement.NEVER
        :type interval: str

        :returns: None
        :raises: - ValueError - if interval is not valid;
                  - ldap.NO_SUCH_OBJECT - if agmtdn does not exist
        """
        if not agmtdn:
            raise InvalidArgumentError("agreement DN is missing")

        # check the validity of the interval
        if interval != Agreement.ALWAYS and interval != Agreement.NEVER:
            self._check_interval(interval)

        # Check if the replica agreement exists
        try:
            self.conn.getEntry(agmtdn, ldap.SCOPE_BASE)
        except ldap.NO_SUCH_OBJECT:
            raise

        # update it
        self.log.info("Schedule replication agreement %s", agmtdn)
        mod = [(ldap.MOD_REPLACE, 'nsds5replicaupdateschedule', [ensure_bytes(interval)])]
        self.conn.modify_s(agmtdn, mod)

    def getProperties(self, agmnt_dn=None, properties=None):
        """Get a dictionary of the requested properties.
        If properties parameter is missing, it returns all the properties.

        :param agmtdn: DN of the replica agreement
        :type agmtdn: str
        :param properties: List of properties name
        :type properties: list

        :returns: Returns a dictionary of the properties
        :raises: - ValueError - if invalid property name
                  - ldap.NO_SUCH_OBJECT - if agmtdn does not exist
                  - InvalidArgumentError - missing mandatory argument

        :supported properties are:
                RA_NAME, RA_SUFFIX, RA_BINDDN, RA_BINDPW, RA_METHOD,
                RA_DESCRIPTION, RA_SCHEDULE, RA_TRANSPORT_PROT, RA_FRAC_EXCLUDE,
                RA_FRAC_EXCLUDE_TOTAL_UPDATE, RA_FRAC_STRIP, RA_CONSUMER_PORT,
                RA_CONSUMER_HOST, RA_CONSUMER_TOTAL_INIT, RA_TIMEOUT, RA_CHANGES
        """

        if not agmnt_dn:
            raise InvalidArgumentError("agmtdn is a mandatory argument")

        #
        # prepare the attribute list to retrieve from the RA
        # if properties is None, all RA attributes are retrieved
        #
        attrs = []
        if properties:
            for prop_name in properties:
                prop_attr = RA_PROPNAME_TO_ATTRNAME[prop_name]
                if not prop_attr:
                    raise ValueError("Improper property name: %s ", prop_name)
                attrs.append(prop_attr)

        filt = "(objectclass=*)"
        result = {}
        try:
            entry = self.conn.getEntry(agmnt_dn, ldap.SCOPE_BASE, filt, attrs)

            # Build the result from the returned attributes
            for attr in entry.getAttrs():
                # given an attribute name retrieve the property name
                props = [k for k, v in RA_PROPNAME_TO_ATTRNAME.items()
                         if v.lower() == attr.lower()]

                # If this attribute is present in the RA properties, adds it to
                # result
                if len(props) > 0:
                    result[props[0]] = entry.getValues(attr)

        except ldap.NO_SUCH_OBJECT:
            raise

        return result

    def setProperties(self, suffix=None, agmnt_dn=None, agmnt_entry=None,
                      properties=None):
        """Set the properties of the agreement entry. If an 'agmnt_entry'
        is provided, it updates the entry, else it updates the entry on
        the server. If the 'agmnt_dn' is provided it retrieves the entry
        using it, else it retrieve the agreement using the 'suffix'.

        :param suffix: Suffix stored in that agreement (online update)
        :type suffix: str
        :param agmnt_dn: DN of the agreement (online update)
        :type agmnt_dn: str
        :param agmnt_entry: Entry of a agreement (offline update)
        :type agmnt_entry: lib389.Entry
        :param properties: Dictionary of properties
        :type properties: dict

        :returns: None
        :raises: - ValueError - if invalid properties
                  - ValueError - if invalid agreement_entry
                  - ValueError - if agmnt_dn or suffix are not associated to a replica
                  - InvalidArgumentError - missing mandatory argument

        :supported properties are:
                RA_NAME, RA_SUFFIX, RA_BINDDN, RA_BINDPW, RA_METHOD,
                RA_DESCRIPTION, RA_SCHEDULE, RA_TRANSPORT_PROT, RA_FRAC_EXCLUDE,
                RA_FRAC_EXCLUDE_TOTAL_UPDATE, RA_FRAC_STRIP, RA_CONSUMER_PORT,
                RA_CONSUMER_HOST, RA_CONSUMER_TOTAL_INIT, RA_TIMEOUT, RA_CHANGES
        """

        # No properties provided
        if len(properties) == 0:
            return

        # check that the given properties are valid
        for prop in properties:
            # skip the prefix to add/del value
            if not inProperties(prop, RA_PROPNAME_TO_ATTRNAME):
                raise ValueError("unknown property: %s" % prop)
            else:
                self.log.debug("setProperties: %s:%s",
                               prop, properties[prop])

        # At least we need to have suffix/agmnt_dn/agmnt_entry
        if not suffix and not agmnt_dn and not agmnt_entry:
            raise InvalidArgumentError("suffix and agmnt_dn and agmnt_entry "
                                       "are missing")

        # TODO
        if suffix:
            raise NotImplementedError

        # The caller provides a set of properties to set into a replica entry
        if agmnt_entry:
            if not isinstance(agmnt_entry, Entry):
                raise ValueError("invalid instance of the agmnt_entry")

            # that is fine, now set the values
            for prop in properties:
                val = rawProperty(prop)

                # for Entry update it is a replace
                agmnt_entry.update({RA_PROPNAME_TO_ATTRNAME[val]:
                                   properties[prop]})

            return

        # for each provided property build the mod
        mod = []
        for prop in properties:

            # retrieve/check the property name
            # and if the value needs to be added/deleted/replaced
            if prop.startswith('+'):
                mod_type = ldap.MOD_ADD
                prop_name = prop[1:]
            elif prop.startswith('-'):
                mod_type = ldap.MOD_DELETE
                prop_name = prop[1:]
            else:
                mod_type = ldap.MOD_REPLACE
                prop_name = prop

            attr = RA_PROPNAME_TO_ATTRNAME[prop_name]
            if not attr:
                raise ValueError("invalid property name %s" % prop_name)

            # Now do the value checking we can do
            if prop_name == RA_SCHEDULE:
                self._check_interval(properties[prop])

            mod.append((mod_type, attr, ensure_bytes(properties[prop])))

        # Now time to run the effective modify
        self.conn.modify_s(agmnt_dn, mod)

    def list(self, suffix=None, consumer_host=None, consumer_port=None,
             agmtdn=None):
        """Returns the search result of the replica agreement(s) under the
        replica (replicaRoot is 'suffix').

        Either 'suffix' or 'agmtdn' need to be specfied.
        'consumer_host' and 'consumer_port' are either not specified or
        specified both.

        If 'agmtdn' is specified, it returns the search result entry of
        that replication agreement, else if consumer host/port are specified
        it returns the replica agreements toward that consumer host:port.

        Finally if neither 'agmtdn' nor 'consumser host/port' are
        specifies it returns all the replica agreements under the replica
        (replicaRoot is 'suffix').

        :param suffix: The suffix targeted by the total update
        :type suffix: str
        :param consumer_host: Hostname of the consumer
        :type consumer_host: str
        :param consumer_port: Port of the consumer
        :type consumer_port: int
        :param agmtdn: DN of the replica agreement
        :type agmtdn: str

        :returns: Search result of the replica agreements
        :raises: - InvalidArgument - if missing mandatory argument
                    (agmtdn or suffix, then host and port)
                  - ValueError - if some properties are not valid
                  - NoSuchEntryError - If no replica defined for the suffix
        """

        if not suffix and not agmtdn:
            raise InvalidArgumentError("suffix or agmtdn are required")

        if (consumer_host and not consumer_port) or (not consumer_host and
                                                     consumer_port):
            raise InvalidArgumentError(
                "consumer host/port are required together")

        if agmtdn:
            # easy case, just return the RA
            filt = "objectclass=*"
            return self.conn.search_s(agmtdn, ldap.SCOPE_BASE, filt)
        else:
            # Retrieve the replica
            replica_entries = self.conn.replica.list(suffix)
            if not replica_entries:
                raise NoSuchEntryError("Error: no replica set up for suffix "
                                       "(%s)" % suffix)
            replica_entry = replica_entries[0]

            # Now returns the replica agreement for that suffix that replicates
            # to consumer host/port
            if consumer_host and consumer_port:
                '''
                Currently python does not like long continuous lines when it
                comes to string formatting, so we need to separate each line
                like this.
                '''
                filt = "(&(|(objectclass=%s)" % RA_OBJECTCLASS_VALUE
                filt += "(objectclass=%s))" % RA_WINDOWS_OBJECTCLASS_VALUE
                filt += "(%s=%s)" % (RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_HOST],
                                     consumer_host)
                filt += "(%s=%d))" % (
                    RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_PORT], consumer_port)
            else:
                filt = ("(|(objectclass=%s)(objectclass=%s))" %
                        (RA_OBJECTCLASS_VALUE, RA_WINDOWS_OBJECTCLASS_VALUE))

            return self.conn.search_s(replica_entry.dn, ldap.SCOPE_ONELEVEL,
                                      filt)

    def create(self, suffix=None, host=None, port=None, properties=None,
               winsync=False):
        """Create (and return) a replication agreement from self to consumer.
        Self is the supplier.

        :param suffix: Replication Root
        :type suffix: str
        :param host: Consumer host
        :type host: str
        :param port: Consumer port
        :type port: int
        :param winsync: Identifies the agree as a WinSync agreement
        :type winsync: bool
        :param properties: Agreement properties
        :type properties: dict

        :returns: DN of the created agreement
        :raises: - InvalidArgumentError - If the suffix is missing
                 - NoSuchEntryError - if a replica doesn't exist for that suffix
                 - ldap.LDAPError - ldap error
        """

        # Check we have a suffix [ mandatory ]
        if not suffix:
            self.log.warning("create: suffix is missing")
            raise InvalidArgumentError('suffix is mandatory')

        if not properties:
            properties = {}

        # Compute the normalized suffix to be set in RA entry
        properties[RA_SUFFIX] = normalizeDN(suffix)

        # Adding agreement under the replica entry
        replica_entries = self.conn.replica.list(suffix)
        if not replica_entries:
            raise NoSuchEntryError(
                "Error: no replica set up for suffix: %s" % suffix)
        replica = replica_entries[0]

        # Define agreement entry
        if RA_NAME not in properties:
            properties[RA_NAME] = 'meTo_%s:%s' % (host, port)
        dn_agreement = ','.join(["cn=%s" % properties[RA_NAME], replica.dn])

        # Set the required properties(if not already set)
        if RA_BINDDN not in properties:
            properties[RA_BINDDN] = defaultProperties[REPLICATION_BIND_DN]
        if RA_BINDPW not in properties:
            properties[RA_BINDPW] = defaultProperties[REPLICATION_BIND_PW]
        if RA_METHOD not in properties:
            properties[RA_METHOD] = defaultProperties[REPLICATION_BIND_METHOD]
        if RA_TRANSPORT_PROT not in properties:
            properties[RA_TRANSPORT_PROT] = \
                defaultProperties[REPLICATION_TRANSPORT]
        if RA_TIMEOUT not in properties:
            properties[RA_TIMEOUT] = defaultProperties[REPLICATION_TIMEOUT]
        if RA_DESCRIPTION not in properties:
            properties[RA_DESCRIPTION] = properties[RA_NAME]
        if RA_CONSUMER_HOST not in properties:
            properties[RA_CONSUMER_HOST] = host
        if RA_CONSUMER_PORT not in properties:
            properties[RA_CONSUMER_PORT] = str(port)

        # This is probably unnecessary because
        # we can just raise ALREADY_EXISTS
        try:
            entry = self.conn.getEntry(dn_agreement, ldap.SCOPE_BASE)
            self.log.warning("Agreement already exists: %r", dn_agreement)
            return dn_agreement
        except ldap.NO_SUCH_OBJECT:
            entry = None

        # Iterate over the properties, adding them to the entry
        entry = Entry(dn_agreement)
        entry.update({'objectclass': ["top", RA_OBJECTCLASS_VALUE]})
        for prop in properties:
            entry.update({RA_PROPNAME_TO_ATTRNAME[prop]: properties[prop]})

        # we make a copy here because we cannot change
        # the passed in properties dict
        propertiescopy = {}
        if properties:
            import copy
            propertiescopy = copy.deepcopy(properties)

        # Check if this a Winsync Agreement
        if winsync:
            self.conn.setupWinSyncAgmt(propertiescopy, entry)

        try:
            self.log.debug("Adding replica agreement: [%r]", entry)
            self.conn.add_s(entry)
        except ldap.LDAPError as e:
            self.log.fatal('Failed to add replication agreement: %s', e)
            raise e

        entry = self.conn.waitForEntry(dn_agreement)
        if entry:
            # More verbose but shows what's going on
            if 'chain' in propertiescopy:
                raise NotImplementedError
                '''
                chain_args = {
                    'suffix': suffix,
                    'binddn': binddn,
                    'bindpw': bindpw
                }
                # Work on `self` aka producer
                if replica.nsds5replicatype == MASTER_TYPE:
                    self.conn.setupChainingFarm(**chain_args)
                # Work on `consumer`
                # TODO - is it really required?
                if replica.nsds5replicatype == LEAF_TYPE:
                    chain_args.update({
                        'isIntermediate': 0,
                        'urls': self.conn.toLDAPURL(),
                        'args': propertiescopy['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)
                elif replica.nsds5replicatype == HUB_TYPE:
                    chain_args.update({
                        'isIntermediate': 1,
                        'urls': self.conn.toLDAPURL(),
                        'args': propertiescopy['chainargs']
                    })
                    consumer.setupConsumerChainOnUpdate(**chain_args)
                '''
        return dn_agreement

    def delete(self, suffix=None, consumer_host=None, consumer_port=None,
               agmtdn=None):
        """Delete a replication agreement

        :param suffix: The suffix that the agreement is configured for
        :type suffix: str
        :param consumer_host: Host of the server that the agreement points to
        :type consumer_host: str
        :param consumer_port: Port of the server that the agreement points to
        :type consumer_port: int
        :param agmtdn: DN of the replica agreement
        :type agmtdn: str

        :returns: None
        :raises: - ldap.LDAPError - for ldap operation failures
                  - TypeError - if too many agreements were found
                  - NoSuchEntryError - if no agreements were found
        """

        if not (suffix and consumer_host and consumer_port) and not agmtdn:
            raise InvalidArgumentError("Suffix with consumer_host and consumer_port" +
                                       " or agmtdn are required")

        agmts = self.list(suffix, consumer_host, consumer_port, agmtdn)

        if agmts:
            if len(agmts) > 1:
                raise TypeError('Too many agreements were found')
            else:
                # Delete the agreement
                try:
                    agmt_dn = agmts[0].dn
                    self.conn.delete_s(agmt_dn)
                    self.log.info('Agreement (%s) was successfully removed', agmt_dn)
                except ldap.LDAPError as e:
                    self.log.error('Failed to delete agreement (%s), '
                                   'error: %s', agmt_dn, e)
                    raise
        else:
            raise NoSuchEntryError('No agreements were found')

    def init(self, suffix=None, consumer_host=None, consumer_port=None):
        """Trigger a total update of the consumer replica
        - self is the supplier,
        - consumer is a DirSrv object (consumer can be a master)
        - cn_format - use this string to format the agreement name

        :param suffix: The suffix targeted by the total update [mandatory]
        :type suffix: str
        :param consumer_host: Hostname of the consumer [mandatory]
        :type consumer_host: str
        :param consumer_port: Port of the consumer [mandatory]
        :type consumer_port: int

        :returns: None
        :raises: InvalidArgument - if missing mandatory argument
        """

        #
        # check the required parameters are set
        #
        if not suffix:
            self.log.fatal("initAgreement: suffix is missing")
            raise InvalidArgumentError('suffix is mandatory argument')

        nsuffix = normalizeDN(suffix)

        if not consumer_host:
            self.log.fatal("initAgreement: host is missing")
            raise InvalidArgumentError('host is mandatory argument')

        if not consumer_port:
            self.log.fatal("initAgreement: port is missing")
            raise InvalidArgumentError('port is mandatory argument')
        #
        # check the replica agreement already exist
        #
        replica_entries = self.conn.replica.list(suffix)
        if not replica_entries:
            raise NoSuchEntryError(
                "Error: no replica set up for suffix " + suffix)
        replica_entry = replica_entries[0]
        self.log.debug("initAgreement: looking for replica agreements "
                       "under %s", replica_entry.dn)
        try:
            '''
            Currently python does not like long continuous lines when it
            comes to string formatting, so we need to separate each line
            like this.
            '''
            filt = "(&(objectclass=nsds5replicationagreement)"
            filt += "(nsds5replicahost=%s)" % consumer_host
            filt += "(nsds5replicaport=%d)" % consumer_port
            filt += "(nsds5replicaroot=%s))" % nsuffix

            entry = self.conn.getEntry(replica_entry.dn, ldap.SCOPE_ONELEVEL,
                                       filt)
        except ldap.NO_SUCH_OBJECT:
            msg = ('initAgreement: No replica agreement to ' +
                   '{host}:{port} for suffix {suffix}'.format(
                       host=consumer_host, port=consumer_port, suffix=nsuffix))
            self.log.fatal(msg)
            raise

        #
        # trigger the total init
        #
        self.log.info("Starting total init %s", entry.dn)
        mod = [(ldap.MOD_ADD, 'nsds5BeginReplicaRefresh', ensure_bytes('start'))]
        self.conn.modify_s(entry.dn, mod)

    def pause(self, agmtdn, interval=NEVER):
        """Pause this replication agreement.  This replication agreement
        will send no more changes.  Use the resume() method to "unpause".

        It tries to disable the replica agreement. If it fails (not
        implemented in all version),

        It uses the schedule() with interval '2358-2359 0'

        :param agmtdn: agreement dn
        :type agmtdn: str
        :param interval: - 'HHMM-HHMM D+' With D=[0123456]+
                          - Agreement.ALWAYS
                          - Agreement.NEVER
                          - Default is NEVER
        :type interval: str

        :returns: None
        :raises: ValueError - if interval is not valid
        """

        self.log.info("Pausing replication %s", agmtdn)
        mod = [(ldap.MOD_REPLACE, 'nsds5ReplicaEnabled', [b'off'])]
        self.conn.modify_s(ensure_str(agmtdn), mod)

        # Allow a little time for repl agmt thread to stop
        time.sleep(5)

    def resume(self, agmtdn, interval=ALWAYS):
        """Resume a paused replication agreement, paused with the "pause" method.
        It tries to enabled the replica agreement. If it fails
        (not implemented in all versions)
        It uses the schedule() with interval '0000-2359 0123456'.

        :param agmtdn: agreement dn
        :type agmtdn: str
        :param interval: - 'HHMM-HHMM D+' With D=[0123456]+
                          - Agreement.ALWAYS
                          - Agreement.NEVER
                          - Default is NEVER
        :type interval: str

        :returns: None
        :raises: - ValueError - if interval is not valid
                  - ldap.NO_SUCH_OBJECT - if agmtdn does not exist
        """

        self.log.info("Resuming replication %s", agmtdn)
        mod = [(ldap.MOD_REPLACE, 'nsds5ReplicaEnabled', [b'on'])]
        self.conn.modify_s(ensure_str(agmtdn), mod)

        # Allow a little time for the repl agmt thread to start
        time.sleep(2)

    def changes(self, agmnt_dn):
        """Get a number of changes sent by this agreement.

        :param agmtdn: agreement dn
        :type agmtdn: str

        :returns: Number of changes
        :raises: NoSuchEntryError - if agreement entry with changes
                  attribute is not found
        """

        retval = 0
        try:
            ent = self.conn.getEntry(ensure_str(agmnt_dn), ldap.SCOPE_BASE,
                                     "(objectclass=*)",
                                     [RA_PROPNAME_TO_ATTRNAME[RA_CHANGES]])
        except:
            raise NoSuchEntryError(
                "Error reading status from agreement", agmnt_dn)

        if ent.nsds5replicaChangesSentSinceStartup:
            val = ent.nsds5replicaChangesSentSinceStartup
            items = val.split(ensure_bytes(' '))
            if len(items) == 1:
                retval = int(items[0])
            else:
                for item in items:
                    ary = item.split(ensure_bytes(":"))
                    if ary and len(ary) > 1:
                        retval = retval + int(ary[1].split(ensure_bytes("/"))[0])
        return retval
