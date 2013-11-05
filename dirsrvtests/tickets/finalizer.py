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
from constants import *

log = logging.getLogger(__name__)

global installation_prefix


def _remove_instance(args):
    
    # check the instance parameters
    args_instance['newhost'] = args.get('host', None)
    if not args_instance['newhost']:
        raise ValueError("host not defined")
    
    args_instance['newport'] = args.get('port', None)
    if not args_instance['newport']:
        raise ValueError("port not defined")
    
    args_instance['newinstance'] = args.get('serverid', None)
    if not args_instance['newinstance']:
        raise ValueError("serverid not defined")

    args_instance['prefix'] = args.get('prefix', None)

    # Get the status of the instance and remove it if it exists
    instance   = DirSrvTools.existsInstance(args_instance)
    if instance:
        log.debug("_remove_instance %s %s:%d" % (instance.serverId, instance.host, instance.port))
        DirSrvTools.removeInstance(instance)


def test_finalizer():
    global installation_prefix
    
    # for each defined instance, remove it
    for instance in ALL_INSTANCES:
        if installation_prefix:
            # overwrite the environment setting
            instance['prefix'] = installation_prefix
        _remove_instance(instance)
        
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

