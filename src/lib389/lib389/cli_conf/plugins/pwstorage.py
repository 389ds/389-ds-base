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


def create_parser(subparsers):
    pwstorage = subparsers.add_parser('pwstorage-scheme', help='Manage password storage scheme plugins',
                                      formatter_class=CustomHelpFormatter)
    scheme_subcommands = pwstorage.add_subparsers(help='scheme')

    legacy_variant = 'pbkdf2-sha256-legacy'
    for variant in sorted((*PBKDF2_VARIANTS, legacy_variant)):
        variant_parser = scheme_subcommands.add_parser(variant, help=f'Manage {variant.upper()} scheme',
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
