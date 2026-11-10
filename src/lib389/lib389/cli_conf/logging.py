# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from lib389.config import Config
from lib389.cli_base import CustomHelpFormatter

ERROR_LEVELS = {
    "acl": {
        "level": 128,
        "desc":
            "Provides very detailed access control list processing information"
    },
    "aclsumary": {
        "level": 262144,
        "desc": "Summarizes information about access to the server, much less "
                + "verbose than level 'acl'"
    },
    "backend": {
        "level": 524288,
        "desc": "Backend debug logging"
    },
    "ber": {
        "level": 16,
        "desc": "Logs the number of packets sent and received by the server"
    },
    "cache": {
        "level": 32768,
        "desc": "Database entry cache logging"
    },
    "config": {
        "level": 64,
        "desc": "Prints any .conf configuration files used with the server, "
                + "line by line, when the server is started"
    },
    "connection": {
        "level": 8,
        "desc": "Logs the current connection status, including the connection "
                + "methods used for a SASL bind"
    },
    "default": {
        "level": 16384,
        "desc": "Default logging level"
    },
    "filter": {
        "level": 32,
        "desc": "Logs all of the functions called by a search operation"
    },
    "heavytrace": {
        "level": 4,
        "desc": "Logs when the server enters and exits a function, with "
                + "additional debugging messages"
    },
    "house": {
        "level": 4096,
        "desc": "Logging for housekeeping thread"
    },
    "packet":
    {
        "level": 2,
        "desc": "Network packet logging"
    },
    "parse": {
        "level": 2048,
        "desc": "Logs schema parsing debugging information"
    },
    "plugin": {
        "level": 65536,
        "desc": "Plugin logging"
    },
    "pwpolicy": {
        "level": 1048576,
        "desc": "Debug information about password policy behavior"
    },
    "replication": {
        "level": 8192,
        "desc": "Debug replication logging"
    },
    "shell": {
        "level": 1024,
        "desc": "Special authentication/connection tracking"
    },
    "trace": {
        "level": 1,
        "desc": "Logs a message when the server enters and exits a function"
    },
}

ACCESS_LEVELS = {
    "entry": {
        "level": 512,
        "desc": "Log entry and referral stats"
    },
    "default": {
        "level": 256,
        "desc": "Standard access logging"
    },
    "internal": {
        "level": 4,
        "desc": "Log internal operations"
    },
}

ACCESS_ATTR_MAP = {
    'nsslapd-accesslog': 'Log name and location',
    'nsslapd-accesslog-mode': 'File mode',
    'nsslapd-accesslog-maxlogsperdir': 'Max logs',
    'nsslapd-accesslog-logging-enabled': 'Logging enabled',
    'nsslapd-accesslog-compress': 'Compression enabled',
    'nsslapd-accesslog-logexpirationtime': 'Deletion interval',
    'nsslapd-accesslog-logexpirationtimeunit': 'Deletion interval unit',
    'nsslapd-accesslog-logrotationsync-enabled': 'TOD rotation enabled',
    'nsslapd-accesslog-logrotationsynchour': 'TOD rotation hour',
    'nsslapd-accesslog-logrotationsyncmin': 'TOD rotation minute',
    'nsslapd-accesslog-logrotationtime': 'Rotation interval',
    'nsslapd-accesslog-logrotationtimeunit': 'Rotation interval unit',
    'nsslapd-accesslog-logmaxdiskspace': 'Max disk space',
    'nsslapd-accesslog-level': 'Log level',
    'nsslapd-accesslog-maxlogsize': 'Max log size',
    'nsslapd-accesslog-logbuffering': 'Buffering enabled',
    'nsslapd-accesslog-logminfreediskspace': 'Minimum free disk space',
    'nsslapd-accesslog-time-format': 'Time format for JSON logging (strftime)',
    'nsslapd-accesslog-log-format': 'Logging format',
}

AUDIT_ATTR_MAP = {
    'nsslapd-auditlog': 'Log name and location',
    'nsslapd-auditlog-mode': 'File mode',
    'nsslapd-auditlog-maxlogsperdir': 'Max logs',
    'nsslapd-auditlog-logging-enabled': 'Logging enabled',
    'nsslapd-auditlog-compress': 'Compression enabled',
    'nsslapd-auditlog-logexpirationtime': 'Deletion interval',
    'nsslapd-auditlog-logexpirationtimeunit': 'Deletion interval unit',
    'nsslapd-auditlog-logrotationsync-enabled': 'TOD rotation enabled',
    'nsslapd-auditlog-logrotationsynchour': 'TOD rotation hour',
    'nsslapd-auditlog-logrotationsyncmin': 'TOD rotation minute',
    'nsslapd-auditlog-logrotationtime': 'Rotation interval',
    'nsslapd-auditlog-logrotationtimeunit': 'Rotation interval unit',
    'nsslapd-auditlog-logmaxdiskspace': 'Max disk space',
    'nsslapd-auditlog-maxlogsize': 'Max log size',
    'nsslapd-auditlog-logbuffering': 'Buffering enabled',
    'nsslapd-auditlog-logminfreediskspace': 'Minimum free disk space',
    'nsslapd-auditlog-display-attrs': 'Additional attrs to display',
    'nsslapd-auditlog-time-format': 'Time format for JSON logging (strftime)',
    'nsslapd-auditlog-log-format': 'Logging format',
}

AUDITFAIL_ATTR_MAP = {
    'nsslapd-auditfaillog': 'Log name and location',
    'nsslapd-auditfaillog-mode': 'File mode',
    'nsslapd-auditfaillog-maxlogsperdir': 'Max logs',
    'nsslapd-auditfaillog-logging-enabled': 'Logging enabled',
    'nsslapd-auditfaillog-compress': 'Compression enabled',
    'nsslapd-auditfaillog-logexpirationtime': 'Deletion interval',
    'nsslapd-auditfaillog-logexpirationtimeunit': 'Deletion interval unit',
    'nsslapd-auditfaillog-logrotationsync-enabled': 'TOD rotation enabled',
    'nsslapd-auditfaillog-logrotationsynchour': 'TOD rotation hour',
    'nsslapd-auditfaillog-logrotationsyncmin': 'TOD rotation minute',
    'nsslapd-auditfaillog-logrotationtime': 'Rotation interval',
    'nsslapd-auditfaillog-logrotationtimeunit': 'Rotation interval unit',
    'nsslapd-auditfaillog-logmaxdiskspace': 'Max disk space',
    'nsslapd-auditfaillog-maxlogsize': 'Max log size',
    'nsslapd-auditfaillog-logminfreediskspace': 'Minimum free disk space',
}

ERROR_ATTR_MAP = {
    'nsslapd-errorlog': 'Log name and location',
    'nsslapd-errorlog-mode': 'File mode',
    'nsslapd-errorlog-maxlogsperdir': 'Max logs',
    'nsslapd-errorlog-logging-enabled': 'Logging enabled',
    'nsslapd-errorlog-compress': 'Compression enabled',
    'nsslapd-errorlog-logexpirationtime': 'Deletion interval',
    'nsslapd-errorlog-logexpirationtimeunit': 'Deletion interval unit',
    'nsslapd-errorlog-logrotationsync-enabled': 'TOD rotation enabled',
    'nsslapd-errorlog-logrotationsynchour': 'TOD rotation hour',
    'nsslapd-errorlog-logrotationsyncmin': 'TOD rotation minute',
    'nsslapd-errorlog-logrotationtime': 'Rotation interval',
    'nsslapd-errorlog-logrotationtimeunit': 'Rotation interval unit',
    'nsslapd-errorlog-logmaxdiskspace': 'Max disk space',
    'nsslapd-errorlog-level': 'Log level',
    'nsslapd-errorlog-maxlogsize': 'Max log size',
    'nsslapd-errorlog-logbuffering': 'Buffering enabled',
    'nsslapd-errorlog-logminfreediskspace': 'Minimum free disk space',
    'nsslapd-errorlog-time-format': 'Time format for JSON logging (strftime)',
    'nsslapd-errorlog-log-format': 'Logging format',
}

SECURITY_ATTR_MAP = {
    'nsslapd-securitylog': 'Log name and location',
    'nsslapd-securitylog-mode': 'File mode',
    'nsslapd-securitylog-maxlogsperdir': 'Max logs',
    'nsslapd-securitylog-logging-enabled': 'Logging enabled',
    'nsslapd-securitylog-compress': 'Compression enabled',
    'nsslapd-securitylog-logexpirationtime': 'Deletion interval',
    'nsslapd-securitylog-logexpirationtimeunit': 'Deletion interval unit',
    'nsslapd-securitylog-logrotationsync-enabled': 'TOD rotation enabled',
    'nsslapd-securitylog-logrotationsynchour': 'TOD rotation hour',
    'nsslapd-securitylog-logrotationsyncmin': 'TOD rotation minute',
    'nsslapd-securitylog-logrotationtime': 'Rotation interval',
    'nsslapd-securitylog-logrotationtimeunit': 'Rotation interval unit',
    'nsslapd-securitylog-logmaxdiskspace': 'Max disk space',
    'nsslapd-securitylog-maxlogsize': 'Max log size',
    'nsslapd-securitylog-logbuffering': 'Buffering enabled',
    'nsslapd-securitylog-logminfreediskspace': 'Minimum free disk space',
}


###############################################################################
# Generic functions
###############################################################################

def update_config(inst, basedn, log, args):
    """
    Update a simple log config attribute
    """
    base = "nsslapd-" + args.logtype + "log"
    if 'keyword' in args and args.keyword is not None:
        attr = base + "-" + args.keyword
    else:
        attr = base
    config = Config(inst, basedn)
    config.set(attr, args.values)
    log.info(f"Successfully updated {args.logtype} log configuration")


def get_log_config(inst, basedn, log, args):
    """
    Get all the configuration settings for the log
    """
    conf = Config(inst, basedn)
    attrs = conf.get_all_attrs_utf8()
    json_result = {}
    attr_map = {}
    levels = {}

    if args.logtype == "access":
        attr_map = ACCESS_ATTR_MAP
        levels = ACCESS_LEVELS
    elif args.logtype == "error":
        attr_map = ERROR_ATTR_MAP
        levels = ERROR_LEVELS
    elif args.logtype == "security":
        attr_map = SECURITY_ATTR_MAP
    elif args.logtype == "audit":
        attr_map = AUDIT_ATTR_MAP
    elif args.logtype == "auditfail":
        attr_map = AUDITFAIL_ATTR_MAP
    else:
        raise ValueError(f"Unknown logtype: {args.logtype}")

    sorted_results = []
    for attr, value in attrs.items():
        attr = attr.lower()
        if attr in attr_map:
            attr_val = value[0]
            if 'level' in attr:
                # Convert log level number into individual levels
                level_vals = []
                for name, obj in levels.items():
                    if int(attr_val) & obj['level']:
                        level_vals.append(name)
                level_vals.sort()
                if args.json:
                    attr_val = level_vals
                else:
                    attr_val = (',').join(level_vals)
            elif 'display-attrs' in attr:
                display_attr_vals = value[0].split()
                display_attr_vals.sort()
                if args.json:
                    attr_val = display_attr_vals
                else:
                    attr_val = (',').join(display_attr_vals)

            if args.json:
                json_result[attr_map[attr]] = attr_val
            else:
                sorted_results.append(attr_map[attr] + " = " + attr_val)

    if args.json:
        log.info(json.dumps(json_result, indent=4))
    else:
        sorted_results.sort()
        header = args.logtype + " log configuration"
        log.info("\n" + header.title())
        log.info('-' * 80)
        for result in sorted_results:
            log.info(result)


def list_log_levels(inst, basedn, log, args):
    """
    List all the log levels
    """
    if args.logtype == 'access':
        levels = ACCESS_LEVELS
    else:  # args.logtype == 'error'
        levels = ERROR_LEVELS

    result = []
    if not args.json:
        log.info('\nLevel         Description')
        log.info('-' * 80)

    for name, obj in levels.items():
        desc = obj['desc']
        if args.json:
            result.append({name: desc})
        else:
            space = 14 - len(name)
            log.info(name + (" " * space) + desc)

    if args.json:
        result = {'type': 'list', 'items': result}
        log.info(json.dumps(result, indent=4))


def set_log_level(inst, basedn, log, args):
    """
    Set the log level
    """
    new_level = 0
    attr = f'nsslapd-{args.logtype}log-level'

    if args.logtype == 'access':
        level_map = ACCESS_LEVELS
    else:  # args.logtype == 'error'
        level_map = ERROR_LEVELS

    for level in args.levels:
        if level in level_map:
            new_level += level_map[level]['level']
        else:
            log.error('Ignoring unknown log level: ' + level)
    if new_level > 0:
        config = Config(inst, basedn)
        config.set(attr, str(new_level))
        log.info(f'Successfully set {args.logtype} log level')


def set_display_attrs(inst, basedn, log, args):
    """
    Set the log level
    """
    attr = 'nsslapd-auditlog-display-attrs'
    value = (" ").join(args.values)
    value = value.replace(",", " ")
    config = Config(inst, basedn)
    config.set(attr, value)
    log.info('Successfully updated audit log configuration')


#
# CLI parser
#
def create_parser(subparsers):
    """
    CLI log parser
    """
    log_parser = subparsers.add_parser(
        "logging", help="Manage the server logs",
        formatter_class=CustomHelpFormatter
    )
    log_sub_parsers = log_parser.add_subparsers()

    for log_type in ['access', 'audit', 'auditfail', 'error', 'security']:
        subcommands = log_sub_parsers.add_parser(
            log_type, help="Manage " + log_type + " log settings",
            formatter_class=CustomHelpFormatter
        )
        log_subparsers = subcommands.add_subparsers()

        # Get config
        get_parser = log_subparsers.add_parser(
            "get", help="Get " + log_type + " log configuration",
            formatter_class=CustomHelpFormatter
        )
        get_parser.set_defaults(func=get_log_config, logtype=log_type)

        # Set config
        set_parser = log_subparsers.add_parser(
            "set", help="Set " + log_type + " log configuration",
            formatter_class=CustomHelpFormatter
        )
        set_parsers = set_parser.add_subparsers()

        # Log levels
        if log_type in ['access', 'error']:
            # List levels
            list_parser = log_subparsers.add_parser(
                "list-levels",
                help="List all the log levels",
                formatter_class=CustomHelpFormatter,
            )
            list_parser.set_defaults(func=list_log_levels, logtype=log_type)

            # Set level
            set_level_parser = set_parsers.add_parser(
                "level", help="Set the log level",
                formatter_class=CustomHelpFormatter
            )
            set_level_parser.set_defaults(func=set_log_level, logtype=log_type)
            set_level_parser.add_argument(
                "levels", nargs="+", help="log level"
            )

        # Enable disable logging
        on_parser = set_parsers.add_parser(
            "logging-enabled",
            help="Enable access logging",
            formatter_class=CustomHelpFormatter,
        )
        on_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logging-enabled",
            values=["on"]
        )

        off_parser = set_parsers.add_parser(
            "logging-disabled",
            help="Disable " + log_type + " logging",
            formatter_class=CustomHelpFormatter,
        )
        off_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logging-enabled",
            values=["off"]
        )

        # Mode/permissions
        set_mode_parser = set_parsers.add_parser(
            "mode",
            help="Set the log file permissions. Default is 600",
            formatter_class=CustomHelpFormatter,
        )
        set_mode_parser.set_defaults(func=update_config, logtype=log_type,
                                     keyword="mode")
        set_mode_parser.add_argument(
            "values", nargs=1,
            help="File permissions. Default is 600"
        )

        # Location
        loc_set_parser = set_parsers.add_parser(
            "location",
            help="Set the log name and location",
            formatter_class=CustomHelpFormatter,
        )
        loc_set_parser.set_defaults(func=update_config, logtype=log_type)
        loc_set_parser.add_argument(
            "values", nargs=1, help="Log name and location"
        )

        # Compression
        comp_on_parser = set_parsers.add_parser(
            "compress-enabled",
            help="Enable log compression for rotated logs",
            formatter_class=CustomHelpFormatter,
        )
        comp_on_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="compress",
            values=["on"]
        )

        comp_off_parser = set_parsers.add_parser(
            "compress-disabled",
            help="Disable log compression for rotated logs",
            formatter_class=CustomHelpFormatter,
        )
        comp_off_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="compress",
            values=["off"]
        )

        # Buffering
        if log_type != "auditfail":
            buf_on_parser = set_parsers.add_parser(
                "buffering-enabled",
                help="Enable log buffering",
                formatter_class=CustomHelpFormatter,
            )
            buf_on_parser.set_defaults(
                func=update_config, logtype=log_type, keyword="logbuffering",
                values=["on"]
            )

            buf_off_parser = set_parsers.add_parser(
                "buffering-disabled",
                help="Disable log buffering",
                formatter_class=CustomHelpFormatter,
            )
            buf_off_parser.set_defaults(
                func=update_config, logtype=log_type, keyword="logbuffering",
                values=["off"]
            )

        # Log rotation size settings
        set_maxlog_parser = set_parsers.add_parser(
            "max-logs",
            help="Set the maximum number of rotated logs the server "
            + "will maintain",
            formatter_class=CustomHelpFormatter,
        )
        set_maxlog_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="maxlogsperdir",
            values=["off"]
        )
        set_maxlog_parser.add_argument(
            "values",
            nargs=1,
            help="Set the maximum number of rotated logs the server "
            + "will maintain",
        )

        set_logsize_parser = set_parsers.add_parser(
            "max-logsize",
            help="Set the maximum size for a log in MB",
            formatter_class=CustomHelpFormatter,
        )
        set_logsize_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="maxlogsize"
        )
        set_logsize_parser.add_argument(
            "values", nargs=1, help="Set the maximum size for a log in MB"
        )

        # Log rotation interval settings
        set_interval_parser = set_parsers.add_parser(
            "rotation-interval",
            help="Set the interval for when a log is rotated."
            + "This works with the interval unit",
            formatter_class=CustomHelpFormatter,
        )
        set_interval_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logrotationtime"
        )
        set_interval_parser.add_argument(
            "values",
            nargs=1,
            help="Set the interval for when a log is rotated."
            + "This works with the interval unit",
        )

        set_interval_unit_parser = set_parsers.add_parser(
            "rotation-interval-unit",
            help="Set the time unit for the rotation interval for when"
            + 'a log is rotated.  Choose between: "minute", "hour", "day", '
            + '"week", and "month"',
            formatter_class=CustomHelpFormatter,
        )
        set_interval_unit_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logrotationtimeunit"
        )
        set_interval_unit_parser.add_argument(
            "values",
            nargs=1,
            help="Set the time unit for the rotation interval for when"
            + 'a log is rotated.  Choose between: "minute", "hour", "day", '
            + '"week", and "month"',
        )

        # Log rotation TOD settings
        set_tod_enabled = set_parsers.add_parser(
            "rotation-tod-enabled",
            help='Enable "time of day" rotation for expired logs',
            formatter_class=CustomHelpFormatter,
        )
        set_tod_enabled.set_defaults(
            func=update_config,
            logtype=log_type,
            keyword="logrotationsync-enabled",
            values=["on"],
        )

        set_tod_disabled = set_parsers.add_parser(
            "rotation-tod-disabled",
            help='Disable "time of day" rotation for expired logs',
            formatter_class=CustomHelpFormatter,
        )
        set_tod_disabled.set_defaults(
            func=update_config,
            logtype=log_type,
            keyword="logrotationsync-enabled",
            values=["off"],
        )

        set_tod_hour_parser = set_parsers.add_parser(
            "rotation-tod-hour",
            help="Set the hour when an expired log should be rotated",
            formatter_class=CustomHelpFormatter,
        )
        set_tod_hour_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logrotationsynchour"
        )
        set_tod_hour_parser.add_argument(
            "values", nargs=1,
            help="Set the hour when an expired log should be rotated"
        )

        set_tod_hour_parser = set_parsers.add_parser(
            "rotation-tod-minute",
            help="Set the minute when an expired log should be rotated",
            formatter_class=CustomHelpFormatter,
        )
        set_tod_hour_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logrotationsyncmin"
        )
        set_tod_hour_parser.add_argument(
            "values", nargs=1,
            help="Set the minute when an expired log should be rotated"
        )

        # Log deletion settings
        set_expire_interval_parser = set_parsers.add_parser(
            "deletion-interval",
            help="Set the interval a rotated log should be deleted. "
            + "This works with the deletion internal unit setting",
            formatter_class=CustomHelpFormatter,
        )
        set_expire_interval_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logexpirationtime"
        )
        set_expire_interval_parser.add_argument(
            "values",
            nargs=1,
            help="Set the interval a rotated log should be deleted. "
            + "This works with the deletion internal unit setting",
        )

        set_expire_interval_unit_parser = set_parsers.add_parser(
            "deletion-interval-unit",
            help="Set the interval unit a rotated log should be deleted. "
            + 'Choose from: "day", "week", or "month"',
            formatter_class=CustomHelpFormatter,
        )
        set_expire_interval_unit_parser.set_defaults(
            func=update_config, logtype=log_type,
            keyword="logexpirationtimeunit"
        )
        set_expire_interval_unit_parser.add_argument(
            "values",
            nargs=1,
            help="Set the interval unit a rotated log should be deleted. "
            + 'Choose from: "day", "week", or "month"',
        )

        set_max_disk_space_parser = set_parsers.add_parser(
            "max-disk-space",
            help="Set the maximum amount of disk space in MB rotated logs can "
            + "consume before rotated logs are deleted.",
            formatter_class=CustomHelpFormatter,
        )
        set_max_disk_space_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logmaxdiskspace"
        )
        set_max_disk_space_parser.add_argument(
            "values",
            nargs=1,
            help="Set the maximum amount of disk space in MB rotated logs can "
            + "consume before rotated logs are deleted.",
        )

        set_free_disk_space_parser = set_parsers.add_parser(
            "free-disk-space",
            help="The server deletes the oldest rotated log file when the "
            + "available disk space in MB is less than this amount.",
            formatter_class=CustomHelpFormatter,
        )
        set_free_disk_space_parser.set_defaults(
            func=update_config, logtype=log_type, keyword="logminfreediskspace"
        )
        set_free_disk_space_parser.add_argument(
            "values",
            nargs=1,
            help="Set the minimum available disk space in MB that triggers "
            + "the server to delete rotated log files.",
        )

        if log_type in ['access', 'audit', 'error']:
            # JSON logging
            set_log_format_parser = set_parsers.add_parser(
                "log-format",
                help='Choose between "default", "json", or "json-pretty"',
                formatter_class=CustomHelpFormatter,
            )
            set_log_format_parser.set_defaults(
                func=update_config, logtype=log_type, keyword="log-format"
            )
            set_log_format_parser.add_argument(
                "values", nargs=1,
                help='Choose between "default", "json", or "json-pretty"'
            )

            set_time_format_parser = set_parsers.add_parser(
                "time-format",
                help="Time format for JSON logging (strftime)",
                formatter_class=CustomHelpFormatter,
            )
            set_time_format_parser.set_defaults(
                func=update_config, logtype=log_type, keyword="time-format"
            )
            set_time_format_parser.add_argument(
                "values", nargs=1, help="Time format for JSON logging "
                                        + "(strftime)"
            )

        if log_type == 'audit':
            set_display_attrs_parser = set_parsers.add_parser(
                "display-attrs",
                help="Sets additional identifying attrs to display",
                formatter_class=CustomHelpFormatter,
            )
            set_display_attrs_parser.set_defaults(func=set_display_attrs)
            set_display_attrs_parser.add_argument(
                "values", nargs="+",
                help="Sets additional identifying attrs to display"
            )
