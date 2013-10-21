from bug_harness import DSAdminHarness as DSAdmin
from dsadmin import Entry
from dsadmin.tools import DSAdminTools
"""
    An harness for bug replication.

"""
import os

REPLBINDDN = ''
REPLBINDPW = ''


@static_var("REPLICAID", 1)
def get_next_replicaid(replica_id=None, replica_type=None):
    if replica_id:
        REPLICAID = replica_id
        return REPLICAID
    # get a default replica_id if it's a MASTER,
    # or 0 if consumer
    if replica_type == MASTER_TYPE:
        REPLICAID += 1
        return REPLICAID

    return 0


class DSAdminHarness(DSAdmin, DSAdminTools):
    """Harness wrapper around dsadmin.

       Specialize the DSAdmin behavior (No, I don't care about Liskov ;))
    """
    def setupSSL(self, secport, sourcedir=os.environ['SECDIR'], secargs=None):
        """Bug scripts requires SECDIR."""
        return DSAdminTools.setupSSL(self, secport, sourcedir, secargs)

    def setupAgreement(self, repoth, args):
        """Set default replia credentials """
        args.setdefault('binddn', REPLBINDDN)
        args.setdefault('bindpw', REPLBINDPW)

        return DSAdmin.setupAgreement(self, repoth, args)

    def setupReplica(self, args):
        """Set default replia credentials """
        args.setdefault('binddn', REPLBINDDN)
        args.setdefault('bindpw', REPLBINDPW)
        # manage a progressive REPLICAID
        args.setdefault(
            'id', get_next_replicaid(args.get('id'), args.get('type')))
        return DSAdmin.setupReplica(self, args)

    def setupBindDN(self, binddn=REPLBINDDN, bindpw=REPLBINDPW):
        return DSAdmin.setupBindDN(self, binddn, bindpw)

    def setupReplBindDN(self, binddn=REPLBINDDN, bindpw=REPLBINDPW):
        return self.setupBindDN(binddn, bindpw)

    def setupBackend(self, suffix, binddn=None, bindpw=None, urls=None, attrvals=None, benamebase=None, verbose=False):
        """Create a backends using the first available cn."""
        # if benamebase is set, try creating without appending
        if benamebase:
            benum = 0
        else:
            benum = 1

        # figure out what type of be based on args
        if binddn and bindpw and urls:  # its a chaining be
            benamebase = benamebase or "chaindb"
        else:  # its a ldbm be
            benamebase = benamebase or "localdb"

        done = False
        while not done:
            # if benamebase is set, benum starts at 0
            # and the first attempt tries to create the
            # simple benamebase. On failure benum is
            # incremented and the suffix is appended
            # to the cn
            if benum:
                benamebase_tmp = benamebase + str(benum)  # e.g. localdb1
            else:
                benamebase_tmp = benamebase

            try:
                cn = DSAdmin.setupBackend(suffix, binddn, bindpw,
                                          urls, attrvals, benamebase, verbose)
                done = True
            except ldap.ALREADY_EXISTS:
                benum += 1

        return cn


    def createInstance(args):
        # eventually set prefix
        args.setdefault('prefix', os.environ.get('PREFIX', None))
        args.setdefault('sroot', os.environ.get('SERVER_ROOT', None))
        DSAdminTools.createInstance(args)

