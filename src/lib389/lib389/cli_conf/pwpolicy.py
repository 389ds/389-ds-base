# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.utils import ensure_str, ensure_list_str
from lib389.pwpolicy import PwPolicyEntries, PwPolicyManager
from lib389.idm.account import Account

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

def _args_to_attrs(args):
    attrs = {}
    for arg in vars(args):
        val = getattr(args, arg)
        if arg in arg_to_attr and val is not None:
            attrs[arg_to_attr[arg]] = val
    return attrs


def _get_policy_type(inst, dn=None):
    pwp_manager = PwPolicyManager(inst)
    if dn is None:
        return "Global Password Policy"
    elif pwp_manager.is_user_policy(dn):
        return "User Policy"
    elif pwp_manager.is_subtree_policy(dn):
        return "Subtree Policy"
    else:
        raise ValueError("The policy wasn't set up for the target dn entry or it is invalid")


def _get_pw_policy(inst, targetdn, log, use_json=None):
    pwp_manager = PwPolicyManager(inst)
    policy_type = _get_policy_type(inst, targetdn)

    if "global" in policy_type.lower():
        targetdn = 'cn=config'
        pwp_manager.pwp_attributes.extend(['passwordIsGlobalPolicy', 'nsslapd-pwpolicy_local'])
    else:
        targetdn = pwp_manager.get_pwpolicy_entry(targetdn).dn

    entries = inst.search_s(targetdn, ldap.SCOPE_BASE, 'objectclass=*', pwp_manager.pwp_attributes)
    entry = entries[0]

    if use_json:
        str_attrs = {}
        for k in entry.data:
            str_attrs[ensure_str(k)] = ensure_list_str(entry.data[k])

        # ensure all the keys are lowercase
        str_attrs = dict((k.lower(), v) for k, v in str_attrs.items())

        print(json.dumps({"type": "entry", "pwp_type": policy_type, "dn": ensure_str(targetdn), "attrs": str_attrs}))
    else:
        if "global" in policy_type.lower():
            response = "Global Password Policy: cn=config\n------------------------------------\n"
        else:
            response = "Local {} Policy: {}\n------------------------------------\n".format(policy_type, targetdn)
        for k in entry.data:
            response += "{}: {}\n".format(k, ensure_str(entry.data[k][0]))
        print(response)


def list_policies(inst, basedn, log, args):
    log = log.getChild('list_policies')
    targetdn = args.DN[0]
    pwp_manager = PwPolicyManager(inst)

    if args.json:
        result = {'type': 'list', 'items': []}
    else:
        result = ""

    # Verify target dn exists before getting started
    user_entry = Account(inst, args.DN[0])
    if not user_entry.exists():
        raise ValueError('The target entry dn does not exist')

    # User pwpolicy entry is under the container that is under the parent,
    # so we need to go one level up
    if pwp_manager.is_user_policy(targetdn):
        policy_type = _get_policy_type(inst, user_entry.dn)
        if args.json:
            result['items'].append([user_entry.dn, policy_type])
        else:
            result += "%s (%s)\n" % (user_entry.dn, policy_type.lower())
    else:
        pwp_entries = PwPolicyEntries(inst, targetdn)
        for pwp_entry in pwp_entries.list():
            cn = pwp_entry.get_attr_val_utf8_l('cn')
            if pwp_entry.is_subtree_policy():
                entrydn = cn.replace('cn=nspwpolicyentry_subtree,', '')
            else:
                entrydn = cn.replace('cn=nspwpolicyentry_user,', '')
            policy_type = _get_policy_type(inst, entrydn)

            if args.json:
                result['items'].append([entrydn, policy_type])
            else:
                result += "%s (%s)\n" % (entrydn, policy_type.lower())

    if args.json:
        print(json.dumps(result))
    else:
        print(result)


def get_local_policy(inst, basedn, log, args):
    log = log.getChild('get_local_policy')
    _get_pw_policy(inst, args.DN[0], log, args.json)


def get_global_policy(inst, basedn, log, args):
    log = log.getChild('get_global_policy')
    _get_pw_policy(inst, None, log, args.json)


def create_subtree_policy(inst, basedn, log, args):
    log = log.getChild('create_subtree_policy')
    # Gather the attributes
    attrs = _args_to_attrs(args)
    pwp_manager = PwPolicyManager(inst)
    pwp_manager.create_subtree_policy(args.DN[0], attrs)

    print('Successfully created subtree password policy')


def create_user_policy(inst, basedn, log, args):
    log = log.getChild('create_user_policy')
    # Gather the attributes
    attrs = _args_to_attrs(args)
    pwp_manager = PwPolicyManager(inst)
    pwp_manager.create_user_policy(args.DN[0], attrs)

    print('Successfully created user password policy')


def set_global_policy(inst, basedn, log, args):
    log = log.getChild('set_global_policy')
    # Gather the attributes
    attrs = _args_to_attrs(args)
    pwp_manager = PwPolicyManager(inst)
    pwp_manager.set_global_policy(attrs)

    print('Successfully updated global password policy')


def set_local_policy(inst, basedn, log, args):
    log = log.getChild('set_local_policy')
    targetdn = args.DN[0]
    # Gather the attributes
    attrs = _args_to_attrs(args)
    pwp_manager = PwPolicyManager(inst)
    pwp_entry = pwp_manager.get_pwpolicy_entry(args.DN[0])
    policy_type = _get_policy_type(inst, targetdn)

    modlist = []
    for attr, value in attrs.items():
        modlist.append((attr, value))
    if len(modlist) > 0:
        pwp_entry.replace_many(*modlist)
    else:
        raise ValueError("There are no password policies to set")

    print('Successfully updated %s' % policy_type.lower())


def del_local_policy(inst, basedn, log, args):
    log = log.getChild('del_local_policy')
    targetdn = args.DN[0]
    policy_type = _get_policy_type(inst, targetdn)
    pwp_manager = PwPolicyManager(inst)
    pwp_manager.delete_local_policy(targetdn)
    print('Successfully deleted %s' % policy_type.lower())


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
    set_parser.add_argument('--pwdmaxfailures', help="The maximum number of allowed failed password attempts before the account gets locked")
    set_parser.add_argument('--pwdresetfailcount', help="The number of seconds to wait before reducing the failed login count on an account")
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