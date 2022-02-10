// LDAP options.
const LdapOptions = {
  sizeLimit: 1000,
  timeLimit: 5
};
// Size limit.
export function setSizeLimit (limit) {
  LdapOptions.sizeLimit = limit;
  console.log(`LdapOptions.sizeLimit = ${LdapOptions.sizeLimit}`);
};

export function getSizeLimit () {
  return LdapOptions.sizeLimit;
}

// Time limit.
export function setTimeLimit (limit) {
  LdapOptions.timeLimit = limit;
  console.log(`LdapOptions.timeLimit = ${LdapOptions.timeLimit}`);
};

export function getTimeLimit () {
  return LdapOptions.timeLimit;
}
