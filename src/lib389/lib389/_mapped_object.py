# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import logging

from lib389._constants import *
from lib389.utils import ensure_bytes, ensure_str

from lib389._entry import Entry

# This function filter and term generation provided thanks to
# The University Of Adelaide. <william@adelaide.edu.au>

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
            filt += '(%s=%s)' % (attr, value)
    if extra is not None:
        filt += '{FILT}'.format(FILT=extra)
    return filt

class DSLogging(object):
    """
    The benefit of this is automatic name detection, and correct application of level
    and verbosity to the object.
    """
    def __init__(self, verbose=False):
        # Maybe we can think of a way to make this display the instance name or __unicode__?
        self._log = logging.getLogger(type(self).__name__)
        if verbose:
            self._log.setLevel(logging.DEBUG)


class DSLdapObject(DSLogging):
    def __init__(self, instance, dn=None, batch=False):
        """
        """
        self._instance = instance
        super(DSLdapObject, self).__init__(self._instance.verbose)
        # This allows some factor objects to be overriden
        self._dn = ''
        if dn is not None:
            self._dn = dn

        self._batch = batch
        self._naming_attr = None
        self._protected = True

    def __unicode__(self):
        val = self._dn
        if self._naming_attr:
            val = self.get(self._naming_attr)
        return ensure_str(val)

    def __str__(self):
        return self.__unicode__()

    def set(self, key, value):
        self._log.debug("%s set(%r, %r)" % (self._dn, key, value))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            raise ValueError("Invalid state. Cannot set properties on instance that is not ONLINE.")
        if self._batch:
            pass
        else:
            return self._instance.modify_s(self._dn, [(ldap.MOD_REPLACE, key, value)])

    def get(self, key):
        """Get an attribute under dn"""
        self._log.debug("%s get(%r)" % (self._dn, key))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
            # In the future, I plan to add a mode where if local == true, we can use
            # get on dse.ldif to get values offline.
        else:
            return self._instance.getEntry(self._dn).getValues(key)

    def remove(self, key):
        """Remove a value defined by key"""
        self._log.debug("%s get(%r, %r)" % (self._dn, key, value))
        if self._instance.state != DIRSRV_STATE_ONLINE:
            ValueError("Invalid state. Cannot remove properties on instance that is not ONLINE")
        else:
            # Do a mod_delete on the value.
            pass

    def delete(self):
        """
        Deletes the object defined by self._dn.
        This can be changed with the self._protected flag!
        """
        self._log.debug("%s delete" % (self._dn))
        if not self._protected:
            pass


# A challenge of this, is how do we manage indexes? They have two naming attribunes....

class DSLdapObjects(DSLogging):
    def __init__(self, instance, batch=False):
        self._childobject = DSLdapObject
        self._instance = instance
        super(DSLdapObjects, self).__init__(self._instance.verbose)
        self._objectclasses = []
        self._create_objectclasses = []
        self._filterattrs = []
        self._list_attrlist = ['dn']
        self._basedn = ""
        self._batch = batch
        self._scope = ldap.SCOPE_SUBTREE
        self._rdn_attribute = None
        self._must_attributes = None

    def list(self):
        # Filter based on the objectclasses and the basedn
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
        insts = map(lambda r: self._childobject(instance=self._instance, dn=r.dn, batch=self._batch), results)
        return insts

    def get(self, selector):
        # Filter based on the objectclasses and the basedn
        # Based on the selector, we should filter on that too.
        results = self._instance.search_s(
            base=self._basedn,
            scope=self._scope,
            # This will yield and & filter for objectClass with as many terms as needed.
            filterstr=_gen_and(
                _gen_filter(_term_gen('objectclass'), self._objectclasses,
                    extra=_gen_or(
                        # This will yield all combinations of selector to filterattrs.
                        # This won't work with multiple values in selector (yet)
                        _gen_filter(self._filterattrs, _term_gen(selector))
                    )
                )
            ),
            attrlist=self._list_attrlist,
        )

        if len(results) == 0:
            raise ldap.NO_SUCH_OBJECT("No object exists given the filter criteria %s" % selector)
        if len(results) > 1:
            raise ldap.UNWILLING_TO_PERFORM("Too many objects matched selection criteria %s" % selector)
        return self._childobject(instance=self._instance, dn=results[0].dn, batch=self._batch)

    def _validate(self, rdn, properties):
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

        # Get the rdn out of the properties if it's unset???
        if rdn is None and self._rdn_attribute in properties:
            # First see if we can get it from the properties.
            trdn = properties.get(self._rdn_attribute)
            if type(trdn) != list:
                raise ldap.UNWILLING_TO_PERFORM("rdn %s from properties is not in a list" % self._rdn_attribute)
            if len(trdn) != 1:
                raise ldap.UNWILLING_TO_PERFORM("Cannot determine rdn %s from properties. Too many choices" % (self._rdn_attribute))
            rdn = trdn[0]

        if type(rdn) != str:
            raise ldap.UNWILLING_TO_PERFORM("rdn %s must be a utf8 string (str)", rdn)

        for attr in self._must_attributes:
            if properties.get(attr, None) is None:
                raise ldap.UNWILLING_TO_PERFORM('Attribute %s must not be None' % attr)

        # We may need to map over the data in the properties dict to satisfy python-ldap
        # to do str -> bytes
        #
        # Do we need to fix anything here in the rdn_attribute?
        dn = '%s=%s,%s' % (self._rdn_attribute, rdn, self._basedn)
        # Do we need to do extra dn validation here?
        return (dn, rdn, properties)

    def create(self, rdn=None, properties=None):
        assert(len(self._create_objectclasses) > 0)
        self._log.debug('Creating %s : %s' % (rdn, properties))
        # Make sure these aren't none.
        # Create the dn based on the various properties.
        (dn, rdn, valid_props) = self._validate(rdn, properties)
        # Check if the entry exists or not? .add_s is going to error anyway ...
        self._log.debug('Validated %s : %s' % (dn, properties))

        e = Entry(dn)
        e.update({'objectclass' : self._create_objectclasses})
        e.update(valid_props)
        self._instance.add_s(e)

        # Now return the created instance.
        return self._childobject(instance=self._instance, dn=dn, batch=self._batch)

