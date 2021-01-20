# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""Utilities for DirSrv.

    TODO put them in a module!
"""
try:
    from subprocess import Popen as my_popen, PIPE
except ImportError:
    from popen2 import popen2

    def my_popen(cmd_l, stdout=None):
        class MockPopenResult(object):
            def wait(self):
                pass
        p = MockPopenResult()
        p.stdout, p.stdin = popen2(cmd_l)
        return p

import re
import os
import logging
import shutil
import ldap
import socket
import time
import stat
from datetime import datetime
import sys
import filecmp
import pwd
import shlex
import operator
import subprocess
import math
# Setuptools ships with 'packaging' module, let's use it from there
try:
    from pkg_resources.extern.packaging.version import LegacyVersion
# Fallback to a normal 'packaging' module in case 'setuptools' is stripped
except:
    from packaging.version import LegacyVersion
from socket import getfqdn
from ldapurl import LDAPUrl
from contextlib import closing

import lib389
from lib389.paths import Paths
from lib389.dseldif import DSEldif
from lib389._constants import (
        DEFAULT_USER, VALGRIND_WRAPPER, DN_CONFIG, CFGSUFFIX, LOCALHOST,
        ReplicaRole, CONSUMER_REPLICAID, SENSITIVE_ATTRS
    )
from lib389.properties import (
        SER_HOST, SER_USER_ID, SER_GROUP_ID, SER_STRICT_HOSTNAME_CHECKING, SER_PORT,
        SER_ROOT_DN, SER_ROOT_PW, SER_SERVERID_PROP, SER_CREATION_SUFFIX,
        SER_INST_SCRIPTS_ENABLED, SER_SECURE_PORT, REPLICA_ID
    )

MAJOR, MINOR, _, _, _ = sys.version_info

DEBUGGING = os.getenv('DEBUGGING', default=False)

log = logging.getLogger(__name__)

#
# Various searches to be used in getEntry
#   eg getEntry(*searches['NAMINGCONTEXTS'])
#
searches = {
    'NAMINGCONTEXTS': ('', ldap.SCOPE_BASE, '(objectclass=*)',
                       ['namingcontexts']),
    'ZOMBIE': ('', ldap.SCOPE_SUBTREE,
               '(&(objectclass=glue)(objectclass=extensibleobject))', ['dn'])
}

# Map table for pseudolocalized strings
_chars = {
    " ": u"\u2003",
    "!": u"\u00a1",
    "\"": u"\u2033",
    "#": u"\u266f",
    "$": u"\u20ac",
    "%": u"\u2030",
    "&": u"\u214b",
    "'": u"\u00b4",
    ")": u"}",
    "(": u"{",
    "*": u"\u204e",
    "+": u"\u207a",
    ",": u"\u060c",
    "-": u"\u2010",
    ".": u"\u00b7",
    "/": u"\u2044",
    "0": u"\u24ea",
    "1": u"\u2460",
    "2": u"\u2461",
    "3": u"\u2462",
    "4": u"\u2463",
    "5": u"\u2464",
    "6": u"\u2465",
    "7": u"\u2466",
    "8": u"\u2467",
    "9": u"\u2468",
    ":": u"\u2236",
    ";": u"\u204f",
    "<": u"\u2264",
    "=": u"\u2242",
    ">": u"\u2265",
    "?": u"\u00bf",
    "@": u"\u055e",
    "A": u"\u00c5",
    "B": u"\u0181",
    "C": u"\u00c7",
    "D": u"\u00d0",
    "E": u"\u00c9",
    "F": u"\u0191",
    "G": u"\u011c",
    "H": u"\u0124",
    "I": u"\u00ce",
    "J": u"\u0134",
    "K": u"\u0136",
    "L": u"\u013b",
    "M": u"\u1e40",
    "N": u"\u00d1",
    "O": u"\u00d6",
    "P": u"\u00de",
    "Q": u"\u01ea",
    "R": u"\u0154",
    "S": u"\u0160",
    "T": u"\u0162",
    "U": u"\u00db",
    "V": u"\u1e7c",
    "W": u"\u0174",
    "X": u"\u1e8a",
    "Y": u"\u00dd",
    "Z": u"\u017d",
    "[": u"\u2045",
    "\\": u"\u2216",
    "]": u"\u2046",
    "^": u"\u02c4",
    "_": u"\u203f",
    "`": u"\u2035",
    "a": u"\u00e5",
    "b": u"\u0180",
    "c": u"\u00e7",
    "d": u"\u00f0",
    "e": u"\u00e9",
    "f": u"\u0192",
    "g": u"\u011d",
    "h": u"\u0125",
    "i": u"\u00ee",
    "j": u"\u0135",
    "k": u"\u0137",
    "l": u"\u013c",
    "m": u"\u0271",
    "n": u"\u00f1",
    "o": u"\u00f6",
    "p": u"\u00fe",
    "q": u"\u01eb",
    "r": u"\u0155",
    "s": u"\u0161",
    "t": u"\u0163",
    "u": u"\u00fb",
    "v": u"\u1e7d",
    "w": u"\u0175",
    "x": u"\u1e8b",
    "y": u"\u00fd",
    "z": u"\u017e",
    "{": u"(",
    "}": u")",
    "|": u"\u00a6",
    "~": u"\u02de",
}

#
# Utilities
#

def selinux_present():
    """
    Determine if selinux libraries are on a system, and if so, if we are in
    a state to consume them (enabled, disabled).

    :returns: bool
    """
    status = False

    try:
        import selinux
        if selinux.is_selinux_enabled():
            # We have selinux, continue.
            status = True
        else:
            # We have the module, but it's disabled.
            log.error('selinux is disabled, will not relabel ports or files.' )
    except ImportError:
        # No python module, so who knows what state we are in.
        log.error('selinux python module not found, will not relabel files.' )

    return status


def selinux_restorecon(path):
    """
    Relabel a filesystem rooted at path.

    :param path: The filesystem path to recursively relabel
    :type path: str:
    """

    try:
        import selinux
    except ImportError:
        log.debug('selinux python module not found, skipping relabel path %s' % path)
        return

    if not selinux.is_selinux_enabled():
        log.debug('selinux is disabled, skipping relabel path %s' % path)
        return

    try:
        selinux.restorecon(path, recursive=True)
    except:
        log.debug("Failed to run restorecon on: " + path)


def _get_selinux_port_policies(port):
    """Get a list of selinux port policies for the specified port, 'tcp' protocol and
    excluding 'unreserved_port_t', 'reserved_port_t', 'ephemeral_port_t' labels"""

    # [2:] - cut first lines containing the headers. [:-1] - empty line
    policy_lines = subprocess.check_output(["semanage", "port", "-l"], encoding='utf-8').split("\n")[2:][:-1]
    policies = []
    for line in policy_lines:
        data = line.split()
        ports_list = []
        for p in data[2:]:
            if "," in p:
                p = p[:-1]
            if "-" in p:
                p = range(int(p.split("-")[0]), int(p.split("-")[1]))
            else:
                p = [int(p)]
            ports_list.extend(p)
        if data[1] == 'tcp' and port in ports_list and \
           data[0] not in ['unreserved_port_t', 'reserved_port_t', 'ephemeral_port_t']:
            policies.append({'protocol': data[1], 'type': data[0], 'ports': ports_list})
    return policies


def selinux_label_port(port, remove_label=False):
    """
    Either set or remove an SELinux label(ldap_port_t) for a TCP port

    :param port: The TCP port to be labeled
    :type port: str
    :param remove_label: Set True if the port label should be removed
    :type remove_label: boolean
    :raises: ValueError: Error message
    """
    try:
        import selinux
    except ImportError:
        log.debug('selinux python module not found, skipping port labeling.')
        return

    if not selinux.is_selinux_enabled():
        log.debug('selinux is disabled, skipping port relabel')
        return

    # We only label ports that ARE NOT in the default policy that comes with
    # a RH based system.
    port = int(port)
    selinux_default_ports = [389, 636, 3268, 3269, 7389]
    if port in selinux_default_ports:
        log.debug('port {} already in {}, skipping port relabel'.format(port, selinux_default_ports))
        return
    label_set = False
    label_ex = None

    policies = _get_selinux_port_policies(port)

    for policy in policies:
        if "ldap_port_t" == policy['type']:
            label_set = True  # Port already has our label
            if port in policy['ports']:
                # The port is within the range, just return
                return
            break
        elif not remove_label:
            # Port belongs to someone else (bad)
            # This is only an issue during setting a label, not removing a label
            raise ValueError("Port {} was already labeled with: ({})  Please choose a different port number".format(port, policy['type']))

    if (remove_label and label_set) or (not remove_label and not label_set):
        for i in range(5):
            try:
                result = subprocess.run(["semanage", "port",
                                         "-d" if remove_label else "-a",
                                         "-t", "ldap_port_t",
                                         "-p", "tcp", str(port)],
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE)
                args = ' '.join(ensure_list_str(result.args))
                stdout = ensure_str(result.stdout)
                stderr = ensure_str(result.stderr)
                log.debug(f"CMD: {args} ; STDOUT: {stdout} ; STDERR: {stderr}")
                return
            except (OSError, subprocess.CalledProcessError) as e:
                label_ex = e
                time.sleep(3)
        raise ValueError("Failed to mangle port label: " + str(label_ex))


def is_a_dn(dn, allow_anon=True):
    """Returns True if the given string is a DN, False otherwise."""
    try:
        if dn == "" and allow_anon is True:
            # The empty string is anonymous.
            return True
        if len(ldap.dn.str2dn(dn)) > 0:
            # We have valid components in the dn.
            return True
    except ldap.DECODING_ERROR:
        # An invalid dn was given.
        pass
    except TypeError:
        # An invalid type was passed to be checked
        pass
    return False


def is_dn_parent(parent_dn, child_dn, ):
    """ check if child_dn is a really a child of parent_dn
    :return True - if child directly under parent, otherwise False
    """
    parent_dn = parent_dn.lower()
    child_dn = child_dn.lower()

    # Do some basic validation first
    if not ldap.dn.is_dn(parent_dn) or not ldap.dn.is_dn(child_dn):
        return False
    if parent_dn == child_dn:
        return False

    # Get the DN comps and length
    parent_dn_comps = ldap.dn.str2dn(parent_dn)
    child_dn_comps = ldap.dn.str2dn(child_dn)
    parent_comp_len = len(parent_dn_comps)
    child_comp_len = len(child_dn_comps)

    # Do a little more validation
    if child_comp_len <= parent_comp_len or (child_comp_len - parent_comp_len) != 1:
        return False

    # Okay, finally comparing the base DN Comps to see if they match
    for i, e in reversed(list(enumerate(parent_dn_comps))):
        child_idx = i + (child_comp_len - parent_comp_len)
        if child_dn_comps[child_idx] != e:
            return False

    # If we got here then child is a direct decendent of parent
    return True


def normalizeDN(dn, usespace=False):
    # not great, but will do until we use a newer version of python-ldap
    # that has DN utilities
    ary = ldap.explode_dn(dn.lower())
    joinstr = ","
    if usespace:
        joinstr = ", "
    return joinstr.join(ary)


def escapeDNValue(dn):
    '''convert special characters in a DN into LDAPv3 escapes.

     e.g.
    "dc=example,dc=com" -> \"dc\=example\,\ dc\=com\"'''
    for cc in (' ', '"', '+', ',', ';', '<', '>', '='):
        dn = dn.replace(cc, '\\' + cc)
    return dn


def escapeDNFiltValue(dn):
    '''convert special characters in a DN into LDAPv3 escapes
    for use in search filters'''
    for cc in (' ', '"', '+', ',', ';', '<', '>', '='):
        dn = dn.replace(cc, '\\%x' % ord(cc))
    return dn


def suffixfilt(suffix):
    """Return a filter matching any possible suffix form.

        eg. normalized, escaped, spaced...
    """
    nsuffix = normalizeDN(suffix)
    spacesuffix = normalizeDN(nsuffix, True)
    escapesuffix = escapeDNFiltValue(nsuffix)
    filt = ('(|(cn=%s)(cn=%s)(cn=%s)(cn="%s")(cn="%s")(cn=%s)(cn="%s"))' %
            (escapesuffix, nsuffix, spacesuffix, nsuffix, spacesuffix, suffix,
             suffix))
    return filt


#
# path tools
#
def get_sbin_dir(sroot=None, prefix=None):
    """Return the sbin directory (default /usr/sbin)."""
    if sroot:
        return "%s/bin/slapd/admin/bin" % sroot
    elif prefix and prefix != '/':
        return "%s/sbin" % prefix
    return "/usr/sbin"


def get_bin_dir(sroot=None, prefix=None):
    """Return the sbin directory (default /usr/bin)."""
    if sroot:
        return "%s/bin/slapd/admin/bin" % sroot
    elif prefix and prefix != '/':
        return "%s/bin" % prefix
    return "/usr/bin"


def get_data_dir(prefix=None):
    """Return the shared data directory (default /usr/share/dirsrv/data)."""
    if prefix and prefix != '/':
        return "%s/share/dirsrv/data" % prefix
    return "/usr/share/dirsrv/data"


def get_plugin_dir(prefix=None):
    """
    Return the plugin directory (default /usr/lib64/dirsrv/plugins).
    This should be 64/32bit aware.
    """
    if prefix and prefix != '/':
        # With prefix installations, even 64 bit, it can be under /usr/lib/
        if os.path.exists("%s/usr/lib/dirsrv/plugins" % prefix):
            return "%s/usr/lib/dirsrv/plugins" % prefix
        else:
            if os.path.exists("%s/usr/lib64/dirsrv/plugins" % prefix):
                return "%s/usr/lib64/dirsrv/plugins" % prefix

    # Need to check for 32/64 bit installations
    if not os.path.exists("/usr/lib64/dirsrv/plugins"):
        if os.path.exists("/usr/lib/dirsrv/plugins"):
            return "/usr/lib/dirsrv/plugins"
    return "/usr/lib64/dirsrv/plugins"


#
# valgrind functions
#
def valgrind_enable(sbin_dir, wrapper=None):
    '''
    Copy the valgrind ns-slapd wrapper into the /sbin directory
    (making a backup of the original ns-slapd binary).

    The script calling valgrind_enable() must be run as the 'root' user
    as selinux needs to be disabled for valgrind to work

    The server instance(s) should be stopped prior to calling this function.
    Then after calling valgrind_enable():
    - Start the server instance(s) with a timeout of 60 (valgrind takes a
      while to startup)
    - Run the tests
    - Stop the server
    - Get the results file
    - Run valgrind_check_file(result_file, "pattern", "pattern", ...)
    - Run valgrind_disable()

    :param sbin_dir: the location of the ns-slapd binary (e.g. /usr/sbin)
    :param wrapper: The valgrind wrapper script for ns-slapd (if not set,
                     a default wrapper is used)
    :raise IOError: If there is a problem setting up the valgrind scripts
    :raise EnvironmentError: If script is not run as 'root'
    '''

    if os.geteuid() != 0:
        log.error('This script must be run as root to use valgrind')
        raise EnvironmentError

    if not wrapper:
        # use the default ns-slapd wrapper
        wrapper = '%s/%s' % (os.path.dirname(os.path.abspath(__file__)),
                             VALGRIND_WRAPPER)

    nsslapd_orig = '%s/ns-slapd' % sbin_dir
    nsslapd_backup = '%s/ns-slapd.original' % sbin_dir

    if os.path.isfile(nsslapd_backup):
        # There is a backup which means we never cleaned up from a previous
        # run(failed test?)
        if not filecmp.cmp(nsslapd_backup, nsslapd_orig):
            # Files are different sizes, we assume valgrind is already setup
            log.info('Valgrind is already enabled.')
            return

    # Check both nsslapd's exist
    if not os.path.isfile(wrapper):
        raise IOError('The valgrind wrapper (%s) does not exist. file=%s' %
                      (wrapper, __file__))

    if not os.path.isfile(nsslapd_orig):
        raise IOError('The binary (%s) does not exist or is not accessible.' %
                      nsslapd_orig)

    # Make a backup of the original ns-slapd and copy the wrapper into place
    try:
        shutil.copy2(nsslapd_orig, nsslapd_backup)
    except IOError as e:
        log.fatal('valgrind_enable(): failed to backup ns-slapd, error: %s' %
                  e.strerror)
        raise IOError('failed to backup ns-slapd, error: %s' % e.strerror)

    # Copy the valgrind wrapper into place
    try:
        shutil.copy2(wrapper, nsslapd_orig)
    except IOError as e:
        log.fatal('valgrind_enable(): failed to copy valgrind wrapper '
                  'to ns-slapd, error: %s' % e.strerror)
        raise IOError('failed to copy valgrind wrapper to ns-slapd, error: %s' %
                      e.strerror)

    # Disable selinux
    os.system('setenforce 0')

    log.info('Valgrind is now enabled.')


def valgrind_disable(sbin_dir):
    '''
    Restore the ns-slapd binary to its original state - the server instances
    are expected to be stopped.

    Note - selinux is enabled at the end of this process.

    :param sbin_dir - the location of the ns-slapd binary (e.g. /usr/sbin)
    :raise ValueError
    :raise EnvironmentError: If script is not run as 'root'
    '''

    if os.geteuid() != 0:
        log.error('This script must be run as root to use valgrind')
        raise EnvironmentError

    nsslapd_orig = '%s/ns-slapd' % sbin_dir
    nsslapd_backup = '%s/ns-slapd.original' % sbin_dir

    # Restore the original ns-slapd
    try:
        shutil.copyfile(nsslapd_backup, nsslapd_orig)
    except IOError as e:
        log.fatal('valgrind_disable: failed to restore ns-slapd, error: %s' %
                  e.strerror)
        raise ValueError('failed to restore ns-slapd, error: %s' % e.strerror)

    # Delete the backup now
    try:
        os.remove(nsslapd_backup)
    except OSError as e:
        log.fatal('valgrind_disable: failed to delete backup ns-slapd, error:'
                  ' %s' % e.strerror)
        raise ValueError('Failed to delete backup ns-slapd, error: %s' %
                         e.strerror)

    # Enable selinux
    os.system('setenforce 1')

    log.info('Valgrind is now disabled.')


def valgrind_get_results_file(dirsrv_inst):
    """
    Return the valgrind results file for the dirsrv instance.
    """

    """
    The "ps -ef | grep valgrind" looks like:

        nobody 26239 1 10 14:33 ? 00:00:06 valgrind -q --tool=memcheck
        --leak-check=yes --leak-resolution=high --num-callers=50
        --log-file=/var/tmp/slapd.vg.26179 /usr/sbin/ns-slapd.original
        -D /etc/dirsrv/slapd-localhost -i /var/run/dirsrv/slapd-localhost.pid
        -w /var/run/dirsrv/slapd-localhost.startpid

    We need to extract the "--log-file" value
    """
    cmd = ("ps -ef | grep valgrind | grep 'slapd-" + dirsrv_inst.serverid +
           " ' | awk '{ print $14 }' | sed -e 's/\-\-log\-file=//'")

    # Run the command and grab the output
    p = os.popen(cmd)
    results_file = p.readline()
    p.close()

    return results_file


def valgrind_check_file(results_file, *patterns):
    '''
    Check the valgrind results file for the all the patterns
    @param result_file - valgrind results file (must be read after server is
                         stopped)
    @param patterns - A plain text or regex pattern string args that should
                      be searched for
    @return True/False - Return true if one if the patterns match a stack
                         trace
    @raise IOError
    '''

    # Verify results file
    if not results_file:
        assert False

    # Check the result file fo the leak text
    results_file = results_file.replace('\n', '')
    found = False
    pattern_count = len(patterns)
    matched_count = 0

    vlog = open(results_file)
    for line in vlog:
        for match_txt in patterns:
            if re.search(match_txt, line):
                matched_count += 1
                break

        if len(line.split()) == 1:
            # Check if this stack stack matched all the patterns
            if pattern_count == matched_count:
                found = True
                print('valgrind: match found in results file: %s' %
                      (results_file))
                break
            else:
                matched_count = 0
    vlog.close()

    return found


#
# functions using sockets
#
def isLocalHost(host_name):
    """True if host_name points to a local ip.

        Uses gethostbyname()
    """
    # first see if this is a "well known" local hostname
    if host_name == 'localhost' or \
       host_name == 'localhost.localdomain' or \
       host_name == socket.gethostname():
        return True

    # first lookup ip addr
    try:
        ip_addr = socket.gethostbyname(host_name)
        if ip_addr.startswith("127."):
            # log.debug("this ip is on loopback, retain only the first octet")
            ip_addr = '127.'
    except socket.gaierror:
        log.debug("no ip address for %r" % host_name)
        return False

    # next, see if this IP addr is one of our
    # local addresses
    p = my_popen(['/sbin/ip', 'addr'], stdout=PIPE)
    child_stdout = p.stdout.read()
    found = ('inet %s' % ip_addr).encode() in child_stdout
    p.wait()

    return found


def getdomainname(name=''):
    fqdn = getfqdn(name)
    index = fqdn.find('.')
    if index >= 0:
        return fqdn[index + 1:]
    else:
        # This isn't correct. There is no domain name, so return empty.str
        # return fqdn
        return ""


def getdefaultsuffix(name=''):
    dm = getdomainname(name)
    if dm:
        return "dc=" + dm.replace('.', ',dc=')
    else:
        return 'dc=localdomain'


def get_server_user(args):
    """Return the unix username used from the server inspecting the following
       keys in args.

        'newuserid', 'admconf', 'sroot' -> ssusers.conf

    """
    if 'newuserid' not in args:
        if 'admconf' in args:
            args['newuserid'] = args['admconf'].SuiteSpotUserID
        elif 'sroot' in args:
            ssusers = open("%s/shared/config/ssusers.conf" % args['sroot'])
            for line in ssusers:
                ary = line.split()
                if len(ary) > 1 and ary[0] == 'SuiteSpotUser':
                    args['newuserid'] = ary[-1]
            ssusers.close()
    if 'newuserid' not in args:
        args['newuserid'] = os.environ['LOGNAME']
        if args['newuserid'] == 'root':
            args['newuserid'] = DEFAULT_USER


def update_newhost_with_fqdn(args):
    """Replace args[SER_HOST] with its fqdn and returns True if local.

    One of the arguments to createInstance is newhost.  If this is specified,
    we need to convert it to the fqdn.  If not given, we need to figure out
    what the fqdn of the local host is.  This method sets newhost in args to
    the appropriate value and returns True if newhost is the localhost, False
    otherwise"""
    if SER_HOST in args:
        args[SER_HOST] = getfqdn(args[SER_HOST])
        isLocal = isLocalHost(args[SER_HOST])
    else:
        isLocal = True
        args[SER_HOST] = getfqdn()
    return isLocal


def getcfgdsuserdn(cfgdn, args):
    """Return a DirSrv object bound anonymously or to the admin user.

    If the config ds user ID was given, not the full DN, we need to figure
    out the full DN.

    Try in order to:
        1- search the directory anonymously;
        2- look in ldap.conf;
        3- try the default DN.

    This may raise a file or LDAP exception.
    """
    # create a connection to the cfg ds
    conn = lib389.DirSrv(args['cfgdshost'], args['cfgdsport'], "", "", None)
    # if the caller gave a password, but not the cfguser DN, look it up
    if 'cfgdspwd' in args and \
            ('cfgdsuser' not in args or not is_a_dn(args['cfgdsuser'])):
        if 'cfgdsuser' in args:
            ent = conn.getEntry(cfgdn, ldap.SCOPE_SUBTREE,
                                "(uid=%s)" % args['cfgdsuser'],
                                ['dn'])
            args['cfgdsuser'] = ent.dn
        elif 'sroot' in args:
            ldapconf = open(
                "%s/shared/config/ldap.conf" % args['sroot'], 'r')
            for line in ldapconf:
                ary = line.split()  # default split is all whitespace
                if len(ary) > 1 and ary[0] == 'admnm':
                    args['cfgdsuser'] = ary[-1]
            ldapconf.close()
        elif 'admconf' in args:
            args['cfgdsuser'] = args['admconf'].userdn
        elif 'cfgdsuser' in args:
            dsuser = ("uid=%s,ou=Administrators,ou=TopologyManagement,%s" %
                      (args['cfgdsuser'], cfgdn))
            args['cfgdsuser'] = dsuser
        conn.unbind()
        conn = lib389.DirSrv(
            args['cfgdshost'], args['cfgdsport'], args['cfgdsuser'],
            args['cfgdspwd'], None)
    return conn


def update_admin_domain(isLocal, args):
    """Get the admin domain to use."""
    if isLocal and 'admin_domain' not in args:
        if 'admconf' in args:
            args['admin_domain'] = args['admconf'].admindomain
        elif 'sroot' in args:
            dsconf = open('%s/shared/config/ds.conf' % args['sroot'], 'r')
            for line in dsconf:
                ary = line.split(":")
                if len(ary) > 1 and ary[0] == 'AdminDomain':
                    args['admin_domain'] = ary[1].strip()
            dsconf.close()


def getoldcfgdsinfo(args):
    """Use the old style sroot/shared/config/dbswitch.conf to get the info"""
    dbswitch = open("%s/shared/config/dbswitch.conf" % args['sroot'], 'r')
    try:
        matcher = re.compile(r'^directory\s+default\s+')
        for line in dbswitch:
            m = matcher.match(line)
            if m:
                url = LDAPUrl(line[m.end():])
                ary = url.hostport.split(":")
                if len(ary) < 2:
                    ary.append(389)
                else:
                    ary[1] = int(ary[1])
                ary.append(url.dn)
                return ary
    finally:
        dbswitch.close()


def getnewcfgdsinfo(new_instance_arguments):
    """Use the new style prefix /etc/dirsrv/admin-serv/adm.conf.

        new_instance_arguments = {'admconf': obj } where obj.ldapurl != None
    """
    try:
        url = LDAPUrl(new_instance_arguments['admconf'].ldapurl)
    except AttributeError:
        log.error("missing ldapurl attribute in new_instance_arguments: %r" %
                  new_instance_arguments)
        raise

    ary = url.hostport.split(":")
    if len(ary) < 2:
        ary.append(389)
    else:
        ary[1] = int(ary[1])
    ary.append(url.dn)
    return ary


def getcfgdsinfo(new_instance_arguments):
    """Returns a 3-tuple consisting of the host, port, and cfg suffix.

        `new_instance_arguments` = {
            'cfgdshost':
            'cfgdsport':
            'new_style':
        }
    We need the host and port of the configuration directory server in order
    to create an instance.  If this was not given, read the dbswitch.conf file
    to get the information.  This method will raise an exception if the file
    was not found or could not be open.  This assumes new_instance_arguments
    contains the sroot parameter for the server root path.
    """
    try:
        return (new_instance_arguments['cfgdshost'],
                int(new_instance_arguments['cfgdsport']),
                CFGSUFFIX)
    except KeyError:  # if keys are missing...
        if new_instance_arguments['new_style']:
            return getnewcfgdsinfo(new_instance_arguments)

        return getoldcfgdsinfo(new_instance_arguments)


def getserverroot(cfgconn, isLocal, args):
    """Grab the serverroot from the instance dir of the config ds if the user
    did not specify a server root directory"""
    if cfgconn and 'sroot' not in args and isLocal:
        ent = cfgconn.getEntry(
            DN_CONFIG, ldap.SCOPE_BASE, "(objectclass=*)",
            ['nsslapd-instancedir'])
        if ent:
            args['sroot'] = os.path.dirname(
                ent.getValue('nsslapd-instancedir'))


def getadminport(cfgconn, cfgdn, args):
    """Return a 2-tuple (asport, True) if the admin server is using SSL,
    False otherwise.

    Get the admin server port so we can contact it via http.  We get this from
    the configuration entry using the CFGSUFFIX and cfgconn.  Also get any
    other information we may need from that entry.
    """
    asport = 0
    secure = False
    if cfgconn:
        dn = cfgdn
        if 'admin_domain' in args:
            dn = "cn=%s,ou=%s, %s" % (
                args[SER_HOST], args['admin_domain'], cfgdn)
        filt = "(&(objectclass=nsAdminServer)(serverHostName=%s)" % args[
            SER_HOST]
        if 'sroot' in args:
            filt += "(serverRoot=%s)" % args['sroot']
        filt += ")"
        ent = cfgconn.getEntry(
            dn, ldap.SCOPE_SUBTREE, filt, ['serverRoot'])
        if ent:
            if 'sroot' not in args and ent.serverRoot:
                args['sroot'] = ent.serverRoot
            if 'admin_domain' not in args:
                ary = ldap.explode_dn(ent.dn, 1)
                args['admin_domain'] = ary[-2]
            dn = "cn=configuration, " + ent.dn
            ent = cfgconn.getEntry(dn, ldap.SCOPE_BASE, '(objectclass=*)',
                                   ['nsServerPort',
                                    'nsSuiteSpotUser',
                                    'nsServerSecurity'])
            if ent:
                asport = ent.nsServerPort
                secure = (ent.nsServerSecurity and (
                    ent.nsServerSecurity == 'on'))
                if 'newuserid' not in args:
                    args['newuserid'] = ent.nsSuiteSpotUser
        cfgconn.unbind()
    return asport, secure


def formatInfData(args):
    """Return the .inf data for a silence setup via setup-ds.pl.

        args = {
            # new instance values
            newhost, newuserid, newport, SER_ROOT_DN, newrootpw, newsuffix,

            # The following parameters require to register the new instance
            # in the admin server
            have_admin, cfgdshost, cfgdsport, cfgdsuser,cfgdspwd, admin_domain

            InstallLdifFile, AddOrgEntries, ConfigFile, SchemaFile,
            ldapifilepath

            # Setup the o=NetscapeRoot namingContext
            setup_admin,
        }

        @see https://access.redhat.com/site/documentation/en-US/
             Red_Hat_Directory_Server/8.2/html/Installation_Guide/
             Installation_Guide-Advanced_Configuration-Silent.html
        [General]
        FullMachineName= dir.example.com
        SuiteSpotUserID= nobody
        SuiteSpotGroup= nobody
        AdminDomain= example.com
        ConfigDirectoryAdminID= admin
        ConfigDirectoryAdminPwd= admin
        ConfigDirectoryLdapURL= ldap://dir.example.com:389/o=NetscapeRoot

        [slapd]
        SlapdConfigForMC= Yes
        UseExistingMC= 0
        ServerPort= 389
        ServerIdentifier= dir
        Suffix= dc=example,dc=com
        RootDN= cn=Directory Manager
        RootDNPwd= password
        ds_bename=exampleDB
        AddSampleEntries= No

        [admin]
        Port= 9830
        ServerIpAddress= 111.11.11.11
        ServerAdminID= admin
        ServerAdminPwd= admin

    """
    args = args.copy()
    args['CFGSUFFIX'] = CFGSUFFIX

    content = ("[General]" "\n")
    content += ("FullMachineName= %s\n" % args[SER_HOST])
    content += ("SuiteSpotUserID= %s\n" % args[SER_USER_ID])
    content += ("SuiteSpotGroup= %s\n" % args[SER_GROUP_ID])
    content += ("StrictHostCheck= %s\n" % args[SER_STRICT_HOSTNAME_CHECKING])

    if args.get('have_admin'):
        content += ("AdminDomain= %(admin_domain)s" "\n"
                    "ConfigDirectoryLdapURL= ldap://%(cfgdshost)s:%"
                    "(cfgdsport)d/%(CFGSUFFIX)s" "\n"
                    "ConfigDirectoryAdminID= %(cfgdsuser)s" "\n"
                    "ConfigDirectoryAdminPwd= %(cfgdspwd)s" "\n") % args

    content += ("\n" "\n" "[slapd]" "\n")
    content += ("ServerPort= %s\n" % args[SER_PORT])
    content += ("RootDN= %s\n" % args[SER_ROOT_DN])
    content += ("RootDNPwd= %s\n" % args[SER_ROOT_PW])
    content += ("ServerIdentifier= %s\n" % args[SER_SERVERID_PROP])
    content += ("Suffix= %s\n" % args[SER_CREATION_SUFFIX])

    # Create admin?
    if args.get('setup_admin'):
        content += ("SlapdConfigForMC= Yes" "\n"
                    "UseExistingMC= 0 " "\n")

    if 'InstallLdifFile' in args:
        content += """\nInstallLdifFile= %s\n""" % args['InstallLdifFile']
    if 'AddOrgEntries' in args:
        content += """\nAddOrgEntries= %s\n""" % args['AddOrgEntries']
    if 'ConfigFile' in args:
        for ff in args['ConfigFile']:
            content += """\nConfigFile= %s\n""" % ff
    if 'SchemaFile' in args:
        for ff in args['SchemaFile']:
            content += """\nSchemaFile= %s\n""" % ff

    if 'ldapifilepath' in args:
        content += "\nldapifilepath=%s\n" % args['ldapifilepath']
    if SER_INST_SCRIPTS_ENABLED in args:
        content += "\n{}={}\n".format(SER_INST_SCRIPTS_ENABLED, args[SER_INST_SCRIPTS_ENABLED])

    return content


def generate_ds_params(inst_num, role=ReplicaRole.STANDALONE):
    """Generate a host, port, secure port, server ID and replica ID
    for the selected role and instance number. I.e. inst_num=1, role="master".

    @param inst_num - an instance number in a range from 1 to 99
    @param role - ReplicaRole.STANDALONE, ReplicaRole.MASTER, ReplicaRole.HUB, ReplicaRole.CONSUMER
    @return - the dict with next keys: host, port, secure port, server id and replica id
    """

    if inst_num not in range(1, 100):
        raise ValueError("Instance number should be in a range from 1 to 99")

    if role not in (ReplicaRole.STANDALONE, ReplicaRole.MASTER, ReplicaRole.HUB, ReplicaRole.CONSUMER):
        raise ValueError('Role should be {}, {}, {}, {}'.format(ReplicaRole.STANDALONE, ReplicaRole.MASTER,
                                                                ReplicaRole.HUB, ReplicaRole.CONSUMER))

    instance_data = {}
    relevant_num = 38900

    # Set relevant number for ports
    if role == ReplicaRole.MASTER:
        relevant_num += 100
    elif role == ReplicaRole.HUB:
        relevant_num += 200
    elif role == ReplicaRole.CONSUMER:
        relevant_num += 300

    # Define replica ID
    if role == ReplicaRole.MASTER:
        replica_id = inst_num
    else:
        replica_id = CONSUMER_REPLICAID

    # Fill the dict with data
    server_id = "{}{}".format(role.name.lower(), inst_num)
    instance_data[SER_HOST] = LOCALHOST
    instance_data[SER_PORT] = relevant_num + inst_num
    instance_data[SER_SECURE_PORT] = relevant_num + inst_num + 24700
    instance_data[SER_SERVERID_PROP] = server_id
    instance_data[REPLICA_ID] = replica_id

    return instance_data


def get_ds_version(paths=None):
    """
    Return version of ns-slapd installed on this system. This is determined by the defaults.inf
    file, so is correct for the built and installed ns-slapd binary. This function works without
    root permissions.

    returns a string of the form: 1.3.4.8
    """
    if paths is None:
        paths = Paths()
    return paths.version


def ds_is_related(relation, *ver, instance=None):
    """
    Return a result of a comparison between the current version of ns-slapd and a provided version.
    """
    ops = {'older': operator.lt,
           'newer': operator.ge}
    if instance is None:
        ds_ver = get_ds_version()
    else:
        ds_ver = get_ds_version(instance.ds_paths)
    if len(ver) > 1:
        for cmp_ver in ver:
            if cmp_ver.startswith(ds_ver[:3]):
                return ops[relation](LegacyVersion(ds_ver),LegacyVersion(cmp_ver))
    else:
        return ops[relation](LegacyVersion(ds_ver), LegacyVersion(ver[0]))


def ds_is_older(*ver, instance=None):
    """
    Return True if the current version of ns-slapd is older than a provided version
    """
    return ds_is_related('older', *ver, instance=instance)


def ds_is_newer(*ver, instance=None):
    """
    Return True if the current version of ns-slapd is newer than a provided version
    """
    return ds_is_related('newer', *ver, instance=instance)

def ds_supports_new_changelog():
    """
    Return True if the current version of ns-slapd supports changelogs under cn=changelog,cn=<backend>,cn=ldbm..
    """
    return ds_is_newer('1.4.4.3')

def gentime_to_datetime(gentime):
    """Convert Generalized time to datetime object

    :param gentime: Time in the format - YYYYMMDDHHMMSSZ (20170126120000Z)
    :type password: str
    :returns: datetime.datetime object
    """

    return datetime.strptime(gentime, '%Y%m%d%H%M%SZ')


def gentime_to_posix_time(gentime):
    """Convert Generalized time to POSIX time format

    :param gentime: Time in the format - YYYYMMDDHHMMSSZ (20170126120000Z)
    :type password: str
    :returns: Epoch time int
    """
    time_tuple = (int(gentime[:4]),
                  int(gentime[4:6]),
                  int(gentime[6:8]),
                  int(gentime[8:10]),
                  int(gentime[10:12]),
                  int(gentime[12:14]),
                  0, 0, 0)

    return time.mktime(time_tuple)


def getDateTime():
    """
    Return the date and time:

        2016-04-21 21:21:00

    """
    return time.strftime("%Y-%m-%d %H:%M:%S")


def socket_check_open(host, port):
    """
    Check if a socket can be opened.  Need to handle cases where IPv6 is completely
    disabled.
    """
    try:
        # Trying IPv6 first ...
        family = socket.AF_INET6
        with closing(socket.socket(family, socket.SOCK_STREAM)) as sock:
            if sock.connect_ex((host, port)) == 0:
                return True
            else:
                return False
    except OSError:
        # No IPv6, adjust hostname, and try IPv4 ...
        family = socket.AF_INET
        if host == "::1":
            host = "127.0.0.1"
        with closing(socket.socket(family, socket.SOCK_STREAM)) as sock:
            if sock.connect_ex((host, port)) == 0:
                return True
            else:
                return False


def ensure_bytes(val):
    if val is not None and type(val) != bytes:
        return val.encode()
    return val


def ensure_str(val):
    if val is not None and type(val) != str:
        try:
            result = val.decode('utf-8')
        except UnicodeDecodeError:
            # binary value, just return str repr?
            result = str(val)
        return result
    return val


def ensure_int(val):
    if val is not None and not isinstance(val, int):
        return int(val)
    return val


def ensure_list_bytes(val):
    return [ensure_bytes(v) for v in val]


def ensure_list_str(val):
    return [ensure_str(v) for v in val]


def ensure_list_int(val):
    return [ensure_int(v) for v in val]


def ensure_dict_str(val):
    if MAJOR <= 2:
        return val
    retdict = {}
    for k in val:
        if isinstance(val[k], list):
            retdict[k] = ensure_list_str(val[k])
        else:
            retdict[k] = ensure_str(val[k])
    return retdict


def pseudolocalize(string):
    pseudo_string = u""
    for char in string:
        try:
            pseudo_string += _chars[char]
        except KeyError:
            pseudo_string += char
    return pseudo_string


def assert_c(condition, msg="Assertion Failed"):
    """This is the same as assert, but assert is compiled out
    when optimisation is enabled. This prevents compiling out.
    """
    if not condition:
        raise AssertionError(msg)


def format_cmd_list(cmd):
    """Format the subprocess command list to the quoted shell string"""

    return " ".join(map(shlex.quote, cmd))


def get_ldapurl_from_serverid(instance):
    """ Take an instance name, and get the host/port/protocol from dse.ldif
    and return a LDAP URL to use in the CLI tools (dsconf)

    :param instance: The server ID of a server instance
    :return tuple of LDAPURL and certificate directory (for LDAPS)
    """
    try:
        dse_ldif = DSEldif(None, instance)
    except:
        return (None, None)

    port = dse_ldif.get("cn=config", "nsslapd-port", single=True)
    secureport = dse_ldif.get("cn=config", "nsslapd-secureport", single=True)
    host = dse_ldif.get("cn=config", "nsslapd-localhost", single=True)
    sec = dse_ldif.get("cn=config", "nsslapd-security", single=True)
    ldapi_listen = dse_ldif.get("cn=config", "nsslapd-ldapilisten", single=True)
    ldapi_autobind = dse_ldif.get("cn=config", "nsslapd-ldapiautobind", single=True)
    ldapi_socket = dse_ldif.get("cn=config", "nsslapd-ldapifilepath", single=True)
    certdir = dse_ldif.get("cn=config", "nsslapd-certdir", single=True)

    if ldapi_listen is not None and ldapi_listen.lower() == "on" and \
       ldapi_autobind is not None and ldapi_autobind.lower() == "on" and \
       ldapi_socket is not None:
        # Use LDAPI
        socket = ldapi_socket.replace("/", "%2f")  # Escape the path
        return ("ldapi://" + socket, None)
    elif sec is not None and sec.lower() == "on" and secureport is not None:
        # Use LDAPS
        return ("ldaps://{}:{}".format(host, secureport), certdir)
    else:
        # Use LDAP
        return ("ldap://{}:{}".format(host, port), None)


def get_instance_list():
    # List all server instances
    paths = Paths()
    conf_dir = os.path.join(paths.sysconf_dir, 'dirsrv')
    insts = []
    try:
        for inst in os.listdir(conf_dir):
            if inst.startswith('slapd-') and not inst.endswith('.removed'):
                insts.append(inst)
    except OSError as e:
        log.error("Failed to check directory: {} - {}".format(conf_dir, str(e)))
    except IOError as e:
        log.error(e)
        log.error("Perhaps you need to be a different user?")
    insts.sort()
    return insts


def get_user_is_ds_owner():
    # Check if we have permission to administer the DS instance. This is required
    # for some tasks such as installing, killing, or editing configs for the
    # instance.
    cur_uid = os.getuid()
    if cur_uid == 0:
        # We are root, we have permission
        return True
    cur_username = pwd.getpwuid(cur_uid)[0]
    p = Paths()
    if cur_username == p.user:
        # We are the same user, all good
        return True
    return False


def get_user_is_root():
    cur_uid = os.getuid()
    if cur_uid == 0:
        # We are root, we have permission
        return True
    return False


def basedn_to_ldap_dns_uri(basedn):
    # ldap:///dc%3Dexample%2Cdc%3Dcom
    return "ldaps:///" + basedn.replace("=", "%3D").replace(",", "%2C")


def display_log_value(attr, value, hide_sensitive=True):
    # Mask all the sensitive attribute values
    if DEBUGGING or not hide_sensitive:
        return value
    else:
        if attr.lower() in SENSITIVE_ATTRS:
            if type(value) in (list, tuple):
                return list(map(lambda _: '********', value))
            else:
                return '********'
        else:
            return value


def display_log_data(data, hide_sensitive=True):
    # Take a dict and mask all the sensitive data
    return {a: display_log_value(a, v, hide_sensitive) for a, v in data.items()}


def convert_bytes(bytes):
    bytes = int(bytes)
    if bytes == 0:
        return "0 B"
    size_name = ["B", "KB", "MB", "GB", "TB", "PB"]
    i = int(math.floor(math.log(bytes, 1024)))
    pow = math.pow(1024, i)
    siz = round(bytes / pow, 2)
    return "{} {}".format(siz, size_name[i])


def search_filter_escape_bytes(bytes_value):
    """ Convert a byte sequence to a properly escaped for LDAP (format BACKSLASH HEX HEX) string"""
    # copied from https://github.com/cannatag/ldap3/blob/master/ldap3/utils/conv.py
    if str is not bytes:
        if isinstance(bytes_value, str):
            bytes_value = bytearray(bytes_value, encoding='utf-8')
        return ''.join([('\\%02x' % int(b)) for b in bytes_value])
    else:
        raise RuntimeError('Running with Python 2 is unsupported')


def print_nice_time(seconds):
    """Convert seconds to a pretty format
    """
    seconds = int(seconds)
    d, s = divmod(seconds, 24*60*60)
    h, s = divmod(s, 60*60)
    m, s = divmod(s, 60)
    d_plural = ""
    h_plural = ""
    m_plural = ""
    s_plural = ""
    if d > 1:
        d_plural = "s"
    if h != 1:
        h_plural = "s"
    if m != 1:
        m_plural = "s"
    if s != 1:
        s_plural = "s"
    if d > 0:
        return f'{d:d} day{d_plural}, {h:d} hour{h_plural}, {m:d} minute{m_plural}, {s:d} second{s_plural}'
    elif h > 0:
        return f'{h:d} hour{h_plural}, {m:d} minute{m_plural}, {s:d} second{s_plural}'
    elif m > 0:
        return f'{m:d} minute{m_plural}, {s:d} second{s_plural}'
    else:
        return f'{s:d} second{s_plural}'


def copy_with_permissions(source, target):
    """Copy a file while preserving all permissions"""

    shutil.copy2(source, target)
    st = os.stat(source)
    os.chown(target, st[stat.ST_UID], st[stat.ST_GID])
    os.chmod(target, st[stat.ST_MODE])


def cmp(self, x, y):
    """
    Replacement for built-in function cmp that was removed in Python 3

    Compare the two objects x and y and return an integer according to
    the outcome. The return value is negative if x < y, zero if x == y
    and strictly positive if x > y.
    """
    return (x > y) - (x < y)


def is_valid_hostname(hostname):
    if len(hostname) > 255:
        return False
    if hostname[-1] == ".":
        hostname = hostname[:-1] # strip exactly one dot from the right, if present
    allowed = re.compile("(?!-)[A-Z\d-]{1,63}(?<!-)$", re.IGNORECASE)
    return all(allowed.match(x) for x in hostname.split("."))
