// General menu

// Entry menu
export const ENTRY_MENU = {
  search: 'search',
  edit: 'edit',
  new: 'new',
  rename: 'rename',
  lockRole: 'lockRole',
  lockAccount: 'lockAccount',
  unlockRole: 'unlockRole',
  unlockAccount: 'unlockAccount',
  acis: 'acis',
  cos: 'cos',
  delete: 'delete',
  refresh: 'refresh'
};

// Time limit for an LDAP search request.
export const LDAP_PING_TIME_LIMIT = 5; // In seconds.

// Minimum number of items per page in a Pagination.
export const MIN_PER_PAGE = 10;

// Binary attributes.
export const BINARY_ATTRIBUTES = [
  'jpegphoto',
  'usercertificate',
  'usercertificate;binary',
  'cacertificate',
  'cacertificate;binary',
  'nssymmetrickey'
];

// Max size allowed for the Cockpit Websocket:
// https://github.com/cockpit-project/cockpit/blob/dee6324d037f3b8961d1b38960b4226c7e473abf/src/websocket/websocketconnection.c#L154
// export const WEB_SOCKET_MAX_PAYLOAD = 128 * 1024; // 128 KB.
export const WEB_SOCKET_MAX_PAYLOAD = 1280 * 1024;

// export const ENCODED_VAL_SEP = 'BASE_64_ENCODED_VALUE=';

// LDIF formatting.
// https://access.redhat.com/documentation/en-us/red_hat_directory_server/11/html/administration_guide/ldif_file_format-continuing_lines_in_ldif
export const LDIF_MAX_CHAR_PER_LINE = 78;

// LDAP operations.
export const LDAP_OPERATIONS = {
  add: 'add',
  delete: 'delete',
  replace: 'replace'
}

// Entry type.
export const ENTRY_TYPE = {
  user: 'USER',
  ou: 'OU',
  other: 'OTHER'
}

// Attributes - User.
export const INET_ORG_ATTRS = [
  'audio', 'businessCategory', 'carLicense', 'departmentNumber', 'displayName', 'employeeNumber',
  'employeeType', 'givenName', 'homePhone', 'homePostalAddress', 'initials', 'jpegPhoto', 'labeledURI',
  'mail', 'manager', 'mobile', 'o', 'pager', 'photo', 'roomNumber', 'secretary', 'uid', 'userCertificate',
  'x500UniqueIdentifier', 'preferredLanguage', 'userSMIMECertificate', 'userPKCS12'
];
export const ORG_PERSON_ATTRS = [
  'title', 'x121Address', 'registeredAddress', 'destinationIndicator', 'preferredDeliveryMethod', 'telexNumber',
  'teletexTerminalIdentifier', 'internationalISDNNumber', 'facsimileTelephoneNumber',
  'street', 'postOfficeBox', 'postalCode', 'postalAddress', 'physicalDeliveryOfficeName', 'ou', 'st', 'l'
];
export const PERSON_REQ_ATTRS = ['cn', 'sn'];
export const PERSON_OPT_ATTRS = [
  'userPassword', 'telephoneNumber', 'seeAlso', 'description'
];
export const SINGLE_VALUED_ATTRS = ['preferredDeliveryMethod', 'displayName',
  'employeeNumber', 'preferredLanguage'];

// Attributes - Organizational Unit.
export const OU_REQ_ATTRS = ['ou'];

export const OU_OPT_ATTRS = [
  'businessCategory', 'description', 'destinationIndicator',
  'facsimileTelephoneNumber', 'internationalISDNNumber', 'l',
  'physicalDeliveryOfficeName', 'postalAddress', 'postalCode',
  'postOfficeBox', 'preferredDeliveryMethod',
  'registeredAddress', 'searchGuide', 'seeAlso', 'st', 'street',
  'telephoneNumber', 'teletexTerminalIdentifier',
  'telexNumber', 'userPassword', 'x121Address'
];
