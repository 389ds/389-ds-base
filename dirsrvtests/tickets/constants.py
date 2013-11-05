'''
Created on Oct 31, 2013

@author: tbordaz
'''
import os
from lib389 import DN_DM

LOCALHOST = "localhost.localdomain"
SUFFIX    = 'dc=example,dc=com'
PASSWORD  = 'password'

# Used for standalone topology
HOST_STANDALONE = LOCALHOST
PORT_STANDALONE = 33389
SERVERID_STANDALONE = 'standalone'

# Used for One master / One consumer topology
HOST_MASTER = LOCALHOST
PORT_MASTER = 40389
SERVERID_MASTER = 'master'
REPLICAID_MASTER = 1

HOST_CONSUMER = LOCALHOST
PORT_CONSUMER = 50389
SERVERID_CONSUMER = 'consumer'

# Used for two masters / two consumers toplogy
HOST_MASTER_1 = LOCALHOST
PORT_MASTER_1 = 44389
SERVERID_MASTER_1 = 'master_1'
REPLICAID_MASTER_1 = 1

HOST_MASTER_2 = LOCALHOST
PORT_MASTER_2 = 45389
SERVERID_MASTER_2 = 'master_2'
REPLICAID_MASTER_2 = 2

HOST_CONSUMER_1 = LOCALHOST
PORT_CONSUMER_1 = 54389
SERVERID_CONSUMER_1 = 'consumer_1'

HOST_CONSUMER_2 = LOCALHOST
PORT_CONSUMER_2 = 55389
SERVERID_CONSUMER_2 = 'consumer_2'

# Each defined instance above must be added in that list 
ALL_INSTANCES = [ {'host': HOST_STANDALONE, 'port': PORT_STANDALONE, 'serverid': SERVERID_STANDALONE},
                  {'host': HOST_MASTER,     'port': PORT_MASTER,     'serverid': SERVERID_MASTER},
                  {'host': HOST_CONSUMER,   'port': PORT_CONSUMER,   'serverid': SERVERID_CONSUMER},
                  {'host': HOST_MASTER_1,   'port': PORT_MASTER_1,   'serverid': SERVERID_MASTER_1},
                  {'host': HOST_MASTER_2,   'port': PORT_MASTER_2,   'serverid': SERVERID_MASTER_2},
                  {'host': HOST_CONSUMER_1, 'port': PORT_CONSUMER_1, 'serverid': SERVERID_CONSUMER_1},
                  {'host': HOST_CONSUMER_2, 'port': PORT_CONSUMER_2, 'serverid': SERVERID_CONSUMER_2},
                 ]
# This is a template
args_instance = {
                   'prefix': os.environ.get('PREFIX', None),
                   'backupdir': os.environ.get('BACKUPDIR', "/tmp"),
                   'newrootdn': DN_DM,
                   'newrootpw': PASSWORD,
                   'newhost': LOCALHOST,
                   'newport': 389,
                   'newinstance': "template",
                   'newsuffix': SUFFIX,
                   'no_admin': True}
