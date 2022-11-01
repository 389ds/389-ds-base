# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# A set of constants defining the lint errors we can return to a caller.
# as well as some functions to help process them.


# Database checks
DSBLE0001 = {
    'dsle': 'DSBLE0001',
    'severity': 'MEDIUM',
    'description': 'Possibly incorrect mapping tree.',
    'items': [],
    'detail': """This backend may be missing the correct mapping tree references. Mapping Trees allow
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
    'fix': """Either you need to create the mapping tree, or you need to repair the related
mapping tree. You will need to do this by hand by editing cn=config, or stopping
the instance and editing dse.ldif.
"""
}

DSBLE0002 = {
    'dsle': 'DSBLE0002',
    'severity': 'HIGH',
    'description': 'Unable to query backend.',
    'items': [],
    'detail': """Unable to query the backend.  LDAP error (ERROR)""",
    'fix': """Check the server's error and access logs for more information."""
}

DSBLE0003 = {
    'dsle': 'DSBLE0003',
    'severity': 'LOW',
    'description': 'Uninitialized backend database.',
    'items': [],
    'detail': """The backend database has not been initialized yet""",
    'fix': """You need to import an LDIF file, or create the suffix entry, in order to initialize the database."""
}

# Config checks
DSCLE0001 = {
    'dsle': 'DSCLE0001',
    'severity': 'LOW',
    'description': 'Different log timestamp format.',
    'items': ['cn=config', ],
    'detail': """nsslapd-logging-hr-timestamps-enabled changes the log format in directory server from

[07/Jun/2017:17:15:58 +1000]

to

[07/Jun/2017:17:15:58.716117312 +1000]

This actually provides a performance improvement. Additionally, this setting will be
removed in a future release.
""",
    'fix': """Set nsslapd-logging-hr-timestamps-enabled to on.
You can use 'dsconf' to set this attribute.  Here is an example:

    # dsconf slapd-YOUR_INSTANCE config replace nsslapd-logging-hr-timestamps-enabled=on"""
}

DSCLE0002 = {
    'dsle': 'DSCLE0002',
    'severity': 'HIGH',
    'description': 'Weak passwordStorageScheme.',
    'items': ['cn=config', ],
    'detail': """Password storage schemes in Directory Server define how passwords are hashed via a
one-way mathematical function for storage. Knowing the hash it is difficult to gain
the input, but knowing the input you can easily compare the hash.

Many hashes are well known for cryptograhpic verification properties, but are
designed to be *fast* to validate. This is the opposite of what we desire for password
storage. In the unlikely event of a disclosure, you want hashes to be *difficult* to
verify, as this adds a cost of work to an attacker.

In Directory Server, we offer one hash suitable for this (PBKDF2-SHA512) and one hash
for "legacy" support (SSHA512).

Your configuration does not use these for password storage or the root password storage
scheme.
""",
    'fix': """Perform a configuration reset of the values:

passwordStorageScheme
nsslapd-rootpwstoragescheme

IE, stop Directory Server, and in dse.ldif delete these two lines. When Directory Server
is started, they will use the server provided defaults that are secure.

You can also use 'dsconf' to replace these values.  Here is an example:

    # dsconf slapd-YOUR_INSTANCE config replace passwordStorageScheme=PBKDF2-SHA512 nsslapd-rootpwstoragescheme=PBKDF2-SHA512"""
}

# Security checks
DSELE0001 = {
    'dsle': 'DSELE0001',
    'severity': 'MEDIUM',
    'description': 'Weak TLS protocol version.',
    'items': ['cn=encryption,cn=config', ],
    'detail': """This Directory Server may not be using strong TLS protocol versions. TLS1.0 is known to
have a number of issues with the protocol. Please see:

https://tools.ietf.org/html/rfc7457

It is advised you set this value to the maximum possible.""",
    'fix': """There are two options for setting the TLS minimum version allowed.  You,
can set "sslVersionMin" in "cn=encryption,cn=config" to a version greater than "TLS1.0"
You can also use 'dsconf' to set this value.  Here is an example:

    # dsconf slapd-YOUR_INSTANCE security set --tls-protocol-min=TLS1.2

You must restart the Directory Server for this change to take effect.

Or, you can set the system wide crypto policy to FUTURE which will use a higher TLS
minimum version, but doing this affects the entire system:

    # update-crypto-policies --set FUTURE"""
}

# RI plugin checks
DSRILE0001 = {
    'dsle': 'DSRILE0001',
    'severity': 'LOW',
    'description': 'Referential integrity plugin may be slower.',
    'items': ['cn=referential integrity postoperation,cn=plugins,cn=config', ],
    'detail': """The referential integrity plugin has an asynchronous processing mode.
This is controlled by the update-delay flag.  When this value is 0, referential
integrity plugin processes these changes inside of the operation that modified
the entry - ie these are synchronous.

However, when this is > 0, these are performed asynchronously.

This leads to only having referint enabled on one supplier in MMR to prevent replication conflicts and loops.
Additionally, because these are performed in the background these updates may cause spurious update
delays to your server by batching changes rather than smaller updates during sync processing.

We advise that you set this value to 0, and enable referint on all suppliers as it provides a more predictable behaviour.
""",
    'fix': """Set referint-update-delay to 0.

You can use 'dsconf' to set this value.  Here is an example:

    # dsconf slapd-YOUR_INSTANCE plugin referential-integrity set --update-delay 0

You must restart the Directory Server for this change to take effect."""
}

# Note - ATTR and BACKEND are replaced by the reporting function
DSRILE0002 = {
    'dsle': 'DSRILE0002',
    'severity': 'HIGH',
    'description': 'Referential integrity plugin configured with unindexed attribute.',
    'items': ['cn=referential integrity postoperation,cn=plugins,cn=config'],
    'detail': """The referential integrity plugin is configured to use an attribute (ATTR)
that does not have an "equality" index in backend (BACKEND).
Failure to have the proper indexing will lead to unindexed searches which
cause high CPU and can significantly slow the server down.""",
    'fix': """Check the attributes set in "referint-membership-attr" to make sure they have
an index defined that has at least the equality "eq" index type.  You will
need to reindex the database after adding the missing index type. Here is an
example using dsconf:

    # dsconf slapd-YOUR_INSTANCE backend index add --attr=ATTR --reindex --index-type=eq BACKEND
"""
}

# MemberOf plugin checks
DSMOLE0001 = {
    'dsle': 'DSMOLE0001',
    'severity': 'HIGH',
    'description': 'MemberOf operations can become very slow',
    'items': ['cn=memberof plugin,cn=plugins,cn=config', ],
    'detail': """The MemberOf plugin does internal searches when updating a group, or running the fixup task.
These internal searches will be unindexed leading to poor performance and high CPU.

We advise that you index the memberOf group attributes for equality searches.
""",
    'fix': """Check the attributes set in "memberofgroupattr" to make sure they have
an index defined that has equality "eq" index type.  You will need to reindex the
database after adding the missing index type. Here is an example using dsconf:

    # dsconf slapd-YOUR_INSTANCE backend index add --attr=ATTR --index-type=eq --reindex BACKEND
"""
}

# Disk Space check.  Note - PARTITION is replaced by the calling function
DSDSLE0001 = {
    'dsle': 'DSDSLE0001',
    'severity': 'HIGH',
    'description': 'Low disk space.',
    'items': ['Server', 'cn=config'],
    'detail': """The disk partition used by the server (PARTITION), either for the database, the
configuration files, or the logs is over 90% full.  If the partition becomes
completely filled serious problems can occur with the database or the server's
stability.""",
    'fix': """Attempt to free up disk space.  Also try removing old rotated logs, or disable any
verbose logging levels that might have been set.  You might consider enabling
the "Disk Monitoring" feature in cn=config to help prevent a disorderly shutdown
of the server:

    nsslapd-disk-monitoring: on

You can use 'dsconf' to set this value.  Here is an example:

    # dsconf slapd-YOUR_INSTANCE config replace nsslapd-disk-monitoring=on

You must restart the Directory Server for this change to take effect.

Please see the Administration guide for more information:

    https://access.redhat.com/documentation/en-us/red_hat_directory_server/10/html/administration_guide/diskmonitoring
"""
}

# Replication check.   Note - AGMT and SUFFIX are replaced by the reporting function
DSREPLLE0001 = {
    'dsle': 'DSREPLLE0001',
    'severity': 'HIGH',
    'description': 'Replication agreement not set to be synchronized.',
    'items': ['Replication', 'Agreement'],
    'detail': """The replication agreement (AGMT) under "SUFFIX" is not in synchronization.""",
    'fix': """You may need to reinitialize this replication agreement.  Please check the errors
log for more information.  If you do need to reinitialize the agreement you can do so
using dsconf.  Here is an example:

    # dsconf slapd-YOUR_INSTANCE repl-agmt init "AGMT" --suffix SUFFIX"""
}

# Note - SUFFIX and COUNT will be replaced by the calling function
DSREPLLE0002 = {
    'dsle': 'DSREPLLE0002',
    'severity': 'LOW',
    'description': 'Replication conflict entries found.',
    'items': ['Replication', 'Conflict Entries'],
    'detail': "There were COUNT conflict entries found under the replication suffix \"SUFFIX\".",
    'fix': """While conflict entries are expected to occur in an MMR environment, they
should be resolved.  In regards to conflict entries there is always the original/counterpart
entry that has a normal DN, and then the conflict version of that entry.  Technically both
entries are valid, you as the administrator, needs to decide which entry you want to keep.
First examine/compare both entries to determine which one you want to keep or remove.  You
can use the CLI tool "dsconf" to resolve the conflict.  Here is an example:

    List the conflict entries:

        # dsconf slapd-YOUR_INSTANCE  repl-conflict list dc=example,dc=com

    Examine conflict entry and its counterpart entry:

        # dsconf slapd-YOUR_INSTANCE  repl-conflict compare <DN of conflict entry>

    Remove conflict entry and keep only the original/counterpart entry:

        # dsconf slapd-YOUR_INSTANCE  repl-conflict delete <DN of conflict entry>

    Replace the original/counterpart entry with the conflict entry:

        # dsconf slapd-YOUR_INSTANCE  repl-conflict swap <DN of conflict entry>
"""
}

DSREPLLE0003 = {
    'dsle': 'DSREPLLE0003',
    'severity': 'MEDIUM',
    'description': 'Unsynchronized replication agreement.',
    'items': ['Replication', 'Agreement'],
    'detail': """The replication agreement (AGMT) under "SUFFIX" is not in synchronization.
Status message: MSG""",
    'fix': """Replication is not in synchronization but it may recover.  Continue to
monitor this agreement."""
}

DSREPLLE0004 = {
    'dsle': 'DSREPLLE0004',
    'severity': 'MEDIUM',
    'description': 'Unable to get replication agreement status.',
    'items': ['Replication', 'Agreement'],
    'detail': """Failed to get the agreement status for agreement (AGMT) under "SUFFIX".  Error (ERROR).""",
    'fix': """None"""
}

DSREPLLE0005 = {
    'dsle': 'DSREPLLE0005',
    'severity': 'MEDIUM',
    'description': 'Replication consumer not reachable.',
    'items': ['Replication', 'Agreement'],
    'detail': """The replication agreement (AGMT) under "SUFFIX" is not in synchronization,
because the consumer server is not reachable.""",
    'fix': """Check if the consumer is running, and also check the errors log for more information."""
}

# Replication changelog
DSCLLE0001 = {
    'dsle': 'DSCLLE0001',
    'severity': 'LOW',
    'description': 'Changelog trimming not configured.',
    'items': ['Replication', 'Changelog',  'Backends'],
    'detail': """The replication changelog does have any kind of trimming configured.  This will
lead to the changelog size growing indefinitely.""",
    'fix': """Configure changelog trimming, preferably by setting the maximum age of a changelog
record.  Here is an example:

    # dsconf slapd-YOUR_INSTANCE replication set-changelog --suffix YOUR_SUFFIX --max-age 30d"""
}

# Certificate checks
DSCERTLE0001 = {
    'dsle': 'DSCERTLE0001',
    'severity': 'MEDIUM',
    'description': 'Certificate about to expire.',
    'items': ['Expiring Certificate'],
    'detail': """The certificate (CERT) will expire in less than 30 days""",
    'fix': """Renew the certificate before it expires to prevent disruptions with TLS connections."""
}

DSCERTLE0002 = {
    'dsle': 'DSCERTLE0002',
    'severity': 'HIGH',
    'description': 'Certificate expired.',
    'items': ['Expired Certificate'],
    'detail': """The certificate (CERT) has expired""",
    'fix': """Renew or remove the certificate."""
}

# Virtual Attrs & COS.  Note - ATTR and SUFFIX are replaced by the reporting function
DSVIRTLE0001 = {
    'dsle': 'DSVIRTLE0001',
    'severity': 'HIGH',
    'description': 'Virtual attribute indexed.',
    'items': ['Virtual Attributes'],
    'detail': """You should not index virtual attributes, and as this will break searches that
use the attribute in a filter.""",
    'fix': """Remove the index for this attribute from the backend configuration.
Here is an example using 'dsconf' to remove an index:

    # dsconf slapd-YOUR_INSTANCE backend index delete --attr ATTR SUFFIX"""
}

# File permissions (resolv.conf
DSPERMLE0001 = {
    'dsle': 'DSPERMLE0001',
    'severity': 'MEDIUM',
    'description': 'Incorrect file permissions.',
    'items': ['File Permissions'],
    'detail': """The file "FILE" does not have the expected permissions (PERMS).  This
can cause issues with replication and chaining.""",
    'fix': """Change the file permissions:

    # chmod PERMS FILE"""
}

# TLS db password/pin files
DSPERMLE0002 = {
    'dsle': 'DSPERMLE0002',
    'severity': 'HIGH',
    'description': 'Incorrect security database file permissions.',
    'items': ['File Permissions'],
    'detail': """The file "FILE" does not have the expected permissions (PERMS).  The
security database pin/password files should only be readable by Directory Server user.""",
    'fix': """Change the file permissions:

    # chmod PERMS FILE"""
}

# NsState time skew issues
DSSKEWLE0001 = {
    'dsle': 'DSSKEWLE0001',
    'severity': 'Low',
    'description': 'Medium time skew.',
    'items': ['Replication'],
    'detail': """The time skew is over 6 hours.  If this time skew continues to increase
to 24 hours then replication can potentially stop working.  Please continue to
monitor the time skew offsets for increasing values.""",
    'fix': """Monitor the time skew and avoid making changes to the system time.
Also look at https://access.redhat.com/documentation/en-us/red_hat_directory_server/11/html/administration_guide/managing_replication-troubleshooting_replication_related_problems
and find the paragraph "Too much time skew"."""
}

DSSKEWLE0002 = {
    'dsle': 'DSSKEWLE0002',
    'severity': 'Medium',
    'description': 'Major time skew.',
    'items': ['Replication'],
    'detail': """The time skew is over 12 hours.  If this time skew continues to increase
to 24 hours then replication can potentially stop working.  Please continue to
monitor the time skew offsets for increasing values.  Setting nsslapd-ignore-time-skew
to "on" on each replica will allow replication to continue, but if the time skew
continues to increase other more serious replication problems can occur.""",
    'fix': """Monitor the time skew and avoid making changes to the system time.
If you get close to 24 hours of time skew replication may stop working.
In that case configure the server to ignore the time skew until the system
times can be fixed/synchronized:

    # dsconf slapd-YOUR_INSTANCE config replace nsslapd-ignore-time-skew=on

Also look at https://access.redhat.com/documentation/en-us/red_hat_directory_server/11/html/administration_guide/managing_replication-troubleshooting_replication_related_problems
and find the paragraph "Too much time skew"."""
}

DSSKEWLE0003 = {
    'dsle': 'DSSKEWLE0003',
    'severity': 'High',
    'description': 'Extensive time skew.',
    'items': ['Replication'],
    'detail': """The time skew is over 24 hours.  Setting nsslapd-ignore-time-skew
to "on" on each replica will allow replication to continue, but if the
time skew continues to increase other serious replication problems can
occur.""",
    'fix': """Avoid making changes to the system time, and make sure the clocks
on all the replicas are correct.  If you haven't set the server's
"ignore time skew" setting then do the following on all the replicas
until the time issues have been resolved:

    # dsconf slapd-YOUR_INSTANCE config replace nsslapd-ignore-time-skew=on

Also look at https://access.redhat.com/documentation/en-us/red_hat_directory_server/11/html/administration_guide/managing_replication-troubleshooting_replication_related_problems
and find the paragraph "Too much time skew"."""
}

DSLOGNOTES0001 = {
    'dsle': 'DSLOGNOTES0001',
    'severity': 'Medium',
    'description': 'Unindexed Search',
    'items': ['Performance'],
    'detail': """Found NUMBER fully unindexed searches in the current access log.
Unindexed searches can cause high CPU and slow down the entire server's performance.\n""",
    'fix': """Examine the searches that are unindexed, and either properly index the attributes
in the filter, increase the nsslapd-idlistscanlimit, or stop using that filter."""
}

DSLOGNOTES0002 = {
    'dsle': 'DSLOGNOTES0002',
    'severity': 'Medium',
    'description': 'Unknown Attribute In Filter',
    'items': ['Possible Performance Impact'],
    'detail': """Found NUMBER searches in the current access log that are using an
unknown attribute in the search filter.\n""",
    'fix': """Stop using this these unknown attributes in the filter, or add the schema
to the server and make sure it's properly indexed."""
}
