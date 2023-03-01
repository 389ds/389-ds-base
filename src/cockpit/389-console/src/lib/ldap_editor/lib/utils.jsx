
import cockpit from 'cockpit';
import React from 'react';
import {
    Label,
} from '@patternfly/react-core';
import {
    InfoCircleIcon,
} from '@patternfly/react-icons';
import {
  getTimeLimit,
  getSizeLimit
} from './options.jsx';
import {
  LDAP_PING_TIME_LIMIT,
  LDIF_MAX_CHAR_PER_LINE
} from './constants.jsx';
import { log_cmd } from "../../tools.jsx";

export function generateUniqueId () {
  return Math.random().toString(36).substring(2, 15);
}

// Convert DS timestamp to a friendly string: 20180921142257Z -> 10/21/2018, 2:22:57 PM
function getDateString (timestamp) {
  if (!!timestamp === false) {
    console.log('Not a real timestamp!')
    return '';
  }

  const year = timestamp.substring(0, 4);
  const month = timestamp.substring(4, 6);
  const day = timestamp.substring(6, 8);
  const hour = timestamp.substring(8, 10);
  const minute = timestamp.substring(10, 12);
  const sec = timestamp.substring(12, 14);

  const value = `${year}-${month}-${day}T${hour}:${minute}:${sec}Z`;
  const myDate = new Date(value);
  return myDate.toLocaleString();
}

function getModDateUTC (modDate) {
  if (!modDate) {
    // Some entries ( for instance "cn=plugins,cn=config" )
    // don't have the modifyTimestamp attribute present.
    // console.log('Not a real modifyTimestamp value!')
    return '';
  }
  const y = modDate.substring(0, 4);
  const m = modDate.substring(4, 6);
  const d = modDate.substring(6, 8);
  const h = modDate.substring(8, 10);
  const min = modDate.substring(10, 12);
  const sec = modDate.substring(12, 14);
  const value = `${y}-${m}-${d}T${h}:${min}:${sec}Z`;
  // const date = new Date();
  // date.setTime(Date.parse(value));

  const date = new Date(value);
  return date.toUTCString();
}

export function getUserSuffixes (serverId, suffixCallback) {
  const suffixCmd = [
    'dsconf',
    '-j',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    'backend',
    'suffix',
    'list',
    '--suffix'
  ];
  log_cmd("getUserSuffixes", "list suffixes", suffixCmd);
  cockpit
    .spawn(suffixCmd, { superuser: true, err: "message" })
    .done(content => {
      const suffList = JSON.parse(content);
      suffixCallback(suffList.items)
    })
    .fail(_ => {
      console.log('FAIL ' + _.toString());
    });
}

export function ldapPing (serverId, pingCallback) {
  const cmd = [
    'ldapsearch',
    '-LLL',
    '-Y',
    'EXTERNAL',
    '-b',
    'cn=config', // params.baseDn,
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    '-s',
    'base',
    '-l',
    LDAP_PING_TIME_LIMIT,
    '1.1'
  ];

  log_cmd("ldapPing", "", cmd);
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(() => {
      pingCallback(true);
    })
    .fail(err => {
      console.log('FAIL err.exit_status ==> ' + err.exit_status);
      console.log('FAIL err.message ==> ' + err.message);
      pingCallback(false,
        {
          errCode: err.exit_status,
          errMsg: err.message
        });
    });
}

export function getRootSuffixEntryDetails (params, entryDetailsCallback) {
  // This is a base scope search. No need for a size limit.
  const timeLimit = getTimeLimit();

  const cmd = [
    'ldapsearch',
    '-LLL',
    '-o',
    'ldif-wrap=no',
    '-Y',
    'EXTERNAL',
    '-b',
    params.baseDn,
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + params.serverId + '.socket',
    '-s',
    'base',
    '-l',
    timeLimit,
    '*',
    'numSubordinates',
    'modifyTimestamp'
    // 'entryId' // To uniquely identify the node in the treee view ==> <instanceId>_<entryId>
    // Doesn't work!! Some entries don't have an entryID (cn=config and its children for instance).
  ];

  log_cmd("getRootSuffixEntryDetails", "", cmd);

  let dn = null; // The Root DSE DN is an empty one.
  let numSubordinates = '';
  let modifyTimestamp = '';
  // let entryId = '';
  let result = {};
  let entryArray = []; // Will contain the entry but the numSubordinates and modifyTimestamp.
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      // console.log('typeof data = ' + typeof data);
      // console.log('SUCCESS ' + data);
      const lines = data.split('\n');
      lines.map(currentLine => {
        if (isAttributeLine(currentLine, 'dn:')) {
          entryArray.push(splitAttributeValue(currentLine));
          // console.log('currentLine = ' + currentLine);
          // console.log('currentLine.split()[0]  = ' + currentLine.split(':')[0]);
          // console.log('currentLine.split()[1]  = ' + currentLine.split(':')[1]);
          // Convert base64-encoded DNs

          const pos = currentLine.indexOf(':');
          if (currentLine.startsWith('dn::')) {
            dn = b64DecodeUnicode(currentLine.substring(pos + 2).trim());
          } else {
            dn = currentLine.substring(pos + 1).trim();
          }
          // console.log('The DN is ' + dn);
        } else if (isAttributeLine(currentLine, 'numSubordinates:')) {
          numSubordinates = (currentLine.split(':')[1]).trim()
          // console.log('numSubordinates = ' + numSubordinates);
        } else if (isAttributeLine(currentLine, 'modifyTimestamp:')) {
          modifyTimestamp = (currentLine.split(':')[1]).trim()
        } else {
          entryArray.push(splitAttributeValue(currentLine));
        }
      });
      result = JSON.stringify(
        {
          dn: dn,
          numSubordinates: numSubordinates,
          modifyTimestamp: getModDateUTC(modifyTimestamp),
          parentId: params.parentId,
          fullEntry: entryArray,
          errorCode: 0
        });
    })
    .fail(err => {
      // console.log('FAIL err.exit_status ==> ' + err.exit_status);
      // console.log('FAIL err.message ==> ' + err.message);
      result = JSON.stringify(
        {
          dn: params.baseDn,
          numSubordinates: '???',
          modifyTimestamp: '???',
          // entryId: (Math.floor(Math.random() * Math.floor(1000000))).toString(), // Choose a random number between 0 and 1 million.
          parentId: params.parentId,
          fullEntry: [{ attribute: '???: ', value: '???' }],
          errorCode: err.exit_status
        });
    })
    .always(() => entryDetailsCallback(result));
}

function getResourceLimits () {
  const timeLimit = getTimeLimit();
  const sizeLimit = getSizeLimit();
  let limits = [];
  if (timeLimit > 0) {
    limits.push('-l', timeLimit);
  }
  if (sizeLimit > 0) {
    limits.push('-z', sizeLimit);
  }

  return limits;
}

export function getSearchEntries (params, resultCallback) {
  /*
     params.serverId,
     params.searchBase
     params.searchFilter
     params.searchScope
     params.sizeLimit,
     params.timeLimit
  */
  const cmd = [
    'ldapsearch',
    '-LLL',
    '-o',
    'ldif-wrap=no',
    '-Y',
    'EXTERNAL',
    '-b',
    params.searchBase,
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + params.serverId + '.socket',
    '-s',
    params.searchScope,
    '-l',
    params.timeLimit,
    '-z',
    params.sizeLimit,
    params.searchFilter,
    '*',
    '+'
  ];

  log_cmd("getSearchEntries", "", cmd);
  let dn = '';
  let numSubordinates = '0';
  let modifyTimestamp = '';
  let searchResult = null;
  let allEntries = [];
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' }) // string.split("\n\r")
    .done(data => {
      searchResult = data;
    })
    .catch((err, data) => {
      if (err.exit_status === 4) {
        console.log('Size limit hit'); // Use the partial data.
        searchResult = data;
        params.addNotification(
            "info",
            `Size limit of ${params.sizeLimit} was exceeded.  The child entries of "${params.searchBase}" have been truncated.`
        );
      } else {
        searchResult = null;
        resultCallback(null, { status: err.exit_status, msg: err.message });
      }
    })
    .finally(() => {
      if (searchResult === null) {
        return;
      }
      const lines = searchResult.split('\n');
      let ldapsubentry = false;
      let isRole = false;
      let isLockable = false;
      lines.map(currentLine => {
        const accountObjectclasses = ['nsaccount', 'nsperson', 'simplesecurityobject',
                                      'organization', 'person', 'account', 'organizationalunit',
                                      'netscapeserver', 'domain', 'posixaccount', 'shadowaccount',
                                      'posixgroup', 'mailrecipient', 'nsroledefinition'];
        if (isAttributeLine(currentLine, 'dn:')) {
          // Convert base64-encoded DNs
          const pos = currentLine.indexOf(':');
          if (currentLine.startsWith('dn::')) {
            dn = b64DecodeUnicode(currentLine.substring(pos + 2).trim());
          } else {
            dn = currentLine.substring(pos + 1).trim();
          }
          ldapsubentry = false;
          isRole = false;
          isLockable = false;
        } else if (isAttributeLine(currentLine, 'numSubordinates:')) {
          numSubordinates = (currentLine.split(':')[1]).trim()
        } else if (isAttributeLine(currentLine, 'modifyTimestamp:')) {
          modifyTimestamp = (currentLine.split(':')[1]).trim()
        } else if (isAttributeLine(currentLine, 'objectclass: ldapsubentry')) {
          ldapsubentry = true;
        } else if (isAttributeLine(currentLine, 'objectclass: nsroledefinition')) {
          isRole = true;
        }
        for (const accountOC of accountObjectclasses) {
          if (isAttributeLine(currentLine, `objectclass: ${accountOC}`)) {
              isLockable = true;
          }
        }

        if (currentLine === '' && dn !== '') {
          const result = JSON.stringify(
            {
              dn: dn,
              numSubordinates: numSubordinates,
              modifyTimestamp: getModDateUTC(modifyTimestamp),
              ldapsubentry: ldapsubentry,
              isRole: isRole,
              isLockable: isLockable,
            });
          allEntries.push(result);

          // Reset the variables:
          dn = '';
          numSubordinates = '0';
          modifyTimestamp = '';
        }
      });
      // Process the list of entries.
      resultCallback(allEntries, null);
    });
}

export function getBaseLevelEntryAttributes (serverId, baseDn, entryAttributesCallback) {
  /* const cmd = [
    'ldapsearch',
    '-LLL',
    '-o',
    'ldif-wrap=no',
    '-Y',
    'EXTERNAL',
    '-b',
    baseDn,
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    '-s',
    'base',
    '(|(objectClass=*)(objectClass=ldapSubEntry))',
    '*'
  ]; */

  // This is a base scope search. No need for a size limit.
  const timeLimit = getTimeLimit();
  const optionTimeLimit = timeLimit > 0 ? `-l ${timeLimit}` : '';

  const cmd = [
    '/usr/bin/sh',
    '-c',
    `ldapsearch -LLL -o ldif-wrap=no -Y EXTERNAL -b "${baseDn}"` +
    ` -H ldapi://%2fvar%2frun%2fslapd-${serverId}.socket` +
    ` ${optionTimeLimit}` +
    ' -s base "(|(objectClass=*)(objectClass=ldapSubEntry))" nsRoleDN nsAccountLock \\*' // +
    // ' | /usr/bin/head -c 150001' // Taking 1 additional character to check if the
    // the entry was indeed bigger than 150K.
  ];

  // TODO: The return code will always be 0 because of the ' | /usr/bin/head -c 150001' part.
  // Need to find a way to retrieve the LDAP return code...
  /*
    [root@cette ~]# ldapsearch -LLL -o ldif-wrap=no -Y EXTERNAL -b "o=empty" -H ldapi://%2fvar%2frun%2fslapd-ALPS_Grenoble.socket -s base "(|(objectClass=*)(objectClass=ldapSubEntry))" \* | /usr/bin/head -c 150001 2>/dev/null
    SASL/EXTERNAL authentication started
    SASL username: gidNumber=0+uidNumber=0,cn=peercred,cn=external,cn=auth
    SASL SSF: 0
    No such object (32)
    [root@cette ~]# echo $?
    0
    [root@cette ~]#
  */

  log_cmd("getBaseLevelEntryAttributes", "", cmd);
  let entryArray = [];
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      // TODO: Make this configurable ( option to keep X number of characters )
      const lines = data.split('\n');
      lines.map(currentLine => {
        if (currentLine !== '') {
          if (currentLine.length < 1000 || currentLine.substring(0, 9).toLowerCase().startsWith("jpegphoto")) {
            entryArray.push(splitAttributeValue(currentLine));
          } else {
            const myTruncatedValue = (<div name="truncated" attr={splitAttributeValue(currentLine).attribute}>
                                          <Label icon={<InfoCircleIcon />} color="blue" >
                                              Value is too large to display
                                          </Label>
                                      </div>);
            entryArray.push(myTruncatedValue);
          }
        }
      });
      entryAttributesCallback(entryArray);
    })
    .catch(err => {
      // console.log('getBaseLevelEntryAttributes in catch()');
      if (err.message === undefined) { // No error. Send the result array.
        // entryAttributesCallback(entryArray);
      } else { /* if (msg.includes("Can't contact LDAP server")) {
        // TODO: Handle the LDAP down status with a specific message? */
        entryAttributesCallback([{
          attribute: '???: ',
          value: '???',
          // errMsg: err.message
          errorCode: err.exit_status
        }]);
        console.log('getBaseLevelEntryAttributes failed with exit_status = ' + err.exit_status);
        console.log('getBaseLevelEntryAttributes error message = ' + err.message);
      }
    });
}

// Called to get an LDAP backup of the entry during a deletion.
// TODO: Use getBaseLevelEntryAttributes() with an additonal option to retrieve operational attributes.
export function getBaseLevelEntryFullAttributes (serverId, baseDn, entryAttributesCallback) {
  // This is a base scope search. No need for a size limit.
  const timeLimit = getTimeLimit();

  const cmd = [
    'ldapsearch',
    '-LLL',
    '-o',
    'ldif-wrap=no',
    '-Y',
    'EXTERNAL',
    '-b',
    baseDn,
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    '-s',
    'base',
    '-l',
    timeLimit,
    '(|(objectClass=*)(objectClass=ldapSubEntry))',
    '*',
    '+'
  ];

  log_cmd("getBaseLevelEntryFullAttributes", "", cmd);
  let entryArray = [];
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      /* const lines = data.split('\n');
      lines.map(currentLine => {
        if (currentLine !== '') {
          entryArray.push(splitAttributeValue(currentLine));
        }
      }); */
      entryAttributesCallback(data);
    })
    .fail(err => {
      console.log('getBaseLevelEntryFullAttributes failed with exit_status = ' + err.exit_status);
      console.log('getBaseLevelEntryFullAttributes error message = ' + err.message);
      entryAttributesCallback('');
    });
}

function isAttributeLine (line, attr) {
  return (line.toLowerCase().startsWith(attr.toLowerCase()));
}

// TODO: Handle base64-encoded attributes where the separator is '::'
function splitAttributeValue (attrVal) {
  const index = attrVal.indexOf(':');
  if (index === -1) {
    // console.log('No colon found in attrVal = ' + attrVal);
    return { attribute: '', value: '' };
  }
  const attr = attrVal.substring(0, index);
  const val = attrVal.substring(index);
  return { attribute: attr, value: val };
}

// TODO: Optimize this function to return only a subset of the entry.
// Then on user expansion, search ( base level ) and return the whole entry.
// ==> Done.
export function getOneLevelEntries (params, oneLevelCallback) {
  const limits = getResourceLimits();

  const filter = params.filter
    ? params.filter
    : '(|(objectClass=*)(objectClass=ldapSubEntry))';

  const cmd = [
    'ldapsearch',
    '-LLL',
    '-o',
    'ldif-wrap=no',
    '-Y',
    'EXTERNAL',
    '-b',
    params.baseDn,
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + params.serverId + '.socket',
    '-s',
    'one',
    filter,
    /* '-l',
    timeLimit,
    '-z',
    sizeLimit, */
    ...limits,
    '1.1',
    'numSubordinates',
    'modifyTimestamp',
    'objectclass'
  ];

  log_cmd("getOneLevelEntries", "", cmd);
  let dn = '';
  let numSubordinates = '';
  let modifyTimestamp = '';
  let searchResult = null;
  let allEntries = [];
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' }) // string.split("\n\r")
    .done(data => {
      searchResult = data;
    })
    .catch((err, data) => {
      console.log('FAIL err.exit_status ==> ' + err.exit_status);
      console.log('FAIL err.message ==> ' + err.message);
      if (err.exit_status === 4) {
        // Use the partial data.
        searchResult = data;
        const size_limit = getSizeLimit();
        params.addNotification(
            "info",
            `Size limit of ${size_limit} was exceeded.  The child entries of "${params.baseDn}" have been truncated.  ` +
            `Use the "Search" feature if you want to adjust the size limit and retrieve more entries`
        );
      } else {
        searchResult = null;
        oneLevelCallback(null, params, { status: err.exit_status, msg: err.message });
      }
    })
    .finally(() => {
      if (searchResult === null) {
        return;
      }
      const lines = searchResult.split('\n');
      let ldapsubentry = false;
      let isRole = false;
      let isLockable = false;
      lines.map(currentLine => {
        const accountObjectclasses = ['nsaccount', 'nsperson', 'simplesecurityobject',
                                      'organization', 'person', 'account', 'organizationalunit',
                                      'netscapeserver', 'domain', 'posixaccount', 'shadowaccount',
                                      'posixgroup', 'mailrecipient', 'nsroledefinition'];
        if (isAttributeLine(currentLine, 'dn:')) {
          // Convert base64-encoded DNs
          const pos = currentLine.indexOf(':');
          if (currentLine.startsWith('dn::')) {
            dn = b64DecodeUnicode(currentLine.substring(pos + 2).trim());
          } else {
            dn = currentLine.substring(pos + 1).trim();
          }
          ldapsubentry = false;
          isRole = false;
          isLockable = false;
        } else if (isAttributeLine(currentLine, 'numSubordinates:')) {
          numSubordinates = (currentLine.split(':')[1]).trim()
        } else if (isAttributeLine(currentLine, 'modifyTimestamp:')) {
          modifyTimestamp = (currentLine.split(':')[1]).trim()
        } else if (isAttributeLine(currentLine, 'objectclass: ldapsubentry')) {
          ldapsubentry = true;
        } else if (isAttributeLine(currentLine, 'objectclass: nsroledefinition')) {
          isRole = true;
        }
        for (const accountOC of accountObjectclasses) {
          if (isAttributeLine(currentLine, `objectclass: ${accountOC}`)) {
              isLockable = true;
          }
        }

        if (currentLine === '' && dn !== '') {
          const result = JSON.stringify(
            {
              dn: dn,
              numSubordinates: numSubordinates,
              modifyTimestamp: getModDateUTC(modifyTimestamp),
              ldapsubentry: ldapsubentry,
              isRole: isRole,
              isLockable: isLockable,
            });
          allEntries.push(result);

          // Reset the variables:
          dn = '';
          numSubordinates = '0';
          modifyTimestamp = '';
        }
      });
      // Process the list of entries.
      oneLevelCallback(allEntries, params, null);
    });
}

// Generic search that returns an array of LDAP entries.
// Returns an empty array in case of failure.
export function runGenericSearch (params, searchCallback) {
  /* const cmd = [
    'ldapsearch',
    '-LLL',
    '-o',
    'ldif-wrap=no',
    '-Y',
    'EXTERNAL',
    '-b',
    params.baseDn,
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + params.serverId + '.socket',
    '-s',
    params.scope,
    params.filter,
    params.attributes
  ]; */

  const cmd = [
    '/usr/bin/sh',
    '-c',
    'ldapsearch -LLL -o ldif-wrap=no -Y EXTERNAL -b "' + params.baseDn +
    '" -H ldapi://%2fvar%2frun%2fslapd-' + params.serverId + '.socket' +
    ' -s ' + params.scope +
    ' "' + params.filter + '" ' +
    params.attributes
  ];

  log_cmd("runGenericSearch", "", cmd);
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      const resulEntries = data.split('\n\n'); // Split by empty line.
      // console.log(`resulEntries = ${resulEntries}`);
      resulEntries.pop(); // Remove the last empty line.
      searchCallback(resulEntries);
      // console.log(`resulEntries after pop() = ${resulEntries}`);
    })
    .fail(err => {
      console.log('FAIL err.exit_status ==> ' + err.exit_status);
      console.log('FAIL err.message ==> ' + err.message);
      searchCallback([]);
    });
}

export function getMonitoringInfo (serverId, monitorEntryCallback) {
  const cmd = [
    'ldapsearch',
    '-LLL',
    '-o',
    'ldif-wrap=no',
    '-Y',
    'EXTERNAL',
    '-b',
    'cn=monitor',
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    '-s',
    'base',
    'version',
    'threads',
    'currentConnections',
    'totalConnections',
    'startTime'
  ];

  log_cmd("getMonitoringInfo", "", cmd);
  // TODO: Use an object
  // monitorObject = {version: version, threads: threads, ...}
  let version = '';
  let threads = '';
  let currentConnections = '';
  let totalConnections = '';
  let startTime = '';
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      // console.log('SUCCESS ' + data);
      const lines = data.split('\n');
      lines.map(currentLine => {
        if (isAttributeLine(currentLine, 'version:')) {
          version = (currentLine.split(':')[1]).trim()
        } else if (isAttributeLine(currentLine, 'threads:')) {
          threads = (currentLine.split(':')[1]).trim()
          // console.log('threads = ' + threads);
        } else if (isAttributeLine(currentLine, 'currentConnections:')) {
          currentConnections = (currentLine.split(':')[1]).trim()
          // console.log('currentConnections = ' + currentConnections);
        } else if (isAttributeLine(currentLine, 'totalConnections:')) {
          totalConnections = (currentLine.split(':')[1]).trim()
          // console.log('currentConnections = ' + currentConnections);
        } else if (isAttributeLine(currentLine, 'startTime:')) {
          startTime = (currentLine.split(':')[1]).trim()
          // console.log('startTime = ' + startTime);
        }
      });
      const result = JSON.stringify(
        {
          failed: false,
          version: version,
          threads: threads,
          currentConnections: currentConnections,
          totalConnections: totalConnections,
          startTime: getDateString(startTime)
        }
      );
      monitorEntryCallback(result);
    })
    .fail((err) => {
      console.log('FAIL err.message ==> ' + err.message);
      // const m1 = JSON.parse(err)
      // const m2 = JSON.parse(err).desc
      // console.log('m1 = ' + m1);
      // console.log('m2 = ' + m2);
      const result = JSON.stringify(
        {
          failed: true,
          errMessage: err.message
        }
      );
      monitorEntryCallback(result);
    });
}

// Compute the size of the logs.
function computeLogSize (size) {
  if (size > 0) {
    // const exponent = Math.floor(Math.log(size) / Math.log(1024));
    // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Math/log
    const value = 1 / Math.log(1024);
    const exponent = Math.floor(Math.log(size) * value);
    return (size / Math.pow(1024, exponent)).toFixed(2) * 1 +
    ' ' + ['B', 'KB', 'MB', 'GB', 'TB'][exponent];
  } else {
    return '0 B';
  }
}

export function listAccessLogs (logDirectory, logListCallback) {
// export function listAccessLogs (logDirectory) {
  const cmd = [
    '/usr/bin/sh',
    '-c',
    'cd ' + logDirectory + ' ; ' +
    'stat -c "%n %s %U %a" ' +
    './access* | grep -v access.rotationinfo'
  ];

  log_cmd("listAccessLogs", "", cmd);

  let logDataArray = [];

  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      // console.log('SUCCESS ' + data);
      const lines = data.split('\n');
      lines.map(currentLine => {
        // console.log('currentLine = ' + currentLine);
        if (currentLine.trim() !== '') {
          const info = currentLine.split(' ');
          // console.log(info[1] + ' ==> ' + computeLogSize(info[1]));
          logDataArray.push({
            name: info[0].substring(2), // Remove the './' part.
            size: computeLogSize(info[1]),
            owner: info[2],
            rights: info[3]
          })
        }
      });
    })
    .fail((err) => {
      console.log('FAIL - listAccessLogs() ==> ' + err.message);
    })
    .always(() => {
      logListCallback(logDataArray);
    });
}

// Modify ( or create ) an LDAP entry.
export function modifyLdapEntry (params, ldifArray, modifyEntryCallback) {
  const serverId = params.serverId;
  const addOption = params.isAddOp ? '-a' : '';
  let ldifData = '';
  let logLdifData = '';
  if (ldifArray.length === 0) {
    console.log('modifyLdapEntry() ==> Empty array! Nothing to do.');
    return;
  }
  for (const line of ldifArray) {
    ldifData += `${line}\n`;
    if (line.toLowerCase().startsWith("userpassword")) {
        logLdifData += `userPassword: ********\n`;
    } else {
        logLdifData += `${line}\n`;
    }
  };
  const cmd = [
    '/usr/bin/sh',
    '-c',
    `ldapmodify ${addOption} -Y EXTERNAL -H ` +
    `ldapi://%2fvar%2frun%2fslapd-${serverId}.socket `
  ];
  const cmd_copy = [
    '/usr/bin/sh',
    '-c',
    `ldapmodify ${addOption} -Y EXTERNAL -H ` +
    `ldapi://%2fvar%2frun%2fslapd-${serverId}.socket\n`,
    logLdifData  // hides userpassword value from console log
  ];

  let result = {};
  log_cmd("modifyLdapEntry", "", cmd_copy);
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .input(ldifData, true)
    .input()
    .done(data => {
      result = { errorCode: 0, output: data };
    })
    .fail((err) => {
      console.log('FAIL - modifyLdapEntry() ==> ' + err.message);
      console.log('Error code (err.exit_status) = ' + err.exit_status);
      let errMessage = '';
      for (const msg of err.message.split('\n')) {
        if (!msg.startsWith('SASL')) {
          errMessage += msg;
        }
      }
      result = { errorCode: err.exit_status, output: errMessage };
    })
    .always(() => {
      modifyEntryCallback(result);
    });
}

// Create an LDAP entry.
export function createLdapEntry (serverId, ldifArray, createEntryCallback) {
  const params = {
    serverId: serverId,
    isAddOp: true
  };

  modifyLdapEntry(params, ldifArray, createEntryCallback);
}

// Retrieve all objectClasses along with their required and optional attributes.
export function getAllObjectClasses (serverId, allOcCallback) {
  const cmd = [
    'dsconf',
    '--json',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    'schema',
    'objectclasses',
    'list'
  ];
  let result = [];
  log_cmd("getAllObjectClasses", "", cmd);
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      const myObject = JSON.parse(data);
      for (const oc of myObject.items) {
        result.push({
          name: oc.name[0], // name[0] is the attribute name in lower case.
          required: oc.must,
          optional: oc.may
        });
      }
    })
    .fail((err) => {
      console.log('FAIL - getAllObjectClasses - err.message ==> ' + err.message);
    })
    .always(() => {
      allOcCallback(result);
    });
}

// Retrieve the list of attributes that are single-valued.
export function getSingleValuedAttributes (serverId, svCallback) {
  const cmd = [
    'dsconf',
    '--json',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    'schema',
    'attributetypes',
    'list'
  ];
  let result = [];
  log_cmd("getSingleValuedAttributes", "", cmd);
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      const myObject = JSON.parse(data);
      for (const attr of myObject.items) {
        if (attr.single_value) {
          result.push(attr.name[0]); // name[0] is the attribute name in lower case.
        }
      }
    })
    .fail((err) => {
      console.log('FAIL - getAttributeList - err.message ==> ' + err.message);
    })
    .always(() => {
      svCallback(result);
    });
}

// Retrieve the names and OID of all attributes.
export function getAttributesNameAndOid (serverId, attrCallback) {
  const cmd = [
    'dsconf',
    '--json',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    'schema',
    'attributetypes',
    'list'
  ];
  let result = [];
  log_cmd("getAttributesNameAndOid", "", cmd);
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      const myObject = JSON.parse(data);
      for (const attr of myObject.items) {
        result.push([attr.names[0], attr.oid[0]]);
      }
    })
    .fail((err) => {
      console.log('FAIL - getAttributesNameAndOid - err.message ==> ' + err.message);
    })
    .always(() => {
      attrCallback(result);
    });
}

export function deleteLdapData (serverId, entryDN, numSubordinates, deleteCallback) {
  let cmd = [
    'ldapdelete',
    '-Y',
    'EXTERNAL',
    '-H',
    'ldapi://%2fvar%2frun%2fslapd-' + serverId + '.socket',
    entryDN
  ];
  if (numSubordinates > 0) {
    cmd.push('-r'); // Recursive deletion.
  }

  let result = {};
  log_cmd("deleteLdapData", "", cmd);
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      // console.log('SUCCESS - deleteLdapData() ==> ' + data);
      result = { errorCode: 0, output: data };
    })
    .fail((err) => {
      console.log('FAIL - deleteLdapData() ==> ' + err.message);
      let errMessage = '';
      for (const msg of err.message.split('\n')) {
        if (!msg.startsWith('SASL')) {
          errMessage += msg;
        }
      }
      result = { errorCode: err.exit_status, output: errMessage };
    })
    .always(() => {
      deleteCallback(result);
    });
}

export function showCertificate (certificate, showCertCallback) {
  const cmd = [
    '/usr/bin/sh',
    '-c',
    'echo ' + certificate +
    ' | base64 --decode | openssl x509 -inform DER -noout -subject -issuer -dates -serial ;' +
    // ' echo HOST_TIME_GMT=`TZ=GMT date "+%Y-%m-%d %H:%M:%S %Z"` ' // Issue with Firefox and Safari.
    ' echo HOST_TIME_GMT=`TZ=GMT date`'
  ];

  let result = {};
  let certDataArray = [];

  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    // .input(decodedCert)
    .done(data => {
      // console.log('SUCCESS - showCertificate() ==> ' + data);
      const lineArray = data.split('\n');
      let certEndTime = 0;
      let hostTime = 0;
      lineArray.map(line => {
        const pos = line.indexOf('=');
        const param = line.substring(0, pos);
        const paramVal = line.substring(pos + 1);
        if (param !== 'HOST_TIME_GMT') {
          certDataArray.push({
            param: param,
            paramVal: paramVal
          });
          if (param === 'notAfter') {
            certEndTime = Date.parse(paramVal);
          }
        } else {
          hostTime = Date.parse(paramVal);
        }
      });

      result = {
        code: 'OK',
        data: certDataArray,
        timeDifference: certEndTime - hostTime
      };
    })
    .fail((err) => {
      console.log('FAIL - showCertificate() ==> ' + err.message);
      result = { code: 'FAIL' };
    })
    .always(() => {
      showCertCallback(result);
    });
}

// From https://stackoverflow.com/questions/30106476/using-javascripts-atob-to-decode-base64-doesnt-properly-decode-utf-8-strings
export function b64DecodeUnicode (str) {
  // Going backwards: from bytestream, to percent-encoding, to original string.
  try {
      let result = decodeURIComponent(atob(str).split('').map(c => {
          return '%' + ('00' + c.charCodeAt(0).toString(16)).slice(-2);
      }).join(''));
      return result;
  } catch(e) {
      console.debug("b64DecodeUnicode failed to decode: ", str);
      return str;
  }
}

// Takes a line with an encoded attribute.
// Returns [attribute, decodedValue].
export function decodeLine (line) {
  const pos = line.indexOf('::');
  return [
    line.substring(0, pos),
    b64DecodeUnicode(line.substring(pos + 2).trim())
  ];
}

// Quick and dirty function to retrieve RDN data.
export function getRdnInfo (entryDn) {
  let myRdn = '';
  const myArray = entryDn.split(',');
  for (const datum of myArray) {
    myRdn += datum;
    if (datum.slice(-1) !== '\\') {
      // The separator "," was not escaped. We've hopefully got the RDN.
      break;
    }
    myRdn += ',';
  }
  const myRdnData = myRdn.split('=');
  return {
    rdnAttr: myRdnData[0],
    rdnVal: myRdnData[1]
  }
}

// Create the top entry LDIF data
/*
  Only a limited subset of attributes are supported to create the RDN of a top entry:
  c ==> objectclass country
  cn ==> objectclass nsContainer
  dc ==> objectclass domain
  o ==> objectclass organization
  ou ==> objectclass organizationalUnit
*/
export function generateRootEntryLdif (suffixDn, ldifCallback) {
  const rdnObj = getRdnInfo(suffixDn);
  /* let myRdn = '';
  const myArray = suffixDn.split(',');
  for (const datum of myArray) {
    myRdn += datum;
    if (datum.slice(-1) !== '\\') {
      // The separator "," was not escaped. We've got the RDN.
      break;
    }
    myRdn += ',';
  } */

  const myRdnAttr = rdnObj.rdnAttr;
  const myRdnVal = rdnObj.rdnVal;

  let myObjectClass;
  let isValid = true;
  switch (myRdnAttr) {
    case 'c':
      myObjectClass = 'country';
      break;
    case 'cn':
      myObjectClass = 'nsContainer';
      break;
    case 'dc':
      myObjectClass = 'domain';
      break;
    case 'o':
      myObjectClass = 'organization';
      break;
    case 'ou':
      myObjectClass = 'organizationalUnit';
      break;
    default:
      isValid = false;
      console.log(`The RDN attribute ${myRdnAttr} is not supported for root entry creation!`);
  }

  let ldifArray = [];
  if (isValid) {
    ldifArray.push(`dn: ${suffixDn}`);
    ldifArray.push('objectClass: top');
    ldifArray.push(`objectClass: ${myObjectClass}`);
    ldifArray.push(`${myRdnAttr}: ${myRdnVal}`);
  } else {
    ldifArray.push(`The RDN attribute "${myRdnAttr}" is not supported for root entry creation!`);
  }
  ldifCallback(ldifArray)
}

// Get all ACIs that apply to a given entry.
export function retrieveAllAcis (params, aciCallback) {
    let entryDn = params.baseDn;
    const potentialEntries = [params.baseDn];
    let resultArray = []; // An item will be like {entryDn:<DN>, aciArray:[aci:<ACI_1>,aci:<ACI_2>]}
    const myParams = {
        serverId: params.serverId,
        baseDn: entryDn,
        scope: 'base',
        filter: 'objectClass=*',
        attributes: '1.1 aci'
    };

    runGenericSearch(myParams, (result) => {
        if (result.length > 0) {
            const myAcis = result[0].split('\n'); // There should be a single result ( base scope search ).
            if (myAcis.length > 1) { // The array contains at least 1 ACI.
                myAcis.shift() // Remove the DN (first element) from the array.
                const actualAcis = myAcis.map(aciLine => {
                    const pos = aciLine.indexOf(':');
                    const theAci = aciLine.startsWith('aci:: ')
                        ? b64DecodeUnicode((aciLine.substring(pos + 2)).trim())
                        : (aciLine.substring(pos + 1)).trim();
                    return theAci;
                })
                const myBaseDn = myParams.baseDn;
                const myObj = {
                    entryDn: myBaseDn,
                    aciArray: actualAcis,
                }
                resultArray.push(myObj);
            }
        }
        aciCallback(resultArray);
    });
}

export function isValidIpAddress (ipAddress) {
    const regexIPv4 = /^(?=(?:[^.]*\.){2,3}[^.]*$)(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(?:\.(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)){1,3}(?:\.\*)?$/;
    const regexIPv6 = /(?:^|(?<=\s))(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))(?=\s|$)/;

    let result = false;

    if (ipAddress.includes(":")) {
        // IPv6
        result = ipAddress.match(regexIPv6);
    } else {
        // IPv4
        result = ipAddress.match(regexIPv4);
    }
    return result !== null;
}

export function isValidHostname (hostname) {
    const regex = /^((\*)[]|((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)|((\*\.)?([a-zA-Z0-9-]+\.){0,5}[a-zA-Z0-9-]+\.[a-zA-Z]{2,63}?))$/;
    const result = hostname.match(regex);
    return result !== null;
}

/*
// Encode binary attributes.
export function base64encode (fileName, encodingCallback) {
  const cmd = [
    '/usr/bin/base64',
    fileName
  ];
  const encodedValue = null;
  console.log('Command = ' + cmd.toString());
  cockpit
    .spawn(cmd, { superuser: true, err: 'message' })
    .done(data => {
      encodingCallback(data);
    })
    .fail((err) => {
      console.log('FAIL - base64encode - err.message ==> ' + err.message);
    })
    .always(() => {
      encodingCallback(null);
    });
}
*/

// Fold long LDIF lines and return an array
// containing lines with 78 characters maximum.
export function foldLine (line) {
  if (line.length <= LDIF_MAX_CHAR_PER_LINE) {
    return [line];
  }

  const size = Math.ceil(line.length / LDIF_MAX_CHAR_PER_LINE)
  let lineArray = new Array(size);
  // Handle the first line separately since it doesn't start with a space.
  lineArray[0] = line.substring(0, LDIF_MAX_CHAR_PER_LINE);

  for (let i = 1; i < size; i++) {
    // The continuation character is a single space.
    lineArray[i] = ' ' + line.substring(i * LDIF_MAX_CHAR_PER_LINE, (i + 1) * LDIF_MAX_CHAR_PER_LINE);
  }

  return lineArray;
}

export function isValidLDAPUrl (url) {
    if (url.startsWith("ldap:///")) {
        return true;
    }
    return false;
}

export function getBaseDNFromTree (entrydn, treeViewRootSuffixes) {
    for (const suffixObj of treeViewRootSuffixes) {
        if (entrydn.toLowerCase().indexOf(suffixObj.name.toLowerCase()) !== -1) {
            return suffixObj.name;
        }
    }
    return "";
}
