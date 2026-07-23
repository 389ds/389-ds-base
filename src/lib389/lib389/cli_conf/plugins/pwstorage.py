# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from lib389.password_plugins import (
    PBKDF2Plugin,
    PBKDF2SHA1Plugin,
    PBKDF2SHA256LegacyPlugin,
    PBKDF2SHA256Plugin,
    PBKDF2SHA512Plugin
)
from lib389.cli_base import CustomHelpFormatter


PBKDF2_VARIANTS = {
    'pbkdf2': PBKDF2Plugin,
    'pbkdf2-sha1': PBKDF2SHA1Plugin,
    'pbkdf2-sha256': PBKDF2SHA256Plugin,
    'pbkdf2-sha512': PBKDF2SHA512Plugin,
}

def _get_pbkdf2_plugin(inst, variant):
    plugin_class = PBKDF2_VARIANTS.get(variant)
    if plugin_class is None:
        raise ValueError(f"Unsupported PBKDF2 variant: {variant}")
    return plugin_class(inst)


def pbkdf2_get_iterations(inst, basedn, log, args):
    log = log.getChild('pbkdf2_get_iterations')
    plugin = _get_pbkdf2_plugin(inst, args.variant)

    attr = 'nsslapd-pwdpbkdf2numiterations'

    if hasattr(args, 'json') and args.json:
        vals = {}
        rounds = plugin.get_rounds()
        vals[attr] = [rounds]
        print(json.dumps({
            "type": "entry",
            "dn": plugin._dn,
            "attrs": vals
        }, indent=4))
    else:
        rounds = plugin.get_rounds()
        log.info(f'Current number of iterations for {args.variant}: {rounds}')


def pbkdf2_set_iterations(inst, basedn, log, args):
    log = log.getChild('pbkdf2_set_iterations')
    plugin = _get_pbkdf2_plugin(inst, args.variant)
    plugin.set_rounds(args.iterations)
    log.info(f'Successfully set number of iterations for {args.variant} to {args.iterations}')


def pbkdf2_get_accept_max_iterations(inst, basedn, log, args):
    log = log.getChild('pbkdf2_get_accept_max_iterations')
    plugin = PBKDF2SHA256LegacyPlugin(inst)
    value = plugin.get_accept_max_iterations()

    if hasattr(args, 'json') and args.json:
        print(json.dumps({
            "type": "entry",
            "dn": plugin._dn,
            "attrs": {'nsslapd-pwdpbkdf2acceptmaxiterations': [value]}
        }, indent=4))
    else:
        log.info(f'Current accept max iterations for {args.variant}: {value}')


def pbkdf2_set_accept_max_iterations(inst, basedn, log, args):
    log = log.getChild('pbkdf2_set_accept_max_iterations')
    plugin = PBKDF2SHA256LegacyPlugin(inst)
    plugin.set_accept_max_iterations(args.iterations)
    log.info(
        f'Successfully set accept max iterations for {args.variant} to {args.iterations}. '
        'A server restart is required for the change to take effect'
    )


def pbkdf2_variant_get_accept_max_iterations(inst, basedn, log, args):
    log = log.getChild('pbkdf2_variant_get_accept_max_iterations')
    plugin = _get_pbkdf2_plugin(inst, args.variant)
    attr = 'nsslapd-pwdpbkdf2acceptmaxiterations'
    if hasattr(args, 'json') and args.json:
        vals = {}
        accept_max = plugin.get_accept_max_iterations()
        vals[attr] = [accept_max]
        print(json.dumps({
            "type": "entry",
            "dn": plugin._dn,
            "attrs": vals
        }, indent=4))
    else:
        accept_max = plugin.get_accept_max_iterations()
        log.info(f'Current accept max iterations for {args.variant}: {accept_max}')


def pbkdf2_variant_set_accept_max_iterations(inst, basedn, log, args):
    log = log.getChild('pbkdf2_variant_set_accept_max_iterations')
    plugin = _get_pbkdf2_plugin(inst, args.variant)
    plugin.set_accept_max_iterations(args.iterations)
    log.info(f'Successfully set accept max iterations for {args.variant} to {args.iterations}')


def pbkdf2_delete_accept_max_iterations(inst, basedn, log, args):
    log = log.getChild('pbkdf2_delete_accept_max_iterations')
    plugin = _get_pbkdf2_plugin(inst, args.variant)
    plugin.delete_accept_max_iterations()
    log.info(f'Successfully removed accept max iterations override for {args.variant}')


def create_parser(subparsers):
    pwstorage = subparsers.add_parser('pwstorage-scheme', help='Manage password storage scheme plugins',
                                      formatter_class=CustomHelpFormatter)
    scheme_subcommands = pwstorage.add_subparsers(help='scheme')

    legacy_variant = 'pbkdf2-sha256-legacy'
    for variant in sorted((*PBKDF2_VARIANTS, legacy_variant)):
        if variant == 'pbkdf2':
            variant_help = (
                'Manage legacy PBKDF2 scheme ({PBKDF2}); settings are independent '
                'of pbkdf2-sha1 even though both use SHA-1'
            )
        elif variant == 'pbkdf2-sha1':
            variant_help = (
                'Manage PBKDF2-SHA1 scheme ({PBKDF2-SHA1}); settings are independent '
                'of legacy pbkdf2 even though both use SHA-1'
            )
        else:
            variant_help = f'Manage {variant.upper()} scheme'

        variant_parser = scheme_subcommands.add_parser(variant, help=variant_help,
                                                       formatter_class=CustomHelpFormatter)

        variant_subcommands = variant_parser.add_subparsers(help='action')

        if variant == legacy_variant:
            get_accept_max = variant_subcommands.add_parser(
                'get-accept-max-iterations',
                help='Get maximum accepted iterations',
                formatter_class=CustomHelpFormatter
            )
            get_accept_max.set_defaults(
                func=pbkdf2_get_accept_max_iterations,
                variant=legacy_variant
            )

            set_accept_max = variant_subcommands.add_parser(
                'set-accept-max-iterations',
                help='Set maximum accepted iterations',
                formatter_class=CustomHelpFormatter
            )
            set_accept_max.add_argument(
                'iterations',
                type=int,
                help='Maximum accepted iterations (2,048-2,147,483,647)'
            )
            set_accept_max.set_defaults(
                func=pbkdf2_set_accept_max_iterations,
                variant=legacy_variant
            )
            continue

        get_iter = variant_subcommands.add_parser('get-num-iterations', help='Get number of iterations',
                                                  formatter_class=CustomHelpFormatter)
        get_iter.set_defaults(func=pbkdf2_get_iterations, variant=variant)

        set_iter = variant_subcommands.add_parser('set-num-iterations', help='Set number of iterations',
                                                  formatter_class=CustomHelpFormatter)
        set_iter.add_argument('iterations', type=int, help='Number of iterations (10,000-10,000,000)')
        set_iter.set_defaults(func=pbkdf2_set_iterations, variant=variant)

        get_accept = variant_subcommands.add_parser(
            'get-accept-max-iterations',
            help='Get maximum iterations accepted from stored hashes on bind',
            formatter_class=CustomHelpFormatter)
        get_accept.set_defaults(func=pbkdf2_variant_get_accept_max_iterations, variant=variant)

        set_accept = variant_subcommands.add_parser(
            'set-accept-max-iterations',
            help='Set maximum iterations accepted from stored hashes on bind',
            formatter_class=CustomHelpFormatter)
        set_accept.add_argument('iterations', type=int, help='Maximum iterations accepted on bind (>= 10,000)')
        set_accept.set_defaults(func=pbkdf2_variant_set_accept_max_iterations, variant=variant)

        delete_accept = variant_subcommands.add_parser(
            'delete-accept-max-iterations',
            help='Remove accept max iterations override (default to encrypt rounds)',
            formatter_class=CustomHelpFormatter)
        delete_accept.set_defaults(func=pbkdf2_delete_accept_max_iterations, variant=variant)
