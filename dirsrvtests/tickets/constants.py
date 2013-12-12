'''
Created on Oct 31, 2013

@author: tbordaz
'''
import os
from lib389 import DN_DM
from lib389._constants import *
from lib389.properties import *

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
ALL_INSTANCES = [ {SER_HOST: HOST_STANDALONE, SER_PORT: PORT_STANDALONE, SER_SERVERID_PROP: SERVERID_STANDALONE},
                  {SER_HOST: HOST_MASTER,     SER_PORT: PORT_MASTER,     SER_SERVERID_PROP: SERVERID_MASTER},
                  {SER_HOST: HOST_CONSUMER,   SER_PORT: PORT_CONSUMER,   SER_SERVERID_PROP: SERVERID_CONSUMER},
                  {SER_HOST: HOST_MASTER_1,   SER_PORT: PORT_MASTER_1,   SER_SERVERID_PROP: SERVERID_MASTER_1},
                  {SER_HOST: HOST_MASTER_2,   SER_PORT: PORT_MASTER_2,   SER_SERVERID_PROP: SERVERID_MASTER_2},
                  {SER_HOST: HOST_CONSUMER_1, SER_PORT: PORT_CONSUMER_1, SER_SERVERID_PROP: SERVERID_CONSUMER_1},
                  {SER_HOST: HOST_CONSUMER_2, SER_PORT: PORT_CONSUMER_2, SER_SERVERID_PROP: SERVERID_CONSUMER_2},
                 ]
# This is a template
args_instance = {
                   SER_DEPLOYED_DIR: os.environ.get('PREFIX', None),
                   SER_BACKUP_INST_DIR: os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR),
                   SER_ROOT_DN: DN_DM,
                   SER_ROOT_PW: PASSWORD,
                   SER_HOST: LOCALHOST,
                   SER_PORT: DEFAULT_PORT,
                   SER_SERVERID_PROP: "template",
                   SER_CREATION_SUFFIX: DEFAULT_SUFFIX}


