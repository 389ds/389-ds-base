# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

# Replacement of the dbgen.pl utility

from lib389.utils import (ensure_str, pseudolocalize)
import random
import os
import pwd
import grp

global node_count

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
"accounting",
"product development",
"product testing",
"human resources",
"payroll",
"people",
"groups",
]

DBGEN_TEMPLATE = """dn: {DN}{CHANGETYPE}
objectClass: top
objectClass: person
objectClass: organizationalPerson
objectClass: inetOrgPerson
objectclass: inetUser
cn: {CN}
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
postalAddress: 518, Dept #851, Room#{OU}
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

DBGEN_OU_TEMPLATE = """dn: ou={OU},{SUFFIX}
objectClass: top
objectClass: organizationalUnit
ou: {OU}

"""

RANDOM_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqurstuvwxyz0123456789_#@%&()?~$^`~*-=+{}|"\'.,<>'


def finalize_ldif_file(instance, ldif_file):
    # Make the file owned by dirsrv
    os.chmod(ldif_file, 0o644)
    if os.getuid() == 0:
        # root user - chown the ldif to the server user
        userid = ensure_str(instance.userid)
        uid = pwd.getpwnam(userid).pw_uid
        gid = grp.getgrnam(userid).gr_gid
        os.chown(ldif_file, uid, gid)


def get_index(idx, numUsers):
    # Get ldclt style entry "0" padded index number
    zeroLen = len(str(numUsers)) - len(str(idx))
    index = '0' * zeroLen
    index = index + str(idx)
    return index


def get_node(suffix):
    # Build a node/container entry based on the suffix DN
    rdn_attr = suffix.split('=')[0].lower()
    rdn_attr_val = suffix.split('=')[1].split(',')[0]
    if rdn_attr == 'c':
        oc = 'country'
    elif rdn_attr == 'cn':
        oc = 'nscontainer'
    elif rdn_attr == 'dc':
        oc = 'domain'
    elif rdn_attr == 'o':
        oc = 'organization'
    elif rdn_attr == 'ou':
        oc = 'organizationalunit'
    else:
        # Unsupported rdn
        raise ValueError("Suffix RDN '{}' in '{}' is not supported.  Supported RDN's are: 'c', 'cn', 'dc', 'o', and 'ou'".format(rdn_attr, suffix))

    return f"""dn: {suffix}
objectClass: top
objectClass: {oc}
{rdn_attr}: {rdn_attr_val}
aci: (target=ldap:///{suffix})(targetattr=*)(version 3.0; acl "Self Write"; allow(write) userdn = "ldap:///self";)
aci: (target=ldap:///{suffix})(targetattr=*)(version 3.0; acl "Directory Admin Group"; allow(write) groupdn = "ldap:///cn=Directory Administrators,ou=Groups,{suffix}";)
aci: (target=ldap:///{suffix})(targetattr=*)(version 3.0; acl "Anonymous Access"; allow(read, search, compare) userdn = "ldap:///anyone";)

"""


def randomPick(values):
    # Return a randomly selected value from the provided list of values
    val_count = len(values)
    val_count -= 1
    idx = random.randint(0, val_count)
    return values[idx].lstrip()


def write_generic_user(LDIF, index, number, parent, name="user", changetype="", pseudol10n=False):
    uid_val = name + get_index(index, number)
    ou = random.choice(DBGEN_OUS)
    first = uid_val
    last = uid_val[::-1]  # reverse name
    cn = f"{first} {last}"
    initials = "%s. %s" % (first[0], last[0])
    l = random.choice(DBGEN_LOCATIONS)
    title = "%s %s" % (random.choice(DBGEN_TITLE_LEVELS), random.choice(DBGEN_POSITIONS))
    if pseudol10n:
        ou = pseudolocalize(ou)
        first = pseudolocalize(first)
        last = pseudolocalize(last)
        initials = pseudolocalize(initials)
        l = pseudolocalize(l)
        title = pseudolocalize(title)
    dn = f"uid={uid_val},{parent}"

    LDIF.write(DBGEN_TEMPLATE.format(
        DN=dn,
        CHANGETYPE=changetype,
        UID=uid_val,
        UIDNUMBER=index,
        FIRST=first,
        LAST=last,
        CN=cn,
        INITIALS=initials,
        OU=ou,
        LOCATION=l,
        TITLE=title,
    ))
    return dn

def dbgen_users(instance, number, ldif_file, suffix, generic=False, entry_name="user", parent=None, startIdx=0, rdnCN=False, pseudol10n=False):
    """
    Generate an LDIF of randomly named entries
    """
    familyname_file = os.path.join(instance.ds_paths.data_dir, 'dirsrv/data/dbgen-FamilyNames')
    givename_file = os.path.join(instance.ds_paths.data_dir, 'dirsrv/data/dbgen-GivenNames')
    familynames = []
    givennames = []
    with open(familyname_file, 'r') as f:
        familynames = [n.strip() for n in f]
    with open(givename_file, 'r') as f:
        givennames = [n.strip() for n in f]

    with open(ldif_file, 'w') as LDIF:
        LDIF.write(get_node(suffix))
        for ou in DBGEN_OUS:
            ou = pseudolocalize(ou) if pseudol10n else ou
            LDIF.write(DBGEN_OU_TEMPLATE.format(SUFFIX=suffix, OU=ou))

        if parent is not None:
            parent_rdn = parent.split(',')[0].split('=')[1]
            if parent_rdn.lower() not in DBGEN_OUS:
                LDIF.write(get_node(parent))

        for i in range(1, int(number) + 1):
            # Pick a random ou
            ou = random.choice(DBGEN_OUS)
            first = random.choice(givennames)
            last = random.choice(familynames)
            if generic:
                i += startIdx
                name = entry_name + get_index(i, number)
                uid = name
                cn = name
            else:
                first = random.choice(givennames)
                last = random.choice(familynames)
                uid = "%s%s%s" % (first[0], last, i)
                cn = f"{first} {last}"
            # How do we subscript from a generator?
            initials = "%s. %s" % (first[0], last[0])
            l = random.choice(DBGEN_LOCATIONS)
            title = "%s %s" % (random.choice(DBGEN_TITLE_LEVELS), random.choice(DBGEN_POSITIONS))
            if pseudol10n:
                ou = pseudolocalize(ou)
                first = pseudolocalize(first)
                last = pseudolocalize(last)
                initials = pseudolocalize(initials)
                l = pseudolocalize(l)
                title = pseudolocalize(title)

            if parent is None:
                parent = f"ou={ou},{suffix}"

            if rdnCN:
                # Not using "uid" so use "cn" instead
                dn = f"cn={cn},{parent}"
            else:
                dn = f"uid={uid},{parent}"

            LDIF.write(DBGEN_TEMPLATE.format(
                DN=dn,
                CHANGETYPE="",
                UID=uid,
                UIDNUMBER=i,
                FIRST=first,
                LAST=last,
                CN=cn,
                INITIALS=initials,
                OU=ou,
                LOCATION=l,
                TITLE=title,
            ))

    finalize_ldif_file(instance, ldif_file)


def dbgen_groups(instance, ldif_file, props):
    """
    Create static group(s) and the member entries

        props = {
            "name": STRING,
            "parent": DN,
            "suffix": DN,
            "number": ###,  --> number of groups to create
            "numMembers": ###,
            "createMembers": True/False  --> Create the member entries (default is True)
            "memberParent": DN
            "membershipAttr": ATTR
        }
    """
    with open(ldif_file, 'w') as LDIF:
        # Create the top node
        LDIF.write(get_node(props['suffix']))
        if props['parent'] is not None:
            if props['parent'] != props['suffix']:
                # Create the group container
                LDIF.write(get_node(props['parent']))
        else:
            props['parent'] = props['suffix']

        if props['memberParent'] is not None:
            if props['memberParent'] != props['suffix'] and props['memberParent'] != props['parent']:
                # Create the member/user container
                LDIF.write(get_node(props['memberParent']))
        else:
            props['memberParent'] = props['suffix']

        for idx in range(1, int(props['number']) + 1):
            # Build the member list and create the member entries
            group_member_list = []
            if props['createMembers']:
                for user_idx in range(1, int(props['numMembers']) + 1):
                    dn = write_generic_user(LDIF, user_idx, props['numMembers'], props['memberParent'], name=f"group_entry{idx}-")
                    group_member_list.append(dn)
            else:
                # Not creating the member entries, just build the DN list of members
                for user_idx in range(1, int(props['numMembers']) + 1):
                    name = "user" + get_index(user_idx, props['numMembers'])
                    dn = f"uid={name},{props['memberParent']}"
                    group_member_list.append(dn)

            if props['number'] == 0:
                # Only creating one group, do not add the idx to DN
                group_dn = f"dn: cn={props['name']},{props['parent']}\n"
                cn = f"cn={props['name']},{props['parent']}"
            else:
                group_dn = f"dn: cn={props['name']}-{idx},{props['parent']}\n"
                cn = f"cn={props['name']}-{idx},{props['parent']}"

            LDIF.write(group_dn)
            LDIF.write('objectclass: top\n')
            LDIF.write('objectclass: groupOfUniqueNames\n')
            LDIF.write('objectclass: groupOfNames\n')
            LDIF.write('objectclass: inetAdmin\n')
            LDIF.write(f'cn: {cn}\n')
            for dn in group_member_list:
                LDIF.write(f"{props['membershipAttr']}: {dn}\n")
            LDIF.write('\n')

    finalize_ldif_file(instance, ldif_file)


def dbgen_cos_def(instance, ldif_file, props):
    """
    Create a COS definition

        props = {
            "cosType": "classic", "pointer", "indirect",
            "defName": VAL,
            "defParent": VAL,
            "defCreateParent": True/False,
            "cosSpecifier": can be used for cosIndirectSpecifier
            "cosAttrs": [],
            "tmpName": DN (need to classic and pointer COS defs)
        }
    """

    if props['cosType'] == 'pointer':
        objectclass = 'objectclass: cosPointerDefinition\n'
    if props['cosType'] == 'indirect':
        objectclass = 'objectclass: cosIndirectDefinition\n'
    if props['cosType'] == 'classic':
        objectclass = 'objectclass: cosClassicDefinition\n'

    with open(ldif_file, 'w') as LDIF:
        # Create parent
        if props['defCreateParent']:
            LDIF.write(get_node(props['defParent']))

        #
        # Create definition
        #
        dn = (f"dn: cn={props['defName']},{props['defParent']}\n")
        LDIF.write(dn)
        LDIF.write('objectclass: top\n')
        LDIF.write('objectclass: cosSuperDefinition\n')
        LDIF.write(objectclass)
        LDIF.write('cn: ' + props['defName'] + "\n")

        if props['cosType'] == 'pointer' or props['cosType'] == 'classic':
            LDIF.write(f"cosTemplateDN: {props['tmpName']}\n")
        if props['cosType'] == 'indirect':
            LDIF.write(f"cosIndirectSpecifier: {props['cosSpecifier']}\n")
        elif props['cosType'] == "classic":
            LDIF.write(f"cosSpecifier: {props['cosSpecifier']}\n")
        for attr in props['cosAttrs']:
            # There can be multiple COS attributes
            LDIF.write(f"cosAttribute: {attr}\n")

    finalize_ldif_file(instance, ldif_file)


def dbgen_cos_template(instance, ldif_file, props):
    """
    Create a COS Template

        props = {
            "tmpName": VAL,
            "tmpParent": VAL,
            "tmpCreateParent": True/False,
            "cosPriority": ####
            "cosTmpAttrVal": Attr/val
        }
    """

    with open(ldif_file, 'w') as LDIF:
        # Create parent
        if props['tmpCreateParent']:
            LDIF.write(get_node(props['tmpParent']))

        # Create template
        dn = f"dn: cn={props['tmpName']},{props['tmpParent']}\n"
        LDIF.write(dn)
        LDIF.write('objectclass: top\n')
        LDIF.write('objectclass: extensibleObject\n')
        LDIF.write('objectclass: cosTemplate\n')
        LDIF.write(f"cn: {props['tmpName']}\n")
        if props['cosPriority'] is not None:
            LDIF.write(f"cosPriority: {props['cosPriority']}\n")
        pair = props['cosTmpAttrVal'].split(':')
        LDIF.write(f"{pair[0]}: {pair[1]}\n")

    finalize_ldif_file(instance, ldif_file)


def dbgen_role(instance, ldif_file, props):
    """
    Create a Role

        props = {
            role_type: "managed", "filter", pr "nested",
            role_name: NAME,
            parent: DN of parent,
            createParent: True/False,
            filter: FILTER,
            role_list: [DN, DN, ...]  # For nested role only
        }
    """

    if props['role_type'].lower() == 'managed':
        objectclasses = ('objectclass: nsSimpleRoleDefinition\n' +
                         'objectclass: nsManagedRoleDefinition\n')
    elif props['role_type'].lower() == 'filtered':
        objectclasses = ('objectclass: nsComplexRoleDefinition\n' +
                         'objectclass: nsFilteredRoleDefinition\n')
    elif props['role_type'].lower() == 'nested':
        objectclasses = ('objectclass: nsComplexRoleDefinition\n' +
                         'objectclass: nsNestedRoleDefinition\n')

    with open(ldif_file, 'w') as LDIF:
        # Create parent entry
        if props['createParent']:
            LDIF.write(get_node(props['parent']))

        dn = f"dn: cn={props['role_name']},{props['parent']}\n"
        LDIF.write(dn)
        LDIF.write('objectclass: top\n')
        LDIF.write('objectclass: LdapSubEntry\n')
        LDIF.write('objectclass: nsRoleDefinition\n')
        LDIF.write(objectclasses)
        LDIF.write(f"cn: {props['role_name']}\n")
        if props['role_type'] == 'nested':
            # Write out each role DN
            for value in props['role_list']:
                LDIF.write(f'nsRoleDN: {value}\n')
        elif props['role_type'] == 'filtered':
            LDIF.write(f"nsRoleFilter: {props['filter']}\n")

    finalize_ldif_file(instance, ldif_file)


def dbgen_mod_load(ldif_file, props):
    """
    Generate a "load" LDIF file that can be consumed by ldapmodify

        props = {
            "createUsers": True/False,
            "deleteUsers": True/False,
            "numUsers": ###,
            "parent": DN,  --> ou=people,dc=example,dc=com
            "createParent": True/False,
            "addUsers": ###,
            "delUsers": ###,
            'modrdnUsers": ###,
            "modUsers": ###,  --> number of entries to modify
            "random": True/False,
            "modAttrs": [ATTR, ATTR, ...]
        }
    """

    entry_dn_list = []  # List used to delete entries at the end of the LDIF
    if props['modAttrs'] is None:
        props['modAttrs'] = ['description', 'title']

    with open(ldif_file, 'w') as LDIF:
        if props['createParent']:
            # Create the container entry that the users will be add to
            LDIF.write(get_node(props['parent']))

        # Create entries
        for user_idx in range(1, props['numUsers'] + 1):
            if props['createUsers']:
                dn = write_generic_user(
                    LDIF, user_idx, props['numUsers'], props['parent'],
                    changetype="\nchangetype: add")
                entry_dn_list.append(dn)
            else:
                dn = f"uid=user{get_index(user_idx, props['numUsers'])},{props['parent']}"
                entry_dn_list.append(dn)

        # Set the types of operations and how many of them to perform
        addc = int(props['addUsers'])
        delc = int(props['delUsers'])
        modc = int(props['modUsers'])
        mrdnc = int(props['modrdnUsers'])
        total_ops = addc + delc + modc + mrdnc

        if props['random']:
            # Mix up the selected operations
            operations = ['add', 'mod', 'modrdn', 'delete']
            while total_ops != 0:
                op = randomPick(operations)
                if op == 'add':
                    if addc == 0:
                        # no more adds to do
                        operations.remove('add')
                        continue
                    dn = write_generic_user(
                        LDIF, addc, props['addUsers'], props['parent'],
                        name="addUser", changetype="\nchangetype: add")
                    entry_dn_list.append(dn)
                    addc -= 1
                elif op == 'mod':
                    if modc == 0:
                        # no more mods to do
                        operations.remove('mod')
                        continue
                    attr = randomPick(props['modAttrs'])
                    val = (''.join((random.choice(RANDOM_CHARS) for i in range(0, random.randint(10, 30)))))
                    LDIF.write(f"dn: uid=user{get_index(modc, props['numUsers'])},{props['parent']}\n")
                    LDIF.write("changetype: modify\n")
                    LDIF.write(f"replace: {attr}\n")
                    LDIF.write(f"{attr}: {val}\n")
                    LDIF.write("\n")
                    modc -= 1
                elif op == 'delete':
                    if delc == 0:
                        # no more deletes to do
                        operations.remove('delete')
                        continue

                    dn_val = f"uid=user{get_index(delc, props['numUsers'])},{props['parent']}"
                    LDIF.write(f"dn: {dn_val}\n")
                    LDIF.write("changetype: delete\n")
                    LDIF.write(" \n")
                    delc -= 1
                    if dn_val in entry_dn_list:
                        entry_dn_list.remove(dn_val)
                elif op == 'modrdn':
                    if mrdnc == 0:
                        # no more modrdns to do
                        operations.remove('modrdn')
                        continue
                    dn_val = f"uid=user{get_index(mrdnc, props['numUsers'])},{props['parent']}"
                    new_dn_val = f"cn=user{get_index(mrdnc, props['numUsers'])},{props['parent']}"
                    LDIF.write(f"dn: {dn_val}\n")
                    LDIF.write("changetype: modrdn\n")
                    LDIF.write(f"newrdn: {new_dn_val}\n")
                    LDIF.write("deleteoldrdn: 1\n")
                    LDIF.write("\n")
                    mrdnc -= 1
                    # Revise the DN list: add the new DN, and remove the old DN
                    entry_dn_list.append(new_dn_val)
                    if dn_val in entry_dn_list:
                        entry_dn_list.remove(dn_val)

                # Update the total count
                total_ops -= 1
        else:
            # Sequentially do each type of operation

            # Do the Adds
            addc = int(props['addUsers'])
            while addc != 0:
                dn = write_generic_user(
                    LDIF, addc, props['addUsers'], props['parent'],
                    name="addUser", changetype="\nchangetype: add")
                addc -= 1
                entry_dn_list.append(dn)

            # Mods
            while modc != 0:
                attr = randomPick(props['modAttrs'])
                val = (''.join((random.choice(RANDOM_CHARS) for i in range(0, random.randint(10, 30)))))
                LDIF.write(f"dn: uid=user{get_index(modc, props['numUsers'])},{props['parent']}\n")
                LDIF.write("changetype: modify\n")
                LDIF.write(f"replace: {attr}\n")
                LDIF.write(f"{attr}: {val}\n")
                LDIF.write("\n")
                modc -= 1

            # Modrdns
            while mrdnc != 0:
                dn_val = f"uid=user{get_index(mrdnc, props['numUsers'])},{props['parent']}"
                new_dn_val = f"cn=user{get_index(mrdnc, props['numUsers'])},{props['parent']}"
                LDIF.write(f"dn: {dn_val}\n")
                LDIF.write("changetype: modrdn\n")
                LDIF.write(f"newrdn: {new_dn_val}\n")
                LDIF.write("deleteoldrdn: 1\n")
                LDIF.write("\n")
                mrdnc -= 1
                # Revise the DN list: add the new DN, and remove the old DN
                entry_dn_list.append(new_dn_val)
                if dn_val in entry_dn_list:
                    entry_dn_list.remove(dn_val)

            # Deletes
            while delc != 0:
                dn_val = f"uid=user{get_index(delc, props['numUsers'])},{props['parent']}"
                LDIF.write(f"dn: {dn_val}\n")
                LDIF.write("changetype: delete\n")
                LDIF.write(" \n")
                delc -= 1
                if dn_val in entry_dn_list:
                    entry_dn_list.remove(dn_val)

        # Cleanup - delete all known entries
        if props['deleteUsers']:
            for dn in entry_dn_list:
                LDIF.write(f"dn: {dn}\n")
                LDIF.write("changetype: delete\n")
                LDIF.write(" \n")


def build_recursive_nodes(LDIF, dn, node_limit, max_entries):
    """
    Recursively create two nodes under each node, continue until there are no
    more max_entries.
    """
    global node_count
    wrote_node1 = False
    wrote_node2 = False

    # Create containers for DN1 and DN2
    dn1 = "ou=1," + dn
    LDIF.write(f'dn: {dn1}\n')
    LDIF.write('objectclass: top\n')
    LDIF.write('objectclass: organizationalUnit\n')
    LDIF.write('ou: ou=1\n\n')

    dn2 = "ou=2," + dn
    LDIF.write(f'dn: {dn2}\n')
    LDIF.write('objectclass: top\n')
    LDIF.write('objectclass: organizationalUnit\n')
    LDIF.write('ou: ou=2\n\n')

    # Add entries under each node
    for entry_idx in range(1, node_limit + 1):
        write_generic_user(LDIF, entry_idx, node_limit, dn1)
        max_entries -= 1
        if not wrote_node1:
            wrote_node1 = True
            node_count += 1
        if max_entries == 0:
            break

        write_generic_user(LDIF, entry_idx, node_limit, dn2)
        max_entries -= 1
        if not wrote_node2:
            wrote_node2 = True
            node_count += 1
        if max_entries == 0:
            break

    # Are we out of entries
    if max_entries == 0:
        return

    # Get the remaining entries to be split between two nodes
    new_node_max = max_entries // 2
    if (max_entries % 2) > 0:
        remainder = 1
    else:
        remainder = 0

    # Recursively build child nodes
    build_recursive_nodes(LDIF, dn1, node_limit, new_node_max)
    build_recursive_nodes(LDIF, dn2, node_limit, new_node_max + remainder)


def dbgen_nested_ldif(instance, ldif_file, props):
    """
    Create a deeply nested LDIF

        props = {
            "numUsers": ####   --> Total number of user entries to create
            'nodeLimit': ####  --> max number of entries to put into each node
            "suffix": DN
        }
    """

    global node_count
    node_count = 0
    with open(ldif_file, 'w') as LDIF:
        # Create the top suffix
        LDIF.write(get_node(props['suffix']))

        # Create all the nodes
        build_recursive_nodes(LDIF, props['suffix'], props['nodeLimit'], props['numUsers'])

    finalize_ldif_file(instance, ldif_file)

    return node_count
