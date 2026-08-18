# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import time
import datetime
import logging
import re
from lib389.utils import cmp

log = logging.getLogger(__name__)


class CSN(object):
    """CSN is Change Sequence Number
        csn.ts is the timestamp (time_t - seconds)
        csn.seq is the sequence number (max 65535)
        csn.rid is the replica ID of the originating supplier
        csn.subseq is not currently used"""

    csnpat = r'(.{8})(.{4})(.{4})(.{4})'
    csnre = re.compile(csnpat)

    def __init__(self, csnstr):
        match = CSN.csnre.match(csnstr)
        self.ts = 0
        self.seq = 0
        self.rid = 0
        self.subseq = 0
        if match:
            self.ts = int(match.group(1), 16)
            self.seq = int(match.group(2), 16)
            self.rid = int(match.group(3), 16)
            self.subseq = int(match.group(4), 16)
        elif csnstr:
            self.ts = 0
            self.seq = 0
            self.rid = 0
            self.subseq = 0
            log.info("%r is not a valid CSN" % csnstr)

    def csndiff(self, oth):
        return (oth.ts - self.ts,
                oth.seq - self.seq,
                oth.rid - self.rid,
                oth.subseq - self.subseq)

    def __cmp__(self, oth):
        if self is oth:
            return 0
        (tsdiff, seqdiff, riddiff, subseqdiff) = self.csndiff(oth)

        diff = tsdiff or seqdiff or riddiff or subseqdiff
        ret = 0
        if diff > 0:
            ret = 1
        elif diff < 0:
            ret = -1
        return ret

    def __eq__(self, oth):
        return cmp(self, oth) == 0

    def diff2str(self, oth):
        retstr = ''
        diff = oth.ts - self.ts
        if diff > 0:
            td = datetime.timedelta(seconds=diff)
            retstr = "is behind by %s" % td
        elif diff < 0:
            td = datetime.timedelta(seconds=-diff)
            retstr = "is ahead by %s" % td
        else:
            diff = oth.seq - self.seq
            if diff:
                retstr = "seq differs by %d" % diff
            elif self.rid != oth.rid:
                retstr = "rid %d not equal to rid %d" % (self.rid, oth.rid)
            else:
                retstr = "equal"
        return retstr

    def get_time_lag(self, oth):
        diff = oth.ts - self.ts
        if diff < 0:
            lag = datetime.timedelta(seconds=-diff)
        else:
            lag = datetime.timedelta(seconds=diff)
        return "{:0>8}".format(str(lag))

    def __repr__(self):
        return ("%s seq: %s rid: %s" % (time.strftime("%x %X", time.localtime(self.ts)),
                                        str(self.seq), str(self.rid)))

    def __str__(self):
        return self.__repr__()


class RUV(object):
    """RUV is Replica Update Vector
        ruv.gen is the generation CSN
        ruv.rid[1] through ruv.rid[N] are dicts - the number (1-N)
        is the replica ID
          ruv.rid[N][url] is the purl
          ruv.rid[N][min] is the min csn
          ruv.rid[N][max] is the max csn
          ruv.rid[N][lastmod] is the last modified timestamp
        example ruv attr:
        nsds50ruv: {replicageneration} 3b0ebc7f000000010000
        nsds50ruv: {replica 1 ldap://myhost:51010}
                    3b0ebc9f000000010000 3b0ebef7000000010000
        nsre_ruvplicaLastModified: {replica 1 ldap://myhost:51010}
                                               292398402093
        if the try repl flag is true, if getting the ruv from the
        suffix fails, try getting the ruv from the cn=replica entry
    """

    pre_gen = r'\{replicageneration\}\s+(\w+)'
    re_gen = re.compile(pre_gen)
    pre_ruv = r'\{replica\s+(\d+)\s+(.+?)\}\s*(\w*)\s*(\w*)'
    re_ruv = re.compile(pre_ruv)

    def __init__(self, ent):
        # rid is a dict
        # key is replica ID - val is dict of url, min csn, max csn
        self.rid = {}
        for item in ent.getValues('nsds50ruv'):
            matchgen = RUV.re_gen.match(item)
            matchruv = RUV.re_ruv.match(item)
            if matchgen:
                self.gen = CSN(matchgen.group(1))
            elif matchruv:
                rid = int(matchruv.group(1))
                self.rid[rid] = {'url': matchruv.group(2),
                                 'min': CSN(matchruv.group(3)),
                                 'max': CSN(matchruv.group(4))}
            else:
                log.info("unknown RUV element %r" % item)
        for item in ent.getValues('nsre_ruvplicaLastModified'):
            matchruv = RUV.re_ruv.match(item)
            if matchruv:
                rid = int(matchruv.group(1))
                self.rid[rid]['lastmod'] = int(matchruv.group(3), 16)
            else:
                log.info("unknown nsre_ruvplicaLastModified item %r" % item)

    def __cmp__(self, oth):
        if self is oth:
            return 0
        if not self:
            return -1  # None is less than something
        if not oth:
            return 1  # something is greater than None
        diff = cmp(self.gen, oth.gen)
        if diff:
            return diff
        for rid in list(self.rid.keys()):
            for item in ('max', 'min'):
                csn = self.rid[rid][item]
                csnoth = oth.rid[rid][item]
                diff = cmp(csn, csnoth)
                if diff:
                    return diff
        return 0

    def __eq__(self, oth):
        return cmp(self, oth) == 0

    def __str__(self):
        ret = 'generation: %s\n' % self.gen
        for rid in list(self.rid.keys()):
            ret = ret + 'rid: %s url: %s min: [%s] max: [%s]\n' % \
                (rid, self.rid[rid]['url'], self.rid[rid].get('min', ''),
                 self. rid[rid].get('max', ''))
        return ret

    def getdiffs(self, oth):
        """Compare two ruvs and return the differences
        returns a tuple - the first element is the
        result of cmp() - the second element is a string"""
        if self is oth:
            return (0, "\tRUVs are the same")
        if not self:
            return (-1, "\tfirst RUV is empty")
        if not oth:
            return (1, "\tsecond RUV is empty")
        diff = cmp(self.gen, oth.gen)
        if diff:
            return (diff, "\tgeneration [" + str(self.gen) +
                    "] not equal to [" + str(oth.gen) +
                    "]: likely not yet initialized")
        retstr = ''
        for rid in list(self.rid.keys()):
            for item in ('max', 'min'):
                csn = self.rid[rid][item]
                csnoth = oth.rid[rid][item]
                csndiff = cmp(csn, csnoth)
                if csndiff:
                    if len(retstr):
                        retstr += "\n"
                    retstr += ("\trid %d %scsn %s\n\t[%s] vs [%s]" %
                               (rid, item, csn.diff2str(csnoth), csn, csnoth))
                    if not diff:
                        diff = csndiff
        if not diff:
            retstr = "\tup-to-date - RUVs are equal"
        return (diff, retstr)
