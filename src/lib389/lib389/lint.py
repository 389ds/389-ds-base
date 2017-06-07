# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# A set of constants defining the lint errors we can return to a caller.
# as well as some functions to help process them.


DSBLE0001 = {
    'dsle': 'DSBLE0001',
    'severity': 'MEDIUM',
    'items' : [],
    'detail' : """
This backend may be missing the correct mapping tree references. Mapping Trees allow
the directory server to determine which backend an operation is routed to in the
abscence of other information. This is extremely important for correct functioning
of LDAP ADD for example.

A correct Mapping tree for this backend must contain the suffix name, the database name
and be a backend type. IE:

cn=o3Dexample,cn=mapping tree,cn=config
cn: o=example
nsslapd-backend: userRoot
nsslapd-state: backend
objectClass: top
objectClass: extensibleObject
objectClass: nsMappingTree

    """,
    'fix' : """
Either you need to create the mapping tree, or you need to repair the related
mapping tree. You will need to do this by hand by editing cn=config, or stopping
the instance and editing dse.ldif.
    """
}

DSCLE0001 = {
    'dsle' : 'DSCLE0001',
    'severity' : 'LOW',
    'items': ['cn=config', ],
    'detail' : """
nsslapd-logging-hr-timestamps-enabled changes the log format in directory server from

[07/Jun/2017:17:15:58 +1000]

to

[07/Jun/2017:17:15:58.716117312 +1000]

This actually provides a performance improvement. Additionally, this setting will be
removed in a future release.
    """,
    'fix' : """
Set nsslapd-logging-hr-timestamps-enabled to on.
    """
}

DSCLE0002 = {
    'dsle': 'DSCLE0002',
    'severity': 'HIGH',
    'items' : ['cn=config', ],
    'detail' : """
Password storage schemes in Directory Server define how passwords are hashed via a
one-way mathematical function for storage. Knowing the hash it is difficult to gain
the input, but knowing the input you can easily compare the hash.

Many hashes are well known for cryptograhpic verification properties, but are
designed to be *fast* to validate. This is the opposite of what we desire for password
storage. In the unlikely event of a disclosure, you want hashes to be *difficult* to
verify, as this adds a cost of work to an attacker.

In Directory Server, we offer one hash suitable for this (PBKDF2_SHA256) and one hash
for "legacy" support (SSHA512).

Your configuration does not use these for password storage or the root password storage
scheme.
    """,
    'fix': """
Perform a configuration reset of the values:

passwordStorageScheme
nsslapd-rootpwstoragescheme

IE, stop Directory Server, and in dse.ldif delete these two lines. When Directory Server
is started, they will use the server provided defaults that are secure.
    """
}

DSELE0001 = {
    'dsle': 'DSELE0001',
    'severity': 'MEDIUM',
    'items' : ['cn=encryption,cn=config', ],
    'detail': """
This Directory Server may not be using strong TLS protocol versions. TLS1.0 is known to
have a number of issues with the protocol. Please see:

https://tools.ietf.org/html/rfc7457

It is advised you set this value to the maximum possible.
    """,
    'fix' : """
set cn=encryption,cn=config sslVersionMin to a version greater than TLS1.0
    """
}


