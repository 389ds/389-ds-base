__all__ = ['LDIFConn']
import ldif
from lib389._entry import Entry
from lib389.utils import normalizeDN

class LDIFConn(ldif.LDIFParser):
    def __init__(
        self,
        input_file,
        ignored_attr_types=None, max_entries=0, process_url_schemes=None
    ):
        """
        See LDIFParser.__init__()

        Additional Parameters:
        all_records
        List instance for storing parsed records
        """
        self.dndict = {}  # maps dn to Entry
        self.dnlist = []  # contains entries in order read
        myfile = input_file
        if isinstance(input_file, basestring):
            myfile = open(input_file, "r")
        ldif.LDIFParser.__init__(self, myfile, ignored_attr_types,
                                 max_entries, process_url_schemes)
        self.parse()
        if isinstance(input_file, basestring):
            myfile.close()

    def handle(self, dn, entry):
        """
        Append single record to dictionary of all records.
        """
        if not dn:
            dn = ''
        newentry = Entry((dn, entry))
        self.dndict[normalizeDN(dn)] = newentry
        self.dnlist.append(newentry)

    def get(self, dn):
        ndn = normalizeDN(dn)
        return self.dndict.get(ndn, Entry(None))

