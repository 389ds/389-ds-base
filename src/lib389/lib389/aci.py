# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Aci class to help parse and create ACIs.

You will access this via the Entry Class.
"""

import ldap

# Helpers to detect common patterns in aci
def _aci_any_targetattr_ne(aci):
    """Returns True if any of the targetattr types is a != type"""

    potential = False
    if 'targetattr' in aci.acidata:
        for ta in aci.acidata['targetattr']:
            if ta['equal'] is False:
                # Got, it lets do this
                potential = True
    return potential


class Aci(object):
    """An object that helps to work with agreement entry

    :param conn: An instance
    :type conn: lib389.DirSrv
    """

    def __init__(self, conn):
        self.conn = conn
        self.log = conn.log

    def list(self, basedn, scope=ldap.SCOPE_SUBTREE):
        """List all acis in the directory server below the basedn confined by
        scope.

        :param basedn: Base DN
        :type basedn: str
        :param scope: ldap.SCOPE_SUBTREE, ldap.SCOPE_BASE,
                       ldap.SCOPE_ONELEVEL, ldap.SCOPE_SUBORDINATE
        :type scope: int

        :returns: A list of EntryAci objects
        """

        acis = []
        rawacientries = self.conn.search_s(basedn, scope, 'aci=*', ['aci'])
        for rawacientry in rawacientries:
            acis += rawacientry.getAcis()
        return acis

    def lint(self, basedn, scope=ldap.SCOPE_SUBTREE):
        """Validate and check for potential aci issues.

        Given a scope and basedn, this will retrieve all the aci's below.
        A number of checks are then run on the aci in isolation, and
        in groups.

        :param basedn: Base DN
        :type basedn: str
        :param scope: ldap.SCOPE_SUBTREE, ldap.SCOPE_BASE,
                       ldap.SCOPE_ONELEVEL, ldap.SCOPE_SUBORDINATE
        :type scope: int

        :returns: A tuple of (bool, list( dict ))
                   - Bool represents if the acis pass or fail as a whole.
                   - The list contains a list of warnings about your acis.
                   - The dict is structured as::

                       {
                         name: "" # DSALEXXXX
                         severity: "" # LOW MEDIUM HIGH
                         detail: "" # explination
                       }
        """

        result = True
        # Not thread safe!!!
        self.warnings = []

        acis = self.list(basedn, scope)
        # Checks again "all acis" go here.
        self._lint_dsale_0001_ne_internal(acis)
        self._lint_dsale_0002_ne_mult_subtree(acis)
        # checks again individual here

        if len(self.warnings) > 0:
            result = False

        return (result, self.warnings)

    def format_lint(self, warnings):
        """Takes the array of warnings and returns a formatted string.

        :param warnings: The array of warnings
        :type warnings: dict

        :returns: Formatted string or warnings
        """

        buf = "-------------------------------------------------------------------------------"

        for warning in warnings:
            buf += """
Directory Server Aci Lint Error: {DSALE}
Severity: {SEVERITY}

Affected Acis:
{ACIS}

Details: {DETAIL}

Advice: {FIX}
-------------------------------------------------------------------------------""".format(
                DSALE=warning['dsale'],
                SEVERITY=warning['severity'],
                ACIS=warning['acis'],
                DETAIL=warning['detail'],
                FIX=warning['fix'],
            )
        return buf

    # These are the aci lint checks.

    def _lint_dsale_0001_ne_internal(self, acis):
        """Check for the presence of "not equals" attributes that will inadvertantly
        allow the return / modification of internal attributes.
        """

        affected = []
        for aci in acis:
            if 'targetattr' in aci.acidata:
                for ta in aci.acidata['targetattr']:
                    if ta['equal'] is False:
                        affected.append(aci.acidata['rawaci'])

        if len(affected) > 0:
            self.warnings.append(
                {
                    'dsale': 'DSALE0001',
                    'severity': 'HIGH',
                    'acis': "\n".join(affected),
                    'detail': """
An aci of the form "(targetAttr!="attr")" exists on your system. This aci
will internally be expanded to mean "all possible attributes including system,
excluding the listed attributes".

This may allow access to a bound user or anonymous to read more data about
directory internals, including aci state or user limits. In the case of write
acis it may allow a dn to set their own resource limits, unlock passwords or
their own aci.

The ability to change the aci on the object may lead to privilege escalation in
some cases.
                    """,
                    'fix': """
Convert the aci to the form "(targetAttr="x || y || z")".
                    """
                }
            )

    def _lint_dsale_0002_ne_mult_subtree(self, acis):
        """This check will show pairs or more of aci that match the same subtree
        with a != rute. These can cause the other rule to be invalidated!
        """

        affected = []
        for aci in acis:
            # The aci has to be a NE, else don't bother checking
            if not _aci_any_targetattr_ne(aci):
                continue

            affect = False
            buf = "%s %s\n" % (aci.entry.dn, aci.acidata['rawaci'])
            for aci_inner in acis:

                if aci_inner == aci:
                    # Don't compare to self!
                    continue
                # Check the inner is a not equal also
                if not _aci_any_targetattr_ne(aci_inner):
                    continue

                # Check if the dn is a substring, ie child, or equal.
                if aci.entry.dn.endswith(aci_inner.entry.dn):
                    # alias the allow rules
                    allow_inner = set(aci_inner.acidata['allow'][0]['values'])
                    allow_outer = set(aci.acidata['allow'][0]['values'])
                    if len(allow_inner & allow_outer):
                        buf += "|- %s %s\n" % (aci_inner.entry.dn, aci_inner.acidata['rawaci'])
                        affect = True

            if affect:
                affected.append(buf)

        if len(affected) > 0:
            self.warnings.append(
                {
                    'dsale': 'DSALE0002',
                    'severity': 'HIGH',
                    'acis': "\n".join(affected),
                    'detail': """
Acis on your system exist which are both not equals targetattr, and overlap in
scope.

The way that directory server processes these, is to invert them to to white
lists, then union the results.

As a result, these acis *may* allow access to the attributes you want them to
exclude.

Consider:

aci: (targetattr !="cn")(version 3.0;acl "Self write all but cn";allow (write)
    (userdn = "ldap:///self");)
aci: (targetattr !="sn")(version 3.0;acl "Self write all but sn";allow (write)
    (userdn = "ldap:///self");)

This combination allows self write to *all* attributes within the subtree.

In cases where the target is members of a group, it may allow a member who is
within two groups to have elevated privilege.
                    """,
                    'fix': """
Convert the aci to the form "(targetAttr="x || y || z")".

Prevent the acis from overlapping, and have them on unique subtrees.
                    """
                }
            )
