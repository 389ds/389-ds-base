# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# Copyright (C) 2019 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import ldap.dn
from ldap.controls import SimplePagedResultsControl
from ldap import filter as ldap_filter
import logging
import json
from functools import partial
from lib389._entry import Entry
from lib389._constants import DIRSRV_STATE_ONLINE
from lib389._mapped_object_lint import DSLint, DSLints
from lib389.utils import (
        ensure_bytes, ensure_str, ensure_int, ensure_list_bytes, ensure_list_str,
        ensure_list_int, display_log_value, display_log_data
        )

# This function filter and term generation provided thanks to
# The University of Adelaide. <william@adelaide.edu.au>


def _term_gen(term):
    while True:
        yield term


def _gen(op=None, extra=None):
    filt = ''
    if type(extra) == list:
        for ext in extra:
            filt += ext
    elif type(extra) == str:
        filt += extra
    if filt != '':
        filt = '(%s%s)' % (op, filt)
    return filt


def _gen_and(extra=None):
    return _gen('&', extra)


def _gen_or(extra=None):
    return _gen('|', extra)


def _gen_not(extra=None):
    return _gen('!', extra)


def _gen_filter(attrtypes, values, extra=None):
    filt = ''
    if attrtypes is None:
        raise ValueError("Attempting to filter on type that doesn't support filtering!")
    for attr, value in zip(attrtypes, values):
        if attr is not None and value is not None:
            filt += '(%s=%s)' % (attr, ldap_filter.escape_filter_chars(value))
    if extra is not None:
        filt += '{FILT}'.format(FILT=extra)
    return filt


# Define wrappers around the ldap operation to have a clear diagnostic
def _ldap_op_s(inst, f, fname, *args, **kwargs):
    # f.__name__ says 'inner' so the wanted name is provided as argument
    try:
        return f(*args, **kwargs)
    except ldap.LDAPError as e:
        new_desc = f"{fname}({args},{kwargs}) on instance {inst.serverid}";
        if len(e.args) >= 1:
            e.args[0]['ldap_request'] = new_desc
            logging.getLogger().debug(f"args={e.args}")
        raise e

def _add_ext_s(inst, *args, **kwargs):
    return _ldap_op_s(inst, inst.add_ext_s, 'add_ext_s', *args, **kwargs)

def _modify_ext_s(inst, *args, **kwargs):
    return _ldap_op_s(inst, inst.modify_ext_s, 'modify_ext_s', *args, **kwargs)

def _delete_ext_s(inst, *args, **kwargs):
    return _ldap_op_s(inst, inst.delete_ext_s, 'delete_ext_s', *args, **kwargs)

def _search_ext_s(inst, *args, **kwargs):
    return _ldap_op_s(inst, inst.search_ext_s, 'search_ext_s', *args, **kwargs)

def _search_s(inst, *args, **kwargs):
    return _ldap_op_s(inst, inst.search_s, 'search_s', *args, **kwargs)


class DSLogging(object):
    """The benefit of this is automatic name detection, and correct application
    of level and verbosity to the object.

    :param verbose: False by default
    :type verbose: bool
    """

    def __init__(self, verbose=False):
        # Maybe we can think of a way to make this display the instance name or __unicode__?
        self._log = logging.getLogger(type(self).__name__)
        if verbose:
            self._log.setLevel(logging.DEBUG)
        else:
            self._log.setLevel(logging.INFO)


class DSLdapObject(DSLogging, DSLint):
    """A single instance of DSLdapObjects

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    # TODO: Automatically create objects when they are requested to have properties added
    def __init__(self, instance, dn=None):
        self._instance = instance
        super(DSLdapObject, self).__init__(self._instance.verbose)
        # This allows some factor objects to be overriden
        self._dn = None
        if dn is not None:
            self._dn = ensure_str(dn)

        self._protected = True
        # Used in creation
        self._create_objectclasses = []
        self._rdn_attribute = None
        self._must_attributes = None
        # attributes, we don't want to compare
        self._compare_exclude = ['entryid', 'modifytimestamp', 'nsuniqueid']
        self._server_controls = None
        self._client_controls = None
        self._object_filter = '(objectClass=*)'

    def __unicode__(self):
        val = self._dn
        if self._rdn_attribute:
            # What if the rdn is multi value and we don't get the primary .... ARGHHH
            val = self.get_attr_val(self._rdn_attribute)
        return ensure_str(val)

    def __str__(self):
        return self.__unicode__()

    def _unsafe_raw_entry(self):
        """Get an Entry object

        :returns: Entry object
        """

        return _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter, attrlist=["*"],
                                           serverctrls=self._server_controls, clientctrls=self._client_controls,
                                           escapehatch='i am sure')[0]

    def exists(self):
        """Check if the entry exists

        :returns: True if it exists
        """

        try:
            _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter, attrsonly=1,
                                        serverctrls=self._server_controls, clientctrls=self._client_controls,
                                        escapehatch='i am sure')
        except ldap.NO_SUCH_OBJECT:
            return False

        return True

    def search(self, scope="subtree", filter='objectclass=*'):
        search_scope = ldap.SCOPE_SUBTREE
        if scope == 'base':
            search_scope = ldap.SCOPE_BASE
        elif scope == 'one':
            search_scope = ldap.SCOPE_ONE
        elif scope == 'subtree':
            search_scope = ldap.SCOPE_SUBTREE
        return _search_ext_s(self._instance,self._dn, search_scope, filter,
                                           serverctrls=self._server_controls,
                                           clientctrls=self._client_controls,
                                           escapehatch='i am sure')

    def display(self, attrlist=['*']):
        """Get an entry but represent it as a string LDIF

        :returns: LDIF formatted string
        """
        e = _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter, attrlist=attrlist,
                                        serverctrls=self._server_controls, clientctrls=self._client_controls,
                                        escapehatch='i am sure')
        if len(e) > 0:
            return e[0].__repr__()
        else:
            return ""

    def display_attr(self, attr):
        """Get all values of given attribute - 'attr: value'

        :returns: Formatted string
        """

        out = ""
        for v in self.get_attr_vals_utf8(attr):
            out += "%s: %s\n" % (attr, v)
        return out

    def _jsonify(self, fn, *args, **kwargs):
        # This needs to map all the values to ensure_str
        attrs = fn(use_json=True, *args, **kwargs)
        str_attrs = {}
        for k in attrs:
            str_attrs[ensure_str(k)] = ensure_list_str(attrs[k])

        # ensure all the keys are lowercase
        str_attrs = dict((k.lower(), v) for k, v in list(str_attrs.items()))

        response = json.dumps({"type": "entry", "dn": ensure_str(self._dn), "attrs": str_attrs}, indent=4)

        return response

    def __getattr__(self, name):
        """This enables a bit of magic to allow us to wrap any function ending with
        _json to it's form without json, then transformed. It means your function
        *must* return it's values as a dict of:

        { attr : [val, val, ...], attr : [], ... }
        to be supported.
        """

        if (name.endswith('_json')):
            int_name = name.replace('_json', '')
            pfunc = partial(self._jsonify, getattr(self, int_name))
            return pfunc
        else:
            raise AttributeError("'%s' object has no attribute '%s'" % (self.__class__.__name__, name))

    # We make this a property so that we can over-ride dynamically if needed
    @property
    def dn(self):
        """Get an object DN

        :returns: DN
        """

        return self._dn

    @property
    def rdn(self):
        """Get an object RDN

        :returns: RDN
        """

        # How can we be sure this returns the primary one?
        return ensure_str(self.get_attr_val(self._rdn_attribute))

    def get_basedn(self):
        """Get the suffix this entry belongs to
        """
        from lib389.backend import Backends
        backends = Backends(self._instance).list()
        for backend in backends:
            suffix = backend.get_suffix()
            if self._dn.endswith(suffix):
                return suffix
        return ""

    def present(self, attr, value=None):
        """Assert that some attr, or some attr / value exist on the entry.

        :param attr: an attribute name
        :type attr: str
        :param value: an attribute value
        :type value: str

        :returns: True if attr is present
        """

        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get presence on instance that is not ONLINE")
        self._log.debug("%s present(%r) %s" % (self._dn, attr, value))

        _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter, attrlist=[attr, ],
                                        serverctrls=self._server_controls, clientctrls=self._client_controls,
                                        escapehatch='i am sure')[0]
        values = self.get_attr_vals_bytes(attr)
        self._log.debug("%s contains %s" % (self._dn, values))

        if value is None:
            # We are just checking if SOMETHING is present ....
            return len(values) > 0
        else:
            # Check if a value really does exist.
            return ensure_bytes(value).lower() in [x.lower() for x in values]

    def add(self, key, value):
        """Add an attribute with a value

        :param key: an attribute name
        :type key: str
        :param value: an attribute value
        :type value: str
        """

        self.set(key, value, action=ldap.MOD_ADD)

    # Basically what it means;
    def replace(self, key, value):
        """Replace an attribute with a value

        :param key: an attribute name
        :type key: str
        :param value: an attribute value
        :type value: str
        """
        self.set(key, value, action=ldap.MOD_REPLACE)

    def replace_many(self, *args):
        """Replace many key, value pairs in a single operation.
        This is useful for configuration changes that require
        atomic operation, and ease of use.

        An example of usage is replace_many((key, value), (key, value))

        No wrapping list is needed for the arguments.

        :param *args: tuples of key,value to replace.
        :type *args: (str, str)
        """

        mods = []
        for arg in args:
            if isinstance(arg[1], list) or isinstance(arg[1], tuple):
                value = ensure_list_bytes(arg[1])
            else:
                value = [ensure_bytes(arg[1])]
            mods.append((ldap.MOD_REPLACE, ensure_str(arg[0]), value))
        return _modify_ext_s(self._instance,self._dn, mods, serverctrls=self._server_controls,
                                           clientctrls=self._client_controls, escapehatch='i am sure')

    # This needs to work on key + val, and key
    def remove(self, key, value):
        """Remove a value defined by key

        :param key: an attribute name
        :type key: str
        :param value: an attribute value
        :type value: str
        """

        # Do a mod_delete on the value.
        self.set(key, value, action=ldap.MOD_DELETE)

    def remove_all(self, key):
        """Remove all values defined by key (if possible).

        If an attribute is multi-valued AND required all values except one will
        be deleted.

        :param key: an attribute name
        :type key: str
        """

        try:
            self.set(key, None, action=ldap.MOD_DELETE)
        except ldap.NO_SUCH_ATTRIBUTE:
            pass

    def ensure_present(self, attr, value):
        """Ensure that an attribute and value are present in a state,
        or add it.

        :param key: an attribute name
        :type key: str
        :param value: an attribute value
        :type value: str
        """
        if not self.present(attr, value):
            self.add(attr, value)

    def ensure_removed(self, attr, value):
        """Ensure that a attribute and value has been removed and not present
        or remove it.

        :param key: an attribute name
        :type key: str
        :param value: an attribute value
        :type value: str
        """
        if self.present(attr, value):
            self.remove(attr, value)

    def ensure_attr_state(self, state):
        """
        Given a dict of attr-values, ensure they are in the same state on the entry. This is
        a stateful assertion, generally used by things like PATCH in a REST api.

        The format is:
            {
                'attr_1': ['value', 'value'],
                'attr_2': [],
            }

        If a value is present in the list, but not in the entry it is ADDED.
        If a value is NOT present in the list, and is on the entry, it is REMOVED.
        If a value is an empty list [], the attr is REMOVED from the entry.
        If an attr is not named in the dictionary, it is not altered.

        This function is atomic - all changes are applied or none are. There are no
        partial updates.

        This function is idempotent - submitting the same request twice will cause no
        action to be taken as we are ensuring a state, not listing actions to take.

        :param state: The entry ava state
        :type state: dict
        """
        self._log.debug('ensure_state')
        # Get all our entry/attrs in a single batch
        entry_state = self.get_attrs_vals_utf8(state.keys())

        # Check what is present/is not present to work out what has to change.
        modlist = []
        for (attr, values) in state.items():
            value_set = set(values)
            entry_set = set(entry_state.get(attr, []))

            # Set difference, is "all items in s but not t".
            value_add = value_set - entry_set
            value_rem = entry_set - value_set

            for value in value_add:
                modlist.append((ldap.MOD_ADD, attr, value))
            for value in value_rem:
                modlist.append((ldap.MOD_DELETE, attr, value))

        self._log.debug('Applying modlist: %s' % modlist)
        # Apply it!
        if len(modlist) > 0:
            self.apply_mods(modlist)

    # maybe this could be renamed?
    def set(self, key, value, action=ldap.MOD_REPLACE):
        """Perform a specified action on a key with value

        :param key: an attribute name
        :type key: str
        :param value: an attribute value
        :type value: str
        :param action: - ldap.MOD_REPLACE - by default
                        - ldap.MOD_ADD
                        - ldap.MOD_DELETE
        :type action: int

        :returns: result of modify_s operation
        :raises: ValueError - if instance is not online
        """

        if action == ldap.MOD_ADD:
            action_txt = "ADD"
        elif action == ldap.MOD_REPLACE:
            action_txt = "REPLACE"
        elif action == ldap.MOD_DELETE:
            action_txt = "DELETE"
        else:
            # This should never happen (bug!)
            action_txt = "UNKNOWN"

        if value is None or len(value) < 512:
            self._log.debug("%s set %s: (%r, %r)" % (self._dn, action_txt, key, display_log_value(key, value)))
        else:
            self._log.debug("%s set %s: (%r, value too large)" % (self._dn, action_txt, key))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot set properties on instance that is not ONLINE.")

        if isinstance(value, list):
            # value = map(lambda x: ensure_bytes(x), value)
            value = ensure_list_bytes(value)
        elif value is not None:
            value = [ensure_bytes(value)]

        return _modify_ext_s(self._instance,self._dn, [(action, key, value)],
                                           serverctrls=self._server_controls, clientctrls=self._client_controls,
                                           escapehatch='i am sure')

    def apply_mods(self, mods):
        """Perform modification operation using several mods at once

        :param mods: [(action, key, value),] or [(ldap.MOD_DELETE, key),]
        :type mods: list of tuples
        :raises: ValueError - if a provided mod op is invalid
        """

        mod_list = []
        for mod in mods:
            if len(mod) < 2:
                # Error
                raise ValueError('Not enough arguments in the mod op')
            elif len(mod) == 2:  # no action
                # This hack exists because the original lib389 Entry type
                # does odd things.
                action, key = mod
                if action != ldap.MOD_DELETE:
                    raise ValueError('Only MOD_DELETE takes two arguments %s' % mod)
                value = None
                # Just add the raw mod, because we don't have a value
                mod_list.append((action, key, value))
            elif len(mod) == 3:
                action, key, value = mod
                if action != ldap.MOD_REPLACE and \
                   action != ldap.MOD_ADD and \
                   action != ldap.MOD_DELETE:
                    raise ValueError('Invalid mod action(%s)' % str(action))
                if isinstance(value, list):
                    value = ensure_list_bytes(value)
                else:
                    value = [ensure_bytes(value)]
                mod_list.append((action, key, value))
            else:
                # Error too many items
                raise ValueError('Too many arguments in the mod op')
        return _modify_ext_s(self._instance,self._dn, mod_list, serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')

    def _unsafe_compare_attribute(self, other):
        """Compare two attributes from two objects. This is currently marked unsafe as it's
        not complete yet.

        The idea is to use the native ldap compare operation, rather than simple comparison of
        values due to schema awareness. LDAP doesn't normalise values, so two objects with:

        cn: value
        cn: VaLuE

        This will fail to compare in python, but would succeed in LDAP beacuse CN is case
        insensitive.

        To allow schema aware checking, we need to call ldap compare extop here.
        """
        pass

    @classmethod
    def compare(cls, obj1, obj2):
        """Compare if two RDN objects have same attributes and values.

        This comparison is a loose comparison, not a strict one i.e. "this object *is* this other object"
        It will just check if the attributes are same.
        'nsUniqueId' attribute is not checked intentionally because we want to compare arbitrary objects
        i.e they may have different 'nsUniqueId' but same attributes.

        Example::

            cn=user1,ou=a
            cn=user1,ou=b

        Comparision of these two objects should result in same, even though their 'nsUniqueId' attribute differs.

        :param obj1: An entry to check
        :type obj1: lib389._mapped_object.DSLdapObject
        :param obj2: An entry to check
        :type obj2: lib389._mapped_object.DSLdapObject
        :returns: True if objects have same attributes else returns False
        :raises: ValueError - if obj1 or obj2 don't inherit DSLdapObject
        """

        # ensuring both the objects are RDN objects
        if not issubclass(type(obj1), DSLdapObject) or not issubclass(type(obj2), DSLdapObject):
            raise ValueError("Invalid arguments: Expecting object types that inherits 'DSLdapObject' class")
        # check if RDN of objects is same
        if obj1.rdn != obj2.rdn:
            obj1._log.debug("%s != %s" % (obj1.rdn, obj2.rdn))
            return False
        obj1_attrs = obj1.get_compare_attrs()
        obj2_attrs = obj2.get_compare_attrs()
        # Bail fast if the keys don't match
        if set(obj1_attrs.keys()) != set(obj2_attrs.keys()):
            obj1._log.debug("%s != %s" % (obj1_attrs.keys(), obj2_attrs.keys()))
            return False
        # Check the values of each key
        # using obj1_attrs.keys() because obj1_attrs.iterkleys() is not supported in python3
        for key in obj1_attrs.keys():
            if set(obj1_attrs[key]) != set(obj2_attrs[key]):
                obj1._log.debug("  v-- %s != %s" % (key, key))
                obj1._log.debug("%s != %s" % (obj1_attrs[key], obj2_attrs[key]))
                return False
        return True

    def get_compare_attrs(self, use_json=False):
        """Get a dictionary having attributes to be compared
        i.e. excluding self._compare_exclude
        """

        self._log.debug("%s get_compare_attrs" % (self._dn))

        all_attrs_dict = self.get_all_attrs()
        all_attrs_lower = {}
        for k in all_attrs_dict:
            all_attrs_lower[k.lower()] = all_attrs_dict[k]

        # removing _compate_exclude attrs from all attrs
        cx = [x.lower() for x in self._compare_exclude]
        compare_attrs = set(all_attrs_lower.keys()) - set(cx)

        compare_attrs_dict = {attr.lower(): all_attrs_lower[attr] for attr in compare_attrs}

        return compare_attrs_dict

    def get_all_attrs(self, use_json=False):
        """Get a dictionary having all the attributes of the entry

        :returns: Dict with real attributes and operational attributes
        """

        self._log.debug("%s get_all_attrs" % (self._dn))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
        else:
            # retrieving real(*) and operational attributes(+)
            attrs_entry = _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter,
                                                      attrlist=["*", "+"], serverctrls=self._server_controls,
                                                      clientctrls=self._client_controls, escapehatch='i am sure')
            if len(attrs_entry) > 0:
                # getting dict from 'entry' object
                attrs_dict = attrs_entry[0].data
                # Should we normalise the attr names here to lower()?
                # This could have unforseen consequences ...
                return attrs_dict
            else:
                return {}

    def get_all_attrs_utf8(self, use_json=False):
        """Get a dictionary having all the attributes of the entry

        :returns: Dict with real attributes and operational attributes
        """

        self._log.debug("%s get_all_attrs" % (self._dn))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
        else:
            # retrieving real(*) and operational attributes(+)
            attrs_entry = _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter,
                                                      attrlist=["*", "+"], serverctrls=self._server_controls,
                                                      clientctrls=self._client_controls, escapehatch='i am sure')
            if len(attrs_entry) > 0:
                # getting dict from 'entry' object
                r = {}
                for (k, vo) in attrs_entry[0].data.items():
                    r[k] = ensure_list_str(vo)
                return r
            else:
                return {}

    def get_attrs_vals(self, keys, use_json=False):
        self._log.debug("%s get_attrs_vals(%r)" % (self._dn, keys))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
        else:
            entry = _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter,
                                                attrlist=keys, serverctrls=self._server_controls,
                                                clientctrls=self._client_controls, escapehatch='i am sure')
            if len(entry) > 0:
                return entry[0].getValuesSet(keys)
            else:
                return []

    def get_attrs_vals_utf8(self, keys, use_json=False):
        self._log.debug("%s get_attrs_vals_utf8(%r)" % (self._dn, keys))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
        entry = _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter, attrlist=keys,
                                            serverctrls=self._server_controls, clientctrls=self._client_controls,
                                            escapehatch='i am sure')
        if len(entry) > 0:
            vset = entry[0].getValuesSet(keys)
            r = {}
            for (k, vo) in vset.items():
                r[k] = ensure_list_str(vo)
            return r
        else:
            return {}

    def get_attr_vals(self, key, use_json=False):
        self._log.debug("%s get_attr_vals(%r)" % (self._dn, key))
        # We might need to add a state check for NONE dn.
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we
            # can use get on dse.ldif to get values offline.
        else:
            # It would be good to prevent the entry code intercepting this ....
            # We have to do this in this method, because else we ignore the scope base.
            entry = _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter,
                                                attrlist=[key], serverctrls=self._server_controls,
                                                clientctrls=self._client_controls, escapehatch='i am sure')
            if len(entry) > 0:
                vals = entry[0].getValues(key)
                if use_json:
                    result = {key: []}
                    for val in vals:
                        result[key].append(val)
                    return result
                else:
                    return vals
            else:
                if use_json:
                    return {}
                else:
                    return []

    def get_attr_val(self, key, use_json=False):
        self._log.debug("%s getVal(%r)" % (self._dn, key))
        # We might need to add a state check for NONE dn.
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we
            # can use get on dse.ldif to get values offline.
        else:
            entry = _search_ext_s(self._instance,self._dn, ldap.SCOPE_BASE, self._object_filter,
                                                attrlist=[key], serverctrls=self._server_controls,
                                                clientctrls=self._client_controls, escapehatch='i am sure')
            if len(entry) > 0:
                return entry[0].getValue(key)
            else:
                return ""

    def get_attr_val_bytes(self, key, use_json=False):
        """Get a single attribute value from the entry in bytes type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_bytes(self.get_attr_val(key))

    def get_attr_vals_bytes(self, key, use_json=False):
        """Get attribute values from the entry in bytes type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_list_bytes(self.get_attr_vals(key))

    def get_attr_val_utf8(self, key, use_json=False):
        """Get a single attribute value from the entry in utf8 type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_str(self.get_attr_val(key))

    def get_attr_val_utf8_l(self, key, use_json=False):
        """Get a single attribute value from the entry in utf8 type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        x = self.get_attr_val(key)
        if x is not None:
            return ensure_str(x).lower()
        else:
            return None

    def get_attr_vals_utf8(self, key, use_json=False):
        """Get attribute values from the entry in utf8 type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_list_str(self.get_attr_vals(key))

    def get_attr_vals_utf8_l(self, key, use_json=False):
        """Get attribute values from the entry in utf8 type and lowercase

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return [x.lower() for x in ensure_list_str(self.get_attr_vals(key))]

    def get_attr_val_int(self, key, use_json=False):
        """Get a single attribute value from the entry in int type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_int(self.get_attr_val(key))

    def get_attr_vals_int(self, key, use_json=False):
        """Get attribute values from the entry in int type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_list_int(self.get_attr_vals(key))

    # Duplicate, but with many values. IE a dict api.
    # This
    def add_values(self, values):
        pass

    def replace_values(self, values):
        pass

    def set_values(self, values, action=ldap.MOD_REPLACE):
        pass

    # Modifies the DN of an entry to the new fqdn provided
    def rename(self, new_rdn, newsuperior=None, deloldrdn=True):
        """Renames the object within the tree.

        If you provide a newsuperior, this will move the object in the tree.
        If you only provide a new_rdn, it stays in the same branch, but just
        changes the rdn.

        Note, if you use newsuperior, you may move this object outside of the
        scope of the related DSLdapObjects manager, which may cause it not to
        appear in .get() requests.

        :param new_rdn: RDN of the new entry
        :type new_rdn: str
        :param newsuperior: New parent DN
        :type newsuperior: str
        """
        # When we are finished with this, we need to update our DN
        # To do this, we probably need to search the new rdn as a filter,
        # and the superior as the base (if it changed)
        if self._protected:
            return

        self._instance.rename_s(self._dn, new_rdn, newsuperior,
                                serverctrls=self._server_controls, clientctrls=self._client_controls,
                                delold=deloldrdn, escapehatch='i am sure')
        if newsuperior is not None:
            # Well, the new DN should be rdn + newsuperior.
            self._dn = '%s,%s' % (new_rdn, newsuperior)
        else:
            old_dn_parts = ldap.explode_dn(self._dn)
            # Replace the rdn
            old_dn_parts[0] = new_rdn
            self._dn = ",".join(old_dn_parts)
        assert self.exists()

        # assert we actually got the change right ....

    def delete(self, recursive=False):
        """Deletes the object defined by self._dn.
        This can be changed with the self._protected flag!
        """

        self._log.debug("%s delete" % (self._dn))
        if not self._protected:
            # Is there a way to mark this as offline and kill it
            if recursive:
                filterstr = "(|(objectclass=*)(objectclass=ldapsubentry))"
                ents = _search_s(self._instance, self._dn, ldap.SCOPE_SUBTREE, filterstr, escapehatch='i am sure')
                for ent in sorted(ents, key=lambda e: len(e.dn), reverse=True):
                    _delete_ext_s(self._instance, ent.dn, serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')
            else:
                _delete_ext_s(self._instance, self._dn, serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')

    def _validate(self, rdn, properties, basedn):
        """Used to validate a create request.
        This way, it can be over-ridden without affecting
        the create types.

        It also checks that all the values in _must_attribute exist
        in some form in the dictionary.

        It has the useful trick of returning the dn, so subtypes
        can use extra properties to create the dn's here for this.
        """

        if properties is None:
            raise ldap.UNWILLING_TO_PERFORM('Invalid request to create. Properties cannot be None')
        if type(properties) != dict:
            raise ldap.UNWILLING_TO_PERFORM("properties must be a dictionary")

        # I think this needs to be made case insensitive
        # How will this work with the dictionary?
        if self._must_attributes is not None:
            for attr in self._must_attributes:
                if properties.get(attr, None) is None:
                    # Put RDN to properties
                    if attr == self._rdn_attribute and rdn is not None:
                        properties[self._rdn_attribute] = ldap.dn.str2dn(rdn)[0][0][1]
                    else:
                        raise ldap.UNWILLING_TO_PERFORM('Attribute %s must not be None' % attr)

        # Make sure the naming attribute is present
        if properties.get(self._rdn_attribute, None) is None and rdn is None:
            raise ldap.UNWILLING_TO_PERFORM('Attribute %s must not be None or rdn provided' % self._rdn_attribute)

        # Great! Now, lets fix up our types
        for k, v in properties.items():
            if isinstance(v, list):
                # Great!
                pass
            else:
                # Turn it into a list instead.
                properties[k] = [v, ]

        # If we were created with a dn= in the object init, we set tdn now, and skip
        # any possible dn derivation steps that follow.
        tdn = self._dn

        # However, if no DN was provided, we attempt to derive the DN from the relevant
        # properties of the object. The idea being that by defining only the attributes
        # of the object, we can just solve a possible rdn instead of asking for the same
        # data twice.
        if tdn is None:
            if basedn is None:
                raise ldap.UNWILLING_TO_PERFORM('Invalid request to create. basedn cannot be None')

            # Were we given a relative component? Yes? Go ahead!
            if rdn is not None:
                tdn = ensure_str('%s,%s' % (rdn, basedn))
            elif properties.get(self._rdn_attribute, None) is not None:
                # Favour the value in the properties dictionary
                v = properties.get(self._rdn_attribute)
                rdn = ensure_str(v[0])

                erdn = ensure_str(ldap.dn.escape_dn_chars(rdn))
                self._log.debug("Using first property %s: %s as rdn" % (self._rdn_attribute, erdn))
                # Now we compare. If we changed this value, we have to put it back to make the properties complete.
                if erdn != rdn:
                    properties[self._rdn_attribute].append(erdn)

                tdn = ensure_str('%s=%s,%s' % (self._rdn_attribute, erdn, basedn))

        # We may need to map over the data in the properties dict to satisfy python-ldap
        str_props = {}
        for k, v in properties.items():
            str_props[k] = ensure_list_bytes(v)
        #
        # Do we need to do extra dn validation here?
        return (tdn, str_props)

    def _create(self, rdn=None, properties=None, basedn=None, ensure=False):
        """Internal implementation of create. This is used by ensure
        and create, to prevent code duplication. You should *never* call
        this method directly.
        """
        assert(len(self._create_objectclasses) > 0)
        basedn = ensure_str(basedn)
        self._log.debug('Checking "%s" under %s : %s' % (rdn, basedn, display_log_data(properties)))
        # Add the objectClasses to the properties
        (dn, valid_props) = self._validate(rdn, properties, basedn)
        # Check if the entry exists or not? .add_s is going to error anyway ...
        self._log.debug('Validated dn {}'.format(dn))

        exists = False

        if ensure:
            # If we are running in stateful ensure mode, we need to check if the object exists, and
            # we can see the state that it is in.
            try:
                _search_ext_s(self._instance,dn, ldap.SCOPE_BASE, self._object_filter, attrsonly=1, serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')
                exists = True
            except ldap.NO_SUCH_OBJECT:
                pass

        if exists and ensure:
            # update properties
            self._log.debug('Exists %s' % dn)
            self._dn = dn
            # Now use replace_many to setup our values
            mods = []
            for k, v in list(valid_props.items()):
                mods.append((ldap.MOD_REPLACE, k, v))
            _modify_ext_s(self._instance,self._dn, mods, serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')
        elif not exists:
            # This case is reached in two cases. One is we are in ensure mode, and we KNOW the entry
            # doesn't exist.
            # The alternate, is that we are in a non-stateful create, so we "just create" and see
            # what happens. I believe the technical term is "yolo create".
            self._log.debug('Creating %s' % dn)
            e = Entry(dn)
            e.update({'objectclass': ensure_list_bytes(self._create_objectclasses)})
            e.update(valid_props)
            # We rely on exceptions here to indicate failure to the parent.
            _add_ext_s(self._instance, e, serverctrls=self._server_controls, clientctrls=self._client_controls, escapehatch='i am sure')
            self._log.debug('Created entry %s : %s' % (dn, display_log_data(e.data)))
            # If it worked, we need to fix our instance dn for the object's self reference. Because
            # we may not have a self reference yet (just created), it may have changed (someone
            # set dn, but validate altered it).
            self._dn = dn
        else:
            # This case can't be reached now that we only check existance on ensure.
            # However, it's good to keep it for "complete" behaviour, exhausting all states.
            # We could highlight bugs ;)
            raise AssertionError("Impossible State Reached in _create")
        return self

    def create(self, rdn=None, properties=None, basedn=None):
        """Add a new entry

        :param rdn: RDN of the new entry
        :type rdn: str
        :param properties: Attributes for the new entry
        :type properties: dict
        :param basedn: Base DN of the new entry
        :type rdn: str

        :returns: DSLdapObject of the created entry
        """
        return self._create(rdn, properties, basedn, ensure=False)

    def ensure_state(self, rdn=None, properties=None, basedn=None):
        """Ensure an entry exists with the following state, created
        if necessary.

        :param rdn: RDN of the new entry
        :type rdn: str
        :param properties: Attributes for the new entry
        :type properties: dict
        :param basedn: Base DN of the new entry
        :type rdn: str

        :returns: DSLdapObject of the created entry
        """
        return self._create(rdn, properties, basedn, ensure=True)


# A challenge of this, is how do we manage indexes? They have two naming attributes....

class DSLdapObjects(DSLogging, DSLints):
    """The object represents the next idea: "Everything is an instance of something
    that exists in this way", i.e. we unite LDAP entries by some
    set of parameters with the object.

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance, basedn=""):
        self._childobject = DSLdapObject
        self._instance = instance
        super(DSLdapObjects, self).__init__(self._instance.verbose)
        self._objectclasses = []
        self._filterattrs = []
        self._list_attrlist = ['dn']
        # Copy this from the child if we need.
        self._basedn = basedn
        self._scope = ldap.SCOPE_SUBTREE
        self._server_controls = None
        self._client_controls = None

    def _get_objectclass_filter(self):
        return _gen_and(
            _gen_filter(_term_gen('objectclass'), self._objectclasses)
        )

    def _get_selector_filter(self, selector):
        return _gen_and([
            self._get_objectclass_filter(),
            _gen_or(
                # This will yield all combinations of selector to filterattrs.
                # This won't work with multiple values in selector (yet)
                _gen_filter(self._filterattrs, _term_gen(selector))
            ),
        ])

    def _entry_to_instance(self, dn=None, entry=None):
        # Normally this won't be used. But for say the plugin type where we
        # have "many" possible child types, this allows us to overload
        # and select / return the right one through ALL our get/list/create
        # functions with very little work on the behalf of the overloader
        return self._childobject(instance=self._instance, dn=dn)

    def list(self, paged_search=None, paged_critical=True):
        """Get a list of children entries (DSLdapObject, Replica, etc.) using a base DN
        and objectClasses of our object (DSLdapObjects, Replicas, etc.)

        :param paged_search: None for no paged search, or an int of page size to use.
        :returns: A list of children entries
        """

        # Filter based on the objectclasses and the basedn
        insts = None
        # This will yield and & filter for objectClass with as many terms as needed.
        filterstr = self._get_objectclass_filter()
        self._log.debug('list filter = %s' % filterstr)

        if type(paged_search) == int:
            self._log.debug('listing with paged search -> %d', paged_search)
            # If paged_search ->
            results = []
            pages = 0
            pctrls = []
            req_pr_ctrl = SimplePagedResultsControl(paged_critical, size=paged_search, cookie='')
            if self._server_controls is not None:
                controls = [req_pr_ctrl] + self._server_controls
            else:
                controls = [req_pr_ctrl]
            while True:
                msgid = self._instance.search_ext(
                        base=self._basedn,
                        scope=self._scope,
                        filterstr=filterstr,
                        attrlist=self._list_attrlist,
                        serverctrls=controls,
                        clientctrls=self._client_controls,
                        escapehatch='i am sure'
                    )
                self._log.info('Getting page %d' % (pages,))
                rtype, rdata, rmsgid, rctrls = self._instance.result3(msgid, escapehatch='i am sure')
                results.extend(rdata)
                pages += 1
                self._log.debug("%s" % rctrls)
                pctrls = [ c for c in rctrls
                    if c.controlType == SimplePagedResultsControl.controlType]
                if pctrls and pctrls[0].cookie:
                    req_pr_ctrl.cookie = pctrls[0].cookie
                    if self._server_controls is not None:
                        controls = [req_pr_ctrl] + self._server_controls
                    else:
                        controls = [req_pr_ctrl]
                else:
                    break
                #End while
            # Result3 doesn't map through Entry, so we have to do it manually.
            results = [Entry(r) for r in results]
            insts = [self._entry_to_instance(dn=r.dn, entry=r) for r in results]
            # End paged search
        else:
            # If not paged
            try:
                results = _search_ext_s(self._instance,
                    base=self._basedn,
                    scope=self._scope,
                    filterstr=filterstr,
                    attrlist=self._list_attrlist,
                    serverctrls=self._server_controls, clientctrls=self._client_controls,
                    escapehatch='i am sure'
                )
                # def __init__(self, instance, dn=None):
                insts = [self._entry_to_instance(dn=r.dn, entry=r) for r in results]
            except ldap.NO_SUCH_OBJECT:
                # There are no objects to select from, se we return an empty array
                insts = []
        return insts

    def exists(self, selector=[], dn=None):
        """Check if a child entry exists

        :returns: True if it exists
        """
        results = []
        try:
            if dn is not None:
                results = self._get_dn(dn)
            else:
                results = self._get_selector(selector)
        except:
            return False

        if len(results) == 1:
            return True
        else:
            return False

    def get(self, selector=[], dn=None, json=False):
        """Get a child entry (DSLdapObject, Replica, etc.) with dn or selector
        using a base DN and objectClasses of our object (DSLdapObjects, Replicas, etc.)

        Note that * is not a valid selector, you should use "list()" instead.

        :param dn: DN of wanted entry
        :type dn: str
        :param selector: An additional filter to search for, i.e. 'backend_name'. The attributes selected are based on object type, ie user will search for uid and cn.
        :type dn: str

        :returns: A child entry
        """

        results = []
        if dn is not None:
            criteria = dn
            search_filter = self._get_objectclass_filter()
            results = self._get_dn(dn)
        else:
            criteria = selector
            search_filter = self._get_selector_filter(selector)
            results = self._get_selector(selector)

        if len(results) == 0:
            raise ldap.NO_SUCH_OBJECT(f"No object exists given the filter criteria: {criteria} {search_filter}")
        if len(results) > 1:
            raise ldap.UNWILLING_TO_PERFORM(f"Too many objects matched selection criteria: {criteria} {search_filter}")
        if json:
            return self._entry_to_instance(results[0].dn, results[0]).get_all_attrs_json()
        else:
            return self._entry_to_instance(results[0].dn, results[0])

    def _get_dn(self, dn):
        # This will yield and & filter for objectClass with as many terms as needed.
        filterstr = self._get_objectclass_filter()
        self._log.debug('_gen_dn filter = %s' % filterstr)
        self._log.debug('_gen_dn dn = %s' % dn)
        return _search_ext_s(self._instance,
            base=dn,
            scope=ldap.SCOPE_BASE,
            filterstr=filterstr,
            attrlist=self._list_attrlist,
            serverctrls=self._server_controls, clientctrls=self._client_controls,
            escapehatch='i am sure'
        )

    def _get_selector(self, selector):
        # Filter based on the objectclasses and the basedn
        # Based on the selector, we should filter on that too.
        # This will yield and & filter for objectClass with as many terms as needed.
        filterstr = self._get_selector_filter(selector)
        self._log.debug('_gen_selector filter = %s' % filterstr)
        return _search_ext_s(self._instance,
            base=self._basedn,
            scope=self._scope,
            filterstr=filterstr,
            attrlist=self._list_attrlist,
            serverctrls=self._server_controls, clientctrls=self._client_controls,
            escapehatch='i am sure'
        )

    def _validate(self, rdn, properties):
        """Validate the factory part of the creation"""

        if properties is None:
            raise ldap.UNWILLING_TO_PERFORM('Invalid request to create. Properties cannot be None')
        if type(properties) != dict:
            raise ldap.UNWILLING_TO_PERFORM("properties must be a dictionary")

        return (rdn, properties)

    def create(self, rdn=None, properties=None):
        """Create an object under base DN of our entry

        :param rdn: RDN of the new entry
        :type rdn: str
        :param properties: Attributes for the new entry
        :type properties: dict

        :returns: DSLdapObject of the created entry
        """

        # Should we inject the rdn to properties?
        # This may not work in all cases, especially when we consider plugins.
        #
        co = self._entry_to_instance(dn=None, entry=None)
        # Make the rdn naming attr available
        self._rdn_attribute = co._rdn_attribute
        (rdn, properties) = self._validate(rdn, properties)
        # Now actually commit the creation req
        return co.create(rdn, properties, self._basedn)

    def ensure_state(self, rdn=None, properties=None):
        """Create an object under base DN of our entry, or
        assert it exists and update it's properties.

        :param rdn: RDN of the new entry
        :type rdn: str
        :param properties: Attributes for the new entry
        :type properties: dict

        :returns: DSLdapObject of the created entry
        """

        # Should we inject the rdn to properties?
        # This may not work in all cases, especially when we consider plugins.
        #
        co = self._entry_to_instance(dn=None, entry=None)
        # Make the rdn naming attr avaliable
        self._rdn_attribute = co._rdn_attribute
        (rdn, properties) = self._validate(rdn, properties)
        # Now actually commit the creation req
        return co.ensure_state(rdn, properties, self._basedn)

    def filter(self, search, attrlist=None, scope=None, strict=False):
        # This will yield and & filter for objectClass with as many terms as needed.
        if search:
            search_filter = _gen_and([self._get_objectclass_filter(), search])
        else:
            search_filter = self._get_objectclass_filter()
        if scope is None:
            scope = self._scope
        if attrlist:
            self._list_attrlist = attrlist
        self._log.debug(f'list filter = {search_filter} with scope {scope} and attribute list {attrlist}')
        try:
            results = _search_ext_s(self._instance,
                base=self._basedn,
                scope=scope,
                filterstr=search_filter,
                attrlist=self._list_attrlist,
                serverctrls=self._server_controls, clientctrls=self._client_controls
            )
            # def __init__(self, instance, dn=None):
            insts = [self._entry_to_instance(dn=r.dn, entry=r) for r in results]
        except ldap.NO_SUCH_OBJECT:
            # There are no objects to select from, se we return an empty array
            if strict:
                raise ldap.NO_SUCH_OBJECT
            insts = []
        return insts

class CompositeDSLdapObject(DSLdapObject):
    """A virtual view as a single object that merges two entry attributes.
       This class is not supposed to be called directly but through subclasses.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Global dn
    :type dn: str
    """

    def __init__(self, instance, dn=None):
        super(CompositeDSLdapObject, self).__init__(instance, dn)
        self._entries =[]
        self._map = {}
        self.log = None

    def add_component(self, entry, attrs):
        """Add a new entry as element of the composite entry.
           An attribute may belong to different entries,
           if that is the case:
                the attribute is written in all entries
                the attribute is read from last entry containing it.

        :param entry: An entry object
        :type entry: DSLdapObject
        :param attrs: Attributes from the entry that are in the composite entry.
        :type attrs: (str,...)
        """

        idx = len(self._entries)
        self._entries.append(entry)
        for attr in attrs:
            nattr = attr.lower()
            if nattr in self._map:
                self._map[nattr].append(idx)
            else:
                self._map[nattr] = [idx,]

    def _find_idx(self, attr, must_have=True):
        """Find the entry index."""
        nattr = ensure_str(attr).lower()
        try:
            return self._map[nattr]
        except KeyError:
            if must_have:
                raise ValueError(f'No mapping for attribute {attr} in composite object')
            return []

    def _unsafe_raw_entry(self):
        raise NotImplementedError("Composite objects have no raw view.")

    def exists(self):
        for entry in self._entries:
            if not entry.exists():
                return False
        return True

    def search(self, scope="subtree", filter='objectclass=*'):
        raise NotImplementedError("Cannot use this method on composite objects.")

    def get_basedn(self):
        raise NotImplementedError("Cannot use this method on composite objects.")

    def present(self, attr, value=None):
        for entry in self._entries:
            if not entry.present():
                return False
        return True

    def _spread_set(self, dataset, set2attr_fn):
        """Separate a set of data to a set per entry."""
        res = []
        for entry in self._entries:
            res.append([])
        for data in dataset:
            attr = set2attr_fn(data)
            for idx in self._find_idx(attr):
                res[idx].append(data)
        return res

    def _spread_dict(self, datadict):
        """Separate a dict of data where key is the attribure to a dict per entry."""
        res = {}
        for entry in self._entries:
            res.append({})
        for attr in datadict.keys():
            for idx in self._find_idx(attr):
                res[idx][attr] = datadict[attr]
        return res

    def replace_many(self, *args):
        dataset = self._spread_set(*args, lambda x: x[0])
        for idx in range(0, self._entries):
            if len(dataset[idx]) > 0:
                self._entries[idx].replace_many(*dataset[idx])

    def ensure_attr_state(self, state):
        # Hard to implement because some requires attributes may be common to several entries
        raise NotImplementedError("Cannot use this method on composite objects.")

    def set(self, key, value, action=ldap.MOD_REPLACE):
        v = { ldap.MOD_REPLACE : "REPLACE",
              ldap.MOD_ADD : "ADD",
              ldap.MOD_DELETE: "DEL" }
        for idx in self._find_idx(key):
            self._entries[idx].set(key, value, action)

    def apply_mods(self, mods):
        dataset = self._spread_set(*args, lambda x: x[1])
        for idx in range(0, self._entries):
            if len(dataset[idx]) > 0:
                self._entries[idx].apply_mods(*dataset[idx])

    def get_all_attrs(self, use_json=False):
        res = {}
        idx = 0
        for entry in self._entries:
            attrs = entry.get_all_attrs(use_json)
            for attr in attrs.keys():
                if idx in self._find_idx(attr, False):
                    res[attr] = attrs[attr]
            idx += 1
        return res

    def get_all_attrs_utf8(self, use_json=False):
        res = {}
        idx = 0
        for entry in self._entries:
            attrs = entry.get_all_attrs_utf8(use_json)
            for attr in attrs.keys():
                if idx in self._find_idx(attr, False):
                    res[attr] = attrs[attr]
            idx += 1
        return res

    def get_attrs_vals(self, keys, use_json=False):
        res = {}
        dataset = self._spread_set(*args, lambda x: x)
        for idx in range(0, self._entries):
            if len(dataset[idx]) > 0:
                res.update(self._entries[idx].apply_mods(*dataset[idx], use_json))

    def get_attrs_vals_utf8(self, keys, use_json=False):
        res = {}
        dataset = self._spread_set(*args, lambda x: x)
        for idx in range(0, self._entries):
            if len(dataset[idx]) > 0:
                res.update(self._entries[idx].apply_mods(*dataset[idx], use_json))

    def get_attr_vals(self, key, use_json=False):
        idx = self._find_idx(key)[-1]
        return self._entries[idx].get_attr_vals(key, use_json)

    def get_attr_vals_utf8(self, key, use_json=False):
        idx = self._find_idx(key)[-1]
        return self._entries[idx].get_attr_vals_utf8(key, use_json)

    def get_attr_val(self, key, use_json=False):
        idx = self._find_idx(key)[-1]
        return self._entries[idx].get_attr_val(key, use_json)

    def get_attr_val_utf8(self, key, use_json=False):
        idx = self._find_idx(key)[-1]
        return self._entries[idx].get_attr_val_utf8(key, use_json)

    def add_values(self, values):
        raise DeprecationWarning("Not implemented any more.")

    def replace_values(self, values):
        raise DeprecationWarning("Not implemented any more.")

    def set_values(self, values, action=ldap.MOD_REPLACE):
        raise DeprecationWarning("Not implemented any more.")

    def rename(self, new_rdn, newsuperior=None, deloldrdn=True):
        raise NotImplementedError("Cannot use this method on composite objects.")

    def delete(self, recursive=False):
        raise NotImplementedError("Cannot use this method on composite objects.")

    def _validate(self, rdn, properties, basedn):
        raise NotImplementedError("Cannot use this method on composite objects.")

    def _create(self, rdn=None, properties=None, basedn=None, ensure=False):
        raise NotImplementedError("Cannot use this method on composite objects.")

    def create(self, rdn=None, properties=None, basedn=None):
        raise NotImplementedError("Cannot use this method on composite objects.")

    def ensure_state(self, rdn=None, properties=None, basedn=None):
        raise NotImplementedError("Cannot use this method on composite objects.")

    def display(self, attrlist=['*']):
        """Get an entry but represent it as a string LDIF
        :returns: LDIF formatted string
        """
        eattrs = {}
        idx = 0
        for entry in self._entries:
            e = _search_ext_s(entry._instance,entry._dn, ldap.SCOPE_BASE, entry._object_filter, attrlist=attrlist,
                                        serverctrls=entry._server_controls, clientctrls=entry._client_controls,
                                        escapehatch='i am sure')[0]
            for attr, vals in e.iterAttrs():
                if idx in self._find_idx(attr, False):
                    eattrs[attr] = vals
            idx += 1
        e = Entry((self.dn, eattrs))
        return e.__repr__()
