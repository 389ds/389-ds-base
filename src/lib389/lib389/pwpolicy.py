# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import json
from ldap import modlist
from lib389.utils import ensure_str, ensure_list_str, ensure_bytes

USER_POLICY = 1
SUBTREE_POLICY = 2


class Pwpolicy(object):
    """A local password policy, either user or subtree

    :param instance: An instance
    :type instance: lib389.DirSrv
    :param dn: Entry DN
    :type dn: str


    cn=nsPwPolicyEntry,DN_OF_ENTRY,cn=nsPwPolicyContainer,SUFFIX

    """

    def __init__(self, conn):
        self.conn = conn
        self.log = conn.log
        self.pwp_attributes = [
            'passwordstoragescheme',
            'passwordChange',
            'passwordMustChange',
            'passwordHistory',
            'passwordInHistory',
            'passwordAdminDN',
            'passwordTrackUpdateTime',
            'passwordWarning',
            'passwordMaxAge',
            'passwordMinAge',
            'passwordExp',
            'passwordGraceLimit',
            'passwordSendExpiringTime',
            'passwordLockout',
            'passwordUnlock',
            'passwordMaxFailure',
            'passwordLockoutDuration',
            'passwordResetFailureCount',
            'passwordCheckSyntax',
            'passwordMinLength',
            'passwordMinDigits',
            'passwordMinAlphas',
            'passwordMinUppers',
            'passwordMinLowers',
            'passwordMinSpecials',
            'passwordMaxRepeats',
            'passwordMin8bit',
            'passwordMinCategories',
            'passwordMinTokenLength',
            'passwordDictPath',
            'passwordDictCheck',
            'passwordPalindrome',
            'passwordMaxSequence',
            'passwordMaxClassChars',
            'passwordMaxSeqSets',
            'passwordBadWords',
            'passwordUserAttributes',
            'passwordIsGlobalPolicy',
            'nsslapd-pwpolicy-local',
            'nsslapd-allow-hashed-passwords'
        ]

    def create_pwp_container(self, basedn):
        attrs = {'objectclass': [b'top', b'nsContainer'],
                 'cn': [b'nsPwPolicyContainer']}
        ldif = modlist.addModlist(attrs)
        try:
            self.conn.add_ext_s(basedn, ldif)
        except ldap.ALREADY_EXISTS:
            # Already exists, no problem
            pass

    def create_user_policy(self, targetdn, args, arg_to_attr):
        """Create a local user password policy entry
        """

        # Verify target dn exists before getting started
        try:
            self.conn.search_s(targetdn, ldap.SCOPE_BASE, "objectclass=*")
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('Can not create user password policy because the target dn does not exist')

        rdns = ldap.dn.explode_dn(targetdn)
        rdns.pop(0)
        parentdn = ",".join(rdns)

        # Create the pwp container if needed
        self.create_pwp_container("cn=nsPwPolicyContainer,{}".format(parentdn))

        # Gather the attributes and create policy entry
        attrs = {}
        for arg in vars(args):
            val = getattr(args, arg)
            if arg in arg_to_attr and val is not None:
                attrs[arg_to_attr[arg]] = ensure_bytes(val)
        attrs['objectclass'] = [b'top', b'ldapsubentry', b'passwordpolicy']
        ldif = modlist.addModlist(attrs)
        user_dn = 'cn="cn=nsPwPolicyEntry,{}",cn=nsPwPolicyContainer,{}'.format(targetdn, parentdn)
        self.conn.add_ext_s(user_dn, ldif)

        # Add policy to entry
        self.conn.modify_s(targetdn, [(ldap.MOD_REPLACE, 'pwdpolicysubentry', ensure_bytes(user_dn))])

    def create_subtree_policy(self, targetdn, args, arg_to_attr):
        """Create a local subtree password policy entry - requires COS entry
        """

        # Verify target dn exists before getting started
        try:
            self.conn.search_s(targetdn, ldap.SCOPE_BASE, "objectclass=*")
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('Can not create user password policy because the target dn does not exist')

        # Create the pwp container if needed
        container_dn = "cn=nsPwPolicyContainer,{}".format(targetdn)
        self.create_pwp_container(container_dn)

        # Create COS entries
        cos_template_entry = 'cn=nsPwTemplateEntry,{}'.format(targetdn)
        cos_template_dn = 'cn="cn=nsPwTemplateEntry,{}",{}'.format(targetdn, container_dn)
        cos_subentry_dn = 'cn="cn=nsPwPolicyEntry,{}",{}'.format(targetdn, container_dn)
        cos_template_attrs = {'cosPriority': b'1', 'pwdpolicysubentry': ensure_bytes(cos_subentry_dn),
                              'cn': ensure_bytes(cos_template_entry)}

        cos_template_attrs['objectclass'] = [b'top', b'ldapsubentry', b'extensibleObject', b'costemplate']
        ldif = modlist.addModlist(cos_template_attrs)
        self.conn.add_ext_s(cos_template_dn, ldif)

        cos_def_attrs = {'objectclass': [b'top', b'ldapsubentry', b'extensibleObject',
                                         b'cosSuperDefinition', b'cosPointerDefinition'],
                         'cosAttribute': b'pwdpolicysubentry default operational',
                         'cosTemplateDn': ensure_bytes(cos_template_dn),
                         'cn': b'nsPwPolicy_CoS'}
        ldif = modlist.addModlist(cos_def_attrs)
        self.conn.add_ext_s("cn=nsPwPolicy_CoS,{}".format(targetdn), ldif)

        # Gather the attributes and create policy sub entry
        attrs = {}
        for arg in vars(args):
            val = getattr(args, arg)
            if arg in arg_to_attr and val is not None:
                attrs[arg_to_attr[arg]] = ensure_bytes(val)
        attrs['objectclass'] = [b'top', b'ldapsubentry', b'passwordpolicy']
        ldif = modlist.addModlist(attrs)
        try:
            self.conn.add_ext_s(cos_subentry_dn, ldif)
        except ldap.ALREADY_EXISTS:
            # Already exists, no problem
            pass

        # Add policy to entry
        self.conn.modify_s(targetdn, [(ldap.MOD_REPLACE, 'pwdpolicysubentry', ensure_bytes(cos_subentry_dn))])

    def delete_local_policy(self, targetdn):
        container_dn = "cn=nsPwPolicyContainer,{}".format(targetdn)

        # First check that the entry exists
        try:
            entries = self.conn.search_s(targetdn, ldap.SCOPE_BASE, 'objectclass=top', ['pwdpolicysubentry'])
            target_entry = entries[0]
        except ldap.NO_SUCH_OBJECT:
            raise ValueError('The entry does not exist, nothing to remove')

        # Subtree or local policy?
        try:
            cos_def_dn = 'cn=nsPwPolicy_CoS,{}'.format(targetdn)
            self.conn.search_s(cos_def_dn, ldap.SCOPE_BASE, "(|(objectclass=ldapsubentry)(objectclass=*))")
            found_subtree = True
        except:
            found_subtree = False

        # If subtree policy then remove COS template and definition
        if found_subtree:
            container_dn = "cn=nsPwPolicyContainer,{}".format(targetdn)
            cos_template_dn = 'cn="cn=nsPwTemplateEntry,{}",{}'.format(targetdn, container_dn)
            policy_dn = 'cn="cn=nsPwPolicyEntry,{}",{}'.format(targetdn, container_dn)
            self.conn.delete_s(cos_template_dn)
            self.conn.delete_s(policy_dn)
            self.conn.delete_s(cos_def_dn)
        else:
            # Remove password subentry from target DN, then remove the policy entry itself
            rdns = ldap.dn.explode_dn(targetdn)
            rdns.pop(0)
            parentdn = ",".join(rdns)
            container_dn = "cn=nsPwPolicyContainer,{}".format(parentdn)
            policy_dn = target_entry.getValue('pwdpolicysubentry')
            if policy_dn is None or policy_dn == "":
                raise ValueError('There is no local password policy for this entry')
            self.conn.delete_s(ensure_str(policy_dn))

        # Remove the passwordPolicySubentry from the target
        self.conn.modify_s(targetdn, [(ldap.MOD_DELETE, 'pwdpolicysubentry', None)])

        # if there are no other entries under the container, then remove the container
        entries = self.conn.search_s(container_dn, ldap.SCOPE_SUBTREE, "(|(objectclass=ldapsubentry)(objectclass=*))")
        if len(entries) == 1:
            self.conn.delete_s(container_dn)

    def get_pwpolicy(self, targetdn=None, use_json=False):
        """Get the local or global password policy entry"""
        global_policy = False
        policy_type = "global"
        if targetdn is not None:
            # Local policy listed by name
            entrydn = 'cn="cn=nsPwPolicyEntry,{}",cn=nsPwPolicyContainer,{}'.format(targetdn, targetdn)
            pwp_attributes = self.pwp_attributes
        else:
            # Global policy
            global_policy = True
            entrydn = "cn=config"
            pwp_attributes = self.pwp_attributes
            pwp_attributes += ['passwordIsGlobalPolicy', 'nsslapd-pwpolicy_local']

        try:
            entries = self.conn.search_s(entrydn,
                                         ldap.SCOPE_BASE,
                                         'objectclass=*',
                                         pwp_attributes)
            entry = entries[0]
        except ldap.NO_SUCH_OBJECT:
            # okay lets see if its auser policy
            rdns = ldap.dn.explode_dn(targetdn)
            rdns.pop(0)
            parentdn = (",").join(rdns)
            entrydn = 'cn="cn=nsPwPolicyEntry,{}",cn=nsPwPolicyContainer,{}'.format(targetdn, parentdn)
            try:
                entries = self.conn.search_s(entrydn,
                                             ldap.SCOPE_BASE,
                                             'objectclass=*',
                                             pwp_attributes)
                entry = entries[0]
            except ldap.LDAPError as e:
                raise ValueError("Could not find password policy for entry: {} Error: {}".format(targetdn, str(e)))
        except ldap.LDAPError as e:
            raise ValueError("Could not find password policy for entry: {} Error: {}".format(targetdn, str(e)))

        if not global_policy:
            # subtree or user policy?
            cos_dn = 'cn=nspwpolicy_cos,' + targetdn
            try:
                self.conn.search_s(cos_dn, ldap.SCOPE_BASE, "(|(objectclass=ldapsubentry)(objectclass=*))")
                policy_type = "Subtree"
            except:
                policy_type = "User"

        if use_json:
            str_attrs = {}
            for k in entry.data:
                str_attrs[ensure_str(k)] = ensure_list_str(entry.data[k])

            # ensure all the keys are lowercase
            str_attrs = dict((k.lower(), v) for k, v in str_attrs.items())

            response = json.dumps({"type": "entry", "pwp_type": policy_type, "dn": ensure_str(targetdn), "attrs": str_attrs})
        else:
            if global_policy:
                response = "Global Password Policy: cn=config\n------------------------------------\n"
            else:
                response = "Local {} Policy: {}\n------------------------------------\n".format(policy_type, targetdn)
            for k in entry.data:
                response += "{}: {}\n".format(k, ensure_str(entry.data[k][0]))

        return response

    def list_policies(self, targetdn, use_json=False):
        """Return a list of the target DN's of all user policies
        """

        if use_json:
            result = {'type': 'list', 'items': []}
        else:
            result = ""

        # First get all the policies
        policies = self.conn.search_s(targetdn, ldap.SCOPE_SUBTREE, "(&(objectclass=ldapsubentry)(objectclass=passwordpolicy))", ['cn'])
        if policies is None or len(policies) == 0:
            if use_json:
                return json.dumps(result)
            else:
                return "No local password polices found"

        # Determine if the policy is subtree or user (subtree policies have COS entries)
        for policy in policies:
            cn = ensure_str(policy.getValue('cn'))
            entrydn = cn.lower().replace('cn=nspwpolicyentry,', '')   # .lstrip()
            cos_dn = cn.lower().replace('cn=nspwpolicyentry', 'cn=nspwpolicy_cos')
            try:
                self.conn.search_s(cos_dn, ldap.SCOPE_BASE, "(|(objectclass=ldapsubentry)(objectclass=*))")
                found_subtree = True
            except:
                found_subtree = False

            if found_subtree:
                # Build subtree policy list
                if use_json:
                    result['items'].append([entrydn, "Subtree Policy"])
                else:
                    result += entrydn + " (subtree policy)\n"
            else:
                # Build user policy list
                if use_json:
                    result['items'].append([entrydn, "User Policy"])
                else:
                    result += entrydn + " (user policy)\n"

        if use_json:
            return json.dumps(result)
        else:
            return result

    def set_policy(self, targetdn, args, arg_to_attr):
        '''This could be a user or subtree policy, so find out which one and
        use the correct container dn'''
        try:
            cos_def_dn = 'cn=nsPwPolicy_CoS,{}'.format(targetdn)
            self.conn.search_s(cos_def_dn, ldap.SCOPE_BASE, "(|(objectclass=ldapsubentry)(objectclass=*))")
            # This is a subtree policy
            container_dn = "cn=nsPwPolicyContainer,{}".format(targetdn)
        except:
            # This is a user policy
            rdns = ldap.dn.explode_dn(targetdn)
            rdns.pop(0)
            parentdn = ",".join(rdns)
            container_dn = "cn=nsPwPolicyContainer,{}".format(parentdn)

        policy_dn = 'cn="cn=nsPwPolicyEntry,{}",{}'.format(targetdn, container_dn)
        mods = []
        for arg in vars(args):
            val = getattr(args, arg)
            if arg in arg_to_attr and val is not None:
                mods.append((ldap.MOD_REPLACE, ensure_str(arg_to_attr[arg]), ensure_bytes(val)))
        if len(mods) > 0:
            self.conn.modify_s(policy_dn, mods)
