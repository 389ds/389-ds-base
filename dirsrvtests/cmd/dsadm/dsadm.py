#! /usr/bin/python2
#
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# --- END COPYRIGHT BLOCK ---

# Authors:
#   Thierry Bordaz <tbordaz@redhat.com>

import sys
import os
import argparse
import pdb
import tempfile
import time
import pwd
import grp
import platform
import socket
import shutil
from subprocess import Popen, PIPE, STDOUT
import string

SETUP_DS  = "/sbin/setup-ds.pl"
REMOVE_DS = "/sbin/remove-ds.pl"
INITCONFIGDIR = ".dirsrv"
SCRIPT_START   = "start-slapd"
SCRIPT_STOP    = "stop-slapd"
SCRIPT_RESTART = "restart-slapd"
ENVIRON_SERVERID    = '389-SERVER-ID'
ENVIRON_USER        = '389-USER'
ENVIRON_GROUP       = '389-GROUP'
ENVIRON_DIRECTORY   = '389-DIRECTORY'
ENVIRON_PORT        = '389-PORT'
ENVIRON_SECURE_PORT = '389-SECURE-PORT'
DEFAULT_PORT_ROOT = str(389)
DEFAULT_PORT_NON_ROOT = str(1389)
DEFAULT_SECURE_PORT_ROOT = str(636)
DEFAULT_SECURE_PORT_NON_ROOT = str(1636)
DEFAULT_USER = 'nobody'
DEFAULT_GROUP = 'nobody'
DEFAULT_ROOT_DN = 'cn=Directory Manager'
DEFAULT_HOSTNAME = socket.gethostname()


    
def validate_user(user):
    '''
    If a user is provided it returns its username
    else it returns the current username.
    It checks that the userId or userName exists
    
    :param: user (optional) can be a userName or userId
    :return: userName of the provided user, if none is provided, it returns current user name
    '''
    assert(user)
    if user.isdigit():
        try:
            username = pwd.getpwuid(int(user)).pw_name
        except KeyError:
            raise KeyError('Unknown userId %d' % user)
        return username
    else:
        try:
            pwd.getpwnam(user).pw_uid
        except KeyError:
            raise KeyError('Unknown userName %s' % user)
        return user
    
def get_default_user():
    user = os.environ.get(ENVIRON_USER, None)
    if not user:
        user = os.getuid()
    return str(user)

def get_default_group():
    '''
    If a group is provided it returns its groupname
    else it returns the current groupname.
    It checks that the groupId or groupName exists
    
    :param: group (optional) can be a groupName or groupId
    :return: groupName of the provided group, if none is provided, it returns current group name
    '''
    group = os.environ.get(ENVIRON_GROUP, None)
    if not group:
        return pwd.getpwuid(os.getuid()).pw_name
    return group

def validate_group(group):
    assert(group)
    if str(group).isdigit():
        try:
            groupname = grp.getgrgid(group).gr_name
            return groupname
        except:
            raise KeyError('Unknown groupId %d' % group)
    else:
        try:
            groupname = grp.getgrnam(group).gr_name
            return groupname
        except:
            raise KeyError('Unknown groupName %s' % group)

def test_get_group():
    try:
        grpname = get_default_group()
        print('get_group: %s' % grpname)
    except:
        raise
        print("Can not find user group")
        pass
    try:
        grpname = get_default_group(group='tbordaz')
        print('get_group: %s' % grpname)
    except:
        raise
        print("Can not find user group")
        pass
    try:
        grpname = get_default_group(group='coucou')
        print('get_group: %s' % grpname)
    except:
        print("Can not find user group coucou")
        pass
    try:
        grpname = get_default_group('thierry')
        print('get_group: %s' % grpname)
    except:
        raise
        print("Can not find user group thierry")
        pass
    try:
        grpname = get_default_group(1000)
        print('get_group: %s' % grpname)
    except:
        raise
        print("Can not find user group 1000")
        pass
    try:
        grpname = get_default_group(20532)
        print('get_group: %s' % grpname)
    except:
        raise
        print("Can not find user group 20532")
        pass
    try:
        grpname = get_default_group(123)
        print('get_group: %s' % grpname)
    except:
        print("Can not find user group 123")
        pass
    
def get_default_port():
    port = os.environ.get(ENVIRON_PORT, None)
    if port:
        return port

    if os.getuid() == 0:
        return DEFAULT_PORT_ROOT
    else:
        return DEFAULT_PORT_NON_ROOT

def validate_port(port):
    assert port
    if not port.isdigit() or int(port) <= 0 :
            raise Exception("port number is invalid: %s" % port)
        
def get_default_directory():
    directory = os.environ.get(ENVIRON_DIRECTORY, None)
    if not directory:
        directory = os.getcwd()
    return directory

def validate_directory(directory):
    assert directory
    if not os.path.isdir(directory):
        raise Exception("Supplied directory path is not a directory")
    
    if not os.access(directory, os.W_OK):
        raise Exception("Supplied directory is not writable")

def get_default_serverid():
    serverid = os.environ.get(ENVIRON_SERVERID, None)
    if not serverid:
        serverid = socket.gethostname().split('.')[0]
    return serverid
        
def validate_serverid(serverid):
    if not serverid:
        raise Exception("Server id is not defined")
    return serverid
        

def get_inst_dir(serverid):
    assert serverid
    home = os.getenv("HOME")
    inst_initconfig_file = "%s/%s/dirsrv-%s" % (home, INITCONFIGDIR, serverid)
    if not os.path.isfile(inst_initconfig_file):
        raise Exception("%s config file not found" % inst_initconfig_file)
    f = open(inst_initconfig_file, "r")
    for line in f:
        if line.startswith("INST_DIR"):
            inst_dir = line.split("=")[1]
            inst_dir = inst_dir.replace("\r", "")
            inst_dir = inst_dir.replace("\n", "")
            return inst_dir

def sanity_check():
    if os.getuid() == 0:
        raise Exception("Not tested for root user.. sorry")
    
    home = os.getenv("HOME")
    inst_initconfig_dir = "%s/%s" % (home, INITCONFIGDIR)
    if not os.path.isdir(inst_initconfig_dir):
        raise Exception("Please create the directory \'%s\' and retry." % inst_initconfig_dir )

class DSadmCmd(object):
    def __init__(self):
        self.version = '0.1'
    
    def _start_subparser(self, subparsers):
        start_parser = subparsers.add_parser(
                'start',
                help='Start a Directory Server Instance')
        start_parser.add_argument('-I', '--server-id', dest='server_id', type=str, nargs='?',
                metavar='SERVER-ID',
                            help='Server Identifier (Default: %s) ' % get_default_serverid())
        start_parser.set_defaults(func=self.start_action)
        
    def _stop_subparser(self, subparsers):
        start_parser = subparsers.add_parser(
                'stop',
                help='Stop a Directory Server Instance')
        start_parser.add_argument('-I', '--server-id', dest='server_id', type=str, nargs='?',
                metavar='SERVER-ID',
                            help='Server Identifier (Default: %s) ' % get_default_serverid())
        start_parser.set_defaults(func=self.stop_action)
        
    def _restart_subparser(self, subparsers):
        start_parser = subparsers.add_parser(
                'restart',
                help='Retart a Directory Server Instance')
        start_parser.add_argument('-I', '--server-id', dest='server_id', type=str, nargs='?',
                metavar='SERVER-ID',
                            help='Server Identifier (Default: %s) ' % get_default_serverid())
        start_parser.set_defaults(func=self.restart_action)
        
    def _delete_subparser(self, subparsers):
        delete_parser = subparsers.add_parser(
                'delete',
                help='Delete a Directory Server Instance')
        delete_parser.add_argument('-I', '--server-id', dest='server_id', type=str, nargs='?',
                metavar='SERVER-ID',
                            help='Server Identifier (Default: %s) ' % get_default_serverid())
        delete_parser.add_argument('-debug', '--debug', dest='debug_level', type=int, nargs='?',
                metavar='DEBUG_LEVEL',
                            help='Debug level (Default: 0)')
        delete_parser.set_defaults(func=self.delete_action)
        
    def _create_subparser(self, subparsers):
        create_parser = subparsers.add_parser(
                'create',
                help='Create a Directory Server Instance')
        create_parser.add_argument('-I', '--server-id', dest='server_id', type=str, nargs='?',
                metavar='SERVER-ID',
                            help='Server Identifier (Default: %s) ' % get_default_serverid())
        create_parser.add_argument('-s', '--suffix', dest='suffix', type=str, nargs='?',
                metavar='SUFFIX-DN',
                            help='Suffix (Default: create no suffix)')
        create_parser.add_argument('-p', '--port', dest='port', type=int, nargs='?',
                metavar='NON-SECURE-PORT',
                            help='Normal Port to listen (Default: %s(root)/%s(non-root)) ' % (DEFAULT_PORT_ROOT, DEFAULT_PORT_NON_ROOT))
        
        create_parser.add_argument('-P', '--secure-port', dest='secure_port', type=int, nargs='?',
                metavar='SECURE-PORT',
                            help='Secure Port to listen (Default: %s(root)/%s(non-root))' % (DEFAULT_SECURE_PORT_ROOT, DEFAULT_SECURE_PORT_NON_ROOT))
    
        create_parser.add_argument('-D', '--rootDN', dest='root_dn', type=str, nargs='?',
                metavar='ROOT-DN',
                            help='Uses DN as Directory Manager DN (Default: \'%s\')' % (DEFAULT_ROOT_DN))
    
        create_parser.add_argument('-u', '--user-name', dest='user_name', type=str, nargs='?',
                metavar='USER-NAME',
                            help='User name of the instance owner (Default: %s)' % DEFAULT_USER)
    
        create_parser.add_argument('-g', '--group-name', dest='group_name', type=str, nargs='?',
                metavar='GROUP-NAME',
                            help='Group name of the instance owner (Default: %s)' % DEFAULT_GROUP)
    
        create_parser.add_argument('-d', '--directory-path', dest='directory_path', type=str, nargs='?',
                metavar='DIRECTORY-PATH',
                            help='Installation directory path (Default: %s)' % get_default_directory())
        create_parser.add_argument('-debug', '--debug', dest='debug_level', type=int, nargs='?',
                metavar='DEBUG_LEVEL',
                            help='Debug level (Default: 0)')
        create_parser.add_argument('-k', '--keep_template', dest='keep_template', type=str, nargs='?',
                            help='Keep template file')
        
        create_parser.set_defaults(func=self.create_action)

    #
    # common function for start/stop/restart actions
    #
    def script_action(self, args, script, action_str):
        args = vars(args)
        serverid = args.get('server_id', None)
        if not serverid:
            serverid = get_default_serverid()
            
        script_file = "%s/%s" % (get_inst_dir(serverid), script)
        if not os.path.isfile(script_file):
            raise Exception("%s not found" % script_file)
        
        if not os.access(script_file, os.X_OK):
            raise Exception("%s not executable" % script_file)

        env = os.environ.copy()
        prog = [ script_file ]
        pipe = Popen(prog, cwd=os.getcwd(), env=env,
                         stdin=PIPE, stdout=PIPE, stderr=STDOUT)
        child_stdin = pipe.stdin
        child_stdout = pipe.stdout
        for line in child_stdout:
                sys.stdout.write(line)
        child_stdout.close()
        child_stdin.close()
        
        rc = pipe.wait()
        if rc == 0:
            print("Directory %s %s" % (serverid, action_str))
        else:
            print("Failure: directory %s not %s (%s)" % (serverid, action_str, rc))
        return
    
    def start_action(self, args):
        self.script_action(args, SCRIPT_START, "started")
        
        
    def stop_action(self, args):
        self.script_action(args, SCRIPT_STOP, "stopped")

    
    def restart_action(self, args):

        self.script_action(args, SCRIPT_RESTART, "restarted")

    def delete_action(self, args):
        args = vars(args)
        serverid = args.get('server_id', None)
        if not serverid:
            serverid = get_default_serverid()
        
        #prepare the remove-ds options
        debug_level = args.get('debug_level', None)
        if debug_level:
            debug_str = ['-d']
            for i in range(1, int(debug_level)):
                debug_str.append('d')
            debug_str = ''.join(debug_str)
            
        env = os.environ.copy()
        prog = [REMOVE_DS]
        if debug_level:
            prog.append(debug_str)
        prog.append("-i")
        prog.append("slapd-%s" % serverid)
        
        # run the REMOVE_DS command and print the possible output
        pipe = Popen(prog, cwd=os.getcwd(), env=env,
                         stdin=PIPE, stdout=PIPE, stderr=STDOUT)
        child_stdin = pipe.stdin
        child_stdout = pipe.stdout
        for line in child_stdout:
            if debug_level:
                sys.stdout.write(line)
        child_stdout.close()
        child_stdin.close()
        
        rc = pipe.wait()
        if rc == 0:
            print("Directory server \'%s\' successfully deleted" % serverid)
        else:
            print("Fail to delete directory \'%s\': %d" % (serverid, rc))
        return

    #
    # used by create subcommand to build the template file
    #
    def _create_setup_ds_file(self, args, user=None, group=None):
        # Get/checks the argument with the following order
        #   - parameter
        #   - Environment
        #   - default
        serverid = args.get('server_id', None)
        if not serverid:
            serverid = get_default_serverid()
        serverid = validate_serverid(serverid)
        
        username = args.get('user_name', None)
        if not username:
            username = get_default_user()
        username = validate_user(username)
            
        groupname = args.get('group_name', None)
        if not groupname:
            groupname = get_default_group()
        groupname = validate_group(groupname)
            
        directoryname = args.get('directory_path', None)
        if not directoryname:
            directoryname = get_default_directory()
        validate_directory(directoryname)
            
        portnumber = args.get('port', None)
        if not portnumber:
            portnumber = get_default_port()
        validate_port(portnumber)
        
        suffix = args.get('suffix', None)

        tempf = tempfile.NamedTemporaryFile(delete=False)

        tempf.write('[General]\n')
        tempf.write('FullMachineName=%s\n' % DEFAULT_HOSTNAME)
        tempf.write('SuiteSpotUserID=%s\n' % username)
        tempf.write('SuiteSpotGroup=%s\n' % groupname)
        tempf.write('ServerRoot=%s\n' % directoryname)
        tempf.write('\n')
        tempf.write('[slapd]\n')
        tempf.write('ServerPort=1389\n')
        tempf.write('ServerIdentifier=%s\n' % serverid)
        if suffix:
            tempf.write('Suffix=%s\n' % suffix)
        tempf.write('RootDN=cn=Directory Manager\n')
        tempf.write('RootDNPwd=Secret12\n')
        tempf.write('sysconfdir=%s/etc\n' % directoryname)
        tempf.write('localstatedir=%s/var\n' % directoryname)
        tempf.write('inst_dir=%s/lib/dirsrv/slapd-%s\n'% (directoryname, serverid))
        tempf.write('config_dir=%s/etc/dirsrv/slapd-%s\n' % (directoryname, serverid))
        tempf.close()
        
        keep_template = args.get('keep_template', None)
        if keep_template:
            shutil.copy(tempf.name, keep_template)
        

        return tempf

    #
    # It silently creates an instance.
    # After creation the instance is started
    # 
    def create_action(self, args):
        args = vars(args)
        
        # retrieve the serverid here just to log the final status
        serverid = args.get('server_id', None)
        if not serverid:
            serverid = get_default_serverid()

        # prepare the template file
        tempf = self._create_setup_ds_file(args)

        #prepare the setup-ds options
        debug_level = args.get('debug_level', None)
        if debug_level:
            debug_str = ['-d']
            for i in range(1, int(debug_level)):
                debug_str.append('d')
            debug_str = ''.join(debug_str)

        #
        # run the SETUP_DS command and print the possible output
        #
        env = os.environ.copy()
        prog = [SETUP_DS]
        if debug_level:
            prog.append(debug_str)
        prog.append("--silent")
        prog.append("--file=%s" % tempf.name)
        tempf.close()

        pipe = Popen(prog, cwd=os.getcwd(), env=env,
                         stdin=PIPE, stdout=PIPE, stderr=STDOUT)
        child_stdin = pipe.stdin
        child_stdout = pipe.stdout
        for line in child_stdout:
            if debug_level:
                sys.stdout.write(line)
        child_stdout.close()
        child_stdin.close()

        os.unlink(tempf.name)
        rc = pipe.wait()
        if rc == 0:
            print("Directory server \'%s\' successfully created" % serverid)
        else:
            print("Fail to create directory \'%s\': %d" % (serverid, rc))
        return

    #
    # parser of the main command. It contains subcommands
    #
    def get_parser(self, argv):

        
        parser = argparse.ArgumentParser(
        description='Managing a local directory server instance')
    
        subparsers = parser.add_subparsers(
                metavar='SUBCOMMAND',
                help='The action to perform')

        #pdb.set_trace()
        # subcommands
        self._create_subparser(subparsers)
        self._delete_subparser(subparsers)
        self._start_subparser(subparsers)
        self._stop_subparser(subparsers)
        self._restart_subparser(subparsers)

        # Sanity check that the debug level is valid
        args = vars(parser.parse_args(argv))
        debug_level = args.get('debug_level', None)
        if debug_level and (int(debug_level) < 1 or int(debug_level > 5)):
            raise Exception("invalid debug level: range 1..5")

        return parser
    
    def main(self, argv):
        sanity_check()
        parser = self.get_parser(argv)
        args = parser.parse_args(argv)
        args.func(args)
        return

if __name__ == '__main__':
    DSadmCmd().main(sys.argv[1:])
