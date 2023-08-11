#!/usr/bin/env python3
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import argparse
import time
import random
import ldap

DESC="""
A test tool that measure base search operation latency when n connections are open. (With moderate average load)
"""

parser = argparse.ArgumentParser(
                    prog='ltest',
                    description='Latency tester')

parser.add_argument('-t', '--test-duration', type=int, help='Latency test duration in seconds')
parser.add_argument('-T', '--wait-time', type=int, default=10, help='Wait time between operations in milliseconds')
parser.add_argument('-H', '--uri', default='ldap://localhost:389', help='LDAP Uniform Resource Identifier')
parser.add_argument('-b', '--basedn', default='ou=people, dc=example, dc=com', help='Search Base DN')
parser.add_argument('-D', '--binddn', default='cn=directory manager', help='Bind DN')
parser.add_argument('-w', '--bindpw', default='password', help='Bind password')
parser.add_argument('-n', '--nbconn', type=int, default=20000, help='Number of connections')
parser.add_argument('-v', '--verbose', action='count', default=0, help='Verbose mode')

args = parser.parse_args()

conns = []
for i in range(args.nbconn):
    try:
        if (i+1) % 1000 == 0:
            print (f'{i+1} connections are open')
        conn = ldap.initialize(args.uri, trace_level=args.verbose)
        conn.set_option(ldap.OPT_REFERRALS, 0)
        conn.simple_bind_s(args.binddn, args.bindpw)
        conns.append(conn)
    except ldap.LDAPError as ex:
        print (f'Failed to open connection #{i}')
        raise ex
print (f'{args.nbconn} connections are open. Starting the latency test using {args} as parameters')

now = time.time()
end_time = None
if args.test_duration:
    print(f'{now}')
    end_time = now + args.test_duration

ltime = now
sum = 0
nbops = 0
while True:
    now = time.time()
    if now != ltime and nbops > 0:
        print(f"Performed {nbops} operations. Average operation time is: {sum/nbops/1000000} ms.")
        sum = 0
        nbops = 0
    if end_time and time.time() >= end_time:
        break
    ltime = now
    time.sleep(args.wait_time/1000.0)
    conn = random.choice(conns)
    stime = time.perf_counter_ns()
    conn.search_s(args.basedn, ldap.SCOPE_BASE, attrlist = ['dn'])
    etime = time.perf_counter_ns()
    sum += (etime-stime)
    nbops += 1
 
for conn in conns:
    conn.unbind()   
