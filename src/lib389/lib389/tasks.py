'''
Created on Feb 10, 2014

@author: tbordaz
'''
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
import ldap
import time

class Tasks(object):
    proxied_methods = 'search_s getEntry'.split()

    def __init__(self, conn):
        """@param conn - a DirSrv instance"""
        self.conn = conn
        self.log = conn.log

    def __getattr__(self, name):
        if name in Tasks.proxied_methods:
            return DirSrv.__getattr__(self.conn, name)

    def checkTask(self, entry, dowait=False):
        '''check task status - task is complete when the nsTaskExitCode attr is set
        return a 2 tuple (true/false,code) first is false if task is running, true if
        done - if true, second is the exit code - if dowait is True, this function
        will block until the task is complete'''
        attrlist = ['nsTaskLog', 'nsTaskStatus', 'nsTaskExitCode',
                    'nsTaskCurrentItem', 'nsTaskTotalItems']
        done = False
        exitCode = 0
        dn = entry.dn
        while not done:
            entry = self.conn.getEntry(dn, attrlist=attrlist)
            self.log.debug("task entry %r" % entry)

            if entry.nsTaskExitCode:
                exitCode = int(entry.nsTaskExitCode)
                done = True
            if dowait:
                time.sleep(1)
            else:
                break
        return (done, exitCode)

    def importLDIF(self, suffix=None, benamebase=None, input_file=None, args=None):
        '''
        Import from a LDIF format a given 'suffix' (or 'benamebase' that stores that suffix). 
        It uses an internal task to acheive this request.

        If 'suffix' and 'benamebase' are specified, it uses 'benamebase' first else 'suffix'. 
        If both 'suffix' and 'benamebase' are missing it raise ValueError

        'input_file' is the ldif input file

        @param suffix - suffix of the backend
        @param benamebase - 'commonname'/'cn' of the backend (e.g. 'userRoot')
        @param ldif_input - file that will contain the entries in LDIF format, to import
        @param args - is a dictionary that contains modifier of the import task
                wait: True/[False] - If True, 'export' waits for the completion of the task before to return
        
        @return None
        
        @raise ValueError

        '''
        # Checking the parameters
        if not benamebase and not suffix:
            raise ValueError("Specify either bename or suffix")
        
        if not input_file:
            raise ValueError("input_file is mandatory")
        
        # Prepare the task entry
        cn = "import" + str(int(time.time()))
        dn = "cn=%s,%s" % (cn, DN_IMPORT_TASK)
        entry = Entry(dn)
        entry.setValues('objectclass', 'top', 'extensibleObject')
        entry.setValues('cn', cn)
        entry.setValues('nsFilename', input_file)
        if benamebase:
            entry.setValues('nsInstance', benamebase)
        else:
            entry.setValues('nsIncludeSuffix', suffix)

        # start the task and possibly wait for task completion
        try:
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS:
            self.log.error("Fail to add the import task of %s" % input_file)
            return -1
        
        exitCode = 0
        if args and args.get(TASK_WAIT, False):
            (done, exitCode) = self.conn.tasks.checkTask(entry, True)

        if exitCode:
            self.log.error("Error: import task %s for file %s exited with %d" % (
                    cn, input_file, exitCode))
        else:
            self.log.info("Import task %s for file %s completed successfully" % (
                    cn, input_file))
        return exitCode

    def exportLDIF(self, suffix=None, benamebase=None, output_file=None, args=None):
        '''
        Export in a LDIF format a given 'suffix' (or 'benamebase' that stores that suffix). 
        It uses an internal task to acheive this request.

        If 'suffix' and 'benamebase' are specified, it uses 'benamebase' first else 'suffix'. 
        If both 'suffix' and 'benamebase' are missing it raises ValueError

        'output_file' is the output file of the export

        @param suffix - suffix of the backend
        @param benamebase - 'commonname'/'cn' of the backend (e.g. 'userRoot')
        @param output_file - file that will contain the exported suffix in LDIF format
        @param args - is a dictionary that contains modifier of the export task
                wait: True/[False] - If True, 'export' waits for the completion of the task before to return
                repl-info: True/[False] - If True, it adds the replication meta data (state information, tombstones and RUV) in the exported file
        
        @return None
        
        @raise ValueError

        '''
        
        # Checking the parameters
        if not benamebase and not suffix:
            raise ValueError("Specify either bename or suffix")
        
        if not output_file:
            raise ValueError("output_file is mandatory")
        
        # Prepare the task entry
        cn = "export%d" % time.time()
        dn = "cn=%s,%s" % (cn, DN_EXPORT_TASK)
        entry = Entry(dn)
        entry.update({
            'objectclass': ['top', 'extensibleObject'],
            'cn': cn,
            'nsFilename': output_file
        })
        if benamebase:
            entry.setValues('nsInstance', benamebase)
        else:
            entry.setValues('nsIncludeSuffix', suffix)
            
        if args.get(EXPORT_REPL_INFO, False):
            entry.setValues('nsExportReplica', 'true')

        # start the task and possibly wait for task completion
        self.conn.add_s(entry)
        exitCode = 0
        if args and args.get(TASK_WAIT, False):
            (done, exitCode) = self.conn.tasks.checkTask(entry, True)
            

        if exitCode:
            self.log.error("Error: export task %s for file %s exited with %d" % (
                    cn, output_file, exitCode))
        else:
            self.log.info("Export task %s for file %s completed successfully" % (
                    cn, output_file))
        return exitCode

    
    def reindex(self, suffix=None, benamebase=None, attrname=None, args=None):
        '''
        Reindex a 'suffix' (or 'benamebase' that stores that suffix) for a given 'attrname'. 
        It uses an internal task to acheive this request.

        If 'suffix' and 'benamebase' are specified, it uses 'benamebase' first else 'suffix'. 
        If both 'suffix' and 'benamebase' are missing it raise ValueError
        
        @param suffix - suffix of the backend
        @param benamebase - 'commonname'/'cn' of the backend (e.g. 'userRoot')
        @param attrname - attribute name
        @param args - is a dictionary that contains modifier of the reindex task
                wait: True/[False] - If True, 'index' waits for the completion of the task before to return
        
        @return None
        
        @raise ValueError if invalid missing benamebase and suffix or invalid benamebase

        '''
        if not benamebase and not suffix:
            raise ValueError("Specify either bename or suffix")
        
        # If backend name was provided, retrieve the suffix
        if benamebase:
            ents = self.conn.mappingtree.list(bename=benamebase)
            if len(ents) != 1:
                raise ValueError("invalid backend name: %s" % benamebase)
            
            attr_suffix = MT_PROPNAME_TO_ATTRNAME[MT_SUFFIX]
            if not ents[0].hasAttr(attr_suffix):
                raise ValueError("invalid backend name: %s, or entry without %s" % (benamebase, attr_suffix))
            
            suffix = ents[0].getValue(attr_suffix)
            
        entries_backend = self.conn.backend.list(suffix=suffix)
        cn = "index_%s_%d" % (attrname, time.time())
        dn = "cn=%s,%s" % (cn, DN_INDEX_TASK)
        entry = Entry(dn)
        entry.update({
            'objectclass': ['top', 'extensibleObject'],
            'cn': cn,
            'nsIndexAttribute': attrname,
            'nsInstance': entries_backend[0].cn
        })
        # assume 1 local backend
        
        # start the task and possibly wait for task completion
        try:
            self.conn.add_s(entry)
        except ldap.ALREADY_EXISTS:
            self.log.error("Fail to add the index task for %s" % attrname)
            return -1
        
        exitCode = 0
        if args and args.get(TASK_WAIT, False):
            (done, exitCode) = self.conn.tasks.checkTask(entry, True)

        if exitCode:
            self.log.error("Error: index task %s exited with %d" % (
                    cn, exitCode))
        else:
            self.log.info("Index task %s completed successfully" % (
                    cn))
        return exitCode

    def fixupMemberOf(self, suffix=None, benamebase=None, filt=None, args=None):
        '''
            Trigger a fixup task on 'suffix' (or 'benamebase' that stores that suffix) related to the 
            entries 'memberof' of groups. It uses an internal task to acheive this request.
            
            If 'suffix' and 'benamebase' are specified, it uses 'benamebase' first else 'suffix'. 
            If both 'suffix' and 'benamebase' are missing it raise ValueError
            
            'filt' is a filter that will select all the entries (under 'suffix') that we need to evaluate/fix. 
            If missing, the default value is "(|(objectclass=inetuser)(objectclass=inetadmin))"
            
            @param suffix - suffix of the backend
            @param benamebase - 'commonname'/'cn' of the backend (e.g. 'userRoot')
            @param args - is a dictionary that contains modifier of the fixupMemberOf task
                wait: True/[False] - If True,  waits for the completion of the task before to return
        

            @return None
            
            @raise ValueError: if benamebase and suffix are specified, or can not retrieve the suffix from the 
                            mapping tree entry
        '''
        if not benamebase and not suffix:
            raise ValueError("Specify either bename or suffix")
        
        # If backend name was provided, retrieve the suffix
        if benamebase:
            ents = self.conn.mappingtree.list(bename=benamebase)
            if len(ents) != 1:
                raise ValueError("invalid backend name: %s" % benamebase)
            
            attr = MT_PROPNAME_TO_ATTRNAME[MT_SUFFIX]
            if not ents[0].hasAttr(attr):
                raise ValueError("invalid backend name: %s, or entry without %s" % (benamebase, attr))
            
            suffix = ents[0].getValue(attr)
        
        cn = "fixupmemberof%d" % time.time()
        dn = "cn=%s,%s" % (cn, DN_MBO_TASK)
        entry = Entry(dn)
        entry.setValues('objectclass', 'top', 'extensibleObject')
        entry.setValues('cn', cn)
        entry.setValues('basedn', suffix)
        if filt:
            entry.setValues('filter', filt)
        
        # start the task and possibly wait for task completion
        self.conn.add_s(entry)
        exitCode = 0
        if args and args.get(TASK_WAIT, False):
            (done, exitCode) = self.conn.tasks.checkTask(entry, True)
            

        if exitCode:
            self.log.error("Error: fixupMemberOf task %s for basedn %s exited with %d" % (cn, suffix, exitCode))
        else:
            self.log.info("fixupMemberOf task %s for basedn %s completed successfully" % (cn, suffix))
        return exitCode

