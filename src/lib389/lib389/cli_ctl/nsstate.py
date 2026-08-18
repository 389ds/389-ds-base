# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import json
from lib389.dseldif import DSEldif
from lib389.cli_base import CustomHelpFormatter


def get_nsstate(inst, log, args):
    """Process the nsState attribute"""
    dse_ldif = DSEldif(inst)
    states = dse_ldif.readNsState(suffix=args.suffix, flip=args.flip)
    if args.json:
        log.info(json.dumps(states, indent=4))
    else:
        for state in states:
            log.info("Replica DN:           " + state['dn'])
            log.info("Replica Suffix:       " + state['suffix'])
            log.info("Replica ID:           " + state['rid'])
            log.info("Gen Time:             " + state['gen_time'])
            log.info("Gen Time String:      " + state['gen_time_str'])
            log.info("Gen as CSN:           " + state['gencsn'])
            log.info("Local Offset:         " + state['local_offset'])
            log.info("Local Offset String:  " + state['local_offset_str'])
            log.info("Remote Offset:        " + state['remote_offset'])
            log.info("Remote Offset String: " + state['remote_offset_str'])
            log.info("Time Skew:            " + state['time_skew'])
            log.info("Time Skew String:     " + state['time_skew_str'])
            log.info("Seq Num:              " + state['seq_num'])
            log.info("System Time:          " + state['sys_time'])
            log.info("Diff in Seconds:      " + state['diff_secs'])
            log.info("Diff in days/secs:    " + state['diff_days_secs'])
            log.info("Endian:               " + state['endian'])
            log.info("")


def create_parser(subparsers):
    repl_get_nsstate = subparsers.add_parser('get-nsstate', help="""Get the replication nsState in a human readable format

Replica DN:           The DN of the replication configuration entry
Replica Suffix:       The replicated suffix
Replica ID:           The Replica identifier
Gen Time              The time the CSN generator was created
Gen Time String:      The time string of generator
Gen as CSN:           The generation CSN
Local Offset:         The offset due to the local clock being set back
Local Offset String:  The offset in a nice human format
Remote Offset:        The offset due to clock difference with remote systems
Remote Offset String: The offset in a nice human format
Time Skew:            The time skew between this server and its replicas
Time Skew String:     The time skew in a nice human format
Seq Num:              The number of multiple csns within a second
System Time:          The local system time
Diff in Seconds:      The time difference in seconds from the CSN generator creation to now
Diff in days/secs:    The time difference broken up into days and seconds
Endian:               Little/Big Endian
""")
    repl_get_nsstate.add_argument('--suffix', default=False, help='The DN of the replication suffix to read the state from')
    repl_get_nsstate.add_argument('--flip', default=False, help='Flip between Little/Big Endian, this might be required for certain architectures')
    repl_get_nsstate.set_defaults(func=get_nsstate)
