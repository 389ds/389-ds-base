# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389.plugins import ReferentialIntegrityPlugin, ReferentialIntegrityConfig
from lib389.cli_conf import add_generic_plugin_parsers, generic_object_edit, generic_object_add

arg_to_attr = {
    'update_delay': 'referint-update-delay',
    'membership_attr': 'referint-membership-attr',
    'entry_scope': 'nsslapd-pluginEntryScope',
    'exclude_entry_scope': 'nsslapd-pluginExcludeEntryScope',
    'container_scope': 'nsslapd-pluginContainerScope',
    'config_entry': 'nsslapd-pluginConfigArea',
    'log_file': 'referint-logfile'
}


def referint_edit(inst, basedn, log, args):
    log = log.getChild('referint_edit')
    plugin = ReferentialIntegrityPlugin(inst)
    generic_object_edit(plugin, log, args, arg_to_attr)


def referint_add_config(inst, basedn, log, args):
    log = log.getChild('referint_add_config')
    targetdn = args.DN
    config = generic_object_add(ReferentialIntegrityConfig, inst, log, args, arg_to_attr, dn=targetdn)
    plugin = ReferentialIntegrityPlugin(inst)
    plugin.replace('nsslapd-pluginConfigArea', config.dn)
    import pdb; pdb.set_trace()
    log.info('ReferentialIntegrity attribute nsslapd-pluginConfigArea (config-entry) '
             'was set in the main plugin config')


def referint_edit_config(inst, basedn, log, args):
    log = log.getChild('referint_edit_config')
    targetdn = args.DN
    config = ReferentialIntegrityConfig(inst, targetdn)
    generic_object_edit(config, log, args, arg_to_attr)


def referint_show_config(inst, basedn, log, args):
    log = log.getChild('referint_show_config')
    targetdn = args.DN
    config = ReferentialIntegrityConfig(inst, targetdn)

    if not config.exists():
        raise ldap.NO_SUCH_OBJECT("Entry %s doesn't exists" % targetdn)
    if args and args.json:
        o_str = config.get_all_attrs_json()
        log.info(o_str)
    else:
        log.info(config.display())


def referint_del_config(inst, basedn, log, args):
    log = log.getChild('referint_del_config')
    targetdn = args.DN
    config = ReferentialIntegrityConfig(inst, targetdn)
    config.delete()
    log.info("Successfully deleted the %s", targetdn)


def _add_parser_args(parser):
    parser.add_argument('--update-delay',
                        help='Sets the update interval. Special values: 0 - The check is performed immediately, '
                             '-1 - No check is performed (referint-update-delay)')
    parser.add_argument('--membership-attr', nargs='+',
                        help='Specifies attributes to check for and update (referint-membership-attr)')
    parser.add_argument('--entry-scope',
                        help='Defines the subtree in which the plug-in looks for the delete '
                             'or rename operations of a user entry (nsslapd-pluginEntryScope)')
    parser.add_argument('--exclude-entry-scope',
                        help='Defines the subtree in which the plug-in ignores any operations '
                             'for deleting or renaming a user (nsslapd-pluginExcludeEntryScope)')
    parser.add_argument('--container-scope',
                        help='Specifies which branch the plug-in searches for the groups to which the user belongs. '
                             'It only updates groups that are under the specified container branch, '
                             'and leaves all other groups not updated (nsslapd-pluginContainerScope)')
    parser.add_argument('--log-file',
                        help='Specifies a path to the Referential integrity logfile.'
                             'For example: /var/log/dirsrv/slapd-YOUR_INSTANCE/referint')


def create_parser(subparsers):
    referint = subparsers.add_parser('referential-integrity',
                                      help='Manage and configure Referential Integrity Postoperation plugin')

    subcommands = referint.add_subparsers(help='action')

    add_generic_plugin_parsers(subcommands, ReferentialIntegrityPlugin)

    edit = subcommands.add_parser('set', help='Edit the plugin settings')
    edit.set_defaults(func=referint_edit)
    _add_parser_args(edit)
    edit.add_argument('--config-entry', help='The value to set as nsslapd-pluginConfigArea')

    config = subcommands.add_parser('config-entry', help='Manage the config entry')
    config_subcommands = config.add_subparsers(help='action')
    add_config = config_subcommands.add_parser('add', help='Add the config entry')
    add_config.set_defaults(func=referint_add_config)
    add_config.add_argument('DN', help='The config entry full DN')
    _add_parser_args(add_config)
    edit_config = config_subcommands.add_parser('set', help='Edit the config entry')
    edit_config.set_defaults(func=referint_edit_config)
    edit_config.add_argument('DN', help='The config entry full DN')
    _add_parser_args(edit_config)
    show_config = config_subcommands.add_parser('show', help='Display the config entry')
    show_config.set_defaults(func=referint_show_config)
    show_config.add_argument('DN', help='The config entry full DN')
    del_config_ = config_subcommands.add_parser('delete', help='Delete the config entry')
    del_config_.set_defaults(func=referint_del_config)
    del_config_.add_argument('DN', help='The config entry full DN')
