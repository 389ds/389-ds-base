import ldif
import re
from ldap.cidict import cidict
import cStringIO

import logging
logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger(__name__)

class FormatDict(cidict):
    def __getitem__(self, name):
        if name in self:
            return ' '.join(cidict.__getitem__(self, name))
        return None
            

class Entry(object):
    """This class represents an LDAP Entry object.

        An LDAP entry consists of a DN and a list of attributes.
        Each attribute consists of a name and a *list* of values.
        String values will be rendered badly!
            ex. {
                'uid': ['user01'],
                'cn': ['User'],
                'objectlass': [ 'person', 'inetorgperson' ]
             }

        In python-ldap, entries are returned as a list of 2-tuples.
        Instance variables:
          dn - string - the string DN of the entry
          data - cidict - case insensitive dict of the attributes and values
    """
    # the ldif class base64 encodes some attrs which I would rather see in raw form - to
    # encode specific attrs as base64, add them to the list below
    ldif.safe_string_re = re.compile('^$')
    base64_attrs = ['nsstate']

    def __init__(self, entrydata):
        """entrydata is the raw data returned from the python-ldap
        result method, which is:
            * a search result entry     -> (dn, {dict...} )
            * or a reference            -> (None, reference)
            * or None.

        If creating a new empty entry, data is the string DN.
        """
        self.ref = None
        if entrydata:
            if isinstance(entrydata, tuple):
                if entrydata[0] is None:
                    self.ref = entrydata[1]  # continuation reference
                else:
                    self.dn = entrydata[0]
                    self.data = cidict(entrydata[1])
            elif isinstance(entrydata, basestring):
                if not '=' in entrydata:
                    raise ValueError('Entry dn must contain "="')
                    
                self.dn = entrydata
                self.data = cidict()
        else:
            #
            self.dn = ''
            self.data = cidict()

    def __nonzero__(self):
        """This allows us to do tests like if entry: returns false if there is no data,
        true otherwise"""
        return self.data is not None and len(self.data) > 0

    def hasAttr(self, name):
        """Return True if this entry has an attribute named name, False otherwise"""
        return self.data and name in self.data

    def __getattr__(self, name):
        """If name is the name of an LDAP attribute, return the first value for that
        attribute - equivalent to getValue - this allows the use of
            entry.cn
        instead of
            entry.getValue('cn')
        This also allows us to return None if an attribute is not found rather than
        throwing an exception"""
        if name == 'dn' or name == 'data':
            return self.__dict__.get(name, None)
        return self.getValue(name)

    def getValues(self, name):
        """Get the list (array) of values for the attribute named name"""
        return self.data.get(name, [])

    def getValue(self, name):
        """Get the first value for the attribute named name"""
        return self.data.get(name, [None])[0]

    def hasValue(self, name, val=None):
        """True if the given attribute is present and has the given value
        
            TODO: list comparison preserves order: should I use a set?
        """
        if not self.hasAttr(name):
            return False
        if not val:
            return True
        if isinstance(val, list):
            return val == self.data.get(name)
        if isinstance(val, tuple):
            return list(val) == self.data.get(name)
        return val in self.data.get(name)

    def hasValueCase(self, name, val):
        """True if the given attribute is present and has the given value - case insensitive value match"""
        if not self.hasAttr(name):
            return False
        return val.lower() in [x.lower() for x in self.data.get(name)]

    def setValue(self, name, *value):
        """Value passed in may be a single value, several values, or a single sequence.
        For example:
           ent.setValue('name', 'value')
           ent.setValue('name', 'value1', 'value2', ..., 'valueN')
           ent.setValue('name', ['value1', 'value2', ..., 'valueN'])
           ent.setValue('name', ('value1', 'value2', ..., 'valueN'))
        Since *value is a tuple, we may have to extract a list or tuple from that
        tuple as in the last two examples above"""
        if isinstance(value[0], list) or isinstance(value[0], tuple):
            self.data[name] = value[0]
        else:
            self.data[name] = value

    def getAttrs(self):
        if not self.data:
            return []
        return self.data.keys()

    def iterAttrs(self, attrsOnly=False):
        if attrsOnly:
            return self.data.iterkeys()
        else:
            return self.data.iteritems()

    setValues = setValue

    def toTupleList(self):
        """Convert the attrs and values to a list of 2-tuples.  The first element
        of the tuple is the attribute name.  The second element is either a
        single value or a list of values."""
        return self.data.items()

    def getref(self):
        return self.ref

    def __str__(self):
        """Convert the Entry to its LDIF representation"""
        return self.__repr__()

    def update(self, dct):
        """Update passthru to the data attribute."""
        log.debug("update dn: %r with %r" % (self.dn, dct))
        for k, v in dct.items():
            if hasattr(v, '__iter__'):
                self.data[k] = v
            else:
                self.data[k] = [v]

    def __repr__(self):
        """Convert the Entry to its LDIF representation"""
        sio = cStringIO.StringIO()
        # what's all this then?  the unparse method will currently only accept
        # a list or a dict, not a class derived from them.  self.data is a
        # cidict, so unparse barfs on it.  I've filed a bug against python-ldap,
        # but in the meantime, we have to convert to a plain old dict for printing
        # I also don't want to see wrapping, so set the line width really high (1000)
        newdata = {}
        newdata.update(self.data)
        ldif.LDIFWriter(
            sio, Entry.base64_attrs, 1000).unparse(self.dn, newdata)
        return sio.getvalue()
