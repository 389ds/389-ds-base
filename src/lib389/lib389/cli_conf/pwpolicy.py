# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap

arg_to_attr = {
        'pwdlocal': 'nsslapd-pwpolicy-local',
        'pwdscheme': 'passwordstoragescheme',
        'pwdchange': 'passwordChange',
        'pwdmustchange': 'passwordMustChange',
        'pwdhistory': 'passwordHistory',
        'pwdhistorycount': 'passwordInHistory',
        'pwdadmin': 'passwordAdminDN',
        'pwdtrack': 'passwordTrackUpdateTime',
        'pwdwarning': 'passwordWarning',
        'pwdisglobal': 'passwordIsGlobalPolicy',
        'pwdexpire': 'passwordExp',
        'pwdmaxage': 'passwordMaxAge',
        'pwdminage': 'passwordMinAge',
        'pwdgracelimit': 'passwordGraceLimit',
        'pwdsendexpiring': 'passwordSendExpiringTime',
        'pwdlockout': 'passwordLockout',
        'pwdunlock': 'passwordUnlock',
        'pwdlockoutduration': 'passwordLockoutDuration',
        'pwdmaxfailures': 'passwordMaxFailure',
        'pwdresetfailcount': 'passwordResetFailureCount',
        'pwdchecksyntax': 'passwordCheckSyntax',
        'pwdminlen': 'passwordMinLength',
        'pwdmindigits': 'passwordMinDigits',
        'pwdminalphas': 'passwordMinAlphas',
        'pwdminuppers': 'passwordMinUppers',
        'pwdminlowers': 'passwordMinLowers',
        'pwdminspecials': 'passwordMinSpecials',
        'pwdmin8bits': 'passwordMin8bit',
        'pwdmaxrepeats': 'passwordMaxRepeats',
        'pwdpalindrome': 'passwordPalindrome',
        'pwdmaxseq': 'passwordMaxSequence',
        'pwdmaxseqsets': 'passwordMaxSeqSets',
        'pwdmaxclasschars': 'passwordMaxClassChars',
        'pwdmincatagories': 'passwordMinCategories',
        'pwdmintokenlen': 'passwordMinTokenLength',
        'pwdbadwords': 'passwordBadWords',
        'pwduserattrs': 'passwordUserAttributes',
        'pwddictcheck': 'passwordDictCheck',
        'pwddictpath': 'passwordDictPath',
        'pwdallowhash': 'nsslapd-allow-hashed-passwords'
    }


def list_policies(inst, basedn, log, args):
    print(inst.pwpolicy.list_policies(args.DN[0], use_json=args.json))


def get_local_policy(inst, basedn, log, args):
    print(inst.pwpolicy.get_pwpolicy(targetdn=args.DN[0], use_json=args.json))


def get_global_policy(inst, basedn, log, args):
    print(inst.pwpolicy.get_pwpolicy(targetdn=None, use_json=args.json))


def create_subtree_policy(inst, basedn, log, args):
    try:
        inst.pwpolicy.create_subtree_policy(args.DN[0], args, arg_to_attr)
        print('Successfully created subtree password policy')
    except ldap.ALREADY_EXISTS:
        raise ValueError('There is already a subtree password policy created for this entry')


def create_user_policy(inst, basedn, log, args):
    try:
        inst.pwpolicy.create_user_policy(args.DN[0], args, arg_to_attr)
        print('Successfully created user password policy')
    except ldap.ALREADY_EXISTS:
        raise ValueError('There is already a user password policy created for this entry')


def set_global_policy(inst, basedn, log, args):
    for arg in vars(args):
        val = getattr(args, arg)
        if arg in arg_to_attr and val is not None:
            inst.config.set(arg_to_attr[arg], val)
    print("Successfully updated global policy")


def set_local_policy(inst, basedn, log, args):
    inst.pwpolicy.set_policy(args.DN[0], args, arg_to_attr)
    print("Successfully updated local policy")


def del_local_policy(inst, basedn, log, args):
    inst.pwpolicy.delete_local_policy(args.DN[0])
    print("Successfully removed local password policy")


def create_parser(subparsers):
    # Create our two parsers for local and global policies
    globalpwp_parser = subparsers.add_parser('pwpolicy', help='Get and set the global password policy settings')
    localpwp_parser = subparsers.add_parser('localpwp', help='Manage local (user/subtree) password policies')

    ############################################
    # Local password policies
    ############################################
    local_subcommands = localpwp_parser.add_subparsers(help='Local password policy')
    # List all the local policies
    list_parser = local_subcommands.add_parser('list', help='List all the local password policies')
    list_parser.set_defaults(func=list_policies)
    list_parser.add_argument('DN', nargs=1, help='Suffix to search for local password policies')
    # Get a local policy
    get_parser = local_subcommands.add_parser('get', help='Get local password policy entry')
    get_parser.set_defaults(func=get_local_policy)
    get_parser.add_argument('DN', nargs=1, help='Get the local policy for this entry DN')
    # The "set" arguments...
    set_parser = local_subcommands.add_parser('set', help='Set an attribute in a local password policy')
    set_parser.set_defaults(func=set_local_policy)
    # General settings
    set_parser.add_argument('--pwdscheme', help="The password storage scheme")
    set_parser.add_argument('--pwdchange', help="Allow users to change their passwords")
    set_parser.add_argument('--pwdmustchange', help="User must change their passwrod after it is reset by an Administrator")
    set_parser.add_argument('--pwdhistory', help="To enable password history set this to \"on\", otherwise \"off\"")
    set_parser.add_argument('--pwdhistorycount', help="The number of password to keep in history")
    set_parser.add_argument('--pwdadmin', help="The DN of an entry or a group of account that can bypass password policy constraints")
    set_parser.add_argument('--pwdtrack', help="Set to \"on\" to track the time the password was last changed")
    set_parser.add_argument('--pwdwarning', help="Send an expiring warning if password expires within this time (in seconds)")
    # Expiration settings
    set_parser.add_argument('--pwdexpire', help="Set to \"on\" to enable password expiration")
    set_parser.add_argument('--pwdmaxage', help="The password expiration time in seconds")
    set_parser.add_argument('--pwdminage', help="The number of seconds that must pass before a user can change their password")
    set_parser.add_argument('--pwdgracelimit', help="The number of allowed logins after the password has expired")
    set_parser.add_argument('--pwdsendexpiring', help="Set to \"on\" to always send the expiring control regardless of the warning period")
    # Account lockout settings
    set_parser.add_argument('--pwdlockout', help="Set to \"on\" to enable account lockout")
    set_parser.add_argument('--pwdunlock', help="Set to \"on\" to allow an account to become unlocked after the lockout duration")
    set_parser.add_argument('--pwdlockoutduration', help="The number of seconds an account stays locked out")
    set_parser.add_argument('--pwdmaxfailures', help="The maximum number of allowed failed password attempts beforet the acocunt gets locked")
    set_parser.add_argument('--pwdresetfailcount', help="The number of secondsto wait before reducingthe failed login count on an account")
    # Syntax settings
    set_parser.add_argument('--pwdchecksyntax', help="Set to \"on\" to Enable password syntax checking")
    set_parser.add_argument('--pwdminlen', help="The minimum number of characters required in a password")
    set_parser.add_argument('--pwdmindigits', help="The minimum number of digit/number characters in a password")
    set_parser.add_argument('--pwdminalphas', help="The minimum number of alpha characters required in a password")
    set_parser.add_argument('--pwdminuppers', help="The minimum number of uppercase characters required in a password")
    set_parser.add_argument('--pwdminlowers', help="The minimum number of lowercase characters required in a password")
    set_parser.add_argument('--pwdminspecials', help="The minimum number of special characters required in a password")
    set_parser.add_argument('--pwdmin8bits', help="The minimum number of 8-bit characters required in a password")
    set_parser.add_argument('--pwdmaxrepeats', help="The maximum number of times the same character can appear sequentially in the password")
    set_parser.add_argument('--pwdpalindrome', help="Set to \"on\" to reject passwords that are palindromes")
    set_parser.add_argument('--pwdmaxseq', help="The maximum number of allowed monotonic character sequences in a password")
    set_parser.add_argument('--pwdmaxseqsets', help="The maximum number of allowed monotonic character sequences that can be duplicated in a password")
    set_parser.add_argument('--pwdmaxclasschars', help="The maximum number of sequential characters from the same character class that is allowed in a password")
    set_parser.add_argument('--pwdmincatagories', help="The minimum number of syntax catagory checks")
    set_parser.add_argument('--pwdmintokenlen', help="Sets the smallest attribute value length that is used for trivial/user words checking.  This also impacts \"--pwduserattrs\"")
    set_parser.add_argument('--pwdbadwords', help="A space-separated list of words that can not be in a password")
    set_parser.add_argument('--pwduserattrs', help="A space-separated list of attributes whose values can not appear in the password (See \"--pwdmintokenlen\")")
    set_parser.add_argument('--pwddictcheck', help="Set to \"on\" to enfore CrackLib dictionary checking")
    set_parser.add_argument('--pwddictpath', help="Filesystem path to specific/custom CrackLib dictionary files")
    # delete local password policy
    del_parser = local_subcommands.add_parser('remove', help='Remove a local password policy')
    del_parser.set_defaults(func=del_local_policy)
    del_parser.add_argument('DN', nargs=1, help='Remove local policy for this entry DN')
    #
    # create USER local password policy
    #
    add_user_parser = local_subcommands.add_parser('adduser', add_help=False, parents=[set_parser], help='Add new user password policy')
    add_user_parser.set_defaults(func=create_user_policy)
    #
    # create SUBTREE local password policy
    #
    add_subtree_parser = local_subcommands.add_parser('addsubtree', add_help=False, parents=[set_parser], help='Add new subtree password policy')
    add_subtree_parser.set_defaults(func=create_subtree_policy)

    ###########################################
    # The global policy (cn=config)
    ###########################################
    global_subcommands = globalpwp_parser.add_subparsers(help='Global password policy')
    # Get policy
    get_global_parser = global_subcommands.add_parser('get', help='Get the global password policy entry')
    get_global_parser.set_defaults(func=get_global_policy)
    # Set policy
    set_global_parser = global_subcommands.add_parser('set', add_help=False, parents=[set_parser],
                                                      help='Set an attribute in a global password policy')
    set_global_parser.set_defaults(func=set_global_policy)
    set_global_parser.add_argument('--pwdlocal', help="Set to \"on\" to enable fine-grained (subtree/user-level) password policies")
    set_global_parser.add_argument('--pwdisglobal', help="Set to \"on\" to enable password policy state attributesto be replicated")
    set_global_parser.add_argument('--pwdallowhash', help="Set to \"on\" to allow adding prehashed passwords")

    #############################################
    # Wrap it up.  Now that we copied all the parent arugments to the subparsers,
    # and the DN argument needed only by the local policies
    #############################################
    set_parser.add_argument('DN', nargs=1, help='Set the local policy for this entry DN')
    add_subtree_parser.add_argument('DN', nargs=1, help='Add/replace the subtree policy for this entry DN')
    add_user_parser.add_argument('DN', nargs=1, help='Add/replace the local password policy for this entry DN')