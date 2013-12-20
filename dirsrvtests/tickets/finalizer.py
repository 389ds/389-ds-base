'''
Created on Nov 5, 2013

@author: tbordaz
'''
import os
import sys
import time
import ldap
import logging
import socket
import time
import logging
import pytest
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import DN_DM
from lib389.properties import *
from constants import *

log = logging.getLogger(__name__)

global installation_prefix
installation_prefix=os.getenv('PREFIX')

def test_finalizer():
    global installation_prefix
    
    # for each defined instance, remove it
    for args_instance in ALL_INSTANCES:
        if installation_prefix:
            # overwrite the environment setting
            args_instance[SER_DEPLOYED_DIR] = installation_prefix
            
        instance = DirSrv(verbose=True)
        instance.allocate(args_instance)
        if instance.exists():
            instance.delete()
            
        # remove any existing backup for this instance
        instance.clearBackupFS()
        
def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to 
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix =  None
        
    test_finalizer()

if __name__ == '__main__':
    run_isolated()

