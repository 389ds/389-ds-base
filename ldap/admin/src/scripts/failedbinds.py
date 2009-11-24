import re
import os, os.path

# regex that matches a BIND request line
regex_num = r'[-]?\d+' # matches numbers including negative
regex_new_conn = re.compile(r'^(\[.+\]) (conn=%s) (fd=%s) (slot=%s) (?:SSL )?connection from (\S+)' % (regex_num, regex_num, regex_num))
regex_sslinfo = re.compile(r'^\[.+\] (conn=%s) SSL (.+)$' % regex_num)
regex_bind_req = re.compile(r'^(\[.+\]) (conn=%s) (op=%s) BIND dn=(.+) method=(\S+) version=\d ?(?:mech=(\S+))?' % (regex_num, regex_num))
regex_bind_res = re.compile(r'^(\[.+\]) (conn=%s) (op=%s) RESULT err=(%s) tag=97 ' % (regex_num, regex_num, regex_num))
regex_unbind = re.compile(r'^\[.+\] (conn=%s) op=%s UNBIND' % (regex_num, regex_num))
regex_closed = re.compile(r'^(\[.+\]) (conn=%s) (op=%s) fd=%s closed' % (regex_num, regex_num, regex_num))
regex_ssl_map_fail = re.compile(r'^\[.+\] (conn=%s) (SSL failed to map client certificate.*)$' % regex_num)

# bind errors we can ignore
ignore_errors = {'0': 'Success',
                 '10': 'Referral',
                 '14': 'SASL Bind In Progress'
                 }

REQ = 0
RES = 1

class Conn:
    def __init__(self, timestamp, conn, fd, slot, ip):
        self.conn = conn
        self.fd = fd
        self.slot = slot
        self.ip = ip
        self.timestamp = timestamp
        self.ops = {}
        self.sslinfo = ''

    def addssl(self, sslinfo):
        if self.sslinfo and sslinfo:
            self.sslinfo += ' '
        self.sslinfo += sslinfo

    def addreq(self, timestamp, opnum, dn, method, mech='SIMPLE'):
        retval = None
        if opnum in self.ops: # result came before request?
            op = self.ops.pop(opnum) # grab the op and remove from list
            if op[RES]['errnum'] in ignore_errors: # don't care about this op
                return retval
            if not mech: mech = "SIMPLE"
            op[REQ] = {'dn': dn, 'method': method, 'timestamp': timestamp,
                       'mech': mech}
            retval = self.logstr(opnum, op)
        else: # store request until we get the result
            op = [None, None] # new empty list
            if not mech: mech = "SIMPLE"
            op[REQ] = {'dn': dn, 'method': method, 'timestamp': timestamp,
                       'mech': mech}
            self.ops[opnum] = op
        return retval

    def addres(self, timestamp, opnum, errnum):
        retval = None
        if opnum in self.ops:
            op = self.ops.pop(opnum) # grab the op and remove from list
            if errnum in ignore_errors: # don't care about this op
                return retval
            op[RES] = {'errnum': errnum, 'timestamp': timestamp}
            retval = self.logstr(opnum, op)
        else: # result came before request in access log - store until we find request
            op = [None, None] # new empty list
            op[RES] = {'errnum': errnum, 'timestamp': timestamp}
            self.ops[opnum] = op
        return retval

    def logstr(self, opnum, op):
        # timestamp connnum opnum err=X request timestamp dn=Y method=Z mech=W timestamp ip=ip
        logstr = '%s %s %s err=%s REQUEST %s dn=%s method=%s mech=%s %s ip=%s extra=%s' % (
            op[RES]['timestamp'], self.conn, opnum, op[RES]['errnum'],
            op[REQ]['timestamp'], op[REQ]['dn'], op[REQ]['method'], op[REQ]['mech'],
            self.timestamp, self.ip, self.sslinfo
            )
        return logstr
        
# key is conn=X
# val is ops hash
#  key is op=Y
#  value is list
#    list[0] is BIND request
#    list[1] is RESULT
conns = {}

# file to log failed binds to
logf = None

def pre(plgargs):
    global logf
    logfile = plgargs.get('logfile', None)
    if not logfile:
        print "Error: missing required argument failedbinds.logfile"
        return False
    needchmod = False
    if not os.path.isfile(logfile): needchmod = True
    logf = open(logfile, 'a', 0) # 0 for unbuffered output
    if needchmod: os.chmod(logfile, 0600)
    return True

def post():
    global logf
    logf.close()
    logf = None

def plugin(line):
    # is this a new conn line?
    match = regex_new_conn.match(line)
    if match:
        (timestamp, connid, fdid, slotid, ip) = match.groups()
        if connid in conns: conns.pop(connid) # remove old one, if any
        conn = Conn(timestamp, connid, fdid, slotid, ip)
        conns[connid] = conn
        return True

    # is this an UNBIND line?
    match = regex_unbind.match(line)
    if match:
        connid = match.group(1)
        if connid in conns: conns.pop(connid) # remove it
        return True

    # is this a closed line?
    match = regex_closed.match(line)
    if match:
        (timestamp, connid, opid) = match.groups()
        if connid in conns: conns.pop(connid) # remove it
        return True

    # is this an SSL info line?
    match = regex_sslinfo.match(line)
    if match:
        (connid, sslinfo) = match.groups()
        if connid in conns:
            conns[connid].addssl(sslinfo)
        return True

    # is this a line with extra SSL mapping info?
    match = regex_ssl_map_fail.match(line)
    if match:
        (connid, sslinfo) = match.groups()
        if connid in conns:
            conns[connid].addssl(sslinfo)
        return True

    # is this a REQUEST line?
    match = regex_bind_req.match(line)
    if match:
        (timestamp, connid, opnum, dn, method, mech) = match.groups()
        # should have seen new conn line - if not, have to create a dummy one
        conn = conns.get(connid, Conn('unknown', connid, '', '', 'unknown'))
        logmsg = conn.addreq(timestamp, opnum, dn, method, mech)
        if logmsg:
            logf.write(logmsg + "\n")
        return True

    # is this a RESULT line?
    match = regex_bind_res.match(line)
    if match:
        (timestamp, connid, opnum, errnum) = match.groups()
        # should have seen new conn line - if not, have to create a dummy one
        conn = conns.get(connid, Conn('unknown', connid, '', '', 'unknown'))
        logmsg = conn.addres(timestamp, opnum, errnum)
        if logmsg:
            logf.write(logmsg + "\n")
        return True

    return True # no match
