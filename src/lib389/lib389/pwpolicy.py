# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from ldap import filter as ldap_filter
from lib389._mapped_object import DSLdapObject, DSLdapObjects
from lib389.backend import Backends
from lib389.config import Config
from lib389.idm.account import Account
from lib389.idm.nscontainer import nsContainers, nsContainer
from lib389.cos import CosPointerDefinitions, CosPointerDefinition, CosTemplates


class PwPolicyManager(object):
    """Manages user, subtree and global password policies

    :param instance: An instance
    :type instance: lib389.DirSrv
    """

    def __init__(self, instance):
        self._instance = instance
        self.log = instance.log
        self.arg_to_attr = {
            'pwdlocal': 'nsslapd-pwpolicy-local',
            'pwdscheme': 'passwordstoragescheme',
            'pwdchange': 'passwordchange',
            'pwdmustchange': 'passwordmustchange',
            'pwdhistory': 'passwordhistory',
            'pwdhistorycount': 'passwordinhistory',
            'pwdadmin': 'passwordadmindn',
            'pwdadminskipupdates': 'passwordadminskipinfoupdate',
            'pwdtrack': 'passwordtrackupdatetime',
            'pwdwarning': 'passwordwarning',
            'pwdisglobal': 'passwordisglobalpolicy',
            'pwdexpire': 'passwordexp',
            'pwdmaxage': 'passwordmaxage',
            'pwdminage': 'passwordminage',
            'pwdgracelimit': 'passwordgracelimit',
            'pwdsendexpiring': 'passwordsendexpiringtime',
            'pwdlockout': 'passwordlockout',
            'pwdunlock': 'passwordunlock',
            'pwdlockoutduration': 'passwordlockoutduration',
            'pwdmaxfailures': 'passwordmaxfailure',
            'pwdresetfailcount': 'passwordresetfailurecount',
            'pwdchecksyntax': 'passwordchecksyntax',
            'pwdminlen': 'passwordminlength',
            'pwdmindigits': 'passwordmindigits',
            'pwdminalphas': 'passwordminalphas',
            'pwdminuppers': 'passwordminuppers',
            'pwdminlowers': 'passwordminlowers',
            'pwdminspecials': 'passwordminspecials',
            'pwdmin8bits': 'passwordmin8bit',
            'pwdmaxrepeats': 'passwordmaxrepeats',
            'pwdpalindrome': 'passwordpalindrome',
            'pwdmaxseq': 'passwordmaxsequence',
            'pwdmaxseqsets': 'passwordmaxseqsets',
            'pwdmaxclasschars': 'passwordmaxclasschars',
            'pwdmincatagories': 'passwordmincategories',
            'pwdmintokenlen': 'passwordmintokenlength',
            'pwdbadwords': 'passwordbadwords',
            'pwduserattrs': 'passworduserattributes',
            'pwddictcheck': 'passworddictcheck',
            'pwddictpath': 'passworddictpath',
            'pwdallowhash': 'nsslapd-allow-hashed-passwords',
            'pwpinheritglobal': 'nsslapd-pwpolicy-inherit-global',
            'pwptprmaxuse': 'passwordTPRMaxUse',
            'pwptprdelayexpireat': 'passwordTPRDelayExpireAt',
            'pwptprdelayvalidfrom': 'passwordTPRDelayValidFrom'
        }

    def _is_duplicate_policy(self, entry, new_props):
        """Check if an existing password policy entry matches the given properties.

        Compares an existing pwp entry with new properties to determine if they are identical.
        - Ignore the cn attribute, as it may contain escaped variants.
        - Normalises keys to lowercase
        - Converts all values to lists of strings
        - Compares values

        :param entry: The existing PwPolicyEntry to compare against.
        :type entry: PwPolicyEntry
        :param new_props: Dictionary of new password policy properties.
        :type new_props: dict
        :returns: True if the existing policy is a duplicate of the new properties, False otherwise.
        :rtype: bool
        """

        # Helper to normalise a value into a list of strings
        def _normalise_val(val):
            if val is None:
                return []
            if isinstance(val, (list, tuple)):
                return [str(v) for v in val]
            return [str(val)]

        # Normalise existing policy properties
        existing = entry.get_attrs_vals_utf8(new_props.keys())
        existing_norm = {}
        for k, v in existing.items():
            if k.lower() == 'cn':
                continue
            key_norm = k.lower()
            val_norm = _normalise_val(v)
            existing_norm[key_norm] = val_norm

        # Normalise new entry properties
        new_norm = {}
        for k, v in new_props.items():
            if k.lower() == 'cn':
                continue
            key_norm = k.lower()
            val_norm = _normalise_val(v)
            new_norm[key_norm] = val_norm

        # Compare policies
        for key, new_vals in new_norm.items():
            old_vals = existing_norm.get(key, [])
            if sorted(old_vals) != sorted(new_vals):
                return False

        return True

    def is_subtree_policy(self, dn):
        """Check if a subtree password policy exists for a given entry DN.

        A subtree policy is indicated by the presence of any CoS template
        (under `cn=nsPwPolicyContainer,<dn>`) that has a `pwdpolicysubentry`
        attribute pointing to an existing entry with objectClass `passwordpolicy`.

        :param dn: Entry DN to check for subtree policy
        :type dn: str

        :returns: True if a subtree policy exists, False otherwise
        :rtype: bool
        """
        try:
            container_basedn = 'cn=nsPwPolicyContainer,{}'.format(dn)
            templates = CosTemplates(self._instance, container_basedn).list()
            for tmpl in templates:
                pwp_dn = tmpl.get_attr_val_utf8('pwdpolicysubentry')
                if not pwp_dn:
                    continue
                # Validate that the referenced entry exists and is a passwordpolicy
                pwp_entry = PwPolicyEntry(self._instance, pwp_dn)
                if pwp_entry.exists() and pwp_entry.present('objectClass', 'passwordpolicy'):
                    return True
        except ldap.LDAPError:
            pass
        return False

    def is_user_policy(self, dn):
        """Check if the entry has a user password policy.

        A user policy is indicated by the target entry having a
        `pwdpolicysubentry` attribute that points to an existing
        entry with objectClass `passwordpolicy`.

        :param dn: Entry DN to check
        :type dn: str

        :returns: True if the entry has a user policy, False otherwise
        :rtype: bool
        """
        try:
            entry = Account(self._instance, dn)
            if not entry.exists():
                return False
            pwp_dn = entry.get_attr_val_utf8('pwdpolicysubentry')
            if not pwp_dn:
                return False
            pwp_entry = PwPolicyEntry(self._instance, pwp_dn)
            return pwp_entry.exists() and pwp_entry.present('objectClass', 'passwordpolicy')
        except ldap.LDAPError:
            return False

    def create_user_policy(self, dn, properties):
        """Creates all entries which are needed for the user
        password policy

        :param dn: Entry DN for the subtree pwpolicy
        :type dn: str
        :param properties: A dict with password policy settings
        :type properties: dict

        :returns: PwPolicyEntry instance
        """

        # Verify target dn exists before getting started
        user_entry = Account(self._instance, dn)
        if not user_entry.exists():
            raise ValueError('Can not create user password policy because the target dn does not exist')

        dn_comps = ldap.dn.explode_dn(user_entry.dn)
        dn_comps.pop(0)
        parentdn = ",".join(dn_comps)

        # Create the pwp container if needed
        pwp_containers = nsContainers(self._instance, basedn=parentdn)
        pwp_container = pwp_containers.ensure_state(properties={'cn': 'nsPwPolicyContainer'})

        # Create or update a policy entry
        properties['cn'] = 'cn=nsPwPolicyEntry_user,%s' % dn
        pwp_entries = PwPolicyEntries(self._instance, pwp_container.dn)
        entry_dn = properties['cn']

        if not pwp_entries.exists(entry_dn):
            pwp_entry = pwp_entries.create(properties=properties)
        else:
            existing_entry = pwp_entries.get(entry_dn)
            if self._is_duplicate_policy(existing_entry, properties):
                raise ldap.ALREADY_EXISTS(f"User password policy already exists")
            pwp_entry = pwp_entries.ensure_state(properties=properties)

        try:
            # Add policy to the entry
            user_entry.replace('pwdpolicysubentry', pwp_entry.dn)
        except ldap.LDAPError as e:
            # failure, undo what we have done
            pwp_entry.delete()
            raise e

        # make sure that local policies are enabled
        self.set_global_policy({'nsslapd-pwpolicy-local': 'on'})

        return pwp_entry

    def create_subtree_policy(self, dn, properties):
        """Creates all entries which are needed for the subtree
        password policy

        :param dn: Entry DN for the subtree pwpolicy
        :type dn: str
        :param properties: A dict with password policy settings
        :type properties: dict

        :returns: PwPolicyEntry instance
        """

        # Verify target dn exists before getting started
        subtree_entry = Account(self._instance, dn)
        if not subtree_entry.exists():
            raise ValueError('Can not create subtree password policy because the target dn does not exist')

        # Create the pwp container if needed
        pwp_containers = nsContainers(self._instance, basedn=dn)
        pwp_container = pwp_containers.ensure_state(properties={'cn': 'nsPwPolicyContainer'})

        # Create or update a policy entry
        properties['cn'] = 'cn=nsPwPolicyEntry_subtree,%s' % dn
        pwp_entries = PwPolicyEntries(self._instance, pwp_container.dn)
        entry_dn = properties['cn']
        if not pwp_entries.exists(entry_dn):
            pwp_entry = pwp_entries.create(properties=properties)
        else:
            existing_entry = pwp_entries.get(entry_dn)
            if self._is_duplicate_policy(existing_entry, properties):
                raise ldap.ALREADY_EXISTS(f"Subtree password policy already exists")
            pwp_entry = pwp_entries.ensure_state(properties=properties)

        # Ensure the CoS template entry (nsPwTemplateEntry) that points to the
        # password policy entry
        cos_templates = CosTemplates(self._instance, pwp_container.dn)
        cos_template = cos_templates.ensure_state(properties={
            'cosPriority': '1',
            'pwdpolicysubentry': pwp_entry.dn,
            'cn': 'cn=nsPwTemplateEntry,%s' % dn
        })

        # Ensure the CoS specification entry at the subtree level
        cos_pointer_defs = CosPointerDefinitions(self._instance, dn)
        cos_pointer_defs.ensure_state(properties={
            'cosAttribute': 'pwdpolicysubentry default operational-default',
            'cosTemplateDn': cos_template.dn,
            'cn': 'nsPwPolicy_CoS'
        })

        # make sure that local policies are enabled
        self.set_global_policy({'nsslapd-pwpolicy-local': 'on'})

        return pwp_entry

    def get_pwpolicy_entry(self, dn):
        """Get a local password policy entry

        :param dn: Entry DN for the local pwpolicy
        :type dn: str

        :returns: PwPolicyEntry instance
        """

        entry = Account(self._instance, dn)
        if not entry.exists():
            raise ValueError('Can not get the password policy entry because the target dn does not exist')

        try:
            Backends(self._instance).get(dn)
            # The DN is a base suffix, it has no parent
            parentdn = dn
        except:
            # Ok, this is a not a suffix, get the parent DN
            dn_comps = ldap.dn.explode_dn(entry.dn)
            dn_comps.pop(0)
            parentdn = ",".join(dn_comps)

        # Get the parent's policies
        pwp_entries = PwPolicyEntries(self._instance, parentdn)
        policies = pwp_entries.list()

        for policy in policies:
            # Sometimes, the cn value includes quotes (for example, after migration from pre-CLI version).
            # We need to strip them as python-ldap doesn't expect them
            dn_comps_str = policy.get_attr_val_utf8_l('cn').strip("\'").strip("\"")
            try:
                dn_comps = ldap.dn.explode_dn(dn_comps_str)
                dn_comps.pop(0)
                pwp_dn = ",".join(dn_comps)
                if pwp_dn == dn.lower():
                    # This DN does have a policy associated with it
                    return policy
            except ldap.DECODING_ERROR:
                # This is some kind of custom password policy
                continue

        # Did not find a policy for this entry
        raise ValueError("No password policy was found for this entry")

    def delete_local_policy(self, dn):
        """Delete a local password policy entry

        :param dn: Entry DN for the local pwpolicy
        :type dn: str
        """

        subtree = False

        # Verify target dn exists, and has a policy
        entry = Account(self._instance, dn)
        if not entry.exists():
            raise ValueError('The target entry dn does not exist')
        pwp_entry = self.get_pwpolicy_entry(entry.dn)

        # Subtree or user policy?
        if self.is_subtree_policy(entry.dn):
            parentdn = dn
            subtree = True
        elif self.is_user_policy(entry.dn):
            dn_comps = ldap.dn.explode_dn(dn)
            dn_comps.pop(0)
            parentdn = ",".join(dn_comps)
        else:
            raise ValueError('The target entry dn does not have a password policy')

        # Starting deleting the policy, ignore the parts that might already have been removed
        pwp_container = nsContainer(self._instance, 'cn=nsPwPolicyContainer,%s' % parentdn)
        if subtree:
            try:
                # Delete template
                cos_templates = CosTemplates(self._instance, pwp_container.dn)
                cos_template = cos_templates.get('cn=nsPwTemplateEntry,%s' % dn)
                cos_template.delete()
            except ldap.NO_SUCH_OBJECT:
                # Already deleted
                pass
            try:
                # Delete definition
                cos_pointer_def = CosPointerDefinition(self._instance, 'cn=nsPwPolicy_CoS,%s' % dn)
                cos_pointer_def.delete()
            except ldap.NO_SUCH_OBJECT:
                # Already deleted
                pass
        else:
            try:
                # Cleanup user entry
                entry.remove("pwdpolicysubentry", pwp_entry.dn)
            except ldap.NO_SUCH_ATTRIBUTE:
                # Policy already removed from user
                pass

        # Remove the local policy
        try:
            pwp_entry.delete()
        except ldap.NO_SUCH_OBJECT:
            # Already deleted
            pass

        try:
            pwp_container.delete()
        except (ldap.NOT_ALLOWED_ON_NONLEAF, ldap.NO_SUCH_OBJECT):
            # There are other policies still using this container, no problem
            pass

    def set_global_policy(self, properties):
        """Configure global password policy

        :param properties: A dictionary with password policy attributes
        :type properties: dict
        """

        modlist = []
        for attr, value in properties.items():
            modlist.append((attr, value))

        if len(modlist) > 0:
            config = Config(self._instance)
            config.replace_many(*modlist)
        else:
            raise ValueError("There are no password policies to set")


class PwPolicyEntry(DSLdapObject):
    """A single instance of a task entry

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str
    """

    def __init__(self, instance, dn):
        super(PwPolicyEntry, self).__init__(instance, dn)
        self._rdn_attribute = 'cn'
        self._must_attributes = ['cn']
        self._create_objectclasses = ['top', 'ldapsubentry', 'passwordpolicy']
        self._protected = False

    def is_user_policy(self):
        """Check if the policy entry is a user password policy"""

        pwp_manager = PwPolicyManager(self._instance)
        cn = self.get_attr_val_utf8_l('cn')
        entrydn = cn.replace('cn=nspwpolicyentry_user,', '')

        return pwp_manager.is_user_policy(entrydn)

    def is_subtree_policy(self):
        """Check if the policy entry is a user password policy"""

        pwp_manager = PwPolicyManager(self._instance)
        cn = self.get_attr_val_utf8_l('cn')
        entrydn = cn.replace('cn=nspwpolicyentry_subtree,', '')

        return pwp_manager.is_subtree_policy(entrydn)


class PwPolicyEntries(DSLdapObjects):
    """DSLdapObjects that represents all password policy entries in container.

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param basedn: Suffix DN
    :type basedn: str
    :param rdn: The DN that will be combined wit basedn
    :type rdn: str
    """

    def __init__(self, instance, basedn):
        super(PwPolicyEntries, self).__init__(instance)
        self._objectclasses = [
            'top',
            'ldapsubentry',
            'passwordpolicy'
        ]
        self._filterattrs = ['cn']
        self._childobject = PwPolicyEntry
        self._basedn = basedn

