#!/usr/bin/env python

import sys
import os, os.path
import errno
import signal
import pprint
import types
import time
import fcntl
import pwd

maxlines = 1000 # set on command line
S_IFIFO = 0010000

buffer = [] # default circular buffer used by default plugin
totallines = 0
logfname = "" # name of log pipe

# default plugin just keeps a circular buffer
def defaultplugin(line):
    global totallines
    buffer.append(line)
    totallines = totallines + 1
    if len(buffer) > maxlines:
        del buffer[0]
    return True

def printbuffer():
    sys.stdout.writelines(buffer)
    print "Read %d total lines" % totallines
    print logfname, "=" * 60

def defaultpost(): printbuffer()

plgfuncs = [] # list of plugin functions
plgpostfuncs = [] # list of post plugin funcs

def finish():
    for postfunc in plgpostfuncs: postfunc()
    sys.exit(0)

def sighandler(signum, frame):
    if signum != signal.SIGHUP: finish()
    else: printbuffer()

def isvalidpluginfile(plg):
    return os.path.isfile(plg)

def my_import(plgfile):
    '''import plgfile as a python module and return
    an error string if error - also return the prefunc if any'''
    if not isvalidpluginfile(plgfile):
        return ("%s is not a valid plugin filename" % plgfile, None, None)
    # __import__ searches for the file in sys.path - we cannot
    # __import__ a file by the full path
    # __import__('basename') looks for basename.py in sys.path
    (dir, fname) = os.path.split(plgfile)
    base = os.path.splitext(fname)[0]
    if not dir: dir = "."
    sys.path.insert(0, dir) # put our path first so it will find our file
    mod = __import__(base) # will throw exception if problem with python file
    sys.path.pop(0) # remove our path

    # check for the plugin functions
    plgfunc = getattr(mod, 'plugin', None)
    if not plgfunc:
        return ('%s does not specify a plugin function' % plgfile, None, base)
    if not isinstance(plgfunc, types.FunctionType):
        return ('the symbol "plugin" in %s is not a function' % plgfile, None, base)
    plgfuncs.append(plgfunc) # add to list in cmd line order

    # check for 'post' func
    plgpostfunc = getattr(mod, 'post', None)
    if plgpostfunc:
        if not isinstance(plgpostfunc, types.FunctionType):
            return ('the symbol "post" in %s is not a function' % plgfile, None, base)
        else:
            plgpostfuncs.append(plgpostfunc) # add to list in cmd line order

    prefunc = getattr(mod, 'pre', None)
    # check for 'pre' func
    if prefunc and not isinstance(prefunc, types.FunctionType):
        return ('the symbol "pre" in %s is not a function' % plgfile, None, base)

    return ('', prefunc, base)

def parse_plugins(parser, options, args):
    '''Each plugin in the plugins list may have additional
    arguments, specified on the command line like this:
    --plugin=foo.py foo.bar=1 foo.baz=2 ...
    that is, each argument to plugin X will be specified as X.arg=value'''
    if not options.plugins: return args

    for plgfile in options.plugins:
        (errstr, prefunc, base) = my_import(plgfile)
        if errstr:
            parser.error(errstr)
            return args

        # parse the arguments to the plugin given on the command line
        bvals = {} # holds plugin args and values, if any
        newargs = []
        for arg in args:
            if arg.startswith(base + '.'):
                argval = arg.replace(base + '.', '')
                (plgarg, plgval) = argval.split('=', 1) # split at first =
                if not plgarg in bvals:
                    bvals[plgarg] = plgval
                elif isinstance(bvals[plgarg],list):
                    bvals[plgarg].append(plgval)
                else: # convert to list
                    bvals[plgarg] = [bvals[plgarg], plgval]
            else:
                newargs.append(arg)
        if prefunc:
            print 'Calling "pre" function in', plgfile
            if not prefunc(bvals):
                parser.error('the "pre" function in %s returned an error' % plgfile)
        args = newargs

    return args

def open_pipe(logfname):
    opencompleted = False
    logf = None
    while not opencompleted:
        try:
            logf = open(logfname, 'r') # blocks until there is some input
            # set pipe to non-blocking
#             oldflags = fcntl.fcntl(logf, fcntl.F_GETFL)
#             fcntl.fcntl(logf, fcntl.F_SETFL, oldflags | os.O_NONBLOCK)
            opencompleted = True
        except IOError, e:
            if e.errno == errno.EINTR:
                continue # open was interrupted, try again
            else: # hard error
                raise Exception, "%s [%d]" % (e.strerror, e.errno)
    return logf

def get_server_pid(serverpidfile, servertimeout):
    endtime = int(time.time()) + servertimeout
    serverpid = 0
    if serverpidfile:
        line = None
        try:
            spfd = open(serverpidfile, 'r')
            line = spfd.readline()
            spfd.close()
        except IOError, e:
            if e.errno != errno.ENOENT: # may not exist yet - that's ok
                raise Exception, "%s [%d]" % (e.strerror, e.errno)
        if line:
            serverpid = int(line)
    return serverpid

def is_server_alive(serverpid):
    retval = False
    try:
        os.kill(serverpid, 0) # sig 0 is a "ping"
        retval = True
    except OSError, e:
        pass # no such process, or EPERM/EACCES
    return retval

def read_and_process_line(logf, plgfuncs):
    line = None
    done = False
    readcompleted = False
    while not readcompleted:
        try:
            line = logf.readline()
            readcompleted = True # read completed
        except IOError, e:
            if e.errno == errno.EINTR:
                continue # read was interrupted, try again
            else: # hard error
                raise Exception, "%s [%d]" % (e.strerror, e.errno)
    if line: # read something
        for plgfunc in plgfuncs:
            if not plgfunc(line):
                print "Aborting processing due to function %s" % str(plgfunc)
                finish()
                done = True
                break
    else: # EOF
        done = True
    return done

def parse_options():
    from optparse import OptionParser
    usage = "%prog <name of pipe> [options]"
    parser = OptionParser(usage)
    parser.add_option("-m", "--maxlines", dest="maxlines", type='int',
                      help="maximum number of lines to keep in the buffer", default=1000)
    parser.add_option("-d", "--debug", dest="debug", action="store_true",
                      default=False, help="gather extra debugging information")
    parser.add_option("-p", "--plugin", type='string', dest='plugins', action='append',
                      help='filename of a plugin to use with this log')
    parser.add_option("-s", "--serverpidfile", type='string', dest='serverpidfile',
                      help='name of file containing the pid of the server to monitor')
    parser.add_option("-t", "--servertimeout", dest="servertimeout", type='int',
                      help="timeout in seconds to wait for the serverpid to be alive", default=60)
    parser.add_option("--serverpid", dest="serverpid", type='int',
                      help="process id of server to monitor", default=0)
    parser.add_option("-u", "--user", type='string', dest='user',
                      help='name of user to set effective uid to')

    options, args = parser.parse_args()

    args = parse_plugins(parser, options, args)

    if len(args) < 1:
        parser.error("You must specify the name of the pipe to use")
    if len(args) > 1:
        parser.error("error - unhandled command line arguments: %s" % args.join(' '))

    return options, args[0]

options, logfname = parse_options()

if len(plgfuncs) == 0:
    plgfuncs.append(defaultplugin)
if len(plgpostfuncs) == 0:
    plgpostfuncs.append(defaultpost)

if options.user:
    try: userid = int(options.user)
    except ValueError: # not a numeric userid - look it up
        userid = pwd.getpwnam(options.user)[2]
    os.seteuid(userid)

serverpid = options.serverpid
if serverpid:
    if not is_server_alive(serverpid):
        print "Server pid [%d] is not alive - exiting" % serverpid
        sys.exit(1)

try:
    if os.stat(logfname).st_mode & S_IFIFO:
        print "Using existing log pipe", logfname
    else:
        print "Error:", logfname, "exists and is not a log pipe"
        print "use a filename other than", logfname
        sys.exit(1)
except OSError, e:
    if e.errno == errno.ENOENT:
        print "Creating log pipe", logfname
        os.mkfifo(logfname)
        os.chmod(logfname, 0600)
    else:
        raise Exception, "%s [%d]" % (e.strerror, e.errno)

print "Listening to log pipe", logfname, "number of lines", maxlines

# set up our signal handlers
signal.signal(signal.SIGHUP, sighandler)
signal.signal(signal.SIGINT, sighandler)
#signal.signal(signal.SIGPIPE, sighandler)
signal.signal(signal.SIGTERM, sighandler)
signal.signal(signal.SIGALRM, sighandler)

if options.serverpidfile:
    # start the timer to wait for the pid file to be available
    signal.alarm(options.servertimeout)

done = False
while not done:
    # open the pipe - will hang until
    # 1. something opens the other end
    # 2. alarm goes off
    logf = open_pipe(logfname)

    if serverpid:
        if not is_server_alive(serverpid):
            print "Server pid [%d] is not alive - exiting" % serverpid
            sys.exit(1)
        else: # cancel the timer
            signal.alarm(0) # cancel timer - got pid

    innerdone = False
    while not innerdone and not done:
        # read and process the next line in the pipe
        # if server exits while we are reading, we will get
        # EOF and innerdone will be True
        # we may have to read some number of lines until
        # we can get the pid file
        innerdone = read_and_process_line(logf, plgfuncs)
        if not serverpid and options.serverpidfile:
            serverpid = get_server_pid(options.serverpidfile, options.servertimeout)
            if serverpid:
                signal.alarm(0) # cancel timer - got pid

    if logf:
        logf.close()
        logf = None

    if not done:
        if serverpid:
            if not is_server_alive(serverpid):
                print "Server pid [%d] is not alive - exiting" % serverpid
                sys.exit(1)
            else: # the server will sometimes close the log and reopen it
                # however, at shutdown, the server will close the log before
                # the process has exited - so is_server_alive will return
                # true for a short time - if we then attempt to open the
                # pipe, the open will hang forever - to avoid this situation
                # we set the alarm again to wake up the open
                signal.alarm(options.servertimeout)
                
        print "log pipe", logfname, "closed - reopening"

finish()
