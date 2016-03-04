'''
Created on Dec 18, 2013

@author: rmeggins
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
SERVERID_STANDALONE = 'schematest'

# Each defined instance above must be added in that list 
ALL_INSTANCES = [ {SER_HOST: HOST_STANDALONE, SER_PORT: PORT_STANDALONE, SER_SERVERID_PROP: SERVERID_STANDALONE},
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


