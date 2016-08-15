# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from ldap import filter as ldap_filter
import logging

from lib389._constants import *
from lib389.utils import ensure_bytes, ensure_str, ensure_list_bytes

from lib389._entry import Entry

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
    """
    The benefit of this is automatic name detection, and correct application
    of level and verbosity to the object.
    """
    def __init__(self, verbose=False):
        # Maybe we can think of a way to make this display the instance name or __unicode__?
        self._log = logging.getLogger(type(self).__name__)
        if verbose:
            self._log.setLevel(logging.DEBUG)
        else:
            self._log.setLevel(logging.INFO)


class DSLdapObject(DSLogging):
    # TODO: Automatically create objects when they are requested to have properties added
    def __init__(self, instance, dn=None, batch=False):
        """
        """
        self._instance = instance
        super(DSLdapObject, self).__init__(self._instance.verbose)
        # This allows some factor objects to be overriden
        self._dn = None
        if dn is not None:
            self._dn = dn

        self._batch = batch
        self._protected = True
        # Used in creation
        self._create_objectclasses = []
        self._rdn_attribute = None
        self._must_attributes = None

    def __unicode__(self):
        val = self._dn
        if self._rdn_attribute:
            # What if the rdn is multi value and we don't get the primary .... ARGHHH
            val = self.get_attr_val(self._rdn_attribute)
        return ensure_str(val)

    def __str__(self):
        return self.__unicode__()

    def display(self):
        e = self._instance.getEntry(self._dn)
        return e.__repr__()

    # We make this a property so that we can over-ride dynamically if needed
    @property
    def dn(self):
        return self._dn

    def present(self, attr, value=None):
        """
        Assert that some attr, or some attr / value exist on the entry.
        """
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get presence on instance that is not ONLINE")
        self._log.debug("%s present(%r) %s" % (self._dn, attr, value))

        e = self._instance.getEntry(self._dn)
        if value is None:
            return e.hasAttr(attr)
        else:
            return e.hasValue(attr, value)

    def add(self, key, value):
        self.set(key, value, action=ldap.MOD_ADD)

    # Basically what it means;
    def replace(self, key, value):
        self.set(key, value, action=ldap.MOD_REPLACE)

    # This needs to work on key + val, and key
    def remove(self, key, value):
        """Remove a value defined by key"""
        # Do a mod_delete on the value.
        self.set(key, value, action=ldap.MOD_DELETE)

    # maybe this could be renamed?
    def set(self, key, value, action=ldap.MOD_REPLACE):
        self._log.debug("%s set(%r, %r)" % (self._dn, key, value))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot set properties on instance that is not ONLINE.")

        if isinstance(value, list):
            # value = map(lambda x: ensure_bytes(x), value)
            value = ensure_list_bytes(value)
        else:
            value = [ensure_bytes(value)]

        if self._batch:
            pass
        else:
            return self._instance.modify_s(self._dn, [(action, key, value)])

    def apply_mods(self, mods):
        """Perform modification operation using several mods at once

        @param mods - list of tuples:  [(action, key, value),]
        @raise ValueError - if a provided mod op is invalid
        @raise LDAPError
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
        return self._instance.modify_s(self._dn, mod_list)

    def get_attr_vals(self, key):
        """Get an attribute's values from the dn"""
        self._log.debug("%s get(%r)" % (self._dn, key))
        # We might need to add a state check for NONE dn.
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we
            # can use get on dse.ldif to get values offline.
        else:
            return self._instance.getEntry(self._dn).getValues(key)

    def get_attr_val(self, key):
        """Get a single attribute value from the dn"""
        self._log.debug("%s getVal(%r)" % (self._dn, key))
        # We might need to add a state check for NONE dn.
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we
            # can use get on dse.ldif to get values offline.
        else:
            return self._instance.getEntry(self._dn).getValue(key)

    # Duplicate, but with many values. IE a dict api.
    # This
    def add_values(self, values):
        pass

    def replace_values(self, values):
        pass

    def set_values(self, values, action=ldap.MOD_REPLACE):
        pass

    def delete(self):
        """
        Deletes the object defined by self._dn.
        This can be changed with the self._protected flag!
        """
        self._log.debug("%s delete" % (self._dn))
        if not self._protected:
            # Is there a way to mark this as offline and kill it
            self._instance.delete_s(self._dn)

    def _validate(self, rdn, properties, basedn):
        """
        Used to validate a create request.
        This way, it can be over-ridden without affecting
        the create types

        It also checks that all the values in _must_attribute exist
        in some form in the dictionary

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
        
        # This change here, means we can pre-load a full dn to _dn, or we can
        # accept based on the rdn
        tdn = self._dn

        if tdn is None:
            if basedn is None:
                raise ldap.UNWILLING_TO_PERFORM('Invalid request to create. basedn cannot be None')

            if properties.get(self._rdn_attribute, None) is not None:
                # Favour the value in the properties dictionary
                v = properties.get(self._rdn_attribute)
                if isinstance(v, list):
                    rdn = ensure_str(v[0])
                else:
                    rdn = ensure_str(v)

                tdn = '%s=%s,%s' % (self._rdn_attribute, rdn, basedn)

        # We may need to map over the data in the properties dict to satisfy python-ldap
        str_props = {}
        for k, v in properties.items():
            if isinstance(v, list):
                # str_props[k] = map(lambda v1: ensure_bytes(v1), v)
                str_props[k] = ensure_list_bytes(v)
            else:
                str_props[k] = ensure_bytes(v)
        #
        # Do we need to do extra dn validation here?
        return (tdn, str_props)

    def create(self, rdn=None, properties=None, basedn=None):
        assert(len(self._create_objectclasses) > 0)
        self._log.debug('Creating %s %s : %s' % (rdn, basedn, properties))
        # Add the objectClasses to the properties
        (dn, valid_props) = self._validate(rdn, properties, basedn)
        # Check if the entry exists or not? .add_s is going to error anyway ...
        self._log.debug('Validated %s : %s' % (dn, valid_props))

        e = Entry(dn)
        e.update({'objectclass': ensure_list_bytes(self._create_objectclasses)})
        e.update(valid_props)
        # We rely on exceptions here to indicate failure to the parent.
        self._log.debug('Creating entry %s : %s' % (dn, e))
        self._instance.add_s(e)
        # If it worked, we need to fix our instance dn
        self._dn = dn
        return self


# A challenge of this, is how do we manage indexes? They have two naming attribunes....

class DSLdapObjects(DSLogging):
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

    def _entry_to_instance(self, dn=None, entry=None):
        # Normally this won't be used. But for say the plugin type where we
        # have "many" possible child types, this allows us to overload
        # and select / return the right one through ALL our get/list/create
        # functions with very little work on the behalf of the overloader
        return self._childobject(instance=self._instance, dn=dn, batch=self._batch)

    def list(self):
        # Filter based on the objectclasses and the basedn
        insts = None
        try:
            results = self._instance.search_s(
                base=self._basedn,
                scope=self._scope,
                # This will yield and & filter for objectClass with as many terms as needed.
                filterstr=_gen_and(
                    _gen_filter(_term_gen('objectclass'), self._objectclasses)
                ),
                attrlist=self._list_attrlist,
            )
            # def __init__(self, instance, dn=None, batch=False):
            insts = [self._entry_to_instance(dn=r.dn, entry=r) for r in results]
        except ldap.NO_SUCH_OBJECT:
            # There are no objects to select from, se we return an empty array
            insts = []
        return insts

    def get(self, selector=[], dn=None):
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
        return self._instance.search_s(
            base=dn,
            scope=ldap.SCOPE_BASE,
            # This will yield and & filter for objectClass with as many terms as needed.
            filterstr=_gen_and(
                _gen_filter(_term_gen('objectclass'), self._objectclasses,)
            ),
            attrlist=self._list_attrlist,
        )

    def _get_selector(self, selector):
        # Filter based on the objectclasses and the basedn
        # Based on the selector, we should filter on that too.
        return self._instance.search_s(
            base=self._basedn,
            scope=self._scope,
            # This will yield and & filter for objectClass with as many terms as needed.
            filterstr=_gen_and(
                _gen_filter(_term_gen('objectclass'), self._objectclasses, extra=_gen_or(
                        # This will yield all combinations of selector to filterattrs.
                        # This won't work with multiple values in selector (yet)
                        _gen_filter(self._filterattrs, _term_gen(selector))
                    )
                )
            ),
            attrlist=self._list_attrlist,
        )

    def _validate(self, rdn, properties):
        """
        Validate the factory part of the creation
        """
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
        # Create the object
        # Should we inject the rdn to properties?
        # This may not work in all cases, especially when we consider plugins.
        # 
        co = self._entry_to_instance(dn=None, entry=None)
        # Make the rdn naming attr avaliable
        self._rdn_attribute = co._rdn_attribute
        (rdn, properties) = self._validate(rdn, properties)
        # Now actually commit the creation req
        return co.create(rdn, properties, self._basedn)
