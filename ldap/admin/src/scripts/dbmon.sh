#!/bin/sh
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
# Copyright (C) 2014 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

DURATION=${DURATION:-0}
INCR=${INCR:-1}
HOST=${HOST:-localhost}
PORT=${PORT:-389}
BINDDN=${BINDDN:-"cn=directory manager"}
BINDPW=${BINDPW:-"secret"}
DBLIST=${DBLIST:-all}
ldbmdn="cn=ldbm database,cn=plugins,cn=config"
VERBOSE=${VERBOSE:-0}

parseldif() {
    awk -v dblist="$DBLIST" -v verbose=$VERBOSE -v indexlist="$INDEXLIST" -F '[:,= ]+' '
        function printary(ary) {
            for (ii in ary) { print ii, "=", ary[ii] }
        }
        BEGIN {
            pagesize=8192 ; CONVFMT="%.3f" ; OFMT=CONVFMT ; SUBSEP=","
            alldb=0
            if (dblist == "all") {
                alldb=1
            } else {
                split(dblist, dbnames)
                for (key in dbnames) { val=dbnames[key] ; dbnames[tolower(val)]=val; delete dbnames[key] }
            }
            allindex=0
            if (indexlist == "all") {
                allindex=1
            } else {
                split(indexlist, idxnames)
                for (key in idxnames) { val=idxnames[key] ; idxnames[tolower(val)]=val; delete idxnames[key] }
            }
            fn="entcur entmax entcnt dncur dnmax dncnt"
            split(fn, fields)
            havednstats=0
            maxdbnamelen=0
        }
        /^[^ ]|^$/ {origline = $0; $0 = unwrapline; unwrapline = origline}
        /^ / {sub(/^ /, ""); unwrapline = unwrapline $0; next}
        /^nsslapd-dbcachesize/ { dbcachesize=$2 }
        /^nsslapd-db-page-size/ { pagesize=$2 }
        /^dbcachehitratio/ { dbhitratio=$2 }
        /^dbcachepagein/ { dbcachepagein=$2 }
        /^dbcachepageout/ { dbcachepageout=$2 }
        /^nsslapd-db-page-ro-evict-rate/ { dbroevict=$2 }
        /^nsslapd-db-pages-in-use/ { dbpages=$2 }
        /^dn: cn=monitor, *cn=[a-zA-Z0-9][a-zA-Z0-9_\.\-]*, *cn=ldbm database, *cn=plugins, *cn=config/ {
            idxnum=-1
            idxname=""
            dbname=tolower($5)
            if ((dbname in dbnames) || alldb) {
                len=length(dbname) ; if (len > maxdbnamelen) { maxdbnamelen=len }
                if (!(dbname in dbnames)) { dbnames[dbname] = dbname }
            }
        }
        /^currententrycachesize/ { stats[dbname,"entcur"]=$2 }
        /^maxentrycachesize/ { stats[dbname,"entmax"]=$2 }
        /^currententrycachecount/ { stats[dbname,"entcnt"]=$2 }
        /^currentdncachesize/ { stats[dbname,"dncur"]=$2 ; havednstats=1 }
        /^maxdncachesize/ { stats[dbname,"dnmax"]=$2 }
        /^currentdncachecount/ { stats[dbname,"dncnt"]=$2 }
        /^dbfilename-/ {
            #rhds
            #dbfilename-3: userRoot/id2entry.db4
            #sunds
            #dbfilename-id2entry: /full/path/to/db/dbname/dbname_id2entry.dbX
            if (dbname in dbnames) {
                split($0, idxline, /[ :/.-]+/)
                idxname=tolower(idxline[4])
                dbn=tolower(idxline[3])
                ilen=length(idxline)
                sundbn=tolower(idxline[ilen-2])
                sunidxname=tolower(idxline[2]) 
                if ((dbn == dbname) && (allindex || (idxname in idxnames))) {
                    idxnum=idxline[2]
                    if (!(idxname in idxnames)) { idxnames[idxname] = idxname }
                    len = length(idxname)
                    if (len > idxmaxlen[dbn]) { idxmaxlen[dbn] = len }
                } else if ((sundbn == dbname) && (allindex || (sunidxname in idxnames))) {
                    idxname=sunidxname
                    idxnum=1 # no index number just index name
                    if (!(idxname in idxnames)) { idxnames[idxname] = idxname }
                    len = length(idxname)
                    if (len > idxmaxlen[sundbn]) { idxmaxlen[sundbn] = len }
                } else {
                    # print "index", idxline[4], "not in idxnames"
                }
            } else {
                # print "dbname", dbname, "not in dbnames"
            }
        }
        /^dbfilepagein-/ { if (idxnum >= 0) { idxstats[dbname,idxname,"pagein"] = $2 } }
        /^dbfilepageout-/ { if (idxnum >= 0) { idxstats[dbname,idxname,"pageout"] = $2 } }
        END {
            free=(dbcachesize-(pagesize*dbpages))
            freeratio=free/dbcachesize
            if (verbose > 1) {
                print "# dbcachefree - free bytes in dbcache"
                print "# free% - percent free in dbcache"
                print "# roevicts - number of read-only pages dropped from cache to make room for other pages"
                print "#            if this is non-zero, it means the dbcache is maxed out and there is page churn"
                print "# hit% - percent of requests that are served by cache"
                print "# pagein - number of pages read into the cache"
                print "# pageout - number of pages dropped from the cache"
            }
            print "dbcachefree", free, "free%", (freeratio*100), "roevicts", dbroevict, "hit%", dbhitratio, "pagein", dbcachepagein, "pageout", dbcachepageout
            if (verbose > 1) {
                print "# dbname - name of database instance - the row shows the entry cache stats"
                print "# count - number of entries in cache"
                print "# free - number of free bytes in cache"
                print "# free% - percent free in cache"
                print "# size - average size of date in cache in bytes (current size/count)"
                if (havednstats) {
                    print "# DNcache - the line below the entry cache stats are the DN cache stats"
                    print "# count - number of dns in dn cache"
                    print "# free - number of free bytes in dn cache"
                    print "# free% - percent free in dn cache"
                    print "# size - average size of dn in dn cache in bytes (currentdncachesize/currentdncachecount)"
                    print "# under each db are the list of selected indexes specified with INDEXLIST"
                }
            }
            if (havednstats) { # make sure there is enough room for dbname:ent and dbname:dn
                maxdbnamelen += 4 # :ent
                dbentext = ":ent"
                dbdnext = ":dn "
            } else {
                dbentext = ""
                dbdnext = ""
            }
            if (maxdbnamelen < 6) { # len of "dbname"
                maxdbnamelen = 6
            }

            if (verbose > 0) {
                fmtstr = sprintf("%%%d.%ds %%10.10s %%13.13s %%6.6s %%7.7s\n", maxdbnamelen, maxdbnamelen)
                printf fmtstr, "dbname", "count", "free", "free%", "size"
            }
            for (dbn in dbnames) {
                cur=stats[dbn,"entcur"]
                max=stats[dbn,"entmax"]
                cnt=stats[dbn,"entcnt"]
                free=max-cur
                freep=free/max*100
                size=(cnt == 0) ? 0 : cur/cnt
                fmtstr = sprintf("%%%d.%ds %%10d %%13d %%6.1f %%7.1f\n", maxdbnamelen, maxdbnamelen)
                printf fmtstr, dbnames[dbn] dbentext, cnt, free, freep, size
                if (havednstats) {
                    dcur=stats[dbn,"dncur"]
                    dmax=stats[dbn,"dnmax"]
                    dcnt=stats[dbn,"dncnt"]
                    dfree=dmax-dcur
                    dfreep=dfree/dmax*100
                    dsize=(dcnt == 0) ? 0 : dcur/dcnt
                    printf fmtstr, dbnames[dbn] dbdnext, dcnt, dfree, dfreep, dsize
                }
                if (indexlist) {
                    len = idxmaxlen[dbn]
                    fmtstr = sprintf("%%%d.%ds %%%d.%ds pagein %%8d pageout %%8d\n", maxdbnamelen, maxdbnamelen, len, len)
                    for (idx in idxnames) {
                        ipi = idxstats[dbn,idx,"pagein"]
                        ipo = idxstats[dbn,idx,"pageout"]
                        # not every db will have every index
                        if (ipi != "" && ipo != "") {
                            printf fmtstr, "+", idxnames[idx], ipi, ipo
                        }
                    }
                }
            }
        }
        '
}

dodbmon() {
    while [ 1 ] ; do
        date
        ldapsearch -xLLL -h $HOST -p $PORT -D "$BINDDN" -w "$BINDPW" -b "$ldbmdn" '(|(cn=config)(cn=database)(cn=monitor))' \
        | parseldif
        echo ""
        sleep $INCR
    done
}

dodbmon
