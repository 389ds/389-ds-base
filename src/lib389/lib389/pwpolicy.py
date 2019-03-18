# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
from lib389._mapped_object import DSLdapObject, DSLdapObjects
from lib389.config import Config
from lib389.idm.account import Account
from lib389.idm.nscontainer import nsContainers, nsContainer
from lib389.cos import CosPointerDefinitions, CosPointerDefinition, CosTemplates

USER_POLICY = 1
SUBTREE_POLICY = 2


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
            'pwdchange': 'passwordChange',
            'pwdmustchange': 'passwordMustChange',
            'pwdhistory': 'passwordHistory',
            'pwdhistorycount': 'passwordInHistory',
            'pwdadmin': 'passwordAdminDN',
            'pwdtrack': 'passwordTrackUpdateTime',
            'pwdwarning': 'passwordWarning',
            'pwdisglobal': 'passwordIsGlobalPolicy',
            'pwdexpire': 'passwordExp',
            'pwdmaxage': 'passwordMaxAge',
            'pwdminage': 'passwordMinAge',
            'pwdgracelimit': 'passwordGraceLimit',
            'pwdsendexpiring': 'passwordSendExpiringTime',
            'pwdlockout': 'passwordLockout',
            'pwdunlock': 'passwordUnlock',
            'pwdlockoutduration': 'passwordLockoutDuration',
            'pwdmaxfailures': 'passwordMaxFailure',
            'pwdresetfailcount': 'passwordResetFailureCount',
            'pwdchecksyntax': 'passwordCheckSyntax',
            'pwdminlen': 'passwordMinLength',
            'pwdmindigits': 'passwordMinDigits',
            'pwdminalphas': 'passwordMinAlphas',
            'pwdminuppers': 'passwordMinUppers',
            'pwdminlowers': 'passwordMinLowers',
            'pwdminspecials': 'passwordMinSpecials',
            'pwdmin8bits': 'passwordMin8bit',
            'pwdmaxrepeats': 'passwordMaxRepeats',
            'pwdpalindrome': 'passwordPalindrome',
            'pwdmaxseq': 'passwordMaxSequence',
            'pwdmaxseqsets': 'passwordMaxSeqSets',
            'pwdmaxclasschars': 'passwordMaxClassChars',
            'pwdmincatagories': 'passwordMinCategories',
            'pwdmintokenlen': 'passwordMinTokenLength',
            'pwdbadwords': 'passwordBadWords',
            'pwduserattrs': 'passwordUserAttributes',
            'pwddictcheck': 'passwordDictCheck',
            'pwddictpath': 'passwordDictPath',
            'pwdallowhash': 'nsslapd-allow-hashed-passwords'
        }

    def is_user_policy(self, dn):
        """Check if the entry has a user password policy

        :param dn: Entry DN with PwPolicy set up
        :type dn: str

        :returns: True if the entry has a user policy, False otherwise
        """

        # CoSTemplate entry also can have 'pwdpolicysubentry', so we better validate this part too
        entry = Account(self._instance, dn)
        try:
            if entry.present("objectclass", "costemplate"):
                # It is a CoSTemplate entry, not user policy
                return False

            # Check if it's a subtree or a user policy
            if entry.present("pwdpolicysubentry"):
                return True
            else:
                return False
        except ldap.NO_SUCH_OBJECT:
            return False

    def is_subtree_policy(self, dn):
        """Check if the entry has a subtree password policy

        :param dn: Entry DN with PwPolicy set up
        :type dn: str

        :returns: True if the entry has a subtree policy, False otherwise
        """

        # CoSTemplate entry also can have 'pwdpolicysubentry', so we better validate this part too
        cos_pointer_def = CosPointerDefinition(self._instance, 'cn=nsPwPolicy_CoS,%s' % dn)
        if cos_pointer_def.exists():
            return True
        else:
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

        rdns = ldap.dn.explode_dn(user_entry.dn)
        rdns.pop(0)
        parentdn = ",".join(rdns)

        # Create the pwp container if needed
        pwp_containers = nsContainers(self._instance, basedn=parentdn)
        pwp_container = pwp_containers.ensure_state(properties={'cn': 'nsPwPolicyContainer'})

        # Create policy entry
        properties['cn'] = 'cn=nsPwPolicyEntry_user,%s' % dn
        pwp_entries = PwPolicyEntries(self._instance, pwp_container.dn)
        pwp_entry = pwp_entries.create(properties=properties)

        # Add policy to the entry
        user_entry.replace('pwdpolicysubentry', pwp_entry.dn)

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

        # Create policy entry
        properties['cn'] = 'cn=nsPwPolicyEntry_subtree,%s' % dn
        pwp_entries = PwPolicyEntries(self._instance, pwp_container.dn)
        pwp_entry = pwp_entries.create(properties=properties)

        # The CoS template entry (nsPwTemplateEntry)
        # that has the pwdpolicysubentry value pointing to the above (nsPwPolicyEntry) entry
        cos_templates = CosTemplates(self._instance, pwp_container.dn)
        cos_template = cos_templates.create(properties={'cosPriority': '1',
                                                        'pwdpolicysubentry': pwp_entry.dn,
                                                        'cn': 'cn=nsPwTemplateEntry,%s' % dn})

        # The CoS specification entry at the subtree level
        cos_pointer_defs = CosPointerDefinitions(self._instance, dn)
        cos_pointer_defs.create(properties={'cosAttribute': 'pwdpolicysubentry default operational',
                                            'cosTemplateDn': cos_template.dn,
                                            'cn': 'nsPwPolicy_CoS'})

        # make sure that local policies are enabled
        self.set_global_policy({'nsslapd-pwpolicy-local': 'on'})

        return pwp_entry

    def get_pwpolicy_entry(self, dn):
        """Get a local password policy entry

        :param dn: Entry DN for the local pwpolicy
        :type dn: str

        :returns: PwPolicyEntry instance
        """

        # Verify target dn exists before getting started
        entry = Account(self._instance, dn)
        if not entry.exists():
            raise ValueError('Can not get the password policy entry because the target dn does not exist')

        # Check if it's a subtree or a user policy
        if self.is_user_policy(entry.dn):
            pwp_entry_dn = entry.get_attr_val_utf8("pwdpolicysubentry")
        elif self.is_subtree_policy(entry.dn):
            pwp_container = nsContainer(self._instance, 'cn=nsPwPolicyContainer,%s' % dn)

            pwp_entries = PwPolicyEntries(self._instance, pwp_container.dn)
            pwp_entry_dn = pwp_entries.get('cn=nsPwPolicyEntry_subtree,%s' % dn).dn
        else:
            raise ValueError("The policy wasn't set up for the target dn entry or it is invalid")

        return PwPolicyEntry(self._instance, pwp_entry_dn)

    def delete_local_policy(self, dn):
        """Delete a local password policy entry

        :param dn: Entry DN for the local pwpolicy
        :type dn: str
        """

        subtree = False
        # Verify target dn exists before getting started
        entry = Account(self._instance, dn)
        if not entry.exists():
            raise ValueError('The target entry dn does not exist')

        if self.is_subtree_policy(entry.dn):
            parentdn = dn
            subtree = True
        elif self.is_user_policy(entry.dn):
            rdns = ldap.dn.explode_dn(entry.dn)
            rdns.pop(0)
            parentdn = ",".join(rdns)
        else:
            raise ValueError("The policy wasn't set up for the target dn entry or the policy is invalid")

        pwp_container = nsContainer(self._instance, 'cn=nsPwPolicyContainer,%s' % parentdn)

        pwp_entries = PwPolicyEntries(self._instance, pwp_container.dn)
        if subtree:
            pwp_entry = pwp_entries.get('cn=nsPwPolicyEntry_subtree,%s' % dn)
        else:
            pwp_entry = pwp_entries.get('cn=nsPwPolicyEntry_user,%s' % dn)

        if self.is_subtree_policy(entry.dn):
            cos_templates = CosTemplates(self._instance, pwp_container.dn)
            cos_template = cos_templates.get('cn=nsPwTemplateEntry,%s' % dn)
            cos_template.delete()

            cos_pointer_def = CosPointerDefinition(self._instance, 'cn=nsPwPolicy_CoS,%s' % dn)
            cos_pointer_def.delete()
        else:
            entry.remove("pwdpolicysubentry", pwp_entry.dn)

        pwp_entry.delete()
        try:
            pwp_container.delete()
        except ldap.NOT_ALLOWED_ON_NONLEAF:
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

