#!/usr/bin/python3

# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

#import os
#import os.path
#import ldap
import sys
import re
import string
import logging
import subprocess
import multiprocessing
import time
import json
import statistics
from random import shuffle, seed, randint, choice
from lib389._constants import *
from lib389.properties import *
from lib389.idm.user import UserAccounts
from lib389.topologies import create_topology
from lib389 import DirSrv
from lib389.config import LMDB_LDBMConfig
from lib389.utils import get_default_db_lib
from pathlib import Path, PosixPath

class IdGenerator:
    # Generates up to nbids unique identifiers

    def __init__(self, nbids):
        self._idx = 0
        self._nbids = nbids

    def __iter__(self):
        self._idx = 0
        return self

    def _idFromIndex(self):
        return self._formatId()

    def _formatId(self, id):
        # Should be overwritten in subclass 
        # Should return an id
        return str(id)

    def __next__(self):
        if (self._idx >= self._nbids):
            raise StopIteration
        self._idx += 1
        return self._formatId(self._idx-1)

    def getIdx(self):
        return self._idx

    def random(self):
        self._idx = randint(0, self._nbids-1);
        return self._formatId(self._idx)

class IdGeneratorWithNames(IdGenerator):
    # Generates up to nbids unique identifiers as names
    # for perf reason a small (self._list_size) number of unique identifier is generated
    # then ids from this list are concat level time (insuring the final id is unique)

    # Generates up to self._list_size power level unique identifiers
    # for perf reason a small (self._list_size) number of unique identifier is generated
    # then ids from this list are concat level time (insuring the final id is unique)
    def __init__(self, nbids):
        super().__init__(nbids)
        self._voyelles = [ 'a', 'e', 'i', 'o', 'u', 'ai', 'an', 'au', 'en', 'ei', 'en', 'eu', 'in', 'on', 'ou' ]
        self._consonnes = [ 'b', 'c', 'ch', 'cr', 'd', 'f', 'g', 'j', 'l', 'm', 'n', 'p', 'ph', 'qu', 'r', 's', 't', 'v' ]
        self._syllabs = [c+v for c in self._consonnes for v in self._voyelles]
        shuffle(self._syllabs) 
        self._level = 0
        self._syllabsLen = len(self._syllabs)
        while (nbids > 0):
            self._level = self._level+1
            nbids = int (nbids / self._syllabsLen)

    def _formatId(self, idx):
        id = ""
        for i in range(self._level):
            id += self._syllabs[int(idx % self._syllabsLen)]
            idx /= self._syllabsLen
        return id.capitalize()

class IdGeneratorWithNumbers(IdGenerator):
    # Generates up to nbids unique identifiers as numbers

    @staticmethod
    def formatId(idx):
        return f'{idx:0>10d}'

    def _formatId(self, idx):
        return IdGeneratorWithNumbers.formatId(idx)


class CsvFile:
    # Helper to write simple csv files

    def __init__(self, fname, width):
        self.fname = fname
        self.f = None
        self.width = width
        self.pos = 1
        self.line = ""
        self.sep = ";"
        self.lineid = 1

    def __enter__(self):
        self.f = open(self.fname, "w") if self.fname else sys.stdout
        return self

    def __exit__(self, type, value, tb):
        if (self.f != sys.stdout):
            self.f.close()
        self.f = None

    def nf(self, str):
        if not str:
            str=""
        self.line += f"{str}{self.sep}"
        self.pos += 1

    def nl(self):
        while self.pos < self.width:
            self.nf(None)
        self.line += "\n"
        self.f.write(self.line)
        self.line = ""
        self.pos = 1
        self.lineid += 1

    def n(self, v):
        # Get name of spreadsheet column
        if (v == 0):
            return ""
        return chr(0x40+v)

    def ref(self, dpl):
        colid = self.pos + dpl - 1
        return f"{self.n(int(colid/26))}{self.n(colid%26+1)}{self.lineid}"

class PerformanceTools:

    def __init__(self, options = {}):
        prefix = os.path.join(os.environ.get('PREFIX', ""))
        perfdir= f"{prefix}/var/log/dirsrv/perfdir"
        print(f"Results and logs are stored in {perfdir} directory.")
        self._options = {
            'nbUsers' : 10000,                
            'seed' : 'lib389PerfTools',
            'resultDir' : perfdir,
            'suffix' : DEFAULT_SUFFIX,
            **options
        }
        seed(self._options['seed'])
        self._instance = None
        os.makedirs(perfdir, mode=0o755, exist_ok = True)
        self._ldclt_template = self.getFilePath("template.ldclt");
        # Generate a dummy template anyway we do not plan to create entries 
        with open(self._ldclt_template, "w") as f:
            f.write("objectclass: inetOrgPerson\n");
        self._users_parents_dn = f"ou=People,{self._options['suffix']}"

    @staticmethod
    def log2Csv(fname, fout):
        # Convert (verbose) log file into csv file  (easier for comparing the results)
        map={}    # ( nb_users, name, nbthreads, db_lib) Tuple to Result map
        names={}  # { Name : None } Map
        has_threads={} # { Name : { threads : { users : users } } } Map
        # Read log file
        maxmes=0
        with open(fname) as f:
            for line in f:
                if (line[0] != '{'):
                    continue
                if (line[-1] == '\n'):
                    line = line[:-1]
                res = eval(line.replace('\n','\\n'))
                nb_users = res['nbUsers']
                db_lib = res['db_lib']
                name = res['measure_name']
                names[name] = None
                try:
                    nbthreads = res['nb_threads']
                except KeyError:
                    nbthreads = ""
                if not name in has_threads:
                    has_threads[name] = {}
                if not nbthreads in has_threads[name]:
                    has_threads[name][nbthreads] = {}
                has_threads[name][nbthreads][nb_users] = nb_users
                key = ( nb_users, name, nbthreads, db_lib)
                if not key in map:
                    map[key] = []
                m = map[key]
                m.append(res)
                if maxmes < len(m):
                    maxmes = len(m)
        # Displays the result: by test name then by thread number then by users number
        # Generates all combinations
        keys=[]
        for name in sorted(names.keys()):
            for t in sorted(has_threads[name].keys()):
                for user in sorted(has_threads[name][t].keys()):
                    keys.append((user, name, t))
        #Generates the csv file
        sep=";"
        with CsvFile(fout, 2*maxmes + 2) as csv:
            csv.nf("test name");
            csv.nf("threads");
            csv.nf("users");
            for idx in range(maxmes):
                csv.nf("bdb");
                csv.nf("mdb");
                csv.nf("%");
            csv.nl();
            for k in keys:
                csv.nf(f"{k[1]}")
                csv.nf(f"{k[2]}")
                csv.nf(f"{k[0]}")
                k0 = ( k[0], k[1], k[2], "bdb" )
                k1 = ( k[0], k[1], k[2], "mdb" )
                for idx in range(maxmes):
                    if k0 in map and idx < len(map[k0]):
                        res = map[k0][idx]
                        csv.nf(res['safemean'])
                    else:
                        csv.nf(None)
                    if k1 in map and idx < len(map[k1]):
                        res = map[k1][idx]
                        csv.nf(res['safemean'])
                    else:
                        csv.nf(None)
                    # Add % formula
                    csv.nf(f"=({csv.ref(-1)}-{csv.ref(-2)})/{csv.ref(-2)}")
                csv.nl();

    def getFilePath(self, filename):
        return os.path.join(self._options['resultDir'], filename)   

    def log(self, filename, msg):
        with open(self.getFilePath(filename), "at") as f:
            f.write(str(msg))
            f.write("\n")

    def initInstance(self):
        if (self._instance):
            return self._instance;
        uidpath = self.getFilePath("uids")
        nb_uids = 0
        try:
            with open(uidpath, 'r') as f:
                while f.readline():
                    nb_uids += 1
        except FileNotFoundError:
            pass
        nb_users = self._options['nbUsers']
        need_rebuild = True
        if (nb_uids == nb_users):
            # Lets try to reuse existing instance
            try :
                self._instance = DirSrv(verbose=True)
                self._instance.local_simple_allocate(serverid="standalone1", password=PW_DM)
                self._instance.open()
                if (self._instance.exists()):
                    if (self._instance.get_db_lib() == get_default_db_lib()):
                        need_rebuild = False
                    else:
                        print (f"db is {self._instance.get_db_lib()} instead of {get_default_db_lib()} ==> instance must be rebuild")
                else:    
                    print (f"missing instance ==> instance must be rebuild")
            except Exception:
                pass
        else:
            print (f"Instance has {nb_uids} users instead of {nb_users} ==> instance must be rebuild")
        if (need_rebuild):
            print ("Rebuilding standalone1 instance")
            # Should rebuild the instance from scratch
            topology = create_topology({ReplicaRole.STANDALONE: 1})
            self._instance = topology.standalone
            #  Adjust db size if needed (i.e about 670 K users)
            defaultDBsize = 1073741824 
            entrySize =  1600 # Real size is around 1525 
            if (self._instance.get_db_lib() == "mdb" and 
                    nb_users * entrySize > defaultDBsize):
                mdb_config = LMDB_LDBMConfig(self._instance)
                mdb_config.replace("nsslapd-mdb-max-size", str(nb_users * entrySize))
                self._instance.restart()
            # Then populate the users
            useraccounts = UserAccounts(self._instance, self._options['suffix'])
            with open(uidpath, 'w') as f:
                uidgen = IdGeneratorWithNumbers(nb_users)
                cnGen = IdGeneratorWithNames(100)
                snGen = IdGeneratorWithNames(100)
        
                for uid in uidgen:
                    cn = cnGen.random()
                    sn = snGen.random()
                    rdn = f"uid={uid}"
                    osuid = uidgen.getIdx() + 1000
                    osgid = int (osuid % 100) + 1000
                    properties = {
                        'uid': uid,
                        'cn': cn,
                        'sn': sn,
                        'uidNumber': str(osuid),
                        'gidNumber': str(osgid),
                        'homeDirectory': f'/home/{uid}'
                    }
                    super(UserAccounts, useraccounts).create(rdn, properties)
                    f.write(f'{uid}\n')
        return self._instance;
        
    @staticmethod
    def filterMeasures(values, m, ecart):
        # keep values around m
        r = []
        for val in values:
            if (val > (1 - ecart) * m and val < (1 + ecart) * m):
                r.append(val)
        return r

    def safeMeasures(self, values, ecart=0.2):
        v = values
        try:
            r = PerformanceTools.filterMeasures(values, statistics.mean(v) , ecart)
            while ( r != v ):
                v = r
                r = PerformanceTools.filterMeasures(values, statistics.mean(v) , ecart)
                if (len(r) == 0):
                    return values
            return r
        except statistics.StatisticsError as e:
            self.log("log", str(e))
            print(e)
            return values

    # Return a dict about the evironment data
    def getEnvInfo(self):
        mem = os.sysconf('SC_PAGE_SIZE') * os.sysconf('SC_PHYS_PAGES') / (1024.**3)
        with open ('/etc/redhat-release') as f:
            release = f.read()
        return {
            "db_lib" : self._instance.get_db_lib(),
            "nb_cpus" : multiprocessing.cpu_count(),
            "total mem" : mem,
            "release" : str(release),
            **self._options
        }

    def finalizeResult(self, res):
        try:
            rawres = res["rawresults"]
            res["rawmean"] = statistics.mean(rawres)
            res["saferesults"] = self.safeMeasures(rawres) # discard first measure result
            res["safemean"] = statistics.mean(res["saferesults"])
            pretty_res_keys = [ 'start_time', 'stop_time', 'measure_name', 'safemean', 'db_lib', 'nbUsers', 'nb_threads' ]
            pretty_res = dict(filter(lambda elem: elem[0] in pretty_res_keys, res.items()))
        except statistics.StatisticsError as e:
            print(e)
            res["exception"] = e
            pretty_res = "#ERROR"
        res["pretty"] = pretty_res
        self.log("out", res["pretty"])
        self.log("log", res)
        return res

    def ldclt(self, measure_name, args, nbThreads=10, nbMes=10):
        # First ldclt measure is always bad so do 1 measure more 
        # and discard it from final result
        nbMes += 1

        prog = os.path.join(self._instance.ds_paths.bin_dir, 'ldclt')
        cmd = [ prog,
            '-h',
            f'{self._instance.host}',
            '-p',
            f'{self._instance.port}',
            '-D',
            f'{self._instance.binddn}',
            '-w',
            f'{self._instance.bindpw}',
            '-N', str(nbMes),
            '-n', str(nbThreads) ]
        for key in args.keys():
            cmd.append(str(key))
            val = args[key]
            if (val):
                cmd.append(str(val))
        start_time = time.time()
        tmout = 30+10*nbMes
        print (f"Running ldclt with a timeout of {tmout} seconds ...\r")
        try:
            result = subprocess.run(args=cmd, capture_output=True, timeout=tmout)
        except subprocess.CalledProcessError as e:
            self.log("log", f'{e.cmd} failed.  measure: {measure_name}\n' +
                      f'instance: {self._instance.serverid}\n' +
                      f'return code is {e.returncode}.\n' +
                      f'stdout: {e.stdout}\n' +
                      f'stderr: {e.stderr}\n' )
            raise e
        print (" Done.")
        stop_time = time.time()
        # Lets parse the result
        res = { "measure_name" : measure_name, 
                "cmd" : cmd,
                "stdout" : result.stdout,
                "stderr" : result.stderr,
                "returncode" : result.returncode,
                "start_time" : start_time,
                "stop_time" : stop_time,
                "stop_time" : stop_time,
                "nb_threads" : nbThreads,
                **self.getEnvInfo() }
        rawres = re.findall(r'Average rate: [^ ]*\s*.([^/]*)', str(result.stdout))
        rawres = [float(i) for i in rawres]
        res["measure0"] = rawres[0]
        res["rawresults"] = rawres[1:]   # Discard first measure
        return self.finalizeResult(res)

    def measure_search_by_uid(self, name, nb_threads = 1):
        nb_users = self._options['nbUsers']
        args  = { "-b" : self._users_parents_dn,
                  "-f" : "uid=XXXXXXXXXX",
                  "-e" : "esearch,random",
                  "-r0" : None,
                  f"-R{nb_users-1}" : None }
        return self.ldclt(name, args, nbThreads=nb_threads)

    # I wish I could make the base dn vary rather than use the dn in filter
    # but I did not find how to do that (the RDN trick as in modify 
    #  generates the same search than measure_search_by_uid test)
    def measure_search_by_filtering_the_dn(self, name, nb_threads = 1):
        nb_users = self._options['nbUsers']
        args  = { "-b" : self._users_parents_dn,
                  "-f" : "uid:dn:=XXXXXXXXXX",
                  "-e" : "esearch,random",
                  "-r0" : None,
                  f"-R{nb_users-1}" : None }
        return self.ldclt(name, args, nbThreads=nb_threads)

    def measure_modify(self, name, nb_threads = 1):
        nb_users = self._options['nbUsers']
        args  = { "-b" : self._users_parents_dn,
                  "-e" : f"rdn=uid:[RNDN(0;{nb_users-1};10)],object={self._ldclt_template},attreplace=sn: random modify XXXXX" }
        return self.ldclt(name, args, nbThreads=nb_threads)

    def offline_export(self):
        start_time = time.time()
        assert (self._instance.db2ldif(DEFAULT_BENAME, (self._options['suffix'],), None, None, None, self._ldif))
        stop_time = time.time()
        # Count entries in ldif file (if not already done)
        if not self._nbEntries:
            self._nbEntries = 0
            with open(self._ldif) as f:
                for line in f:
                    if (line.startswith("dn:")):
                        self._nbEntries += 1
        return self._nbEntries / (stop_time - start_time)

    def offline_import(self):
        start_time = time.time()
        assert (self._instance.ldif2db(DEFAULT_BENAME, None, None, None, self._ldif))
        stop_time = time.time()
        return self._nbEntries / (stop_time - start_time)

    def _do_measure(self, measure_name, measure_cb, nbMes):
        # Perform non ldcltl measure
        #  
        first_time = time.time()
        rawres = []
        for m in range(nbMes):
            try:
                rawres.append( measure_cb() )
                stop_time = time.time()
            except AssertionError:
                continue
        last_time = time.time()
        # Lets parse the result
        res = { "measure_name" : measure_name, 
                "start_time" : first_time,
                "stop_time" : last_time,
                "nb_measures" : nbMes,
                "rawresults" : rawres,
                **self.getEnvInfo() }
        return self.finalizeResult(res)

    def mesure_export_import(self, nbMes=10):
        self._instance.stop()
        self._ldif = self.getFilePath("db.ldif");
        self._nbEntries = None
        res = [ self._do_measure("export", self.offline_export, nbMes), self._do_measure("import", self.offline_import, nbMes) ]
        self._instance.start()
        return res;

    class Tester:
        # Basic tester (used to define ldclt tests)
        def __init__(self, name, description, method_name):
            self._base_name = name
            self._base_description = description
            self._method_name = method_name

        def name(self):
            return self._base_name

        def argsused(self):
            return [ "nb_threads", "name" ]
        
        def description(self):
            return self._base_description

        def run(self, perftools, args):
            args['name'] = self._base_name
            res = getattr(perftools, self._method_name)(self._base_name, nb_threads=args['nb_threads']);
            print (res['pretty'])

        @staticmethod
        def initTester(args):
            os.environ["NSSLAPD_DB_LIB"] = args['db_lib']
            perftools = PerformanceTools( args )
            perftools.initInstance()
            return perftools;

    class TesterImportExport(Tester):
        # A special tester for export/import
        def __init__(self):
            super().__init__("export/import", 
                "Measure export rate in entries per seconds then measure import rate.",
                 None)

        def argsused(self):
            return []
        
        def run(self, perftools, args=None):
            res = perftools.mesure_export_import()
            for r in res:
                print (r['pretty'])

    @staticmethod
    def listTests():
        # List of test for which args.nb_threads is useful
        return { t.name() :  t for t in [ 
            PerformanceTools.Tester("search_uid", "Measure number of searches per seconds using filter with random existing uid.", "measure_search_by_uid"),
            PerformanceTools.Tester("search_uid_in_dn", "Measure number of searches per seconds using filter with random existing uid in dn (i.e: (uid:dn:uid_value)).", "measure_search_by_filtering_the_dn"),
            PerformanceTools.Tester("modify_sn", "Measure number of modify per seconds replacing sn by random value on random entries.", "measure_modify"),
            PerformanceTools.TesterImportExport(),
        ] }

    @staticmethod
    def runAllTests(options):
        for users in ( 100, 1000, 10000, 100000, 1000000 ):
            for db in ( 'bdb', 'mdb' ):
                perftools = PerformanceTools.Tester.initTester({**options, 'nbUsers': users, 'db_lib': db})
                for t in PerformanceTools.listTests().values():
                    if 'nb_threads' in t.argsused():
                        for nbthreads in ( 1, 4, 8 ):
                            t.run(perftools, { "nb_threads" : nbthreads })
                    else:
                        t.run(perftools)


