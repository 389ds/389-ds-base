// LDAP options.
const LdapOptions = {
    sizeLimit: 2000,
    timeLimit: 10
};

// Size limit.
export function setSizeLimit (limit) {
    LdapOptions.sizeLimit = limit;
};

export function getSizeLimit () {
    return LdapOptions.sizeLimit;
}

// Time limit.
export function setTimeLimit (limit) {
    LdapOptions.timeLimit = limit;
};

export function getTimeLimit () {
    return LdapOptions.timeLimit;
}
