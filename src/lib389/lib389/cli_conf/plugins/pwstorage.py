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


def create_parser(subparsers):
    pwstorage = subparsers.add_parser('pwstorage-scheme', help='Manage password storage scheme plugins',
                                      formatter_class=CustomHelpFormatter)
    scheme_subcommands = pwstorage.add_subparsers(help='scheme')

    for variant in sorted(PBKDF2_VARIANTS.keys()):
        variant_parser = scheme_subcommands.add_parser(variant, help=f'Manage {variant.upper()} scheme',
                                                       formatter_class=CustomHelpFormatter)
        
        variant_subcommands = variant_parser.add_subparsers(help='action')

        get_iter = variant_subcommands.add_parser('get-num-iterations', help='Get number of iterations',
                                                  formatter_class=CustomHelpFormatter)
        get_iter.set_defaults(func=pbkdf2_get_iterations, variant=variant)

        set_iter = variant_subcommands.add_parser('set-num-iterations', help='Set number of iterations',
                                                  formatter_class=CustomHelpFormatter)
        set_iter.add_argument('iterations', type=int, help='Number of iterations (10,000-10,000,000)')
        set_iter.set_defaults(func=pbkdf2_set_iterations, variant=variant)
