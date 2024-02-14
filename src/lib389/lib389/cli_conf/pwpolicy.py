# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
from lib389.backend import Backends
from lib389.utils import ensure_str
from lib389.pwpolicy import PwPolicyEntries, PwPolicyManager
from lib389.password_plugins import PasswordPlugins
from lib389.idm.account import Account
from lib389.cli_base import CustomHelpFormatter


def _args_to_attrs(args, arg_to_attr):
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
    elif pwp_manager.is_subtree_policy(dn):
        return "Subtree Policy"
    else:
        return "User Policy"


def _get_pw_policy(inst, targetdn, log, use_json=None):
    pwp_manager = PwPolicyManager(inst)
    policy_type = _get_policy_type(inst, targetdn)
    attr_list = list(pwp_manager.arg_to_attr.values())
    if "global" in policy_type.lower():
        targetdn = 'cn=config'
        policydn = targetdn
        basedn = targetdn
        attr_list.extend(['passwordisglobalpolicy', 'nsslapd-pwpolicy_local'])
        all_attrs = inst.config.get_attrs_vals_utf8(attr_list)
        attrs = {k: v for k, v in all_attrs.items() if len(v) > 0}
    else:
        policy = pwp_manager.get_pwpolicy_entry(targetdn)
        basedn = policy.get_basedn()
        policydn = policy.dn
        all_attrs = policy.get_attrs_vals_utf8(attr_list)
        attrs = {k: v for k, v in all_attrs.items() if len(v) > 0}
    if use_json:
        log.info(json.dumps({
                 "dn": ensure_str(policydn),
                 "targetdn": targetdn,
                 "type": "entry",
                 "pwp_type": policy_type,
                 "basedn": basedn,
                 "attrs": attrs}, indent=4))
    else:
        if "global" in policy_type.lower():
            response = "Global Password Policy: cn=config\n------------------------------------\n"
        else:
            response = "Local {} Policy for \"{}\": {}\n------------------------------------\n".format(policy_type, targetdn, policydn)
        for key, value in list(attrs.items()):
            if len(value) == 0:
                value = ""
            else:
                value = value[0]
            response += "{}: {}\n".format(key, value)
        log.info(response)


def list_policies(inst, basedn, log, args):
    log = log.getChild('list_policies')

    if args.DN is None:
        # list all the password policies for all the backends
        targetdns = []
        backends = Backends(inst).list()
        for backend in backends:
            targetdns.append(backend.get_suffix())
    else:
        targetdns = [args.DN]

    if args.json:
        result = {'type': 'list', 'items': []}
    else:
        result = ""

    for targetdn in targetdns:
        # Verify target dn exists before getting started
        user_entry = Account(inst, targetdn)
        if not user_entry.exists():
            raise ValueError('The target entry dn does not exist')

        # User pwpolicy entry is under the container that is under the parent,
        # so we need to go one level up
        pwp_entries = PwPolicyEntries(inst, targetdn)
        pwp_manager = PwPolicyManager(inst)
        attr_list = list(pwp_manager.arg_to_attr.values())

        for pwp_entry in pwp_entries.list():
            # Sometimes, the cn value includes quotes (for example, after migration from pre-CLI version).
            # We need to strip them as python-ldap doesn't expect them
            dn_comps_str = pwp_entry.get_attr_val_utf8_l('cn').strip("\'").strip("\"")
            dn_comps = ldap.dn.explode_dn(dn_comps_str)
            dn_comps.pop(0)
            entrydn = ",".join(dn_comps)
            policy_type = _get_policy_type(inst, entrydn)
            all_attrs = pwp_entry.get_attrs_vals_utf8(attr_list)
            attrs = {k: v for k, v in all_attrs.items() if len(v) > 0}
            if args.json:
                result['items'].append(
                    {
                        "dn": pwp_entry.dn,
                        "targetdn": entrydn,
                        "pwp_type": policy_type,
                        "basedn": pwp_entry.get_basedn(),
                        "attrs": attrs
                    }
                )
            else:
                result += "%s (%s)\n" % (entrydn, policy_type.lower())

    if args.json:
        log.info(json.dumps(result, indent=4))
    else:
        log.info(result)


def get_local_policy(inst, basedn, log, args):
    log = log.getChild('get_local_policy')
    _get_pw_policy(inst, args.DN[0], log, args.json)


def get_global_policy(inst, basedn, log, args):
    log = log.getChild('get_global_policy')
    _get_pw_policy(inst, None, log, args.json)


def create_subtree_policy(inst, basedn, log, args):
    log = log.getChild('create_subtree_policy')
    # Gather the attributes
    pwp_manager = PwPolicyManager(inst)
    attrs = _args_to_attrs(args, pwp_manager.arg_to_attr)
    pwp_manager.create_subtree_policy(args.DN[0], attrs)

    log.info('Successfully created subtree password policy')


def create_user_policy(inst, basedn, log, args):
    log = log.getChild('create_user_policy')
    pwp_manager = PwPolicyManager(inst)
    attrs = _args_to_attrs(args, pwp_manager.arg_to_attr)
    pwp_manager.create_user_policy(args.DN[0], attrs)

    log.info('Successfully created user password policy')


def set_global_policy(inst, basedn, log, args):
    log = log.getChild('set_global_policy')
    pwp_manager = PwPolicyManager(inst)
    attrs = _args_to_attrs(args, pwp_manager.arg_to_attr)
    pwp_manager.set_global_policy(attrs)
    log.info('Successfully updated global password policy')


def set_local_policy(inst, basedn, log, args):
    log = log.getChild('set_local_policy')
    targetdn = args.DN[0]
    pwp_manager = PwPolicyManager(inst)
    attrs = _args_to_attrs(args, pwp_manager.arg_to_attr)
    pwp_entry = pwp_manager.get_pwpolicy_entry(args.DN[0])
    policy_type = _get_policy_type(inst, targetdn)

    modlist = []
    for attr, value in attrs.items():
        modlist.append((attr, value))
    if len(modlist) > 0:
        pwp_entry.replace_many(*modlist)
    else:
        raise ValueError("There are no password policies to set")

    log.info('Successfully updated %s' % policy_type.lower())


def del_local_policy(inst, basedn, log, args):
    log = log.getChild('del_local_policy')
    targetdn = args.DN[0]
    policy_type = _get_policy_type(inst, targetdn)
    pwp_manager = PwPolicyManager(inst)
    pwp_manager.delete_local_policy(targetdn)
    log.info('Successfully deleted %s' % policy_type.lower())


def list_schemes(inst, basedn, log, args):
    schemes = PasswordPlugins(inst).list()
    scheme_list = []
    for scheme in schemes:
        scheme_list.append(scheme.get_attr_val_utf8('cn'))

    if args.json:
        result = {'type': 'list', 'items': scheme_list}
        log.info(json.dumps(result, indent=4))
    else:
        for scheme in scheme_list:
            log.info(scheme)


def create_parser(subparsers):
    # Create our two parsers for local and global policies
    globalpwp_parser = subparsers.add_parser('pwpolicy', help='Manage the global password policy settings', formatter_class=CustomHelpFormatter)
    localpwp_parser = subparsers.add_parser('localpwp', help='Manage the local user and subtree password policies', formatter_class=CustomHelpFormatter)

    ############################################
    # Local password policies
    ############################################
    local_subcommands = localpwp_parser.add_subparsers(help='Local password policy')
    # List all the local policies
    list_parser = local_subcommands.add_parser('list', help='List all the local password policies', formatter_class=CustomHelpFormatter)
    list_parser.set_defaults(func=list_policies)
    list_parser.add_argument('DN', nargs='?', help='Suffix to search for local password policies')
    # Get a local policy
    get_parser = local_subcommands.add_parser('get', help='Get local password policy entry', formatter_class=CustomHelpFormatter)
    get_parser.set_defaults(func=get_local_policy)
    get_parser.add_argument('DN', nargs=1, help='Get the local policy for this entry DN')
    # The "set" arguments...
    set_parser = local_subcommands.add_parser('set', help='Set an attribute in a local password policy', formatter_class=CustomHelpFormatter)
    set_parser.set_defaults(func=set_local_policy)
    # General settings
    set_parser.add_argument('--pwdscheme', help="The password storage scheme")
    set_parser.add_argument('--pwdchange', help="Allow users to change their passwords")
    set_parser.add_argument('--pwdmustchange', help="Users must change their password after it was reset by an administrator")
    set_parser.add_argument('--pwdhistory', help="To enable password history set this to \"on\", otherwise \"off\"")
    set_parser.add_argument('--pwdhistorycount', help="The number of passwords to keep in history")
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
    set_parser.add_argument('--pwdchecksyntax', help="Set to \"on\" to enable password syntax checking")
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
    set_parser.add_argument('--pwdmincatagories', help="The minimum number of syntax category checks")
    set_parser.add_argument('--pwdmintokenlen', help="Sets the smallest attribute value length that is used for trivial/user words checking.  This also impacts \"--pwduserattrs\"")
    set_parser.add_argument('--pwdbadwords', help="A space-separated list of words that can not be in a password")
    set_parser.add_argument('--pwduserattrs', help="A space-separated list of attributes whose values can not appear in the password (See \"--pwdmintokenlen\")")
    set_parser.add_argument('--pwddictcheck', help="Set to \"on\" to enforce CrackLib dictionary checking")
    set_parser.add_argument('--pwddictpath', help="Filesystem path to specific/custom CrackLib dictionary files")
    set_parser.add_argument('--pwptprmaxuse', help="Number of times a reset password can be used for authentication")
    set_parser.add_argument('--pwptprdelayexpireat', help="Number of seconds after which a reset password expires")
    set_parser.add_argument('--pwptprdelayvalidfrom', help="Number of seconds to wait before using a reset password to authenticated")
    # delete local password policy
    del_parser = local_subcommands.add_parser('remove', help='Remove a local password policy', formatter_class=CustomHelpFormatter)
    del_parser.set_defaults(func=del_local_policy)
    del_parser.add_argument('DN', nargs=1, help='Remove local policy for this entry DN')
    #
    # create USER local password policy
    #
    add_user_parser = local_subcommands.add_parser('adduser', add_help=False, parents=[set_parser], help='Add new user password policy', formatter_class=CustomHelpFormatter)
    add_user_parser.set_defaults(func=create_user_policy)
    #
    # create SUBTREE local password policy
    #
    add_subtree_parser = local_subcommands.add_parser('addsubtree', add_help=False, parents=[set_parser], help='Add new subtree password policy', formatter_class=CustomHelpFormatter)
    add_subtree_parser.set_defaults(func=create_subtree_policy)

    ###########################################
    # The global policy (cn=config)
    ###########################################
    global_subcommands = globalpwp_parser.add_subparsers(help='Global password policy')
    # Get policy
    get_global_parser = global_subcommands.add_parser('get', help='Get the global password policy entry', formatter_class=CustomHelpFormatter)
    get_global_parser.set_defaults(func=get_global_policy)
    # Set policy
    set_global_parser = global_subcommands.add_parser('set', add_help=False, parents=[set_parser],
                                                      help='Set an attribute in a global password policy')
    set_global_parser.set_defaults(func=set_global_policy)
    set_global_parser.add_argument('--pwdlocal', help="Set to \"on\" to enable fine-grained (subtree/user-level) password policies")
    set_global_parser.add_argument('--pwdisglobal', help="Set to \"on\" to enable password policy state attributes to be replicated")
    set_global_parser.add_argument('--pwdallowhash', help="Set to \"on\" to allow adding prehashed passwords")
    set_global_parser.add_argument('--pwpinheritglobal', help="Set to \"on\" to allow local policies to inherit the global policy")
    # list password storage schemes
    list_scehmes_parser = global_subcommands.add_parser('list-schemes', help='Get a list of the current password storage schemes', formatter_class=CustomHelpFormatter)
    list_scehmes_parser.set_defaults(func=list_schemes)

    #############################################
    # Wrap it up.  Now that we copied all the parent arguments to the subparsers,
    # and the DN argument needed only by the local policies
    #############################################
    set_parser.add_argument('DN', nargs=1, help='Set the local policy for this entry DN')
    add_subtree_parser.add_argument('DN', nargs=1, help='Add/replace the subtree policy for this entry DN')
    add_user_parser.add_argument('DN', nargs=1, help='Add/replace the local password policy for this entry DN')
