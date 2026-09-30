# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026, Red Hat inc,
# Copyright (C) 2018, William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
import ldap
import math
import time
from datetime import datetime
from lib389.idm.account import Account, Accounts, AccountState
from lib389.cli_idm import (
    _generic_list,
    _generic_delete,
    _generic_get_dn
)
from lib389.cli_base import (
    _generic_modify_dn,
    _get_arg,
    _get_dn_arg,
    _warn,
    CustomHelpFormatter
    )
from lib389.cli_idm import _generic_rename_dn
from lib389.plugins import AccountPolicyPlugin, AccountPolicyConfig, AccountPolicyEntry
from lib389.cos import CosTemplates
from lib389.mappingTree import MappingTrees
from lib389.idm.role import Roles
from lib389.utils import gentime_to_posix_time

MANY = Accounts
SINGULAR = Account

# Admin reset with passwordMustChange=on records this sentinel expiration time.
PW_MUST_RESET = "19700101000000z"
_LIST_SEPARATOR = "----------------------------------------------------------"
PLUGIN_DISABLED_MSG = "Function is not available because Account Policy Plugin is not enabled"
PLUGIN_UNCONFIGURED_MSG = "Function is not available because Account Policy Plugin is not configured"
PLUGIN_INCOMPLETE_MSG = "Function is not available because Account Policy Plugin is not fully configured"


def _is_base_entry(account, basedn):
    return account.dn.lower() == basedn.lower()


def _iter_accounts(inst, basedn, entries=None):
    if entries is None:
        entries = Accounts(inst, basedn).list()
    for account in entries:
        if not _is_base_entry(account, basedn):
            yield account


def _get_disabled_role_dns(inst, root_suffix):
    if not root_suffix:
        return set()
    try:
        roles = Roles(inst, root_suffix)
        return set(role.dn.lower() for role in roles.get_disabled_roles())
    except ldap.NO_SUCH_OBJECT:
        return set()


def _load_account_policy(inst, basedn, required=False, need_limit=True):
    """Return (plugin_enabled, state_dict) for Account Policy settings.

    If required is True, raise ValueError when the plugin is disabled or
    missing its config entry. When required is False, incomplete inactivity
    settings are skipped so callers such as --locked can still detect
    nsAccountLock and role locks. need_limit=False skips CoS/inactivity-limit
    lookup (used by never-logged-in, which only needs stateattrname).
    """
    state_dict = {
        "state_attr": "",
        "alt_state_attr": "",
        "spec_attr": "",
        "limit_attr": "",
        "limit": None,
        "root_suffix": basedn,
    }
    acct_plugin = AccountPolicyPlugin(inst)
    try:
        enabled = acct_plugin.status()
    except IndexError:
        enabled = False

    if not enabled:
        if required:
            raise ValueError(PLUGIN_DISABLED_MSG)
        return False, state_dict

    config_dn = acct_plugin.get_attr_val_utf8("nsslapd-pluginarg0")
    if config_dn is None:
        if required:
            raise ValueError(PLUGIN_UNCONFIGURED_MSG)
        return True, state_dict

    config = AccountPolicyConfig(inst, config_dn)
    config_settings = config.get_attrs_vals_utf8(["stateattrname", "altstateattrname",
                                                  "specattrname", "limitattrname"])
    state_dict["state_attr"] = (config_settings.get("stateattrname") or [""])[0]
    state_dict["alt_state_attr"] = (config_settings.get("altstateattrname") or [""])[0]
    state_dict["spec_attr"] = (config_settings.get("specattrname") or [""])[0]
    state_dict["limit_attr"] = (config_settings.get("limitattrname") or [""])[0]

    try:
        mapping_trees = MappingTrees(inst)
        state_dict["root_suffix"] = mapping_trees.get_root_suffix_by_entry(basedn)
    except ldap.NO_SUCH_OBJECT:
        pass

    if not need_limit:
        if required and state_dict["state_attr"] == "":
            raise ValueError(PLUGIN_INCOMPLETE_MSG)
        return True, state_dict

    if not state_dict["state_attr"] or not state_dict["limit_attr"]:
        if required:
            raise ValueError(PLUGIN_INCOMPLETE_MSG)
        # Direct and role locks do not need inactivity-policy attributes.
        return True, state_dict

    accpol_entry = config
    if state_dict["spec_attr"]:
        cos_entries = CosTemplates(inst, state_dict["root_suffix"])
        for cos in cos_entries.list():
            if cos.present(state_dict["spec_attr"]):
                accpol_entry_dn = cos.get_attr_val_utf8_l(state_dict["spec_attr"])
                if accpol_entry_dn:
                    accpol_entry = AccountPolicyEntry(inst, accpol_entry_dn)
                    break
    state_dict["limit"] = accpol_entry.get_attr_val_utf8_l(state_dict["limit_attr"])
    return True, state_dict


def _emit_account_list(log, args, json_result, sections, empty_msg):
    """Print JSON or grouped text results.

    sections is a list of (title, dns) tuples used for the non-JSON path.
    """
    if args.json:
        log.info(json.dumps(json_result, indent=2))
        return
    printed = False
    for title, dns in sections:
        if dns:
            log.info("\n{} ({}):".format(title, len(dns)))
            log.info(_LIST_SEPARATOR)
            for dn in dns:
                log.info("- " + dn)
            printed = True
    if not printed:
        log.info(empty_msg)


def get_status(inst, account, process_account_policy, state_dict, disabled_dns=None):
    # Lightweight version of Account.status()
    fetch_attrs = ["nsAccountLock", "nsRole"]
    if state_dict["state_attr"]:
        fetch_attrs.append(state_dict["state_attr"])
    # "1.1" is the Account Policy "disabled" OID for altstateattrname
    if state_dict["alt_state_attr"] and state_dict["alt_state_attr"] != "1.1":
        fetch_attrs.append(state_dict["alt_state_attr"])
    account_data = account.get_attrs_vals_utf8(fetch_attrs)

    last_login_time = ""
    if state_dict["state_attr"]:
        last_login_time = account._dict_get_with_ignore_indexerror(account_data, state_dict["state_attr"])
    if not last_login_time and state_dict["alt_state_attr"] in account_data:
        last_login_time = account._dict_get_with_ignore_indexerror(account_data, state_dict["alt_state_attr"])
    acct_roles = [role.lower() for role in account_data.get("nsRole", account_data.get("nsrole", []))]

    if acct_roles:
        if disabled_dns is None:
            disabled_dns = _get_disabled_role_dns(inst, state_dict["root_suffix"])
        for role in acct_roles:
            if role in disabled_dns:
                return AccountState.INDIRECTLY_LOCKED

    if account._dict_get_with_ignore_indexerror(account_data, "nsAccountLock") == "true":
        return AccountState.DIRECTLY_LOCKED

    if process_account_policy and last_login_time and state_dict["limit"]:
        remaining_time = float(state_dict["limit"]) - (time.mktime(time.gmtime()) - gentime_to_posix_time(last_login_time))
        if remaining_time <= 0:
            return AccountState.INACTIVITY_LIMIT_EXCEEDED

    return AccountState.ACTIVATED


def list(inst, basedn, log, args):
    if getattr(args, 'locked', False):
        list_locked(inst, basedn, log, args)
    elif getattr(args, 'expired_password', False):
        list_expired_password(inst, basedn, log, args)
    elif getattr(args, 'expiring_password', None) is not None:
        list_expiring_password(inst, basedn, log, args)
    elif getattr(args, 'inactive', False):
        list_inactive(inst, basedn, log, args)
    elif getattr(args, 'never_logged_in', False):
        list_never_logged_in(inst, basedn, log, args)
    elif getattr(args, 'must_reset_password', False):
        list_must_reset_password(inst, basedn, log, args)
    else:
        _generic_list(inst, basedn, log.getChild('_generic_list'), MANY, args)


def list_locked(inst, basedn, log, args):
    process_account_policy, state_dict = _load_account_policy(inst, basedn, required=False)
    disabled_dns = _get_disabled_role_dns(inst, state_dict["root_suffix"])
    directly_locked_accounts = []
    indirectly_locked_accounts = []
    inactive_accounts = []

    for account in _iter_accounts(inst, basedn):
        state = get_status(inst, account, process_account_policy, state_dict, disabled_dns)
        if state == AccountState.DIRECTLY_LOCKED:
            directly_locked_accounts.append(account.dn)
        elif state == AccountState.INDIRECTLY_LOCKED:
            indirectly_locked_accounts.append(account.dn)
        elif state == AccountState.INACTIVITY_LIMIT_EXCEEDED:
            inactive_accounts.append(account.dn)

    _emit_account_list(
        log, args,
        {
            "directly_locked": {
                "count": len(directly_locked_accounts),
                "accounts": directly_locked_accounts,
            },
            "indirectly_locked": {
                "count": len(indirectly_locked_accounts),
                "accounts": indirectly_locked_accounts,
            },
            "inactivity_locked": {
                "count": len(inactive_accounts),
                "accounts": inactive_accounts,
            },
        },
        [
            ("Directly locked accounts", directly_locked_accounts),
            ("Indirectly locked accounts", indirectly_locked_accounts),
            ("Inactivity locked accounts", inactive_accounts),
        ],
        "There are no locked accounts"
    )


def list_expired_password(inst, basedn, log, args):
    expired_password_accounts = []
    utc_now = gentime_to_posix_time(datetime.utcnow().strftime('%Y%m%d%H%M%SZ'))

    for account in _iter_accounts(inst, basedn):
        expire_time = account.get_attr_val_utf8_l("passwordexpirationtime")
        if expire_time is None or expire_time == PW_MUST_RESET:
            continue
        if gentime_to_posix_time(expire_time) <= utc_now:
            expired_password_accounts.append(account.dn)

    _emit_account_list(
        log, args,
        {
            "expired_password": {
                "count": len(expired_password_accounts),
                "accounts": expired_password_accounts,
            },
        },
        [("Expired password accounts", expired_password_accounts)],
        "There are no expired password accounts"
    )


def list_must_reset_password(inst, basedn, log, args):
    must_reset_password_accounts = []
    for account in _iter_accounts(inst, basedn):
        expire_time = account.get_attr_val_utf8_l("passwordexpirationtime")
        if expire_time == PW_MUST_RESET:
            must_reset_password_accounts.append(account.dn)

    _emit_account_list(
        log, args,
        {
            "must_reset_password": {
                "count": len(must_reset_password_accounts),
                "accounts": must_reset_password_accounts,
            },
        },
        [("Must reset password accounts", must_reset_password_accounts)],
        "There are no must-reset password accounts"
    )


def list_expiring_password(inst, basedn, log, args):
    try:
        days = int(args.expiring_password)
    except (TypeError, ValueError):
        raise ValueError("expiring-password requires a positive number of days")
    if days < 1:
        raise ValueError("expiring-password requires a positive number of days")

    expiring_password_accounts = []
    utc_now_epoch = gentime_to_posix_time(datetime.utcnow().strftime('%Y%m%d%H%M%SZ'))
    future_expire_time = utc_now_epoch + (days * 86400)
    for account in _iter_accounts(inst, basedn):
        expire_time = account.get_attr_val_utf8_l("passwordexpirationtime")
        if expire_time is None or expire_time == PW_MUST_RESET:
            continue
        account_expire_time = gentime_to_posix_time(expire_time)
        # Still valid, but expires within the requested window
        if utc_now_epoch < account_expire_time <= future_expire_time:
            expiring_password_accounts.append(account.dn)

    _emit_account_list(
        log, args,
        {
            "expiring_password": {
                "count": len(expiring_password_accounts),
                "accounts": expiring_password_accounts,
            },
        },
        [("Expiring password accounts within {} days".format(days), expiring_password_accounts)],
        "There are no expiring password accounts in the next {} days".format(days)
    )


def list_inactive(inst, basedn, log, args):
    process_account_policy, state_dict = _load_account_policy(inst, basedn, required=True)
    disabled_dns = _get_disabled_role_dns(inst, state_dict["root_suffix"])
    inactive_accounts = []

    for account in _iter_accounts(inst, basedn):
        state = get_status(inst, account, process_account_policy, state_dict, disabled_dns)
        if state == AccountState.INACTIVITY_LIMIT_EXCEEDED:
            inactive_accounts.append(account.dn)

    _emit_account_list(
        log, args,
        {
            "inactive_accounts": {
                "count": len(inactive_accounts),
                "accounts": inactive_accounts,
            },
        },
        [("Inactivity locked accounts", inactive_accounts)],
        "There are no inactive accounts"
    )


def list_never_logged_in(inst, basedn, log, args):
    _, state_dict = _load_account_policy(inst, basedn, required=True, need_limit=False)
    state_attr = state_dict["state_attr"]

    accounts_obj = Accounts(inst, basedn)
    # Restrict to user-like objectclasses so roles/groups are not listed
    accounts_obj._objectclasses = [
        'person',
        'inetOrgPerson',
        'organizationalPerson',
        'nsPerson',
        'nsAccount',
        'nsOrgPerson',
        'account',
        'posixAccount',
    ]
    never_logged_in_accounts = []
    no_password_accounts = []
    for account in _iter_accounts(inst, basedn, accounts_obj.list()):
        account_settings = account.get_attrs_vals_utf8([state_attr, "userpassword"])
        if len(account_settings.get(state_attr, [])) == 0:
            never_logged_in_accounts.append(account.dn)
        elif len(account_settings.get("userpassword", [])) == 0:
            no_password_accounts.append(account.dn)

    _emit_account_list(
        log, args,
        {
            "never_logged_in": {
                "count": len(never_logged_in_accounts),
                "accounts": never_logged_in_accounts,
            },
            "no_password": {
                "count": len(no_password_accounts),
                "accounts": no_password_accounts,
            },
        },
        [
            ("Never logged in accounts", never_logged_in_accounts),
            ("No password accounts", no_password_accounts),
        ],
        "There are no never-logged-in or no-password accounts"
    )


def get_dn(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to retrieve")
    _generic_get_dn(inst, basedn, log.getChild('_generic_get_dn'), MANY, dn, args)


def delete(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to delete")
    if warn:
        _warn(dn, msg="Deleting %s %s" % (SINGULAR.__name__, dn))
    _generic_delete(inst, basedn, log.getChild('_generic_delete'), SINGULAR, dn, args)


def modify(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to modify")
    _generic_modify_dn(inst, basedn, log.getChild('_generic_modify_dn'), MANY, dn, args)


def rename(inst, basedn, log, args, warn=True):
    dn = _get_dn_arg(args.dn, msg="Enter dn to modify")
    _generic_rename_dn(inst, basedn, log.getChild('_generic_rename_dn'), MANY, dn, args)


def _print_entry_status(status, dn, log, args):
    info_dict = {}
    if args.json:
        info_dict["dn"] = dn
    else:
        log.info(f'Entry DN: {dn}')
    for name, value in status["params"].items():
        if "Time" in name and value is not None:
            inactivation_date = datetime.fromtimestamp(status["calc_time"] + value)
            if args.json:
                info_dict[name] = f"{int(math.fabs(value))} seconds ({inactivation_date.strftime('%Y-%m-%d %H:%M:%S')})"
            else:
                log.info(f"Entry {name}: {int(math.fabs(value))} seconds ({inactivation_date.strftime('%Y-%m-%d %H:%M:%S')})")
        elif "Date" in name and value is not None:
            if args.json:
                info_dict[name] = f"{value.strftime('%Y%m%d%H%M%SZ')} ({value.strftime('%Y-%m-%d %H:%M:%S')})"
            else:
                log.info(f"Entry {name}: {value.strftime('%Y%m%d%H%M%SZ')} ({value.strftime('%Y-%m-%d %H:%M:%S')})")
    else:
        if args.json:
            info_dict["state"] = f'{status["state"].describe(status["role_dn"])}'
        else:
            log.info(f'Entry State: {status["state"].describe(status["role_dn"])}\n')

    if args.json:
        log.info(json.dumps({"type": "status", "info": info_dict}, indent=4))


def entry_status(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to check")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    status = acct.status()
    _print_entry_status(status, dn, log, args)


def subtree_status(inst, basedn, log, args):
    filter = "(objectclass=*)"
    scope = ldap.SCOPE_SUBTREE
    epoch_inactive_time = None
    if args.scope == "one":
        scope = ldap.SCOPE_ONELEVEL
    if args.filter:
        filter = args.filter
    if args.become_inactive_on:
        datetime_inactive_time = datetime.strptime(args.become_inactive_on, '%Y-%m-%dT%H:%M:%S')
        epoch_inactive_time = datetime.timestamp(datetime_inactive_time)

    account_list = Accounts(inst, basedn).filter(filter, scope=scope)
    if not account_list:
        raise ValueError(f"No entries were found under {basedn}")

    for entry in account_list:
        status = entry.status()
        state = status["state"]
        params = status["params"]
        if args.inactive_only and state == AccountState.ACTIVATED:
            continue
        if args.become_inactive_on:
            if epoch_inactive_time is None or params["Time Until Inactive"] is None or \
               epoch_inactive_time <= (params["Time Until Inactive"] + status["calc_time"]):
                continue
        _print_entry_status(status, entry.dn, log, args)


def bulk_update(inst, basedn, log, args):
    search_filter = "(objectclass=*)"
    scope = ldap.SCOPE_SUBTREE
    scope_str = "sub"
    if args.scope == "one":
        scope = ldap.SCOPE_ONELEVEL
        scope_str = "one"
    if args.filter:
        search_filter = args.filter
    log.info(f"Searching '{basedn}' filter '{search_filter}' scope '{scope_str}' ...")
    entry_list = Accounts(inst, basedn).filter(search_filter, scope=scope)
    if not entry_list:
        raise ValueError(f"No entries were found.")
    log.info(f"Found {len(entry_list)} matching entries.")

    failed_list = []
    success_list = []
    for entry in entry_list:
        if entry.dn.lower() == basedn.lower():
            # skip parent
            failed_list.append(entry.dn + " (Base DN Entry Skipped)")
            continue
        try:
            _generic_modify_dn(inst, basedn, log.getChild('_generic_modify_dn'), MANY, entry.dn, args)
            success_list.append(entry.dn)
        except ldap.LDAPError as e:
            if "desc" in e.args[0]:
                failed_list.append(entry.dn + f" ({e.args[0]['desc']})")
                log.debug(f"Failed to update {entry.dn} ({e.args[0]['desc']})")
            else:
                failed_list.append(entry.dn + f" ({str(e)})")
                log.debug(f"Failed to update {entry.dn} ({str(e)})")
            if args.stop:
                raise ValueError(f"Failed to update entry ({entry.dn}), error: {str(e)}")

    log.info(f"Updates Finished.\nSuccessfully updated {len(success_list)} entries.")
    if len(failed_list) > 0:
        log.info(f"Failed to update {len(failed_list)} entries:")

    count = 1
    for dn in failed_list:
        log.info(f"[{count}] {dn}")
        count += 1


def lock(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to lock")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct.lock()
    log.info(f'Entry {dn} is locked')


def unlock(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to unlock")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)

    try:
        # Get the account status before attempting to unlock
        status = acct.status()
        state = status["state"]

        # Attempt to unlock the account
        acct.unlock()

        # Success message
        log.info(f'Entry {dn} is unlocked')
        if state == AccountState.DIRECTLY_LOCKED:
            log.info(f'The entry was directly locked')
        elif state == AccountState.INACTIVITY_LIMIT_EXCEEDED:
            log.info(f'The entry was locked due to inactivity and is now unlocked by resetting lastLoginTime')

    except ValueError as e:
        # Provide a more detailed error message based on failure reason
        if "through role" in str(e):
            log.error(f"Cannot unlock {dn}: {str(e)}")
            log.info("To unlock this account, you must modify the role that's locking it.")
        else:
            log.error(f"Failed to unlock {dn}: {str(e)}")


def reset_password(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to reset password")
    new_password = _get_arg(args.new_password, hidden=True, confirm=True, msg="Enter new password for %s" % dn)
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)
    acct.reset_password(new_password)
    log.info('reset password for %s' % dn)


def change_password(inst, basedn, log, args):
    dn = _get_dn_arg(args.dn, msg="Enter dn to change password")
    accounts = Accounts(inst, basedn)
    acct = accounts.get(dn=dn)

    if not inst.is_rootdn_bound():
        cur_password = _get_arg(args.current_password, hidden=True, confirm=False, msg="Enter current password for %s" % dn)
        new_password = _get_arg(args.new_password, hidden=True, confirm=True, msg="Enter new password for %s" % dn)
        acct.change_password(cur_password, new_password)
    if inst.is_rootdn_bound():
        # is root/rootdn do not prompt for old password
        new_password = _get_arg(args.new_password, hidden=True, confirm=True, msg="Enter new password for %s" % dn)
        acct.reset_password(new_password)

    log.info('changed password for %s' % dn)


def create_parser(subparsers):
    account_parser = subparsers.add_parser('account', help='''Manage generic accounts, with tasks
like modify, locking and unlocking. To create an account, see "user" subcommand instead.''')

    subcommands = account_parser.add_subparsers(help='action')

    list_parser = subcommands.add_parser('list',
                                         help='list accounts that could login to the directory (returns the full DN of the entry)',
                                         formatter_class=CustomHelpFormatter)
    list_parser.set_defaults(func=list)
    list_filters = list_parser.add_mutually_exclusive_group()
    list_filters.add_argument('--locked', action='store_true',
                              help='list accounts that are locked either directly (nsAccountLock), indirectly (through a role), or by inactivity')
    list_filters.add_argument('--expired-password', action='store_true',
                              help='list accounts that have expired passwords')
    list_filters.add_argument('--expiring-password', type=int, metavar='DAYS',
                              help='list accounts whose password expires within the next specified number of days')
    list_filters.add_argument('--inactive', action='store_true',
                              help='list accounts that are inactive. Requires the Account Policy Plugin to be configured')
    list_filters.add_argument('--must-reset-password', action='store_true',
                              help='list accounts that must reset their password')
    list_filters.add_argument('--never-logged-in', action='store_true',
                              help='list user accounts that have never logged in. Requires the Account Policy Plugin to be configured')

    get_dn_parser = subcommands.add_parser('get-by-dn', help='get-by-dn <dn>', formatter_class=CustomHelpFormatter)
    get_dn_parser.set_defaults(func=get_dn)
    get_dn_parser.add_argument('dn', nargs='?', help='The dn to get and display')

    modify_dn_parser = subcommands.add_parser('modify-by-dn', help='modify-by-dn <dn> <add|delete|replace>:<attribute>:<value> ...',
                                              formatter_class=CustomHelpFormatter)
    modify_dn_parser.set_defaults(func=modify)
    modify_dn_parser.add_argument('dn', nargs=1, help='The dn to get and display')
    modify_dn_parser.add_argument('changes', nargs='+', help="A list of changes to apply in format: <add|delete|replace>:<attribute>:<value>")

    rename_dn_parser = subcommands.add_parser('rename-by-dn', help='rename the object', formatter_class=CustomHelpFormatter)
    rename_dn_parser.set_defaults(func=rename)
    rename_dn_parser.add_argument('dn', help='The dn to rename')
    rename_dn_parser.add_argument('new_dn', help='A new role dn')
    rename_dn_parser.add_argument('--keep-old-rdn', action='store_true',
                                  help="Specify whether the old RDN (i.e. 'cn: old_role') should be kept as an attribute of the entry or not")

    delete_parser = subcommands.add_parser('delete', help='deletes the account', formatter_class=CustomHelpFormatter)
    delete_parser.set_defaults(func=delete)
    delete_parser.add_argument('dn', nargs='?', help='The dn of the account to delete')

    lock_parser = subcommands.add_parser('lock', help='lock', formatter_class=CustomHelpFormatter)
    lock_parser.set_defaults(func=lock)
    lock_parser.add_argument('dn', nargs='?', help='The dn to lock')

    unlock_parser = subcommands.add_parser('unlock', help='unlock', formatter_class=CustomHelpFormatter)
    unlock_parser.set_defaults(func=unlock)
    unlock_parser.add_argument('dn', nargs='?', help='The dn to unlock')

    status_parser = subcommands.add_parser('entry-status', help='status of a single entry', formatter_class=CustomHelpFormatter)
    status_parser.set_defaults(func=entry_status)
    status_parser.add_argument('dn', nargs='?', help='The single entry dn to check')
    status_parser.add_argument('-V', '--details', action='store_true', help="Print more account policy details about the entry")

    status_parser = subcommands.add_parser('subtree-status', help='status of a subtree', formatter_class=CustomHelpFormatter)
    status_parser.set_defaults(func=subtree_status)
    status_parser.add_argument('basedn', help="Search base for finding entries")
    status_parser.add_argument('-V', '--details', action='store_true', help="Print more account policy details about the entries")
    status_parser.add_argument('-f', '--filter', help="Search filter for finding entries")
    status_parser.add_argument('-s', '--scope', choices=['one', 'sub'], help="Search scope (one, sub - default is sub")
    status_parser.add_argument('-i', '--inactive-only', action='store_true', help="Only display inactivated entries")
    status_parser.add_argument('-o', '--become-inactive-on',
                               help="Only display entries that will become inactive before specified date (in a format 2007-04-25T14:30)")

    reset_pw_parser = subcommands.add_parser('reset_password', help='Reset the password of an account. This should be performed by a directory admin.',
                                             formatter_class=CustomHelpFormatter)
    reset_pw_parser.set_defaults(func=reset_password)
    reset_pw_parser.add_argument('dn', nargs='?', help='The dn to reset the password for')
    reset_pw_parser.add_argument('new_password', nargs='?', help='The new password to set')

    change_pw_parser = subcommands.add_parser('change_password',
                                              help='Change the password of an account. This can be performed by any user (with correct rights)',
                                              formatter_class=CustomHelpFormatter)
    change_pw_parser.set_defaults(func=change_password)
    change_pw_parser.add_argument('dn', nargs='?', help='The dn to change the password for')
    change_pw_parser.add_argument('new_password', nargs='?', help='The new password to set')
    change_pw_parser.add_argument('current_password', nargs='?', help='The accounts current password')

    bulk_update_parser = subcommands.add_parser('bulk_update',
                                                help='Perform a common operation to a set of entries',
                                                formatter_class=CustomHelpFormatter)
    bulk_update_parser.set_defaults(func=bulk_update)
    bulk_update_parser.add_argument('basedn', help="Search base for finding entries, only the children of this DN are processed")
    bulk_update_parser.add_argument('-f', '--filter', help="Search filter for finding entries, default is '(objectclass=*)'")
    bulk_update_parser.add_argument('-s', '--scope', choices=['one', 'sub'], help="Search scope (one, sub - default is sub")
    bulk_update_parser.add_argument('-x', '--stop', action='store_true', default=False,
                                    help="Stop processing updates when an error occurs. Default is False")
    bulk_update_parser.add_argument('changes', nargs='+', help="A list of changes to apply in format: <add|delete|replace>:<attribute>:<value>")
