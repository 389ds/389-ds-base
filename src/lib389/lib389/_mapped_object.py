# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import ldap.dn
from ldap import filter as ldap_filter
import logging
from functools import partial

from lib389._entry import Entry
from lib389._constants import DIRSRV_STATE_ONLINE
from lib389.utils import (
        ensure_bytes, ensure_str, ensure_int, ensure_list_bytes, ensure_list_str,
        ensure_list_int
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
    for attr, value in zip(attrtypes, values):
        if attr is not None and value is not None:
            filt += '(%s=%s)' % (attr, ldap_filter.escape_filter_chars(value))
    if extra is not None:
        filt += '{FILT}'.format(FILT=extra)
    return filt


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


class DSLdapObject(DSLogging):
    """A single instance of DSLdapObjects

    :param instance: A instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    :param batch: Not implemented
    :type batch: bool
    """

    # TODO: Automatically create objects when they are requested to have properties added
    def __init__(self, instance, dn=None, batch=False):
        self._instance = instance
        super(DSLdapObject, self).__init__(self._instance.verbose)
        # This allows some factor objects to be overriden
        self._dn = None
        if dn is not None:
            self._dn = ensure_str(dn)

        self._batch = batch
        self._protected = True
        # Used in creation
        self._create_objectclasses = []
        self._rdn_attribute = None
        self._must_attributes = None
        # attributes, we don't want to compare
        self._compare_exclude = ['entryid']
        self._lint_functions = None
        self._server_controls = None
        self._client_controls = None

    def __unicode__(self):
        val = self._dn
        if self._rdn_attribute:
            # What if the rdn is multi value and we don't get the primary .... ARGHHH
            val = self.get_attr_val(self._rdn_attribute)
        return ensure_str(val)

    def __str__(self):
        return self.__unicode__()

    def raw_entry(self):
        """Get an Entry object

        :returns: Entry object
        """

        return self._instance.search_ext_s(self._dn, ldap.SCOPE_BASE, attrlist=["*"], serverctrls=self._server_controls, clientctrls=self._client_controls)[0]

    def exists(self):
        """Check if the entry exists

        :returns: True if it exists
        """

        try:
            self._instance.search_ext_s(self._dn, ldap.SCOPE_BASE, attrsonly=1, serverctrls=self._server_controls, clientctrls=self._client_controls)
        except ldap.NO_SUCH_OBJECT:
            return False

        return True

    def display(self):
        """Get an entry but represent it as a string LDIF

        :returns: LDIF formatted string
        """

        e = self._instance.search_ext_s(self._dn, ldap.SCOPE_BASE, attrlist=["*"], serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
        return e.__repr__()

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
        attrs = fn(*args, **kwargs)
        str_attrs = {}
        for k in attrs:
            str_attrs[ensure_str(k)] = ensure_list_str(attrs[k])

        response = { "dn": ensure_str(self._dn), "attrs" : str_attrs }
        print('json response')
        print(response)

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
            pfunc = partial(self._jsonify, fn=getattr(self, int_name))
            return pfunc

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

        e = self._instance.search_ext_s(self._dn, ldap.SCOPE_BASE, attrlist=[attr, ], serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
        if value is None:
            return e.hasAttr(attr)
        else:
            return e.hasValue(attr, value)

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

        for val in self.get_attr_vals(key):
            self.remove(key, val)

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

        self._log.debug("%s set(%r, %r)" % (self._dn, key, value))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot set properties on instance that is not ONLINE.")

        if isinstance(value, list):
            # value = map(lambda x: ensure_bytes(x), value)
            value = ensure_list_bytes(value)
        elif value is not None:
            value = [ensure_bytes(value)]

        if self._batch:
            pass
        else:
            return self._instance.modify_ext_s(self._dn, [(action, key, value)], serverctrls=self._server_controls, clientctrls=self._client_controls)

    def apply_mods(self, mods):
        """Perform modification operation using several mods at once

        :param mods: [(action, key, value),]
        :type mods: list of tuples
        :raises: ValueError - if a provided mod op is invalid
        """

        mod_list = []
        for mod in mods:
            if len(mod) < 2:
                # Error
                raise ValueError('Not enough arguments in the mod op')
            elif len(mod) == 2:  # no action
                action = ldap.MOD_REPLACE
                key, value = mod
            elif len(mod) == 3:
                action, key, value = mod
                if action != ldap.MOD_REPLACE or \
                   action != ldap.MOD_ADD or \
                   action != ldap.MOD_DELETE:
                    raise ValueError('Invalid mod action(%s)' % str(action))
            else:
                # Error too many items
                raise ValueError('Too many arguments in the mod op')

            if isinstance(value, list):
                value = ensure_list_bytes(value)
            else:
                value = [ensure_bytes(value)]

            mod_list.append((action, key, value))
        return self._instance.modify_ext_s(self._dn, mod_list, serverctrls=self._server_controls, clientctrls=self._client_controls)

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
            return False
        obj1_attrs = obj1.get_compare_attrs()
        obj2_attrs = obj2.get_compare_attrs()
        # Bail fast if the keys don't match
        if set(obj1_attrs.keys()) != set(obj2_attrs.keys()):
            return False
        # Check the values of each key
        # using obj1_attrs.keys() because obj1_attrs.iterkleys() is not supported in python3
        for key in obj1_attrs.keys():
            if set(obj1_attrs[key]) != set(obj2_attrs[key]):
                return False
        return True

    def get_compare_attrs(self):
        """Get a dictionary having attributes to be compared
        i.e. excluding self._compare_exclude
        """

        self._log.debug("%s get_compare_attrs" % (self._dn))
        all_attrs_dict = self.get_all_attrs()
        # removing _compate_exclude attrs from all attrs
        compare_attrs = set(all_attrs_dict.keys()) - set(self._compare_exclude)
        compare_attrs_dict = {attr:all_attrs_dict[attr] for attr in compare_attrs}
        return compare_attrs_dict

    def get_all_attrs(self):
        """Get a dictionary having all the attributes of the entry

        :returns: Dict with real attributes and operational attributes
        """

        self._log.debug("%s get_all_attrs" % (self._dn))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
        else:
            # retrieving real(*) and operational attributes(+)
            attrs_entry = self._instance.search_ext_s(self._dn, ldap.SCOPE_BASE, attrlist=["*", "+"], serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
            # getting dict from 'entry' object
            attrs_dict = attrs_entry.data
            return attrs_dict

    def get_attrs_vals(self, keys):
        self._log.debug("%s get_attrs_vals(%r)" % (self._dn, keys))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
        else:
            entry = self._instance.search_ext_s(self._dn, ldap.SCOPE_BASE, attrlist=keys, serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
            return entry.getValuesSet(keys)

    def get_attr_vals(self, key):
        self._log.debug("%s get_attr_vals(%r)" % (self._dn, key))
        # We might need to add a state check for NONE dn.
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we
            # can use get on dse.ldif to get values offline.
        else:
            # It would be good to prevent the entry code intercepting this ....
            # We have to do this in this method, because else we ignore the scope base.
            entry = self._instance.search_ext_s(self._dn, ldap.SCOPE_BASE, attrlist=[key], serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
            return entry.getValues(key)

    def get_attr_val(self, key):
        self._log.debug("%s getVal(%r)" % (self._dn, key))
        # We might need to add a state check for NONE dn.
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we
            # can use get on dse.ldif to get values offline.
        else:
            entry = self._instance.search_ext_s(self._dn, ldap.SCOPE_BASE, attrlist=[key], serverctrls=self._server_controls, clientctrls=self._client_controls)[0]
            return entry.getValue(key)

    def get_attr_val_bytes(self, key):
        """Get a single attribute value from the entry in bytes type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_bytes(self.get_attr_val(key))

    def get_attr_vals_bytes(self, key):
        """Get attribute values from the entry in bytes type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_list_bytes(self.get_attr_vals(key))

    def get_attr_val_utf8(self, key):
        """Get a single attribute value from the entry in utf8 type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_str(self.get_attr_val(key))

    def get_attr_vals_utf8(self, key):
        """Get attribute values from the entry in utf8 type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_list_str(self.get_attr_vals(key))

    def get_attr_val_int(self, key):
        """Get a single attribute value from the entry in int type

        :param key: An attribute name
        :type key: str
        :returns: A single bytes value
        :raises: ValueError - if instance is offline
        """

        return ensure_int(self.get_attr_val(key))

    def get_attr_vals_int(self, key):
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

    # If the account can be bound to, this will attempt to do so. We don't check
    # for exceptions, just pass them back!
    def bind(self, password=None, *args, **kwargs):
        """Open a new connection and bind with the entry.
        You can pass arguments that will be passed to openConnection.

        :param password: An entry password
        :type password: str
        :returns: Connection with a binding as the entry
        """

        conn = self._instance.openConnection(*args, **kwargs)
        conn.simple_bind_s(self.dn, password)
        return conn

    def delete(self):
        """Deletes the object defined by self._dn.
        This can be changed with the self._protected flag!
        """

        self._log.debug("%s delete" % (self._dn))
        if not self._protected:
            # Is there a way to mark this as offline and kill it
            self._instance.delete_ext_s(self._dn, serverctrls=self._server_controls, clientctrls=self._client_controls)

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
        for attr in self._must_attributes:
            if properties.get(attr, None) is None:
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
                properties[k] = [v,]

        # This change here, means we can pre-load a full dn to _dn, or we can
        # accept based on the rdn
        tdn = self._dn

        if tdn is None:
            if basedn is None:
                raise ldap.UNWILLING_TO_PERFORM('Invalid request to create. basedn cannot be None')

            if properties.get(self._rdn_attribute, None) is not None:
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

        assert(len(self._create_objectclasses) > 0)
        basedn = ensure_str(basedn)
        self._log.debug('Creating "%s" under %s : %s' % (rdn, basedn, properties))
        # Add the objectClasses to the properties
        (dn, valid_props) = self._validate(rdn, properties, basedn)
        # Check if the entry exists or not? .add_s is going to error anyway ...
        self._log.debug('Validated dn %s : valid_props %s' % (dn, valid_props))

        e = Entry(dn)
        e.update({'objectclass': ensure_list_bytes(self._create_objectclasses)})
        e.update(valid_props)
        # We rely on exceptions here to indicate failure to the parent.
        self._log.debug('Creating entry %s : %s' % (dn, e))
        self._instance.add_ext_s(e, serverctrls=self._server_controls, clientctrls=self._client_controls)
        # If it worked, we need to fix our instance dn
        self._dn = dn
        return self

    def lint(self):
        """Override this to create a linter for a type. This means that we can detect
        and report common administrative errors in the server from our cli and
        rest tools.

        The structure of a result is::

          {
            dsle: '<identifier>'. dsle == ds lint error. Will be a code unique to
                                this module for the error, IE DSBLE0001.
            severity: '[HIGH:MEDIUM:LOW]'. severity of the error.
            items: '(dn,dn,dn)'. List of affected DNs or names.
            detail: 'msg ...'. An explination of the error.
            fix: 'msg ...'. Steps to resolve the error.
          }

        :returns: An array of these dicts, on None if there are no errors.
        """

        if not self._lint_functions:
            return None
        results = []
        for fn in self._lint_functions:
            result = fn()
            if result:
                results.append(result)
        return results

# A challenge of this, is how do we manage indexes? They have two naming attribunes....

class DSLdapObjects(DSLogging):
    """The object represents the next idea: "Everything is an instance of something
    that exists in this way", i.e. we unite LDAP entries by some
    set of parameters with the object.

    :param instance: A instance
    :type instance: lib389.DirSrv
    :param batch: Not implemented
    :type batch: bool
    """

    def __init__(self, instance, batch=False):
        self._childobject = DSLdapObject
        self._instance = instance
        super(DSLdapObjects, self).__init__(self._instance.verbose)
        self._objectclasses = []
        self._filterattrs = []
        self._list_attrlist = ['dn']
        # Copy this from the child if we need.
        self._basedn = ""
        self._batch = batch
        self._scope = ldap.SCOPE_SUBTREE
        self._server_controls = None
        self._client_controls = None

    def _get_objectclass_filter(self):
        return _gen_and(
            _gen_filter(_term_gen('objectclass'), self._objectclasses)
        )

    def _entry_to_instance(self, dn=None, entry=None):
        # Normally this won't be used. But for say the plugin type where we
        # have "many" possible child types, this allows us to overload
        # and select / return the right one through ALL our get/list/create
        # functions with very little work on the behalf of the overloader
        return self._childobject(instance=self._instance, dn=dn, batch=self._batch)

    def list(self):
        """Get a list of children entries (DSLdapObject, Replica, etc.) using a base DN
        and objectClasses of our object (DSLdapObjects, Replicas, etc.)

        :returns: A list of children entries
        """

        # Filter based on the objectclasses and the basedn
        insts = None
        # This will yield and & filter for objectClass with as many terms as needed.
        filterstr = self._get_objectclass_filter()
        self._log.debug('list filter = %s' % filterstr)
        try:
            results = self._instance.search_ext_s(
                base=self._basedn,
                scope=self._scope,
                filterstr=filterstr,
                attrlist=self._list_attrlist,
                serverctrls=self._server_controls, clientctrls=self._client_controls
            )
            # def __init__(self, instance, dn=None, batch=False):
            insts = [self._entry_to_instance(dn=r.dn, entry=r) for r in results]
        except ldap.NO_SUCH_OBJECT:
            # There are no objects to select from, se we return an empty array
            insts = []
        return insts

    def get(self, selector=[], dn=None):
        """Get a child entry (DSLdapObject, Replica, etc.) with dn or selector
        using a base DN and objectClasses of our object (DSLdapObjects, Replicas, etc.)

        :param dn: DN of wanted entry
        :type dn: str
        :param selector: An additional filter to objectClasses, i.e. 'backend_name'
        :type dn: str

        :returns: A child entry
        """

        results = []
        if dn is not None:
            results = self._get_dn(dn)
        else:
            results = self._get_selector(selector)

        if len(results) == 0:
            raise ldap.NO_SUCH_OBJECT("No object exists given the filter criteria %s" % selector)
        if len(results) > 1:
            raise ldap.UNWILLING_TO_PERFORM("Too many objects matched selection criteria %s" % selector)
        return self._entry_to_instance(results[0].dn, results[0])

    def _get_dn(self, dn):
        # This will yield and & filter for objectClass with as many terms as needed.
        filterstr = self._get_objectclass_filter()
        self._log.debug('_gen_dn filter = %s' % filterstr)
        return self._instance.search_ext_s(
            base=dn,
            scope=ldap.SCOPE_BASE,
            filterstr=filterstr,
            attrlist=self._list_attrlist,
            serverctrls=self._server_controls, clientctrls=self._client_controls
        )

    def _get_selector(self, selector):
        # Filter based on the objectclasses and the basedn
        # Based on the selector, we should filter on that too.
        # This will yield and & filter for objectClass with as many terms as needed.
        filterstr=_gen_and([
            self._get_objectclass_filter(),
            _gen_or(
                # This will yield all combinations of selector to filterattrs.
                # This won't work with multiple values in selector (yet)
                _gen_filter(self._filterattrs, _term_gen(selector))
            ),
        ])
        self._log.debug('_gen_selector filter = %s' % filterstr)
        return self._instance.search_ext_s(
            base=self._basedn,
            scope=self._scope,
            filterstr=filterstr,
            attrlist=self._list_attrlist,
            serverctrls=self._server_controls, clientctrls=self._client_controls
        )

    def _validate(self, rdn, properties):
        """Validate the factory part of the creation"""

        if properties is None:
            raise ldap.UNWILLING_TO_PERFORM('Invalid request to create. Properties cannot be None')
        if type(properties) != dict:
            raise ldap.UNWILLING_TO_PERFORM("properties must be a dictionary")

        # Get the rdn out of the properties if it's unset???
        if rdn is None and self._rdn_attribute in properties:
            # First see if we can get it from the properties.
            trdn = properties.get(self._rdn_attribute)
            if type(trdn) == str:
                rdn = trdn
            elif type(trdn) == list and len(trdn) != 1:
                raise ldap.UNWILLING_TO_PERFORM("Cannot determine rdn %s from properties. Too many choices" % (self._rdn_attribute))
            elif type(trdn) == list:
                rdn = trdn[0]
            else:
                raise ldap.UNWILLING_TO_PERFORM("Cannot determine rdn %s from properties, Invalid type" % type(trdn))

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
        # Make the rdn naming attr avaliable
        self._rdn_attribute = co._rdn_attribute
        (rdn, properties) = self._validate(rdn, properties)
        # Now actually commit the creation req
        return co.create(rdn, properties, self._basedn)

