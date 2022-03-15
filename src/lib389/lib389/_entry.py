# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import io
import logging
import ldif
import ldap
import binascii
from ldap.cidict import cidict
import sys

from lib389.exceptions import MissingEntryError
from lib389._constants import *
from lib389.properties import *
from lib389.utils import (ensure_str, ensure_bytes, ensure_list_bytes, display_log_data)

MAJOR, MINOR, _, _, _ = sys.version_info

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
    # the ldif class base64 encodes some attrs which I would rather see in raw
    # form - to encode specific attrs as base64, add them to the list below
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
            elif isinstance(entrydata, str):
                if '=' not in entrydata:
                    raise ValueError('Entry dn must contain "="')
                self.dn = entrydata
                self.data = cidict()
        else:
            self.data = cidict()
            self.dn = None

    def __bool__(self):
        """
        This allows us to do tests like if entry: returns false if there
        is no data, true otherwise
        """
        return self.data is not None and len(self.data) > 0

    def __eq__(self, other):
        """
        Compare if two Entry objects are the same.

        This is good for fast comparison of the data we have,
        but it relies on a few things.

        Each Entry must have been searched and retrieved with the
        same filter and attribute requirements.
        If that isn't the case, this will immediately fail.
        """
        # We could make this "strict" by forcing this to pull back all
        # the attributes of the DN, and the nsUniqueID.
        # Guard from accidents
        if not isinstance(other, Entry):
            return False
        # Check that our DN is the same.
        if self.dn != other.dn:
            return False
        # Bail fast if the keys don't match
        if set(self.getAttrs()) != set(other.getAttrs()):
            return False
        # Check the values of each key
        for key in self.getAttrs():
            if set(self.getValues(key)) != set(other.getValues(key)):
                return False
        return True

    def __ne__(self, other):
        """
        Compare if two Entry objects are the same.

        See __eq__ for a description of limitations.
        """
        return not self.__eq__(other)

    def hasAttr(self, name):
        """
        Return True if this entry has an attribute named name, False otherwise
        """
        if self.data is None:
            # Perhaps this should be an exception?
            raise Exception('Invalid data state in Entry')
        # We can't actually enforce this because cidict doesn't inherit Mapping
        # if not isinstance(self.data, collections.Mapping):
        #     raise Exception('Invalid data type for Entry')
        return ensure_str(name) in self.data

    def __getitem__(self, name):
        # This should probably return getValues?
        return self.__getattr__(name)

    def __getattr__(self, name):
        """
        If name is the name of an LDAP attribute, return the first
        value for that attribute - equivalent to getValue - this allows
        the use of
            entry.cn
        instead of
            entry.getValue('cn')
        This also allows us to return None if an attribute is not found
        rather than throwing an exception
        """
        if name == 'dn' or name == 'data':
            return self.__dict__.get(name, None)
        return self.getValue(name)

    def getValuesSet(self, keys):
        """
        returns a set of values based on keys
        """
        result = {}
        for k in keys:
            result[k] = self.data.get(k, [])
        return result

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
        return ensure_bytes(val) in self.data.get(name)

    def hasValueCase(self, name, val):
        """
        True if the given attribute is present and has the given value -
        ase insensitive value match
        """
        if not self.hasAttr(name):
            return False
        return val.lower() in [x.lower() for x in self.data.get(name)]

    def setValue(self, name, *value):
        """
        Value passed in may be a single value, several values,
         or a single sequence.
        For example:
           ent.setValue('name', 'value')
           ent.setValue('name', 'value1', 'value2', ..., 'valueN')
           ent.setValue('name', ['value1', 'value2', ..., 'valueN'])
           ent.setValue('name', ('value1', 'value2', ..., 'valueN'))
        Since *value is a tuple, we may have to extract a list or tuple
        from that tuple as in the last two examples above
        """
        if isinstance(value[0], list) or isinstance(value[0], tuple):
            self.data[name] = value[0]
        else:
            self.data[name] = value

    def getAttrs(self):
        if not self.data:
            return []
        return list(self.data.keys())

    def iterAttrs(self, attrsOnly=False):
        if attrsOnly:
            return self.data.keys()
        else:
            return self.data.items()

    setValues = setValue

    def toTupleList(self):
        """
        Convert the attrs and values to a list of 2-tuples.  The first
        element of the tuple is the attribute name.  The second element
        is either a single value or a list of values.
        """
        # For python3, we have to make sure EVERYTHING is a byte string.
        # Else everything EXPLODES
        lt = None
        if MAJOR >= 3:
            # This converts the dict to a list of tuples,
            lt = []
            for k in self.data.keys():
                # l here is the
                vals = None
                if isinstance(self.data[k], list) or isinstance(self.data[k], tuple):
                    vals = ensure_list_bytes(self.data[k])
                else:
                    vals = ensure_list_bytes([self.data[k]])
                lt.append((k, vals))
            # lt is now complete.
        else:
            lt = list(self.data.items())
        return lt

    def getref(self):
        return self.ref

    def __str__(self):
        """
        Convert the Entry to its LDIF representation
        """
        return self.__repr__()

    def update(self, dct):
        """Update passthru to the data attribute."""
        log.debug("updating dn: {}".format(self.dn))
        for k, v in list(dct.items()):
            if isinstance(v, list) or isinstance(v, tuple):
                self.data[k] = v
            else:
                self.data[k] = [v]
        log.debug("updated dn: {} with {}".format(self.dn, display_log_data(dct)))

    def __repr__(self):
        """Convert the Entry to its LDIF representation"""
        sio = io.StringIO()
        """
        what's all this then?  the unparse method will currently only accept
        a list or a dict, not a class derived from them.  self.data is a
        cidict, so unparse barfs on it.  I've filed a bug against
        python-ldap, but in the meantime, we have to convert to a plain old
        dict for printing.
        I also don't want to see wrapping, so set the line width really high
        (1000)
        """
        newdata = {}
        for k, v in self.data.items():
            newdata[k] = ensure_list_bytes(v)

        ldif.LDIFWriter(
            sio, Entry.base64_attrs, 1000).unparse(self.dn, newdata)
        return sio.getvalue()

    def bin2b64(self):
        """
        Convert any binary values in the entry to base 64
        """
        for attr, vals in self.iterAttrs():
            attr_vals = []
            for val in vals:
                try:
                    val.decode('ascii')
                    attr_vals.append(val)
                except:
                    # We have a binary value we need to convert
                    b64 = binascii.b2a_base64(val)
                    if b64[-1] == '\n':
                        # Strip off the newline
                        b64 = b64[:-1]

                    attr_vals.append(b64)

            self.data[attr] = attr_vals

    def getJSONEntry(self):
        """
        Return a JSON dictionary representation of the entry:

            dn - Entry DN
            attrs - entry's attributes

            {
                "dn": "uid=user,dc=example,dc=com",
                "attrs": {
                    "objectClass": [
                        "top",
                        "person",
                        "organizationalPerson",
                        "inetorgperson",
                        "inetuser"
                    ],
                    "givenName": [
                        "user"
                    ],
                    "uid": [
                        "user"
                    ],
                    "sn": [
                        "user lastname"
                    ],
                    "cn": [
                        "user name"
                    ]
                }
            }
        """
        # Need to convert binary values to base64 for our JSON representations
        self.bin2b64()

        entry = {'dn': self.dn, 'attrs': dict(self.data)}
        return entry

    def create(self, type=ENTRY_TYPE_PERSON, entry_dn=None, properties=None):
        """
        Return - eventually creating a person entry with the given dn and
                 pwd. binddn can be a lib389.Entry
        """
        if not entry_dn:
            raise ValueError("entry_dn is mandatory")

        if not type:
            raise ValueError("type is mandatory")

        ent = Entry(entry_dn)
        ent.setValues(ENTRY_OBJECTCLASS, ENTRY_TYPE_TO_OBJECTCLASS[type])
        ent.setValues(ENTRY_USERPASSWORD, properties.get(ENTRY_USERPASSWORD,
                                                         ""))
        ent.setValues(ENTRY_SN, properties.get(ENTRY_SN,
                                               "bind dn pseudo user"))
        ent.setValues(ENTRY_CN, properties.get(ENTRY_CN,
                                               "bind dn pseudo user"))
        if type == ENTRY_TYPE_INETPERSON:
            ent.setValues(ENTRY_UID, properties.get(ENTRY_UID,
                                                    "bind dn pseudo user"))

        try:
            self.add_s(ent)
        except ldap.ALREADY_EXISTS:
            log.warning("Entry %s already exists" % entry_dn)

        try:
            entry = self._test_entry(entry_dn, ldap.SCOPE_BASE)
            return entry
        except MissingEntryError:
            log.exception("This entry should exist!")
            raise

    def getAcis(self):
        if not self.hasAttr('aci'):
            # There should be a better way to do this? Perhaps
            # self search for the aci attr?
            return []
        self.acis = [EntryAci(self, a, verbose=False) for a in self.getValues('aci')]

        return self.acis


class EntryAci(object):
    """Breaks down an aci attribute string from 389, into a dictionary
    of terms and values. These values can then be manipulated, and
    subsequently rebuilt into an aci string.

    :param entry: An entry
    :type entry: lib389._entry.Entry
    :param rawaci: Aci in a raw form
    :type rawaci: str
    :param verbose: False by default
    :type verbose: bool
    """

    # We seperate the keys into 3 groups, and one group that has overlap.
    # This is so we can not only split the aci, but rebuild it from the
    # dictionary at a later point in time.

    # These are top level aci comoponent keys
    _keys = ['targetscope',
             'targetattrfilters',
             'targattrfilters',
             'targetfilter',
             'targetattr',
             'target',
             'version 3.0;']
    # These are the keys which are seperated by ; in the version 3.0 stanza.
    _v3keys = ['allow',
               'acl',
               'deny']
    # These are the keys which are used on the inside of a v3 allow statement
    # We have them defined, but don't currently use them.
    _v3innerkeys = ['roledn',
                    'userattr',
                    'ip',
                    'dns',
                    'dayofweek',
                    'timeofday',
                    'authmethod',
                    'userdn',
                    'groupdn']
    # These keys values are prefixed with ldap:///, so we need to know to
    # re-prefix ldap:/// onto the value when we rebuild the aci
    _urlkeys = ['target',
                'userdn',
                'groupdn',
                'roledn']

    def __init__(self, entry, rawaci, verbose=False):
        """
        """
        self.verbose = verbose
        self.entry = entry
        self._rawaci = ensure_str(rawaci)
        self.acidata = self._parse_aci(self._rawaci)

    def __eq__(self, other):

        # If we are from different entries, we should fail
        # Can this fail if we have different filters to get the acis though?
        if self.entry.dn != other.entry.dn:
            return False
        # Should we be checking the generated Aci incase it has changed?
        return self.getRawAci() == other.getRawAci()

    def __ne__(self, other):
        return not self.__eq__(other)

    def _format_term(self, key, value_dict):
        rawaci = ''
        if value_dict.get('equal', True):
            rawaci += '="'
        else:
            rawaci += '!="'
        if key in self._urlkeys:
            values = ['ldap:///%s' % x for x in value_dict['values']]
        else:
            values = value_dict['values']
        for value in values[:-1]:
            rawaci += "%s || " % value
        rawaci += values[-1]
        rawaci += '"'
        if self.verbose:
            print("_format_term: %s" % rawaci)
        return rawaci

    def getRawAci(self):
        """This method will rebuild an aci from the contents of the acidata
        dict found on the object.

        :returns: An aci attribute string.
        """

        # Rebuild the aci from the .acidata.
        rawaci = ''
        # For each key in the outer segment
        # Add a (key = val);. Depending on key format val:
        for key in self._keys:
            for value_dict in self.acidata[key]:
                rawaci += '(%s %s)' % (key, self._format_term(key, value_dict))
        # Now create the v3.0 aci part
        rawaci += "(version 3.0; "
        # This could be neater ...
        rawaci += 'acl "%s";' % self.acidata['acl'][0]['values'][0]
        for key in ['allow', 'deny']:
            if len(self.acidata[key]) > 0:
                rawaci += '%s (' % key
                for value in self.acidata[key][0]['values'][:-1]:
                    rawaci += '%s, ' % value
                rawaci += '%s)' % self.acidata[key][0]['values'][-1]
                rawaci += ('(%s);' % self.acidata["%s_raw_bindrules" %
                                                  key][0]['values'][-1])
        rawaci += ")"
        return ensure_bytes(rawaci)

    def _normalize_term(self, term):
        """Normalize the term, remove duplicate spaces around sensitive keywords,
        and remove spaces around "version 3.0 ;"
        :retuns: normalized term string
        """

        # First reduce multiple consecutive spaces
        new_term = ' '.join(term.split())

        # Our parsing of version 3.0; is strict, need to take special care
        if new_term.lower().startswith('version 3.0'):
            parts = new_term.split(';', 1)
            new_term = parts[0].rstrip() + ';' + parts[1]
        return new_term

    def _find_terms(self, aci):
        if self.verbose:
            print("_find_terms aci: %s" % aci)
            print("_find_terms aci: %s" % type(aci))
        lbr_list = []
        rbr_list = []
        depth = 0
        for i, char in enumerate(aci):
            if char == '(' and depth == 0:
                lbr_list.append(i)
            if char == '(':
                depth += 1
            if char == ')' and depth == 1:
                rbr_list.append(i)
            if char == ')':
                depth -= 1
        # Now build a set of terms.
        terms = []
        if self.verbose:
            print("_find_terms lbr_list" % lbr_list)
            print("_find_terms rbr_list" % rbr_list)
        for lb, rb in zip(lbr_list, rbr_list):
            terms.append(self._normalize_term(aci[lb + 1:rb]))
        if self.verbose:
            print("_find_terms terms: %s" % terms)
        return terms

    def _parse_term(self, key, term):
        wdict = {'values': [], 'equal': True}
        # Nearly all terms are = seperated
        # We make a dict that holds "equal" and an array of values
        pre, val = term.split('=', 1)
        val = val.replace('"', '')
        if pre.strip() == '!':
            wdict['equal'] = False
        else:
            wdict['equal'] = True
        wdict['values'] = val.split('||')
        if key in self._urlkeys:
            # We could replace ldap:/// in some attrs?
            wdict['values'] = ([x.replace('ldap:///', '')
                               for x in wdict['values']])
        wdict['values'] = [x.strip() for x in wdict['values']]

        if self.verbose:
            print("_parse_term: %s" % wdict)
        return wdict

    def _parse_bind_rules(self, subterm):
        # First, determine if there are extraneous braces wrapping the term.
        subterm = subterm.strip()
        if subterm[0] == '(' and subterm[-1] == ')':
            subterm = subterm[1:-1]
        # terms = subterm.split('and')
        """
        We could parse everything into nice structures, and then work with
        them.  Or we can just leave the bind rule alone, as a string. Let
        the human do it.  It comes down to cost versus reward.
        """

        if self.verbose:
            print("_parse_bind_rules: %s" % subterm)

        return [subterm]

    def _parse_version_3_0(self, rawacipart, data):
        # We have to do this because it's not the same as other term formats.
        terms = []
        # bindrules = []
        interms = rawacipart.split(';')
        interms = [x.strip() for x in interms]
        for iwork in interms:
            for j in self._v3keys + self._v3innerkeys:
                if iwork.startswith(j) and j == 'acl':
                    t = iwork.split(' ', 1)[1]
                    t = t.replace('"', '')
                    data[j].append({'values': [t]})
                if iwork.startswith(j) and (j == 'allow' or j == 'deny'):
                    first = iwork.index('(') + 1
                    second = iwork.index(')', first)
                    # This could likely be neater ...
                    data[j].append(
                        {'values': [x.strip()
                         for x in iwork[first:second].split(',')]})
                    subterm = iwork[second + 1:]
                    data["%s_raw_bindrules" % j].append({
                        'values': self._parse_bind_rules(subterm)
                    })

        if self.verbose:
            print("_parse_version_3_0: %s" % terms)
        return terms

    def _parse_aci(self, rawaci):
        aci = rawaci
        # depth = 0
        data = {
            'rawaci': rawaci,
            'allow_raw_bindrules': [],
            'deny_raw_bindrules': [],
            }
        for k in self._keys + self._v3keys:
            data[k] = []
        # We need to get a list of all the depth 0 ( and )
        terms = self._find_terms(aci)

        while len(terms) > 0:
            work = terms.pop()
            for k in self._keys + self._v3keys + self._v3innerkeys:
                if work.startswith(k):
                    aci = work.replace(k, '', 1)
                    if k == 'version 3.0;':
                        """
                        We pop more inner terms out, but we don't need to
                        parse them "now" they get added to the queue
                        """
                        terms += self._parse_version_3_0(aci, data)
                        continue
                    data[k].append(self._parse_term(k, aci))
                    break
        if self.verbose:
            print("_parse_aci: %s" % data)
        return data
