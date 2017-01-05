#!/usr/bin/python
#
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import optparse
import sys
from lib389 import topologies

"""This script generates a template test script that handles the
non-interesting parts of a test script:
- topology fixture that doesn't exist in in lib389/topologies.py
- test function (to be completed by the user),
- run-isolated function
"""


def displayUsage():
    """Display the usage"""

    print ('\nUsage:\ncreate_ticket.py -t|--ticket <ticket number> ' +
           '-s|--suite <suite name> ' +
           '[ i|--instances <number of standalone instances> ' +
           '[ -m|--masters <number of masters> -h|--hubs <number of hubs> ' +
           '-c|--consumers <number of consumers> ] -o|--outputfile ]\n')
    print ('If only "-t" is provided then a single standalone instance is ' +
           'created. Or you can create a test suite script using ' +
           '"-s|--suite" instead of using "-t|--ticket". The "-i" option ' +
           'can add mulitple standalone instances (maximum 99). However, you' +
           ' can not mix "-i" with the replication options (-m, -h , -c).  ' +
           'There is a maximum of 99 masters, 99 hubs, and 99 consumers.')
    print('If "-s|--suite" option was chosen, then no topology would be added ' +
          'to the test script. You can find predefined fixtures in the lib389/topologies.py ' +
          'and use them or write a new one if you have a special case.')
    exit(1)


def writeFinalizer():
    """Write the finalizer function - delete/stop each instance"""

    def writeInstanceOp(action):
        """Write instance finializer action"""

        if repl_deployment:
            for idx in range(masters):
                idx += 1
                TEST.write('            master' + str(idx) + '.' + action +
                           '()\n')
            for idx in range(hubs):
                idx += 1
                TEST.write('            hub' + str(idx) + '.' + action +
                           '()\n')
            for idx in range(consumers):
                idx += 1
                TEST.write('            consumer' + str(idx) + '.' + action +
                           '()\n')
        else:
            for idx in range(instances):
                idx += 1
                if idx == 1:
                    idx = ''
                else:
                    idx = str(idx)
                TEST.write('            standalone' + idx + '.' + action +
                           '()\n')

    TEST.write('\n    def fin():\n')
    TEST.write('        """If we are debugging just stop the instances,\n' +
               '        otherwise remove them\n')
    TEST.write('        """\n\n')
    TEST.write('        if DEBUGGING:\n')
    writeInstanceOp('stop')
    TEST.write('        else:\n')
    writeInstanceOp('delete')
    TEST.write('\n    request.addfinalizer(fin)')
    TEST.write('\n\n')


def get_existing_topologies(inst, masters, hubs, consumers):
    """Check if the requested topology exists"""

    if inst:
        if inst == 1:
            i = 'st'
        else:
            i = 'i{}'.format(inst)
    else:
        i = ''
    if masters:
        ms = 'm{}'.format(masters)
    else:
        ms = ''
    if hubs:
        hs = 'h{}'.format(hubs)
    else:
        hs = ''
    if consumers:
        cs = 'c{}'.format(consumers)
    else:
        cs = ''

    my_topology = 'topology_{}{}{}{}'.format(i, ms, hs, cs)

    # Returns True in the first element of a list, if topology was found
    if my_topology in dir(topologies):
        return [True, my_topology]
    else:
        return [False, my_topology]


desc = 'Script to generate an initial lib389 test script.  ' + \
       'This generates the topology, test, final, and run-isolated functions.'

if len(sys.argv) > 0:
    parser = optparse.OptionParser(description=desc, add_help_option=False)

    # Script options
    parser.add_option('-t', '--ticket', dest='ticket', default=None)
    parser.add_option('-s', '--suite', dest='suite', default=None)
    parser.add_option('-i', '--instances', dest='inst', default='0')
    parser.add_option('-m', '--masters', dest='masters', default='0')
    parser.add_option('-h', '--hubs', dest='hubs', default='0')
    parser.add_option('-c', '--consumers', dest='consumers', default='0')
    parser.add_option('-o', '--outputfile', dest='filename', default=None)

    # Validate the options
    try:
        (args, opts) = parser.parse_args()
    except:
        displayUsage()

    if args.ticket is None and args.suite is None:
        print('Missing required ticket number/suite name')
        displayUsage()

    if args.ticket and args.suite:
        print('You must choose either "-t|--ticket" or "-s|--suite", ' +
              'but not both.')
        displayUsage()

    if int(args.masters) == 0:
        if int(args.hubs) > 0 or int(args.consumers) > 0:
            print('You must use "-m|--masters" if you want to have hubs ' +
                  'and/or consumers')
            displayUsage()

    if not args.masters.isdigit() or \
           int(args.masters) > 99 or \
           int(args.masters) < 0:
        print('Invalid value for "--masters", it must be a number and it can' +
              ' not be greater than 99')
        displayUsage()

    if not args.hubs.isdigit() or int(args.hubs) > 99 or int(args.hubs) < 0:
        print('Invalid value for "--hubs", it must be a number and it can ' +
              'not be greater than 99')
        displayUsage()

    if not args.consumers.isdigit() or \
           int(args.consumers) > 99 or \
           int(args.consumers) < 0:
        print('Invalid value for "--consumers", it must be a number and it ' +
              'can not be greater than 99')
        displayUsage()

    if args.inst:
        if not args.inst.isdigit() or \
               int(args.inst) > 99 or \
               int(args.inst) < 0:
            print('Invalid value for "--instances", it must be a number ' +
                  'greater than 0 and not greater than 99')
            displayUsage()
        if int(args.inst) > 0:
            if int(args.masters) > 0 or \
                            int(args.hubs) > 0 or \
                            int(args.consumers) > 0:
                print('You can not mix "--instances" with replication.')
                displayUsage()

    # Extract usable values
    ticket = args.ticket
    suite = args.suite
    if not args.inst and not args.masters and not args.hubs and not args.consumers:
        instances = 1
        my_topology = [True, 'topology_st']
    else:
        instances = int(args.inst)
        masters = int(args.masters)
        hubs = int(args.hubs)
        consumers = int(args.consumers)
        my_topology = get_existing_topologies(instances, masters, hubs, consumers)
    filename = args.filename

    # Create/open the new test script file
    if not filename:
        if ticket:
            filename = 'ticket' + ticket + '_test.py'
        else:
            filename = suite + '_test.py'

    try:
        TEST = open(filename, "w")
    except IOError:
        print("Can\'t open file:", filename)
        exit(1)

    # Write the imports
    if my_topology[0]:
        topology_import = 'from lib389.topologies import {}\n'.format(my_topology[1])
    else:
        topology_import = ''

    TEST.write('import time\nimport ldap\n' +
               'import logging\nimport pytest\n')
    TEST.write('from lib389 import DirSrv, Entry, tools, tasks\nfrom ' +
               'lib389.tools import DirSrvTools\nfrom lib389._constants ' +
               'import *\nfrom lib389.properties import *\n' +
               'from lib389.tasks import *\nfrom lib389.utils import *\n' +
               '{}\n'.format(topology_import))

    TEST.write('DEBUGGING = os.getenv("DEBUGGING", default=False)\n')
    TEST.write('if DEBUGGING:\n')
    TEST.write('    logging.getLogger(__name__).setLevel(logging.DEBUG)\n')
    TEST.write('else:\n')
    TEST.write('    logging.getLogger(__name__).setLevel(logging.INFO)\n')
    TEST.write('log = logging.getLogger(__name__)\n\n\n')

    # Add topology function for non existing (in lib389/topologies.py) topologies only
    if not my_topology[0]:
        # Write the replication or standalone classes
        repl_deployment = False

        if masters + hubs + consumers > 0:
            repl_deployment = True

            TEST.write('class TopologyReplication(object):\n')
            TEST.write('    def __init__(self')
            for idx in range(masters):
                TEST.write(', master' + str(idx + 1))
            for idx in range(hubs):
                TEST.write(', hub' + str(idx + 1))
            for idx in range(consumers):
                TEST.write(', consumer' + str(idx + 1))
            TEST.write('):\n')

            for idx in range(masters):
                TEST.write('        master' + str(idx + 1) + '.open()\n')
                TEST.write('        self.master' + str(idx + 1) + ' = master' +
                           str(idx + 1) + '\n')
            for idx in range(hubs):
                TEST.write('        hub' + str(idx + 1) + '.open()\n')
                TEST.write('        self.hub' + str(idx + 1) + ' = hub' +
                           str(idx + 1) + '\n')
            for idx in range(consumers):
                TEST.write('        consumer' + str(idx + 1) + '.open()\n')
                TEST.write('        self.consumer' + str(idx + 1) + ' = consumer' +
                           str(idx + 1) + '\n')
            TEST.write('\n\n')
        else:
            TEST.write('class TopologyStandalone(object):\n')
            TEST.write('    def __init__(self')
            for idx in range(instances):
                idx += 1
                if idx == 1:
                    idx = ''
                else:
                    idx = str(idx)
                TEST.write(', standalone' + idx)
            TEST.write('):\n')

            for idx in range(instances):
                idx += 1
                if idx == 1:
                    idx = ''
                else:
                    idx = str(idx)
                TEST.write('        standalone' + idx + '.open()\n')
                TEST.write('        self.standalone' + idx + ' = standalone' +
                           idx + '\n')
            TEST.write('\n\n')

        # Write the 'topology function'
        TEST.write('@pytest.fixture(scope="module")\n')
        TEST.write('def {}(request):\n'.format(my_topology[1]))

        if repl_deployment:
            TEST.write('    """Create Replication Deployment"""\n')
            for idx in range(masters):
                idx = str(idx + 1)
                TEST.write('\n    # Creating master ' + idx + '...\n')
                TEST.write('    if DEBUGGING:\n')
                TEST.write('        master' + idx + ' = DirSrv(verbose=True)\n')
                TEST.write('    else:\n')
                TEST.write('        master' + idx + ' = DirSrv(verbose=False)\n')
                TEST.write('    args_instance[SER_HOST] = HOST_MASTER_' + idx +
                           '\n')
                TEST.write('    args_instance[SER_PORT] = PORT_MASTER_' + idx +
                           '\n')
                TEST.write('    args_instance[SER_SERVERID_PROP] = ' +
                           'SERVERID_MASTER_' + idx + '\n')
                TEST.write('    args_instance[SER_CREATION_SUFFIX] = ' +
                           'DEFAULT_SUFFIX\n')
                TEST.write('    args_master = args_instance.copy()\n')
                TEST.write('    master' + idx + '.allocate(args_master)\n')
                TEST.write('    instance_master' + idx + ' = master' + idx +
                           '.exists()\n')
                TEST.write('    if instance_master' + idx + ':\n')
                TEST.write('        master' + idx + '.delete()\n')
                TEST.write('    master' + idx + '.create()\n')
                TEST.write('    master' + idx + '.open()\n')
                TEST.write('    master' + idx + '.replica.enableReplication' +
                           '(suffix=SUFFIX, role=REPLICAROLE_MASTER, ' +
                           'replicaId=REPLICAID_MASTER_' + idx + ')\n')

            for idx in range(hubs):
                idx = str(idx + 1)
                TEST.write('\n    # Creating hub ' + idx + '...\n')
                TEST.write('    if DEBUGGING:\n')
                TEST.write('        hub' + idx + ' = DirSrv(verbose=True)\n')
                TEST.write('    else:\n')
                TEST.write('        hub' + idx + ' = DirSrv(verbose=False)\n')
                TEST.write('    args_instance[SER_HOST] = HOST_HUB_' + idx + '\n')
                TEST.write('    args_instance[SER_PORT] = PORT_HUB_' + idx + '\n')
                TEST.write('    args_instance[SER_SERVERID_PROP] = SERVERID_HUB_' +
                           idx + '\n')
                TEST.write('    args_instance[SER_CREATION_SUFFIX] = ' +
                           'DEFAULT_SUFFIX\n')
                TEST.write('    args_hub = args_instance.copy()\n')
                TEST.write('    hub' + idx + '.allocate(args_hub)\n')
                TEST.write('    instance_hub' + idx + ' = hub' + idx +
                           '.exists()\n')
                TEST.write('    if instance_hub' + idx + ':\n')
                TEST.write('        hub' + idx + '.delete()\n')
                TEST.write('    hub' + idx + '.create()\n')
                TEST.write('    hub' + idx + '.open()\n')
                TEST.write('    hub' + idx + '.replica.enableReplication' +
                           '(suffix=SUFFIX, role=REPLICAROLE_HUB, ' +
                           'replicaId=REPLICAID_HUB_' + idx + ')\n')

            for idx in range(consumers):
                idx = str(idx + 1)
                TEST.write('\n    # Creating consumer ' + idx + '...\n')
                TEST.write('    if DEBUGGING:\n')
                TEST.write('        consumer' + idx + ' = DirSrv(verbose=True)\n')
                TEST.write('    else:\n')
                TEST.write('        consumer' + idx + ' = DirSrv(verbose=False)\n')
                TEST.write('    args_instance[SER_HOST] = HOST_CONSUMER_' + idx +
                           '\n')
                TEST.write('    args_instance[SER_PORT] = PORT_CONSUMER_' + idx +
                           '\n')
                TEST.write('    args_instance[SER_SERVERID_PROP] = ' +
                           'SERVERID_CONSUMER_' + idx + '\n')
                TEST.write('    args_instance[SER_CREATION_SUFFIX] = ' +
                           'DEFAULT_SUFFIX\n')
                TEST.write('    args_consumer = args_instance.copy()\n')
                TEST.write('    consumer' + idx + '.allocate(args_consumer)\n')
                TEST.write('    instance_consumer' + idx + ' = consumer' + idx +
                           '.exists()\n')
                TEST.write('    if instance_consumer' + idx + ':\n')
                TEST.write('        consumer' + idx + '.delete()\n')
                TEST.write('    consumer' + idx + '.create()\n')
                TEST.write('    consumer' + idx + '.open()\n')
                TEST.write('    consumer' + idx + '.replica.enableReplication' +
                           '(suffix=SUFFIX, role=REPLICAROLE_CONSUMER, ' +
                           'replicaId=CONSUMER_REPLICAID)\n')

            writeFinalizer()

            # Create the master agreements
            TEST.write('    # Create all the agreements\n')
            agmt_count = 0

            for idx in range(masters):
                master_idx = idx + 1

                # Create agreements with the other masters (master -> master)
                for idx in range(masters):
                    idx += 1

                    # Skip ourselves
                    if master_idx == idx:
                        continue

                    TEST.write('\n    # Creating agreement from master ' +
                               str(master_idx) + ' to master ' + str(idx) + '\n')
                    TEST.write("    properties = {RA_NAME: " +
                               "'meTo_' + master" + str(idx) +
                               ".host + ':' + str(master" + str(idx) +
                               ".port),\n")
                    TEST.write("                  RA_BINDDN: " +
                               "defaultProperties[REPLICATION_BIND_DN],\n")
                    TEST.write("                  RA_BINDPW: " +
                               "defaultProperties[REPLICATION_BIND_PW],\n")
                    TEST.write("                  RA_METHOD: " +
                               "defaultProperties[REPLICATION_BIND_METHOD],\n")
                    TEST.write("                  RA_TRANSPORT_PROT: " +
                               "defaultProperties[REPLICATION_TRANSPORT]}\n")
                    TEST.write('    m' + str(master_idx) + '_m' + str(idx) +
                               '_agmt = master' + str(master_idx) +
                               '.agreement.create(suffix=SUFFIX, host=master' +
                               str(idx) + '.host, port=master' + str(idx) +
                               '.port, properties=properties)\n')
                    TEST.write('    if not m' + str(master_idx) + '_m' + str(idx) +
                               '_agmt:\n')
                    TEST.write('        log.fatal("Fail to create a master -> ' +
                               'master replica agreement")\n')
                    TEST.write('        sys.exit(1)\n')
                    TEST.write('    log.debug("%s created" % m' + str(master_idx) +
                               '_m' + str(idx) + '_agmt)\n')
                    agmt_count += 1

                # Create agmts from each master to each hub (master -> hub)
                for idx in range(hubs):
                    idx += 1
                    TEST.write('\n    # Creating agreement from master ' +
                               str(master_idx) + ' to hub ' + str(idx) + '\n')
                    TEST.write("    properties = {RA_NAME: " +
                               "'meTo_' + hub" + str(idx) +
                               ".host + ':' + str(hub" + str(idx) +
                               ".port),\n")
                    TEST.write("                  RA_BINDDN: " +
                               "defaultProperties[REPLICATION_BIND_DN],\n")
                    TEST.write("                  RA_BINDPW: " +
                               "defaultProperties[REPLICATION_BIND_PW],\n")
                    TEST.write("                  RA_METHOD: " +
                               "defaultProperties[REPLICATION_BIND_METHOD],\n")
                    TEST.write("                  RA_TRANSPORT_PROT: " +
                               "defaultProperties[REPLICATION_TRANSPORT]}\n")
                    TEST.write('    m' + str(master_idx) + '_h' + str(idx) +
                               '_agmt = master' + str(master_idx) +
                               '.agreement.create(suffix=SUFFIX, host=hub' +
                               str(idx) + '.host, port=hub' + str(idx) +
                               '.port, properties=properties)\n')
                    TEST.write('    if not m' + str(master_idx) + '_h' + str(idx) +
                               '_agmt:\n')
                    TEST.write('        log.fatal("Fail to create a master -> ' +
                               'hub replica agreement")\n')
                    TEST.write('        sys.exit(1)\n')
                    TEST.write('    log.debug("%s created" % m' + str(master_idx) +
                               '_h' + str(idx) + '_agmt)\n')
                    agmt_count += 1

            # Create the hub agreements
            for idx in range(hubs):
                hub_idx = idx + 1

                # Add agreements from each hub to each consumer (hub -> consumer)
                for idx in range(consumers):
                    idx += 1
                    TEST.write('\n    # Creating agreement from hub ' + str(hub_idx)
                               + ' to consumer ' + str(idx) + '\n')
                    TEST.write("    properties = {RA_NAME: " +
                               "'meTo_' + consumer" + str(idx) +
                               ".host + ':' + str(consumer" + str(idx) +
                               ".port),\n")
                    TEST.write("                  RA_BINDDN: " +
                               "defaultProperties[REPLICATION_BIND_DN],\n")
                    TEST.write("                  RA_BINDPW: " +
                               "defaultProperties[REPLICATION_BIND_PW],\n")
                    TEST.write("                  RA_METHOD: " +
                               "defaultProperties[REPLICATION_BIND_METHOD],\n")
                    TEST.write("                  RA_TRANSPORT_PROT: " +
                               "defaultProperties[REPLICATION_TRANSPORT]}\n")
                    TEST.write('    h' + str(hub_idx) + '_c' + str(idx) +
                               '_agmt = hub' + str(hub_idx) +
                               '.agreement.create(suffix=SUFFIX, host=consumer' +
                               str(idx) + '.host, port=consumer' + str(idx) +
                               '.port, properties=properties)\n')
                    TEST.write('    if not h' + str(hub_idx) + '_c' + str(idx) +
                               '_agmt:\n')
                    TEST.write('        log.fatal("Fail to create a hub -> ' +
                               'consumer replica agreement")\n')
                    TEST.write('        sys.exit(1)\n')
                    TEST.write('    log.debug("%s created" % h' + str(hub_idx) +
                               '_c' + str(idx) + '_agmt)\n')
                    agmt_count += 1

            # No Hubs, see if there are any consumers to create agreements to
            if hubs == 0:

                # Create agreements with the consumers (master -> consumer)
                for idx in range(masters):
                    master_idx = idx + 1

                    for idx in range(consumers):
                        idx += 1
                        TEST.write('\n    # Creating agreement from master ' +
                                   str(master_idx) + ' to consumer ' + str(idx) +
                                   '\n')
                        TEST.write("    properties = {RA_NAME: " +
                                   "'meTo_' + consumer" + str(idx) +
                                   ".host + ':' + str(consumer" + str(idx) +
                                   ".port),\n")
                        TEST.write("                  RA_BINDDN: " +
                                   "defaultProperties[REPLICATION_BIND_DN],\n")
                        TEST.write("                  RA_BINDPW: " +
                                   "defaultProperties[REPLICATION_BIND_PW],\n")
                        TEST.write("                  RA_METHOD: " +
                                   "defaultProperties[REPLICATION_BIND_METHOD],\n")
                        TEST.write("                  RA_TRANSPORT_PROT: " +
                                   "defaultProperties[REPLICATION_TRANSPORT]}\n")
                        TEST.write('    m' + str(master_idx) + '_c' + str(idx) +
                                   '_agmt = master' + str(master_idx) +
                                   '.agreement.create(suffix=SUFFIX, ' +
                                   'host=consumer' + str(idx) +
                                   '.host, port=consumer' + str(idx) +
                                   '.port, properties=properties)\n')
                        TEST.write('    if not m' + str(master_idx) + '_c' +
                                   str(idx) + '_agmt:\n')
                        TEST.write('        log.fatal("Fail to create a hub -> ' +
                                   'consumer replica agreement")\n')
                        TEST.write('        sys.exit(1)\n')
                        TEST.write('    log.debug("%s created" % m' +
                                   str(master_idx) + '_c' + str(idx) +
                                   '_agmt)\n')
                        agmt_count += 1

            # Add sleep that allows all the agreements to get situated
            TEST.write('\n    # Allow the replicas to get situated with the new ' +
                       'agreements...\n')
            TEST.write('    time.sleep(5)\n')

            # Write the replication initializations
            TEST.write('\n    # Initialize all the agreements\n')

            # Masters
            for idx in range(masters):
                idx += 1
                if idx == 1:
                    continue
                TEST.write('    master1.agreement.init(SUFFIX, HOST_MASTER_' +
                           str(idx) + ', PORT_MASTER_' + str(idx) + ')\n')
                TEST.write('    master1.waitForReplInit(m1_m' + str(idx) +
                           '_agmt)\n')

            # Hubs
            consumers_inited = False
            for idx in range(hubs):
                idx += 1
                TEST.write('    master1.agreement.init(SUFFIX, HOST_HUB_' +
                           str(idx) + ', PORT_HUB_' + str(idx) + ')\n')
                TEST.write('    master1.waitForReplInit(m1_h' + str(idx) +
                           '_agmt)\n')
                for idx in range(consumers):
                    if consumers_inited:
                        continue
                    idx += 1
                    TEST.write('    hub1.agreement.init(SUFFIX, HOST_CONSUMER_' +
                               str(idx) + ', PORT_CONSUMER_' + str(idx) + ')\n')
                    TEST.write('    hub1.waitForReplInit(h1_c' + str(idx) +
                               '_agmt)\n')
                consumers_inited = True

            # Consumers (master -> consumer)
            if hubs == 0:
                for idx in range(consumers):
                    idx += 1
                    TEST.write('    master1.agreement.init(SUFFIX, ' +
                               'HOST_CONSUMER_' + str(idx) + ', PORT_CONSUMER_' +
                               str(idx) + ')\n')
                    TEST.write('    master1.waitForReplInit(m1_c' + str(idx) +
                               '_agmt)\n')

            TEST.write('\n')

            # Write replicaton check
            if agmt_count > 0:
                # Find the lowest replica type (consumer -> master)
                if consumers > 0:
                    replica = 'consumer1'
                elif hubs > 0:
                    replica = 'hub1'
                else:
                    replica = 'master2'
                TEST.write('    # Check replication is working...\n')
                TEST.write('    if master1.testReplication(DEFAULT_SUFFIX, ' +
                           replica + '):\n')
                TEST.write("        log.info('Replication is working.')\n")
                TEST.write('    else:\n')
                TEST.write("        log.fatal('Replication is not working.')\n")
                TEST.write('        assert False\n')
                TEST.write('\n')

            # Write the finals steps for replication
            TEST.write('    # Clear out the tmp dir\n')
            TEST.write('    master1.clearTmpDir(__file__)\n\n')
            TEST.write('    return TopologyReplication(master1')

            for idx in range(masters):
                idx += 1
                if idx == 1:
                    continue
                TEST.write(', master' + str(idx))
            for idx in range(hubs):
                TEST.write(', hub' + str(idx + 1))
            for idx in range(consumers):
                TEST.write(', consumer' + str(idx + 1))
            TEST.write(')\n\n\n')

        # Standalone servers
        else:
            TEST.write('    """Create DS Deployment"""\n')
            for idx in range(instances):
                idx += 1
                if idx == 1:
                    idx = ''
                else:
                    idx = str(idx)
                TEST.write('\n    # Creating standalone instance ' + idx + '...\n')
                TEST.write('    if DEBUGGING:\n')
                TEST.write('        standalone' + idx +
                           ' = DirSrv(verbose=True)\n')
                TEST.write('    else:\n')
                TEST.write('        standalone' + idx +
                           ' = DirSrv(verbose=False)\n')
                TEST.write('    args_instance[SER_HOST] = HOST_STANDALONE' +
                           idx + '\n')
                TEST.write('    args_instance[SER_PORT] = PORT_STANDALONE' +
                           idx + '\n')
                TEST.write('    args_instance[SER_SERVERID_PROP] = ' +
                           'SERVERID_STANDALONE' + idx + '\n')
                TEST.write('    args_instance[SER_CREATION_SUFFIX] = ' +
                           'DEFAULT_SUFFIX\n')
                TEST.write('    args_standalone' + idx + ' = args_instance.copy' +
                           '()\n')
                TEST.write('    standalone' + idx + '.allocate(args_standalone' +
                           idx + ')\n')

                # Get the status of the instance and restart it if it exists
                TEST.write('    instance_standalone' + idx + ' = standalone' +
                           idx + '.exists()\n')

                # Remove the instance
                TEST.write('    if instance_standalone' + idx + ':\n')
                TEST.write('        standalone' + idx + '.delete()\n')

                # Create and open the instance
                TEST.write('    standalone' + idx + '.create()\n')
                TEST.write('    standalone' + idx + '.open()\n')

            writeFinalizer()

            TEST.write('    return TopologyStandalone(standalone')
            for idx in range(instances):
                idx += 1
                if idx == 1:
                    continue
                TEST.write(', standalone' + str(idx))
            TEST.write(')\n\n\n')

    # Write the test function
    if ticket:
        TEST.write('def test_ticket{}({}):\n'.format(ticket, my_topology[1]))
        if repl_deployment:
            TEST.write('    """Write your replication test here.\n\n')
            TEST.write('    To access each DirSrv instance use:  ' +
                       'topology.master1, topology.master2,\n' +
                       '        ..., topology.hub1, ..., topology.consumer1' +
                       ',...\n\n')
            TEST.write('    Also, if you need any testcase initialization,\n')
            TEST.write('    please, write additional fixture for that' +
                       '(including finalizer).\n')
        else:
            TEST.write('    """Write your testcase here...\n\n')
            TEST.write('    Also, if you need any testcase initialization,\n')
            TEST.write('    please, write additional fixture for that' +
                       '(include finalizer).\n')
        TEST.write('    """\n\n')
    else:
        TEST.write('def test_something({}):\n'.format(my_topology[1]))
        TEST.write('    """Write a single test here...\n\n')
        TEST.write('    Also, if you need any test suite initialization,\n')
        TEST.write('    please, write additional fixture for that (including finalizer).\n'
                   '    Topology for suites are predefined in lib389/topologies.py.\n'
                   '    """\n\n')

    TEST.write('    if DEBUGGING:\n')
    TEST.write('        # Add debugging steps(if any)...\n')
    TEST.write('        pass\n\n\n')

    # Write the main function
    TEST.write("if __name__ == '__main__':\n")
    TEST.write('    # Run isolated\n')
    TEST.write('    # -s for DEBUG mode\n')
    TEST.write('    CURRENT_FILE = os.path.realpath(__file__)\n')
    TEST.write('    pytest.main("-s %s" % CURRENT_FILE)\n\n')

    # Done, close things up
    TEST.close()
    print('Created: ' + filename)
