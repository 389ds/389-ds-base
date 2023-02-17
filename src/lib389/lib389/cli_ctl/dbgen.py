# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.dbgen import (
    dbgen_users,
    dbgen_groups,
    dbgen_cos_def,
    dbgen_cos_template,
    dbgen_role,
    dbgen_mod_load,
    dbgen_nested_ldif,
)
from lib389.utils import is_a_dn

DEFAULT_LDIF = "/ldifgen.ldif"

ignore_args = [
    "ldif_file",
    "func",
    "verbose",
    "list",
    "instance",
    "json",
    "remove_all",
]


def get_ldif_dir(instance):
    """
    Get the server's LDIF directory.
    """
    return instance.get_ldif_dir()


def adjust_ldif_name(instance, ldif_name):
    """
    If just a name is provided append it to the server's LDIF directory
    """
    if ldif_name[0] == '.' or ldif_name[0] == '/':
        # Name appears to already be an absolute path
        return ldif_name
    else:
        # Its just a name, add the server's ldif directory
        return instance.get_ldif_dir() + "/" + ldif_name


def display_args(log, args):
    # Display all the options that are being used to generate the ldif file
    log.info(f"\nGenerating LDIF with the following options:")
    for k, v in vars(args).items():
        if k in ignore_args or v is None:
            continue
        k = k.replace("_", "-")  # Restore arg's original name
        log.info(f" - {k}={v}")
    log.info(f" - ldif-file={args.ldif_file}")
    log.info("\nWriting LDIF ...")


def validate_ldif_file(ldif_file, log=None):
    """
    Check if the LDIF file exists.  If interactive then return some error
    text to the caller, otherwise raise an error.
    """
    try:
        f = open(ldif_file, 'w')
        f.close()
        return True
    except PermissionError:
        # File might already exist
        msg = f"The LDIF file ({ldif_file}) exists and can not be overwritten.  Please choose a different file name."
        if log is not None:
            log.info(msg)
        else:
            raise ValueError(msg)
    except FileNotFoundError:
        msg = f"The LDIF file ({ldif_file}) location does not exist.  Please choose a different location."
        if log is not None:
            log.info(msg)
        else:
            raise ValueError(msg)
    except Exception as e:
        msg = f"The LDIF file ({ldif_file}) can not be written: {str(e)}"
        if log is not None:
            log.info(msg)
        else:
            raise ValueError(msg)

    return False


def get_ldif_file_input(log, default_name=DEFAULT_LDIF):
    valid = False
    while not valid:
        file_name = get_input(log, "Enter the new LDIF file name", default_name)
        valid = validate_ldif_file(file_name, log=log)
    return file_name


def get_input(log, msg, default, type="", options=None):
    # Interactive prompt
    display_default = default
    if isinstance(default, bool):
        if default:
            display_default = "yes"
        else:
            display_default = "no"
    while 1:
        if display_default != "":
            val = input(f'\n{msg} [{display_default}]: ')
        else:
            val = input(f'\n{msg}: ')
        if val != '':
            if type == "dn":
                if is_a_dn(val, allow_anon=False):
                    return val
                else:
                    log.info(f"\n --->  The value you entered \"{val}\" is not a valid DN")
                    continue
            elif type == "int":
                if val.isdigit():
                    if int(val) < 1:
                        log.info("\n --->  You must enter number greater than 0")
                        continue
                    return int(val)
                else:
                    log.info(f"\n --->  The number you entered \"{val}\" is not a number")
                    continue
            elif type == "bool":
                if val.lower() == "y" or val.lower() == "yes":
                    return True
                elif val.lower() == "n" or val.lower() == "no":
                    return False
                else:
                    log.info(f"\n --->  Invalid value ({val}), please enter \"yes\" or \"no\".")
                    continue
            else:
                # Just a string, nothing to validate
                if options is not None:
                    if val.lower() not in options:
                        opt_str = ', '.join(options)
                        log.info(f"\n --->  Invalid value ({val}), please enter one of the following types: {opt_str}")
                        continue
                return val
        else:
            # User selected the default value
            return default


def dbgen_create_users(inst, log, args):
    """
    Create a LDIF of user entries
    """
    if args.number is None or args.suffix is None:
        """
        Interactively get all the info ...
        """
        log.info("Missing required parameters '--number' and/or '--suffix', switching to Interactive mode ...")

        # Get the suffix
        args.suffix = get_input(log, "Enter the suffix", "dc=example,dc=com", "dn")

        # Get the parent
        args.parent = get_input(log, "Enter the parent entry for the users", "ou=people,dc=example,dc=com", "dn")

        # Get the number of users to create
        args.number = get_input(log, "Enter the number of users to create", 100000, "int")

        # Confirm the RDN attribute
        args.rdn_cn = get_input(log, "Do you want to use \"cn\" instead of \"uid\" for the entry RDN attribute (yes/no)", False, "bool")

        # Create generic entries
        args.generic = get_input(log, "Create generic entries that can be used with \"ldclt\" (yes/no)", False, "bool")

        # get offset size
        if args.generic:
            args.start_idx = get_input(log, "Choose the starting index for the generic user entries", 0, "int")

        # localize the data
        args.localize = get_input(log, "Do you want to localize the LDIF data (yes/no)", False, "bool")

        # Get the output LDIF file name
        args.ldif_file = get_ldif_file_input(log, default_name=get_ldif_dir(inst) + DEFAULT_LDIF)
    else:
        args.ldif_file = adjust_ldif_name(inst, args.ldif_file)
        validate_ldif_file(args.ldif_file)

    display_args(log, args)
    dbgen_users(inst, args.number, args.ldif_file, args.suffix, generic=args.generic, parent=args.parent, startIdx=args.start_idx, rdnCN=False, pseudol10n=args.localize)
    log.info(f"Successfully created LDIF file: {args.ldif_file}")


def dbgen_create_groups(inst, log, args):
    """
    Create static groups and their members
    """

    if args.number is None or args.suffix is None:
        """
        Interactively get all the info ...
        """
        log.info("Missing required parameters '--number' and/or '--suffix', switching to Interactive mode ...")

        # Get the number of users to create
        args.number = get_input(log, "Enter the number of groups to create", 1, "int")

        # Get the suffix
        args.suffix = get_input(log, "Enter the suffix", "dc=example,dc=com", "dn")

        # Get the parent
        args.parent = get_input(log, "Enter the parent entry to add the groups under", args.suffix, "dn")

        # Get the membership attr
        args.member_attr = get_input(log, "Enter the attribute to use for the group membership", "uniquemember")

        # Number of members
        args.num_members = get_input(log, "Enter the number of members to add to the group", 10000, "int")

        # Create member entries
        args.create_members = get_input(log, "Do you want to create the member entries (yes/no)", True, "bool")

        # member entries parent
        if args.create_members:
            args.member_parent = get_input(log, "Enter the parent entry to add the users under", args.suffix, "dn")

        # Get the output LDIF file name
        args.ldif_file = get_ldif_file_input(log, default_name=get_ldif_dir(inst) + DEFAULT_LDIF)
    else:
        validate_ldif_file(args.ldif_file)

    props = {
        "name": args.NAME,
        "parent": args.parent,
        "suffix": args.suffix,
        "number": args.number,
        "numMembers": args.num_members,
        "createMembers": args.create_members,
        "memberParent": args.member_parent,
        "membershipAttr": args.member_attr,
    }

    display_args(log, args)
    dbgen_groups(inst, args.ldif_file, props)
    log.info(f"Successfully created LDIF file: {args.ldif_file}")


def dbgen_create_cos_def(inst, log, args):
    """
    Create a COS definition
    """
    if args.type is None or args.parent is None or len(args.cos_attr) == 0 or \
        ((args.type == "classic" or args.type == "indirect") and args.cos_specifier is None) \
        or ((args.type == "classic" or args.type == "pointer") and args.cos_template is None):
        log.info("Missing some required parameters '--parent',  '--type', '--cos-specifier', or '--cos-template', switching to Interactive mode ...")

        # Get the number of users to create
        args.type = get_input(log, "Type of COS definition: \"classic\", \"pointer\", or \"indirect\"",
                              "classic",  options=["classic", "pointer", "indirect"])

        # Get the parent
        args.parent = get_input(log, "Enter the parent entry to add the COS definition under", "dc=example,dc=com", "dn")

        # Create parent
        args.create_members = get_input(log, "Do you want to create the parent entry (yes/no)", True, "bool")

        # COS specifier
        if args.type == "classic" or args.type == "indirect":
            args.cos_specifier = get_input(log, "Enter the COS specifier attribute", "description")

        # COS template DN
        if args.type == "classic" or args.type == "pointer":
            args.cos_template = get_input(log, "Enter the COS Template DN", "cn=COS Template Entry,dc=example,dc=com", "dn")

        # Gather the COS attributes
        while True:
            val = get_input(log, "Enter COS attributes, press Enter when finished", "")
            if val == "" and len(args.cos_attr) > 0:
                break
            args.cos_attr.append(val)

        # Get the output LDIF file name
        args.ldif_file = get_ldif_file_input(log, default_name=get_ldif_dir(inst) + DEFAULT_LDIF)
    else:
        validate_ldif_file(args.ldif_file)

    props = {
        "cosType": args.type,
        "defName": args.NAME,
        "defParent": args.parent,
        "defCreateParent": args.create_parent,
        "cosSpecifier": args.cos_specifier,
        "cosAttrs": args.cos_attr,
        "tmpName": args.cos_template
    }

    display_args(log, args)
    dbgen_cos_def(inst, args.ldif_file, props)
    log.info(f"Successfully created LDIF file: {args.ldif_file}")


def dbgen_create_cos_tmp(inst, log, args):
    """
    Create a COS template entry
    """
    if args.parent is None or args.cos_priority is None or args.cos_attr_val is None:
        log.info("Missing required parameters '--parent', '--cos-priority' or '--cos-attr-val', switching to Interactive mode ...")

        # Get the parent
        args.parent = get_input(log, "Enter the parent entry to add the COS template under", "dc=example,dc=com", "dn")

        # Create parent
        args.create_parent = get_input(log, "Do you want to create the parent entry (yes/no)", True, "bool")

        # Get the COS priority
        args.cos_priority = get_input(log, "Enter the COS priority for this template", "0", "int")

        # Get the attribute value pair
        args.cos_attr_val = get_input(log, "Enter the attribute and value pair.  Use this format: \"ATTRIBUTE:VALUE\"", "postalcode:19605")

        # Get the output LDIF file name
        args.ldif_file = get_ldif_file_input(log, default_name=get_ldif_dir(inst) + DEFAULT_LDIF)
    else:
        validate_ldif_file(args.ldif_file)

    props = {
        "tmpName": args.NAME,
        "tmpParent": args.parent,
        "tmpCreateParent": args.create_parent,
        "cosPriority": args.cos_priority,
        "cosTmpAttrVal": args.cos_attr_val,
    }

    display_args(log, args)
    dbgen_cos_template(inst,  args.ldif_file, props)
    log.info(f"Successfully created LDIF file: {args.ldif_file}")


def dbgen_create_role(inst, log, args):
    """
    Create a Role
    """
    if args.type is None or args.parent is None or \
        (args.type == "filtered" and args.filter is None) or \
        (args.type == "nested" and len(args.role_dn) == 0):
        log.info("Missing some required parameters '--type', '--parent'. '--filter', '--role-dn', switching to Interactive mode ...")

        # Get the number of users to create
        args.type = get_input(log, "Type of Role: \"managed\", \"filtered\", or \"nested\"",
                              "managed",  options=["managed", "filtered", "nested"])

        # Get the parent
        args.parent = get_input(log, "Enter the parent entry to add the Role under", "dc=example,dc=com", "dn")

        # Create parent
        args.create_parent = get_input(log, "Do you want to create the parent entry (yes/no)", True, "bool")

        # Role filter
        if args.type == "filtered":
            args.filter = get_input(log, "Enter the Role filter", "cn=some_value")

        # Role DN (nested only)
        if args.type == "nested":
            while True:
                val = get_input(log, "Enter the Role DN", "cn=some other role,dc=example,dc=com", "dn")
                if val == "" and len(args.role_dn) > 0:
                    break
                args.role_dn.append(val)

        # Get the output LDIF file name
        args.ldif_file = get_ldif_file_input(log, default_name=get_ldif_dir(inst) + DEFAULT_LDIF)
    else:
        validate_ldif_file(args.ldif_file)

    props = {
        "role_type": args.type,
        "role_name": args.NAME,
        "parent": args.parent,
        "createParent": args.create_parent,
        "filter": args.filter,
        "role_list": args.role_dn
    }

    display_args(log, args)
    dbgen_role(inst, args.ldif_file, props)
    log.info(f"Successfully created LDIF file: {args.ldif_file}")


def dbgen_create_mods(inst, log, args):
    """
    Create a LDIF file of update operations that can be consumed by ldapmodify

    There are a lot of options here for creating different types of modification
    LDIFs.  One technique/option is that you can work with existing users.  Use
    dbgen to generate a large generic user ldif, import it, then you can create
    a modification LDIF that will work on that database.  It's a lot faster than
    using ldapmodify to add 1 million first then modify those entries.

    The other nice option is that you can randomize the operations, but this does
    introduce a potential for some of the operations to fail.  You could delete
    an entry before you modify it, etc...
    """

    if args.num_users is None or args.parent is None:
        log.info("Missing required parameters '--num-users' and/or '--parent', switching to Interactive mode ...")

        # Create users
        args.create_users = get_input(log, "Do you want to create the user entries (yes/no)", True, "bool")

        # Delete users
        args.delete_users = get_input(log, "Do you want to delete all the user entries at the end (yes/no)", True, "bool")

        # Get the number of entries
        args.num_users = get_input(log, "Enter the number of user entries that can be modified", "100000", "int")

        # Get the parent entry
        args.parent = get_input(log, "The DN of the parent entry where the user entries are located", "ou=people,dc=example,dc=com")

        # Create parent
        args.create_parent = get_input(log, "Create the parent entry (yes/no)", True, "bool")

        # Add users
        args.add_users = get_input(log, "The number of users to add during the load", 0, "int")

        # Delete users
        args.del_users = get_input(log, "The number of users to delete during the load", 0, "int")

        # modrdn users
        args.modrdn_users = get_input(log, "The number of users to modrdn during the load", 0, "int")

        # modify users
        args.mod_users = get_input(log, "The number of users to modify during the load", 0, "int")

        # Modification attributes
        args.mod_attrs = get_input(log, "List of attributes that will be randomly chosen from when modifying an entry", "description cn")
        args.mod_attrs = args.mod_attrs.split(' ')

        # Randomize the load
        args.randomize = get_input(log, "Randomly perform the specified add, mod, delete, and modrdn operations (yes/no)", True, "bool")

        # Get the output LDIF file name
        args.ldif_file = get_ldif_file_input(log, default_name=get_ldif_dir(inst) + DEFAULT_LDIF)
    else:
        validate_ldif_file(args.ldif_file)

    props = {
        "createUsers": args.create_users,
        "deleteUsers": args.delete_users,
        "numUsers": int(args.num_users),
        "parent": args.parent,
        "createParent": args.create_parent,
        "addUsers": int(args.add_users),
        "delUsers": int(args.del_users),
        "modrdnUsers": int(args.modrdn_users),
        "modUsers": int(args.mod_users),
        "random": args.randomize,
        "modAttrs": args.mod_attrs
    }

    display_args(log, args)
    dbgen_mod_load(args.ldif_file, props)
    log.info(f"Successfully created LDIF file: {args.ldif_file}")


def dbgen_create_nested(inst, log, args):
    """
    Create a cascading/fractal tree.  Every node splits in half and keeps
    branching out in that fashion until all the entries are used up.
    """

    if  args.num_users is None or args.node_limit is None or args.suffix is None:
        # Num users
        args.num_users = get_input(log, "The number of users to add during the load", 1000000, "int")

        # Node limit
        args.node_limit = get_input(log, "The total number of user entries to create under each node/subtree", 500, "int")

        # Suffix
        args.suffix = get_input(log, "Enter the suffix", "dc=example,dc=com", "dn")

        # Get the output LDIF file name
        args.ldif_file = get_ldif_file_input(log, default_name=get_ldif_dir(inst) + DEFAULT_LDIF)
    else:
        args.ldif_file = adjust_ldif_name(inst, args.ldif_file)
        validate_ldif_file(args.ldif_file)

    props = {
        "numUsers": int(args.num_users),
        "nodeLimit": int(args.node_limit),
        "suffix": args.suffix,
    }

    display_args(log, args)
    node_count = dbgen_nested_ldif(inst, args.ldif_file, props)
    log.info(f"Successfully created nested LDIF file ({args.ldif_file}) containing {node_count} nodes/subtrees")


def create_parser(subparsers):
    db_gen_parser = subparsers.add_parser('ldifgen', help="LDIF generator to make sample LDIF files for testing")
    subcommands = db_gen_parser.add_subparsers(help="action")

    # Create just users
    dbgen_users_parser = subcommands.add_parser('users', help='Generate a LDIF containing user entries')
    dbgen_users_parser.set_defaults(func=dbgen_create_users)
    dbgen_users_parser.add_argument('--number', help="The number of users to create.")
    dbgen_users_parser.add_argument('--suffix', help="The database suffix where the entries will be created.")
    dbgen_users_parser.add_argument('--parent', help="The parent entry that the user entries should be created under.  If not specified, the entries are stored under random Organizational Units.")
    dbgen_users_parser.add_argument('--generic', action='store_true', help="Create generic entries in the format of \"uid=user####\".  These entries are also compatible with ldclt.")
    dbgen_users_parser.add_argument('--start-idx', default=0, help="For generic LDIF's you can choose the starting index for the user entries.  The default is \"0\".")
    dbgen_users_parser.add_argument('--rdn-cn', action='store_true', help="Use the attribute \"cn\" as the RDN attribute in the DN instead of \"uid\"")
    dbgen_users_parser.add_argument('--localize', action='store_true', help="Localize the LDIF data")
    dbgen_users_parser.add_argument('--ldif-file', default="ldifgen.ldif", help=f"The LDIF file name.  Default location is the server's LDIF directory using the name 'ldifgen.ldif'")

    # Create static groups
    dbgen_groups_parser = subcommands.add_parser('groups', help='Generate a LDIF containing groups and members')
    dbgen_groups_parser.set_defaults(func=dbgen_create_groups)
    dbgen_groups_parser.add_argument('NAME', help="The group name.")
    dbgen_groups_parser.add_argument('--number', default=1, help="The number of groups to create.")
    dbgen_groups_parser.add_argument('--suffix', help="The database suffix where the groups will be created.")
    dbgen_groups_parser.add_argument('--parent', help="The parent entry that the group entries should be created under.  If not specified the groups are stored under the suffix.")
    dbgen_groups_parser.add_argument('--num-members', default="10000", help="The number of members in the group.  Default is 10000")
    dbgen_groups_parser.add_argument('--create-members', action='store_true', help="Create the member user entries.")
    dbgen_groups_parser.add_argument('--member-parent', help="The entry DN that the members should be created under.  The default is the suffix entry.")
    dbgen_groups_parser.add_argument('--member-attr', default="uniquemember", help="The membership attribute to use in the group.  Default is \"uniquemember\".")
    dbgen_groups_parser.add_argument('--ldif-file', default="ldifgen.ldif", help=f"The LDIF file name.  Default location is the server's LDIF directory using the name 'ldifgen.ldif'")

    # Create a COS definition
    dbgen_cos_def_parser = subcommands.add_parser('cos-def', help='Generate a LDIF containing a COS definition (classic, pointer, or indirect)')
    dbgen_cos_def_parser.set_defaults(func=dbgen_create_cos_def)
    dbgen_cos_def_parser.add_argument('NAME', help="The COS definition name.")
    dbgen_cos_def_parser.add_argument('--type', help="The COS definition type: \"classic\", \"pointer\", or \"indirect\".")
    dbgen_cos_def_parser.add_argument('--parent', help="The parent entry that the COS definition should be created under.")
    dbgen_cos_def_parser.add_argument('--create-parent', action='store_true', help="Create the parent entry")
    dbgen_cos_def_parser.add_argument('--cos-specifier', help="Used in a classic COS definition, this attribute located in the user entry is used to select which COS template to use.")
    dbgen_cos_def_parser.add_argument('--cos-template', help="The DN of the COS template entry, only used for \"classic\" and \"pointer\" COS definitions.")
    dbgen_cos_def_parser.add_argument('--cos-attr', nargs='*', default=[], help="A list of attributes which defines which attribute the COS generates values for.")
    dbgen_cos_def_parser.add_argument('--ldif-file', default="ldifgen.ldif", help=f"The LDIF file name.  Default location is the server's LDIF directory using the name 'ldifgen.ldif'")

    # Create a COS Template
    dbgen_cos_tmp_parser = subcommands.add_parser('cos-template', help='Generate a LDIF containing a COS template')
    dbgen_cos_tmp_parser.set_defaults(func=dbgen_create_cos_tmp)
    dbgen_cos_tmp_parser.add_argument('NAME', help="The COS template name.")
    dbgen_cos_tmp_parser.add_argument('--parent', help="The DN of the entry to store the COS template entry under.")
    dbgen_cos_tmp_parser.add_argument('--create-parent', action='store_true', help="Create the parent entry")
    dbgen_cos_tmp_parser.add_argument('--cos-priority', type=int, help="Sets the priority of this conflicting/competing COS templates.")
    dbgen_cos_tmp_parser.add_argument('--cos-attr-val', help="defines the attribute and value that the template provides.")
    dbgen_cos_tmp_parser.add_argument('--ldif-file', default="ldifgen.ldif", help=f"The LDIF file name.  Default location is the server's LDIF directory using the name 'ldifgen.ldif'")

    # Create Role entries
    dbgen_roles_parser = subcommands.add_parser('roles', help='Generate a LDIF containing a role entry (managed, filtered, or indirect)')
    dbgen_roles_parser.set_defaults(func=dbgen_create_role)
    dbgen_roles_parser.add_argument('NAME', help="The Role name.")
    dbgen_roles_parser.add_argument('--type', help="The Role type: \"managed\", \"filtered\", or \"nested\".")
    dbgen_roles_parser.add_argument('--parent', help="The DN of the entry to store the Role entry under")
    dbgen_roles_parser.add_argument('--create-parent', action='store_true', help="Create the parent entry")
    dbgen_roles_parser.add_argument('--filter', help="A search filter for gathering Role members.  Required for a \"filtered\" role.")
    dbgen_roles_parser.add_argument('--role-dn', nargs='*', default=[], help="A DN of a role entry that should be included in this role.  Used for \"nested\" roles only.")
    dbgen_roles_parser.add_argument('--ldif-file', default="ldifgen.ldif", help=f"The LDIF file name.  Default location is the server's LDIF directory using the name 'ldifgen.ldif'")

    # Create a modification LDIF
    dbgen_mod_load_parser = subcommands.add_parser('mod-load', help='Generate a LDIF containing modify operations.  This is intended to be consumed by ldapmodify.')
    dbgen_mod_load_parser.set_defaults(func=dbgen_create_mods)
    dbgen_mod_load_parser.add_argument('--create-users', action='store_true', help="Create the entries that will be modified or deleted.  By default the script assumes the user entries already exist.")
    dbgen_mod_load_parser.add_argument('--delete-users', action='store_true', help="Delete all the user entries at the end of the LDIF.")
    dbgen_mod_load_parser.add_argument('--num-users', type=int, help="The number of user entries that will be modified or deleted")
    dbgen_mod_load_parser.add_argument('--parent', help="The DN of the parent entry where the user entries are located.")
    dbgen_mod_load_parser.add_argument('--create-parent', action='store_true', help="Create the parent entry")
    dbgen_mod_load_parser.add_argument('--add-users', default=100, help="The number of additional entries to add during the load.")
    dbgen_mod_load_parser.add_argument('--del-users', default=100, help="The number of entries to delete during the load.")
    dbgen_mod_load_parser.add_argument('--modrdn-users', default=100, help="The number of entries to perform a modrdn operation on.")
    dbgen_mod_load_parser.add_argument('--mod-users', default=100, help="The number of entries to modify.")
    dbgen_mod_load_parser.add_argument('--mod-attrs', nargs="*", default=['description'], help="List of attributes the script will randomly choose from when modifying an entry.  The default is \"description\".")
    dbgen_mod_load_parser.add_argument('--randomize', action='store_true', help="Randomly perform the specified add, mod, delete, and modrdn operations")
    dbgen_mod_load_parser.add_argument('--ldif-file', default="ldifgen.ldif", help=f"The LDIF file name.  Default location is the server's LDIF directory using the name 'ldifgen.ldif'")

    # Create a heavily nested LDIF
    dbgen_nested_parser = subcommands.add_parser('nested', help='Generate a heavily nested database LDIF in a cascading/fractal tree design')
    dbgen_nested_parser.set_defaults(func=dbgen_create_nested)
    dbgen_nested_parser.add_argument('--num-users', help="The total number of user entries to create in the entire LDIF (does not include the container entries).")
    dbgen_nested_parser.add_argument('--node-limit', help="The total number of user entries to create under each node/subtree")
    dbgen_nested_parser.add_argument('--suffix', help="The suffix DN for the LDIF")
    dbgen_nested_parser.add_argument('--ldif-file', default="ldifgen.ldif", help=f"The LDIF file name.  Default location is the server's LDIF directory using the name 'ldifgen.ldif'")
