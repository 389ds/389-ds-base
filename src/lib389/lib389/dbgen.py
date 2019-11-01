# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# Replacement of the dbgen.pl utility

from lib389.utils import pseudolocalize
import random
import os
import pwd
import grp

DBGEN_POSITIONS = [
"Accountant",
"Admin",
"Architect",
"Assistant",
"Artist",
"Consultant",
"Czar",
"Dictator",
"Director",
"Diva",
"Dreamer",
"Evangelist",
"Engineer",
"Figurehead",
"Fellow",
"Grunt",
"Guru",
"Janitor",
"Madonna",
"Manager",
"Pinhead",
"President",
"Punk",
"Sales Rep",
"Stooge",
"Visionary",
"Vice President",
"Writer",
"Warrior",
"Yahoo"
]

DBGEN_TITLE_LEVELS = [
"Senior",
"Master",
"Associate",
"Junior",
"Chief",
"Supreme",
"Elite"
]

DBGEN_LOCATIONS = [
"Mountain View", "Redmond", "Redwood Shores", "Armonk",
"Cambridge", "Santa Clara", "Sunnyvale", "Alameda",
"Cupertino", "Menlo Park", "Palo Alto", "Orem",
"San Jose", "San Francisco", "Milpitas", "Hartford", "Windsor",
"Boston", "New York", "Detroit", "Dallas", "Denver", "Brisbane",
]

DBGEN_OUS = [
"Accounting",
"Product Development",
"Product Testing",
"Human Resources",
"Payroll",
"People",
"Groups",
]

DBGEN_TEMPLATE = """dn: {DN}
objectClass: top
objectClass: person
objectClass: organizationalPerson
objectClass: inetOrgPerson
cn: {FIRST} {LAST}
sn: {LAST}
uid: {UID}
givenName: {FIRST}
description: 2;7613;CN=Red Hat CS 71GA Demo,O=Red Hat CS 71GA Demo,C=US;CN=RHCS Agent - admin01,UID=admin01,O=redhat,C=US [1] This is {FIRST} {LAST}'s description.
userPassword: {UID}
departmentNumber: 1230
employeeType: Manager
homePhone: +1 303 937-6482
initials: {INITIALS}
telephoneNumber: +1 303 573-9570
facsimileTelephoneNumber: +1 415 408-8176
mobile: +1 818 618-1671
pager: +1 804 339-6298
roomNumber: 5164
carLicense: 21SJJAG
l: {LOCATION}
ou: {OU}
mail: {UID}@example.com
mail: {UIDNUMBER}@example.com
postalAddress: 518,  Dept #851, Room#{OU}
title: {TITLE}
usercertificate;binary:: MIIBvjCCASegAwIBAgIBAjANBgkqhkiG9w0BAQQFADAnMQ8wDQYD
 VQQDEwZjb25maWcxFDASBgNVBAMTC01NUiBDQSBDZXJ0MB4XDTAxMDQwNTE1NTEwNloXDTExMDcw
 NTE1NTEwNlowIzELMAkGA1UEChMCZnIxFDASBgNVBAMTC01NUiBTMSBDZXJ0MIGfMA0GCSqGSIb3
 DQEBAQUAA4GNADCBiQKBgQDNlmsKEaPD+o3mAUwmW4E40MPs7aiui1YhorST3KzVngMqe5PbObUH
 MeJN7CLbq9SjXvdB3y2AoVl/s5UkgGz8krmJ8ELfUCU95AQls321RwBdLRjioiQ3MGJiFjxwYRIV
 j1CUTuX1y8dC7BWvZ1/EB0yv0QDtp2oVMUeoK9/9sQIDAQABMA0GCSqGSIb3DQEBBAUAA4GBADev
 hxY6QyDMK3Mnr7vLGe/HWEZCObF+qEo2zWScGH0Q+dAmhkCCkNeHJoqGN4NWjTdnBcGaAr5Y85k1
 o/vOAMBsZePbYx4SrywL0b/OkOmQX+mQwieC2IQzvaBRyaNMh309vrF4w5kExReKfjR/gXpHiWQz
 GSxC5LeQG4k3IP34

"""

DBGEN_HEADER = """dn: {SUFFIX}
objectClass: top
objectClass: domain
dc: {RDN}
aci: (target=ldap:///{SUFFIX})(targetattr=*)(version 3.0; acl "acl1"; allow(write) userdn = "ldap:///self";)
aci: (target=ldap:///{SUFFIX})(targetattr=*)(version 3.0; acl "acl2"; allow(write) groupdn = "ldap:///cn=Directory Administrators, {SUFFIX}";)
aci: (target=ldap:///{SUFFIX})(targetattr=*)(version 3.0; acl "acl3"; allow(read, search, compare) userdn = "ldap:///anyone";)

"""

DBGEN_OU_TEMPLATE = """dn: ou={OU},{SUFFIX}
objectClass: top
objectClass: organizationalUnit
ou: {OU}

"""


def dbgen(instance, number, ldif_file, suffix, pseudol10n=False):
    familyname_file = os.path.join(instance.ds_paths.data_dir, 'dirsrv/data/dbgen-FamilyNames')
    givename_file = os.path.join(instance.ds_paths.data_dir, 'dirsrv/data/dbgen-GivenNames')
    familynames = []
    givennames = []
    with open(familyname_file, 'r') as f:
        familynames = [n.strip() for n in f]
    with open(givename_file, 'r') as f:
        givennames = [n.strip() for n in f]

    with open(ldif_file, 'w') as output:
        rdn = suffix.split(",", 1)[0].split("=", 1)[1]
        output.write(DBGEN_HEADER.format(SUFFIX=suffix, RDN=rdn))
        for ou in DBGEN_OUS:
            ou = pseudolocalize(ou) if pseudol10n else ou
            output.write(DBGEN_OU_TEMPLATE.format(SUFFIX=suffix, OU=ou))
        for i in range(0, number):
            # Pick a random ou
            ou = random.choice(DBGEN_OUS)
            first = random.choice(givennames)
            last = random.choice(familynames)
            # How do we subscript from a generator?
            initials = "%s. %s" % (first[0], last[0])
            uid = "%s%s%s" % (first[0], last, i)
            l = random.choice(DBGEN_LOCATIONS)
            title = "%s %s" % (random.choice(DBGEN_TITLE_LEVELS), random.choice(DBGEN_POSITIONS))
            if pseudol10n:
                ou = pseudolocalize(ou)
                first = pseudolocalize(first)
                last = pseudolocalize(last)
                initials = pseudolocalize(initials)
                l = pseudolocalize(l)
                title = pseudolocalize(title)
            dn = "uid=%s,ou=%s,%s" % (uid, ou, suffix)
            output.write(DBGEN_TEMPLATE.format(
                DN=dn,
                UID=uid,
                UIDNUMBER=i,
                FIRST=first,
                LAST=last,
                INITIALS=initials,
                OU=ou,
                LOCATION=l,
                TITLE=title,
                SUFFIX=suffix
            ))

    # Make the file owned by dirsrv
    os.chmod(ldif_file, 0o644)
    if os.getuid() == 0:
        # root user - chown the ldif to the server user
        uid = pwd.getpwnam(instance.userid).pw_uid
        gid = grp.getgrnam(instance.userid).gr_gid
        os.chown(ldif_file, uid, gid)
