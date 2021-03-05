#!/usr/bin/python3
#
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import optparse
import os
import re
import sys
import uuid
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
           '[ -m|--suppliers <number of suppliers> -h|--hubs <number of hubs> ' +
           '-c|--consumers <number of consumers> ] -o|--outputfile ]\n')
    print ('If only "-t" is provided then a single standalone instance is ' +
           'created. Or you can create a test suite script using ' +
           '"-s|--suite" instead of using "-t|--ticket". The "-i" option ' +
           'can add mulitple standalone instances (maximum 99). However, you' +
           ' can not mix "-i" with the replication options (-m, -h , -c).  ' +
           'There is a maximum of 99 suppliers, 99 hubs, and 99 consumers.')
    print('If "-s|--suite" option was chosen, then no topology would be added ' +
          'to the test script. You can find predefined fixtures in the lib389/topologies.py ' +
          'and use them or write a new one if you have a special case.')
    exit(1)


def writeFinalizer():
    """Write the finalizer function - delete/stop each instance"""

    def writeInstanceOp(action):
        TEST.write('            map(lambda inst: inst.{}(), topology.all_insts.values())\n'.format(action))

    TEST.write('\n    def fin():\n')
    TEST.write('        """If we are debugging just stop the instances, otherwise remove them"""\n\n')
    TEST.write('        if DEBUGGING:\n')
    writeInstanceOp('stop')
    TEST.write('        else:\n')
    writeInstanceOp('delete')
    TEST.write('\n    request.addfinalizer(fin)')
    TEST.write('\n\n')


def get_existing_topologies(inst, suppliers, hubs, consumers):
    """Check if the requested topology exists"""
    setup_text = ""

    if inst:
        if inst == 1:
            i = 'st'
            setup_text = "Standalone Instance"
        else:
            i = 'i{}'.format(inst)
            setup_text = "{} Standalone Instances".format(inst)
    else:
        i = ''
    if suppliers:
        ms = 'm{}'.format(suppliers)
        if len(setup_text) > 0:
            setup_text += ", "
        if suppliers == 1:
            setup_text += "Supplier Instance"
        else:
            setup_text += "{} Supplier Instances".format(suppliers)
    else:
        ms = ''
    if hubs:
        hs = 'h{}'.format(hubs)
        if len(setup_text) > 0:
            setup_text += ", "
        if hubs == 1:
            setup_text += "Hub Instance"
        else:
            setup_text += "{} Hub Instances".format(hubs)
    else:
        hs = ''
    if consumers:
        cs = 'c{}'.format(consumers)
        if len(setup_text) > 0:
            setup_text += ", "
        if consumers == 1:
            setup_text += "Consumer Instance"
        else:
            setup_text += "{} Consumer Instances".format(consumers)
    else:
        cs = ''

    my_topology = 'topology_{}{}{}{}'.format(i, ms, hs, cs)

    # Returns True in the first element of a list, if topology was found
    if my_topology in dir(topologies):
        return [True, my_topology, setup_text]
    else:
        return [False, my_topology, setup_text]


def check_id_uniqueness(id_value):
    """Checks if ID is already present in other tests.
    create_test.py script should exist in the directory
    with a 'tests' dir.
    """

    tests_dir = os.path.join(os.getcwd(), 'tests')

    for root, dirs, files in os.walk(tests_dir):
        for name in files:
            if name.endswith('.py'):
                with open(os.path.join(root, name), "r") as cifile:
                    for line in cifile:
                        if re.search(str(id_value), line):
                            return False

    return True


desc = 'Script to generate an initial lib389 test script.  ' + \
       'This generates the topology, test, final, and run-isolated functions.'

if len(sys.argv) > 0:
    parser = optparse.OptionParser(description=desc, add_help_option=False)

    # Script options
    parser.add_option('-t', '--ticket', dest='ticket', default=None)
    parser.add_option('-s', '--suite', dest='suite', default=None)
    parser.add_option('-i', '--instances', dest='inst', default='0')
    parser.add_option('-m', '--suppliers', dest='suppliers', default='0')
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

    if int(args.suppliers) == 0:
        if int(args.hubs) > 0 or int(args.consumers) > 0:
            print('You must use "-m|--suppliers" if you want to have hubs ' +
                  'and/or consumers')
            displayUsage()

    if not args.suppliers.isdigit() or \
           int(args.suppliers) > 99 or \
           int(args.suppliers) < 0:
        print('Invalid value for "--suppliers", it must be a number and it can' +
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
            if int(args.suppliers) > 0 or \
                            int(args.hubs) > 0 or \
                            int(args.consumers) > 0:
                print('You can not mix "--instances" with replication.')
                displayUsage()

    # Extract usable values
    ticket = args.ticket
    suite = args.suite

    if args.inst == '0' and args.suppliers == '0' and args.hubs == '0' \
       and args.consumers == '0':
        instances = 1
        my_topology = [True, 'topology_st', "Standalone Instance"]
    else:
        instances = int(args.inst)
        suppliers = int(args.suppliers)
        hubs = int(args.hubs)
        consumers = int(args.consumers)
        my_topology = get_existing_topologies(instances, suppliers, hubs, consumers)
    filename = args.filename
    setup_text = my_topology[2]

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
        topology_import = 'from lib389.topologies import {} as topo\n'.format(my_topology[1])
    else:
        topology_import = 'from lib389.topologies import create_topology\n'

    TEST.write('import logging\nimport pytest\nimport os\n')
    TEST.write('from lib389._constants import *\n')
    TEST.write('{}\n'.format(topology_import))
    TEST.write('log = logging.getLogger(__name__)\n\n')

    # Add topology function for non existing (in lib389/topologies.py) topologies only
    if not my_topology[0]:
        # Write the replication or standalone classes
        topologies_str = ""
        if suppliers > 0:
            topologies_str += " {} suppliers".format(suppliers)
        if hubs > 0:
            topologies_str += " {} hubs".format(hubs)
        if consumers > 0:
            topologies_str += " {} consumers".format(consumers)
        if instances > 0:
            topologies_str += " {} standalone instances".format(instances)

        # Write the 'topology function'
        TEST.write('\n@pytest.fixture(scope="module")\n')
        TEST.write('def topo(request):\n')
        TEST.write('    """Create a topology with{}"""\n\n'.format(topologies_str))
        TEST.write('    topology = create_topology({\n')
        if suppliers > 0:
            TEST.write('        ReplicaRole.SUPPLIER: {},\n'.format(suppliers))
        if hubs > 0:
            TEST.write('        ReplicaRole.HUB: {},\n'.format(hubs))
        if consumers > 0:
            TEST.write('        ReplicaRole.CONSUMER: {},\n'.format(consumers))
        if instances > 0:
            TEST.write('        ReplicaRole.STANDALONE: {},\n'.format(instances))
        TEST.write('        })\n')

        TEST.write('    # You can write replica test here. Just uncomment the block and choose instances\n')
        TEST.write('    # replicas = Replicas(topology.ms["supplier1"])\n')
        TEST.write('    # replicas.test(DEFAULT_SUFFIX, topology.cs["consumer1"])\n')

        writeFinalizer()
        TEST.write('    return topology\n\n')

    tc_id = '0'
    while not check_id_uniqueness(tc_id): tc_id = uuid.uuid4()

    # Write the test function
    if ticket:
        TEST.write('\ndef test_ticket{}(topo):\n'.format(ticket))
    else:
        TEST.write('\ndef test_something(topo):\n')
    TEST.write('    """Specify a test case purpose or name here\n\n')
    TEST.write('    :id: {}\n'.format(tc_id))
    TEST.write('    :setup: ' + setup_text + '\n')
    TEST.write('    :steps:\n')
    TEST.write('        1. Fill in test case steps here\n')
    TEST.write('        2. And indent them like this (RST format requirement)\n')
    TEST.write('    :expectedresults:\n')
    TEST.write('        1. Fill in the result that is expected\n')
    TEST.write('        2. For each test step\n')
    TEST.write('    """\n\n')
    TEST.write('    # If you need any test suite initialization,\n')
    TEST.write('    # please, write additional fixture for that (including finalizer).\n'
               '    # Topology for suites are predefined in lib389/topologies.py.\n\n')
    TEST.write('    # If you need host, port or any other data about instance,\n')
    TEST.write('    # Please, use the instance object attributes for that (for example, topo.ms["supplier1"].serverid)\n\n\n')

    # Write the main function
    TEST.write("if __name__ == '__main__':\n")
    TEST.write('    # Run isolated\n')
    TEST.write('    # -s for DEBUG mode\n')
    TEST.write('    CURRENT_FILE = os.path.realpath(__file__)\n')
    TEST.write('    pytest.main(["-s", CURRENT_FILE])\n\n')

    # Done, close things up
    TEST.close()
    print('Created: ' + filename)
