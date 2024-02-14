# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from os.path import expanduser
from os import path, remove
from ldapurl import isLDAPUrl
from ldap.dn import is_dn
import configparser
from lib389.utils import is_a_dn
from lib389.cli_base import CustomHelpFormatter


def create_dsrc(inst, log, args):
    """Create the .dsrc file

    [instance]
    uri = ldaps://hostname:port
    basedn = dc=example,dc=com
    people_rdn = ou=people
    groups_rdn = ou=groups
    binddn = uid=user,....
    saslmech = [EXTERNAL|PLAIN]
    tls_cacertdir = /path/to/cacertdir
    tls_cert = /path/to/user.crt
    tls_key = /path/to/user.key
    tls_reqcert = [never, hard, allow]
    starttls = [true, false]
    pwdfile = /path/to/file
    """

    dsrc_file = f'{expanduser("~")}/.dsrc'
    config = configparser.ConfigParser()
    config.read(dsrc_file)

    # Verify this section does not already exist
    instances = config.sections()
    if inst.serverid in instances:
        raise ValueError("There is already a configuration section for this instance!")

    # Process and validate the args
    config[inst.serverid] = {}

    if args.uri is not None:
        if not isLDAPUrl(args.uri):
            raise ValueError("The uri is not a valid LDAP URL!")
        if args.uri.startswith("ldapi"):
            # We must use EXTERNAL saslmech for LDAPI
            args.saslmech = "EXTERNAL"
        config[inst.serverid]['uri'] = args.uri
    if args.basedn is not None:
        if not is_dn(args.basedn):
            raise ValueError("The basedn is not a valid DN!")
        config[inst.serverid]['basedn'] = args.basedn
    if args.binddn is not None:
        if not is_dn(args.binddn):
            raise ValueError("The binddn is not a valid DN!")
        config[inst.serverid]['binddn'] = args.binddn
    if args.saslmech is not None:
        if args.saslmech not in ['EXTERNAL', 'PLAIN']:
            raise ValueError("The saslmech must be EXTERNAL or PLAIN!")
        config[inst.serverid]['saslmech'] = args.saslmech
    if args.tls_cacertdir is not None:
        if not path.exists(args.tls_cacertdir):
            raise ValueError('--tls-cacertdir directory does not exist!')
        config[inst.serverid]['tls_cacertdir'] = args.tls_cacertdir
    if args.tls_cert is not None:
        if not path.exists(args.tls_cert):
            raise ValueError('--tls-cert does not point to an existing file!')
        config[inst.serverid]['tls_cert'] = args.tls_cert
    if args.tls_key is not None:
        if not path.exists(args.tls_key):
            raise ValueError('--tls-key does not point to an existing file!')
        config[inst.serverid]['tls_key'] = args.tls_key
    if args.tls_reqcert is not None:
        if args.tls_reqcert not in ['never', 'hard', 'allow']:
            raise ValueError('--tls-reqcert value is invalid (must be either "never", "allow", or "hard")!')
        config[inst.serverid]['tls_reqcert'] = args.tls_reqcert
    if args.starttls:
         config[inst.serverid]['starttls'] = 'true'
    if args.pwdfile is not None:
        if not path.exists(args.pwdfile):
            raise ValueError('--pwdfile does not exist!')
        config[inst.serverid]['pwdfile'] = args.pwdfile
    if args.people_rdn is not None:
        if not is_dn(args.people_rdn):
            raise ValueError("The value for people-dn is not a valid DN!")
        config[inst.serverid]['people_rdn'] = args.people_rdn
    if args.groups_rdn is not None:
        if not is_dn(args.groups_rdn):
            raise ValueError("The value for groups-dn is not a valid DN!")
        config[inst.serverid]['groups_rdn'] = args.groups_rdn

    if len(config[inst.serverid]) == 0:
        # No args set
        raise ValueError("You must set at least one argument for the new dsrc file!")

    # Print a preview of the config
    log.info(f'Updating "{dsrc_file}" with:\n')
    log.info(f'    [{inst.serverid}]')
    for k, v in config[inst.serverid].items():
        log.info(f'    {k} = {v}')

    # Perform confirmation?
    if not args.do_it:
        while 1:
            val = input(f'\nUpdate "{dsrc_file}" ? [yes]: ').rstrip().lower()
            if val == '' or val == 'y' or val == 'yes':
                break
            if val == 'n' or val == 'no':
                return

    # Now write the file
    with open(dsrc_file, 'w') as configfile:
        config.write(configfile)

    log.info(f'Successfully updated: {dsrc_file}')


def modify_dsrc(inst, log, args):
    """Modify the instance config
    """
    dsrc_file = f'{expanduser("~")}/.dsrc'

    if path.exists(dsrc_file):
        config = configparser.ConfigParser()
        config.read(dsrc_file)

        # Verify we have a section to modify
        instances = config.sections()
        if inst.serverid not in instances:
            raise ValueError("There is no configuration section for this instance to modify!")

        # Process and validate the args
        if args.uri is not None:
            if not isLDAPUrl(args.uri):
                raise ValueError("The uri is not a valid LDAP URL!")
            if args.uri.startswith("ldapi"):
                # We must use EXTERNAL saslmech for LDAPI
                args.saslmech = "EXTERNAL"
            if args.uri == '':
                del config[inst.serverid]['uri']
            else:
                config[inst.serverid]['uri'] = args.uri
        if args.basedn is not None:
            if not is_dn(args.basedn):
                raise ValueError("The basedn is not a valid DN!")
            if args.basedn == '':
                del config[inst.serverid]['basedn']
            else:
                config[inst.serverid]['basedn'] = args.basedn
        if args.binddn is not None:
            if not is_dn(args.binddn):
                raise ValueError("The binddn is not a valid DN!")
            if args.binddn == '':
                del config[inst.serverid]['binddn']
            else:
                config[inst.serverid]['binddn'] = args.binddn
        if args.saslmech is not None:
            if args.saslmech not in ['EXTERNAL', 'PLAIN']:
                raise ValueError("The saslmech must be EXTERNAL or PLAIN!")
            if args.saslmech == '':
                del config[inst.serverid]['saslmech']
            else:
                config[inst.serverid]['saslmech'] = args.saslmech
        if args.tls_cacertdir is not None:
            if not path.exists(args.tls_cacertdir):
                raise ValueError('--tls-cacertdir directory does not exist!')
            if args.tls_cacertdir == '':
                del config[inst.serverid]['tls_cacertdir']
            else:
                config[inst.serverid]['tls_cacertdir'] = args.tls_cacertdir
        if args.tls_cert is not None:
            if not path.exists(args.tls_cert):
                raise ValueError('--tls-cert does not point to an existing file!')
            if args.tls_cert == '':
                del config[inst.serverid]['tls_cert']
            else:
                config[inst.serverid]['tls_cert'] = args.tls_cert
        if args.tls_key is not None:
            if not path.exists(args.tls_key):
                raise ValueError('--tls-key does not point to an existing file!')
            if args.tls_key == '':
                del config[inst.serverid]['tls_key']
            else:
                config[inst.serverid]['tls_key'] = args.tls_key
        if args.tls_reqcert is not None:
            if args.tls_reqcert not in ['never', 'hard', 'allow']:
                raise ValueError('--tls-reqcert value is invalid (must be either "never", "allow", or "hard")!')
            if args.tls_reqcert == '':
                del config[inst.serverid]['tls_reqcert']
            else:
                config[inst.serverid]['tls_reqcert'] = args.tls_reqcert
        if args.starttls:
             config[inst.serverid]['starttls'] = 'true'
        if args.cancel_starttls:
            # coverity[copy_paste_error]
            config[inst.serverid]['starttls'] = 'false'
        if args.pwdfile is not None:
            if not path.exists(args.pwdfile):
                raise ValueError('--pwdfile does not exist!')
            if args.pwdfile == '':
                del config[inst.serverid]['pwdfile']
            else:
                config[inst.serverid]['pwdfile'] = args.pwdfile
        if args.people_rdn is not None:
            if not is_dn(args.people_rdn):
                raise ValueError('--people-rdn is not a valid DN!')
            if args.people_rdn == '':
                del config[inst.serverid]['people_rdn']
            else:
                config[inst.serverid]['people_rdn'] = args.people_rdn
        if args.groups_rdn is not None:
            if not is_dn(args.groups_rdn):
                raise ValueError('--groups-rdn is not a valid DN!')
            if args.groups_rdn == '':
                del config[inst.serverid]['groups_rdn']
            else:
                config[inst.serverid]['groups_rdn'] = args.groups_rdn

        # Okay now rewrite the file
        with open(dsrc_file, 'w') as configfile:
            config.write(configfile)

        log.info(f'Successfully updated: {dsrc_file}')
    else:
        raise ValueError(f'There is no .dsrc file "{dsrc_file}" to update!')


def delete_dsrc(inst, log, args):
    """Delete the .dsrc file
    """
    dsrc_file = f'{expanduser("~")}/.dsrc'
    if path.exists(dsrc_file):
        if not args.do_it:
            # Get confirmation
            while 1:
                val = input(f'\nAre you sure you want to remove this instances configuration ? [no]: ').rstrip().lower()
                if val == 'y' or val == 'yes':
                    break
                if val == '' or val == 'n' or val == 'no':
                    return

        config = configparser.ConfigParser()
        config.read(dsrc_file)
        instances = config.sections()
        if inst.serverid not in instances:
            raise ValueError("The is no configuration for this instance")

        # Update the config object
        del config[inst.serverid]

        if len(config.sections()) == 0:
            # The file would be empty so just delete it
            try:
                remove(dsrc_file)
                log.info(f'Successfully removed: {dsrc_file}')
                return
            except OSError as e:
                raise ValueError(f'Failed to delete "{dsrc_file}",  error: {str(e)}')
        else:
            # write the updated config
            with open(dsrc_file, 'w') as configfile:
                config.write(configfile)
    else:
        raise ValueError(f'There is no .dsrc file "{dsrc_file}" to update!')

    log.info(f'Successfully updated: {dsrc_file}')


def display_dsrc(inst, log, args):
    """Display the contents of the ~/.dsrc file
    """
    dsrc_file = f'{expanduser("~")}/.dsrc'

    if not path.exists(dsrc_file):
        raise ValueError(f'There is no dsrc file "{dsrc_file}" to display!')

    config = configparser.ConfigParser()
    config.read(dsrc_file)
    instances = config.sections()

    result = {}
    for inst_section in instances:
        if args.json:
            result[inst_section] = dict(config[inst_section])
        else:
            log.info(f'[{inst_section}]')
            for k, v in config[inst_section].items():
                log.info(f'{k} = {v}')
            log.info("")

    if args.json:
        log.info(json.dumps(result, indent=4))


def replmon_dsrc(inst, log, args):
    dsrc_file = f'{expanduser("~")}/.dsrc'
    repl_conn_section = 'repl-monitor-connections'
    repl_alias_section = 'repl-monitor-aliases'

    config = configparser.ConfigParser()
    config.read(dsrc_file)

    # Process and validate the args
    if args.add_conn is not None:
        sections = config.sections()
        if repl_conn_section not in sections:
            config.add_section(repl_conn_section)
        for conn in args.add_conn:
            conn_parts = conn.split(":")
            if len(conn_parts) != 5:
                raise ValueError("Missing required information for connection: NAME:HOST:PORT:BINDDN:CREDENTIAL")
            conn_name = conn_parts[0]
            conn_host = conn_parts[1]
            conn_port = conn_parts[2]
            conn_binddn = conn_parts[3]
            conn_cred = conn_parts[4]
            if type(int(conn_port)) != int:
                raise ValueError("Invalid value for PORT, must be a number")
            if not is_a_dn(conn_binddn):
                raise ValueError("The bind DN is not a valid DN")
            if conn_name in config[repl_conn_section]:
                raise ValueError("Replication connection with the same name already exists")
            config[repl_conn_section][conn_name] = f"{conn_host}:{conn_port}:{conn_binddn}:{conn_cred}"

    if args.del_conn is not None:
        sections = config.sections()
        if repl_conn_section not in sections:
            raise ValueError("There are no connections configured")
        for conn in args.del_conn:
            if conn not in config.options(repl_conn_section):
                raise ValueError("Can not find connection: " + conn)
            del config[repl_conn_section][conn]
            if len(config[repl_conn_section]) == 0:
                # Delete section header
                del config[repl_conn_section]

    if args.add_alias is not None:
        sections = config.sections()
        if repl_alias_section not in sections:
            config.add_section(repl_alias_section)
        for alias in args.add_alias:
            alias_parts = alias.split(":")
            if len(alias_parts) != 3:
                raise ValueError("Missing required information for alias: ALIAS_NAME:HOST:PORT")
            alias_name = alias_parts[0]
            alias_host = alias_parts[1]
            alias_port = alias_parts[2]
            if type(int(alias_port)) != int:
                raise ValueError("Invalid value for PORT, must be a number")
            if alias_name in config[repl_alias_section]:
                raise ValueError("Replica alias with the same name already exists")
            config[repl_alias_section][alias_name] = f"{alias_host}:{alias_port}"

    if args.del_alias is not None:
        sections = config.sections()
        if repl_alias_section not in sections:
            raise ValueError("There are no aliases configured")
        for alias in args.del_alias:
            if alias not in config.options(repl_alias_section):
                raise ValueError("Can not find alias: " + alias)
            del config[repl_alias_section][alias]
            if len(config[repl_alias_section]) == 0:
                # delete section header
                del config[repl_alias_section]

    # Okay now rewrite the file
    with open(dsrc_file, 'w') as configfile:
        config.write(configfile)

    log.info(f'Successfully updated: {dsrc_file}')


def create_parser(subparsers):
    dsrc_parser = subparsers.add_parser('dsrc', help="Manage the .dsrc file", formatter_class=CustomHelpFormatter)
    subcommands = dsrc_parser.add_subparsers(help="action")

    # Create .dsrc file
    dsrc_create_parser = subcommands.add_parser('create', help='Generate the .dsrc file', formatter_class=CustomHelpFormatter)
    dsrc_create_parser.set_defaults(func=create_dsrc)
    dsrc_create_parser.add_argument('--uri', help="The URI (LDAP URL) for the Directory Server instance.")
    dsrc_create_parser.add_argument('--basedn', help="The default database suffix.")
    dsrc_create_parser.add_argument('--people-rdn',
                                    help="Set the RDN for the 'people' subtree.  Default is \"ou=people\"")
    dsrc_create_parser.add_argument('--groups-rdn',
                                    help="Set the RDN for the 'groups' subtree.  Default is \"ou=groups\"")
    dsrc_create_parser.add_argument('--binddn', help="The default Bind DN used or authentication.")
    dsrc_create_parser.add_argument('--saslmech', help="The SASL mechanism to use: PLAIN or EXTERNAL.")
    dsrc_create_parser.add_argument('--tls-cacertdir', help="The directory containing the Trusted Certificate Authority certificate.")
    dsrc_create_parser.add_argument('--tls-cert', help="The absolute file name to the server certificate.")
    dsrc_create_parser.add_argument('--tls-key', help="The absolute file name to the server certificate key.")
    dsrc_create_parser.add_argument('--tls-reqcert', help="Request certificate strength: 'never', 'allow', 'hard'")
    dsrc_create_parser.add_argument('--starttls', action='store_true', help="Use startTLS for connection to the server.")
    dsrc_create_parser.add_argument('--pwdfile', help="The absolute path to a file containing the Bind DN's password.")
    dsrc_create_parser.add_argument('--do-it', action='store_true', help="Create the file without any confirmation.")

    dsrc_modify_parser = subcommands.add_parser('modify', help='Modify the .dsrc file', formatter_class=CustomHelpFormatter)
    dsrc_modify_parser.set_defaults(func=modify_dsrc)
    dsrc_modify_parser.add_argument('--uri', nargs='?', const='', help="The URI (LDAP URL) for the Directory Server instance.")
    dsrc_modify_parser.add_argument('--basedn', nargs='?', const='', help="The default database suffix.")
    dsrc_modify_parser.add_argument('--people-rdn', nargs='?', const='', help="Sets the RDN used for the 'people' container")
    dsrc_modify_parser.add_argument('--groups-rdn', nargs='?', const='', help="Sets the RDN used for the 'groups' container")
    dsrc_modify_parser.add_argument('--binddn', nargs='?', const='', help="The default Bind DN used or authentication.")
    dsrc_modify_parser.add_argument('--saslmech', nargs='?', const='', help="The SASL mechanism to use: PLAIN or EXTERNAL.")
    dsrc_modify_parser.add_argument('--tls-cacertdir', nargs='?', const='', help="The directory containing the Trusted Certificate Authority certificate.")
    dsrc_modify_parser.add_argument('--tls-cert', nargs='?', const='', help="The absolute file name to the server certificate.")
    dsrc_modify_parser.add_argument('--tls-key', nargs='?', const='', help="The absolute file name to the server certificate key.")
    dsrc_modify_parser.add_argument('--tls-reqcert', nargs='?', const='', help="Request certificate strength: 'never', 'allow', 'hard'")
    dsrc_modify_parser.add_argument('--starttls', action='store_true', help="Use startTLS for connection to the server.")
    dsrc_modify_parser.add_argument('--cancel-starttls', action='store_true', help="Do not use startTLS for connection to the server.")
    dsrc_modify_parser.add_argument('--pwdfile', nargs='?', const='', help="The absolute path to a file containing the Bind DN's password.")
    dsrc_modify_parser.add_argument('--do-it', action='store_true', help="Update the file without any confirmation.")

    # Delete the instance from the .dsrc file
    dsrc_delete_parser = subcommands.add_parser('delete', help='Delete instance configuration from the .dsrc file.', formatter_class=CustomHelpFormatter)
    dsrc_delete_parser.set_defaults(func=delete_dsrc)
    dsrc_delete_parser.add_argument('--do-it', action='store_true',
                                    help="Delete this instance's configuration from the .dsrc file.")

    # Display .dsrc file
    dsrc_display_parser = subcommands.add_parser('display', help='Display the contents of the .dsrc file.', formatter_class=CustomHelpFormatter)
    dsrc_display_parser.set_defaults(func=display_dsrc)

    # Replication Monitor actions
    dsrc_replmon_parser = subcommands.add_parser('repl-mon', help='Display the contents of the .dsrc file.', formatter_class=CustomHelpFormatter)
    dsrc_replmon_parser.set_defaults(func=replmon_dsrc)
    dsrc_replmon_parser.add_argument('--add-conn', nargs='+', help="Add a replica connection: 'NAME:HOST:PORT:BINDDN:CREDENTIAL'")
    dsrc_replmon_parser.add_argument('--del-conn', nargs='+', help="delete a replica connection by its NAME")
    dsrc_replmon_parser.add_argument('--add-alias', nargs='+', help="Add a host/port alias: 'ALIAS_NAME:HOST:PORT'")
    dsrc_replmon_parser.add_argument('--del-alias', nargs='+', help="delete a host/port alias by its ALIAS_NAME")
