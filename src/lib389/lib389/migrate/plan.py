# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

from lib389.schema import Schema, Resolver
from lib389.backend import Backends
from lib389.migrate.openldap.config import olOverlayType
from lib389.plugins import MemberOfPlugin, ReferentialIntegrityPlugin, AttributeUniquenessPlugins, PassThroughAuthenticationPlugin, PAMPassThroughAuthPlugin, PAMPassThroughAuthConfigs
import ldap
import os
from ldif import LDIFParser
from ldif import LDIFWriter
from uuid import uuid4

import logging
logger = logging.getLogger(__name__)

class MigrationAction(object):
    def __init__(self):
        pass

    def apply(self, inst):
        pass

    def post(self, inst):
        pass

    def __unicode__(self):
        raise Exception('not implemented')

    def display_plan(self, log):
        pass

    def display_post(self, log):
        pass

class DatabaseCreate(MigrationAction):
    def __init__(self, suffix, uuid):
        self.suffix = suffix
        self.uuid = uuid

    def apply(self, inst):
        bes = Backends(inst)
        be = bes.create(properties={
            'cn': self.uuid,
            'nsslapd-suffix': self.suffix,
        })

    def __unicode__(self):
        return f"DatabaseCreate -> {self.suffix}, {self.uuid}"

    def display_plan(self, log):
        log.info(f" * Database Create -> {self.suffix}")

class DatabaseIndexCreate(MigrationAction):
    def __init__(self, suffix, olindex):
        self.suffix = suffix
        self.attr = olindex[0]
        # Will this work with multiple index types
        self.type = olindex[1]

    def apply(self, inst):
        be = Backends(inst).get(self.suffix)
        indexes = be.get_indexes()
        try:
            # If it exists, return. Could be the case as we created the
            # BE and the default indexes applied now.
            indexes.get(self.attr)
            return
        except ldap.NO_SUCH_OBJECT:
            pass
        be.add_index(self.attr, self.type)

    def __unicode__(self):
        return f"DatabaseIndexCreate -> {self.attr} {self.type}, {self.suffix}"

    def display_plan(self, log):
        log.info(f" * Database Add Index -> {self.attr}:{self.type} for {self.suffix}")

class DatabaseReindex(MigrationAction):
    def __init__(self, suffix):
        self.suffix = suffix

    def post(self, inst):
        bes = Backends(inst)
        be = bes.get(self.suffix)
        be.reindex(wait=True)

    def __unicode__(self):
        return f"DatabaseReindex -> {self.suffix}"

    def display_plan(self, log):
        log.info(f" * Database Reindex -> {self.suffix}")

class ImportTransformer(LDIFParser):
    def __init__(self, f_import, f_outport, exclude_attributes_set, exclude_objectclass_set):
        self.exclude_attributes_set = exclude_attributes_set
        # Already all lowered by the db ldif import

        self.exclude_objectclass_set = exclude_objectclass_set
        self.f_outport = f_outport
        self.writer = LDIFWriter(self.f_outport)
        super().__init__(f_import)

    def handle(self, dn, entry):
        attrs = entry.keys()
        # We don't know what form the keys/attrs are in
        # so we have to establish our own map of our
        # idea of these to the attrs idea.
        amap = dict([(x.lower(), x) for x in attrs])

        # Now we can do transforms
        # This has to exist ....
        oc_a = amap['objectclass']
        # If mo present, as nsMemberOf.
        try:
            mo_a = amap['memberof']
            # If mo_a was found, then mo is present, extend the oc to make it valid.
            entry[oc_a] += [b'nsMemberOf']
        except:
            # Not found
            pass

        # We remove the objectclass set and reinsert minus anything excluded.
        e_oc_set = entry[oc_a]
        e_oc_set = [ oc for oc in e_oc_set if oc.lower() not in self.exclude_objectclass_set ]
        entry[oc_a] = e_oc_set

        # Strip anything in the exclude set.
        for attr in self.exclude_attributes_set:
            try:
                ecsn_a = amap[attr]
                entry.pop(ecsn_a)
            except:
                # Not found, move on.
                pass

        # If userPassword is present AND it is a SASL map, then we migrate it to the nsSaslauthId
        # type to prevent account password confusion.
        try:
            pw_a = amap['userpassword']
            pw = entry[pw_a][0]
            if pw.startswith(b'{SASL}'):
                entry.pop(pw_a)
                sasl_id = pw.replace(b'{SASL}', b'')
                # Add the marker objectClass and the map attr
                entry[oc_a] += [b'nsSaslauthAccount']
                entry['nsSaslauthId'] = [sasl_id]
        except:
            # Not found, move on.
            pass

        # Write it out
        self.writer.unparse(dn, entry)

class DatabaseLdifImport(MigrationAction):
    def __init__(self, suffix, ldif_path, exclude_attributes_set, exclude_objectclass_set):
        self.suffix = suffix
        self.ldif_path = ldif_path
        self.exclude_attributes_set = exclude_attributes_set
        self.exclude_objectclass_set = [ x.lower() for x in exclude_objectclass_set ]

    def apply(self, inst):
        # Create a unique op id.
        op_id = str(uuid4())
        op_path = os.path.join(inst.get_ldif_dir(), f'{op_id}.ldif')

        with open(self.ldif_path, 'r') as f_import:
            with open(op_path, 'w') as f_outport:
                p = ImportTransformer(f_import, f_outport, self.exclude_attributes_set, self.exclude_objectclass_set)
                p.parse()

        be = Backends(inst).get(self.suffix)
        task = be.export_ldif()
        task.wait()

        task = be.import_ldif([op_path])
        task.wait()

    def __unicode__(self):
        return f"DatabaseLdifImport -> {self.suffix} {self.ldif_path}, {self.exclude_attributes_set}, {self.exclude_objectclass_set}"

    def display_plan(self, log):
        log.info(f" * Database Import Ldif -> {self.suffix} from {self.ldif_path} - excluding entry attributes = [{self.exclude_attributes_set}], excluding entry objectclasses = [{self.exclude_objectclass_set}]")

    def display_post(self, log):
        log.info(f" * [ ] - Review Database Imported Content is Correct -> {self.suffix}")

class SchemaAttributeUnsupported(MigrationAction):
    def __init__(self, attr):
        self.attr = attr

    def __unicode__(self):
        return f"SchemaAttributeUnsupported -> {self.attr.__unicode__()}"

    def apply(self, inst):
        inst.log.debug(f"SchemaAttributeUnsupported -> {self.attr.__unicode__()} (SKIPPING)")

    def display_plan(self, log):
        log.info(f" * Schema Skip Unsupported Attribute -> {self.attr.names[0]} ({self.attr.oid})")

    def display_post(self, log):
        pass


class SchemaAttributeCreate(MigrationAction):
    def __init__(self, attr):
        self.attr = attr

    def __unicode__(self):
        return f"SchemaAttributeCreate -> {self.attr.__unicode__()}"

    def apply(self, inst):
        schema = Schema(inst)
        inst.log.debug("SchemaAttributeCreate -> %s" % self.attr.schema_str())
        schema.add(self.attr.schema_attribute, self.attr.schema_str())

    def display_plan(self, log):
        log.info(f" * Schema Create Attribute -> {self.attr.names[0]} ({self.attr.oid})")

class SchemaAttributeInconsistent(MigrationAction):
    def __init__(self, attr, ds_attr):
        self.ds_attr = ds_attr
        self.attr = attr

    def __unicode__(self):
        return f"SchemaAttributeInconsistent -> {self.ds_attr} to {self.attr.__unicode__()}"

    def display_plan(self, log):
        log.info(f" * Schema Skip Inconsistent Attribute -> {self.attr.names[0]} ({self.attr.oid})")

    def display_post(self, log):
        log.info(f" * [ ] - Review Schema Inconsistent Attribute -> {self.attr.names[0]} ({self.attr.oid})")

class SchemaAttributeAmbiguous(MigrationAction):
    def __init__(self, attr, overlaps):
        self.attr = attr
        self.overlaps = overlaps

    def __unicode__(self):
        return f"SchemaAttributeAmbiguous -> {self.attr.__unicode__()}"

    def display_plan(self, log):
        log.info(f" * Schema Skip Abmiguous Attribute -> {self.attr.names[0]} ({self.attr.oid}) overlaps {self.overlaps}")

    def display_post(self, log):
        log.info(f" * [ ] - Review Schema Ambiguous Attribute -> {self.attr.names[0]} ({self.attr.oid}) overlaps {self.overlaps}")

class SchemaAttributeInvalid(MigrationAction):
    def __init__(self, attr, reason):
        self.attr = attr
        self.reason = reason

    def __unicode__(self):
        return f"SchemaAttributeInvalid - {self.reason} -> {self.attr.__unicode__()}"

    def apply(self, inst):
        inst.log.debug(f"SchemaAttributeInvalid -> {self.attr.__unicode__()} (SKIPPING)")

    def display_plan(self, log):
        log.info(f" * Schema Skip Invalid Attribute - {self.reason} -> {self.attr.names[0]} ({self.attr.oid})")

    def display_post(self, log):
        log.info(f" * [ ] - Review Schema Invalid Attribute - {self.reason} -> {self.attr.names[0]} ({self.attr.oid})")

class SchemaClassUnsupported(MigrationAction):
    def __init__(self, obj):
        self.obj = obj

    def __unicode__(self):
        return f"SchemaClassUnsupported -> {self.obj.__unicode__()}"

    def apply(self, inst):
        inst.log.debug(f"SchemaClassUnsupported -> {self.obj.__unicode__()} (SKIPPING)")

    def display_plan(self, log):
        log.info(f" * Schema Skip Unsupported ObjectClass -> {self.obj.names[0]} ({self.obj.oid})")

class SchemaClassInvalid(MigrationAction):
    def __init__(self, obj, reason):
        self.obj = obj
        self.reason = reason

    def __unicode__(self):
        return f"SchemaClassInvalid - {self.reason} -> {self.obj.__unicode__()}"

    def apply(self, inst):
        inst.log.debug(f"SchemaClassInvalid -> {self.obj.__unicode__()} (SKIPPING)")

    def display_plan(self, log):
        log.info(f" * Schema Skip Invalid ObjectClass - {self.reason} -> {self.obj.names[0]} ({self.obj.oid})")

    def display_post(self, log):
        log.info(f" * [ ] Review Schema Invalid ObjectClass - {self.reason} -> {self.obj.names[0]} ({self.obj.oid})")

class SchemaClassCreate(MigrationAction):
    def __init__(self, obj):
        self.obj = obj

    def __unicode__(self):
        return f"SchemaClassCreate -> {self.obj.__unicode__()}"

    def apply(self, inst):
        schema = Schema(inst)
        inst.log.debug("SchemaClassCreate -> %s" % self.obj.schema_str())
        schema.add(self.obj.schema_attribute, self.obj.schema_str())

    def display_plan(self, log):
        log.info(f" * Schema Create ObjectClass -> {self.obj.names[0]} ({self.obj.oid})")

class SchemaClassInconsistent(MigrationAction):
    def __init__(self, obj, ds_obj):
        self.ds_obj = ds_obj
        self.obj = obj

    def __unicode__(self):
        return f"SchemaClassInconsistent -> {self.ds_obj} to {self.obj.__unicode__()}"

    def display_plan(self, log):
        log.info(f" * Schema Skip Inconsistent ObjectClass -> {self.obj.names[0]} ({self.obj.oid})")

    def display_post(self, log):
        log.info(f" * [ ] - Review Schema Inconistent ObjectClass -> {self.obj.names[0]} ({self.obj.oid})")

class PluginMemberOfEnable(MigrationAction):
    def __init__(self):
        pass

    def apply(self, inst):
        mo = MemberOfPlugin(inst)
        mo.enable()

    def __unicode__(self):
        return "PluginMemberOfEnable"

    def display_plan(self, log):
        log.info(f" * Plugin:MemberOf Enable")

    def display_post(self, log):
        log.info(f" * [ ] - Review Plugin:MemberOf Migrated Configuration is Correct")

class PluginMemberOfScope(MigrationAction):
    def __init__(self, suffix):
        self.suffix = suffix

    def apply(self, inst):
        mo = MemberOfPlugin(inst)
        try:
            mo.add_entryscope(self.suffix)
        except ldap.TYPE_OR_VALUE_EXISTS:
            pass

    def __unicode__(self):
        return f"PluginMemberOfScope -> {self.suffix}"

    def display_plan(self, log):
        log.info(f" * Plugin:MemberOf Add Suffix -> {self.suffix}")

class PluginMemberOfFixup(MigrationAction):
    def __init__(self, suffix):
        self.suffix = suffix

    def post(self, inst):
        mo = MemberOfPlugin(inst)
        task = mo.fixup(self.suffix)
        task.wait()

    def __unicode__(self):
        return f"PluginMemberOfFixup -> {self.suffix}"

    def display_plan(self, log):
        log.info(f" * Plugin:MemberOf Regenerate (Fixup) -> {self.suffix}")

class PluginRefintEnable(MigrationAction):
    def __init__(self):
        pass
        # Set refint delay to 0

    def apply(self, inst):
        rip = ReferentialIntegrityPlugin(inst)
        rip.set_update_delay(0)
        rip.enable()

    def __unicode__(self):
        return "PluginRefintEnable"

    def display_plan(self, log):
        log.info(f" * Plugin:Referential Integrity Enable")

    def display_post(self, log):
        log.info(f" * [ ] - Review Plugin:Referential Integrity Migrated Configuration is Correct")

class PluginRefintAttributes(MigrationAction):
    def __init__(self, attr):
        self.attr = attr

    def apply(self, inst):
        rip = ReferentialIntegrityPlugin(inst)
        try:
            rip.add_membership_attr(self.attr)
        except ldap.TYPE_OR_VALUE_EXISTS:
            # This is okay, move on.
            pass

    def __unicode__(self):
        return f"PluginRefintAttributes -> {self.attr}"

    def display_plan(self, log):
        log.info(f" * Plugin:Referential Integrity Add Attribute -> {self.attr}")

class PluginRefintScope(MigrationAction):
    def __init__(self, suffix):
        self.suffix = suffix

    def apply(self, inst):
        rip = ReferentialIntegrityPlugin(inst)
        try:
            rip.add_entryscope(self.suffix)
        except ldap.TYPE_OR_VALUE_EXISTS:
            # This is okay, move on.
            pass

    def __unicode__(self):
        return f"PluginRefintScope -> {self.suffix}"

    def display_plan(self, log):
        log.info(f" * Plugin:Referential Integrity Add Scope -> {self.suffix}")


class PluginUniqueConfigure(MigrationAction):
    # This enables and configures.
    def __init__(self, suffix, attr, uuid):
        self.suffix = suffix
        self.attr = attr
        self.uuid = uuid

    def apply(self, inst):
        aups = AttributeUniquenessPlugins(inst)
        try:
            aup = aups.create(properties={
                'cn': f'cn=attr_unique_{self.attr}_{self.uuid}',
                'uniqueness-attribute-name': self.attr,
                'uniqueness-subtrees': self.suffix,
                'nsslapd-pluginEnabled': 'on',
            })
        except ldap.ALREADY_EXISTS:
            # This is okay, move on.
            pass

    def __unicode__(self):
        return f"PluginUniqueConfigure -> {self.suffix}, {self.attr} {self.uuid}"

    def display_plan(self, log):
        log.info(f" * Plugin:Unique Add Attribute and Suffix -> {self.attr} {self.suffix}")


class PluginPwdPolicyAudit(MigrationAction):
    def __init__(self, suffix, inst_name):
        self.suffix = suffix
        self.inst_name = inst_name

    def __unicode__(self):
        return f"PluginPwdPolicyAudit -> {self.suffix}"

    def display_post(self, log):
        log.info(f" * [ ] - Review Imported Password Policies for accounts under {self.suffix}.")
        log.info(f" * [ ] - Enable Password Policy for new accounts in {self.suffix}. See `dsconf {self.inst_name} localpwp --help` ")


class PluginPassThroughDisable(MigrationAction):
    def __init__(self):
        pass

    def apply(self, inst):
        pta = PassThroughAuthenticationPlugin(inst)
        pta.disable()

    def __unicode__(self):
        return "PluginPassThroughDisable"

    def display_plan(self, log):
        log.info(f" * Plugin:PassThrough Disable")

    def display_post(self, log):
        pass


class PluginPAMPassThroughEnable(MigrationAction):
    def __init__(self):
        pass

    def apply(self, inst):
        pta = PAMPassThroughAuthPlugin(inst)
        pta.enable()

    def __unicode__(self):
        return "PAMPassThroughAuthPlugin"

    def display_plan(self, log):
        log.info(f" * Plugin:PamPassThrough Enable")

    def display_post(self, log):
        pass


class PluginPAMPassThroughConfigure(MigrationAction):
    def __init__(self, suffix):
        self.suffix = suffix

    def apply(self, inst):
        ptas = PAMPassThroughAuthConfigs(inst)
        # PAMPassThroughAuthConfig
        pta = ptas.ensure_state(properties = {
            'cn': 'saslauthd',
            'pamService': 'saslauthd',
            'pamIncludeSuffix': self.suffix,
            'pamFilter': '(objectClass=nsSaslauthAccount)',
            'pamIdAttr': 'nsSaslauthId',
            'pamIDMapMethod': 'ENTRY',
            'pamSecure': 'TRUE',
            'pamFallback': 'FALSE',
            'pamModuleIsThreadSafe': 'TRUE',
            'pamMissingSuffix': 'ALLOW',
        })

    def __unicode__(self):
        return "PluginPAMPassThroughConfigure"

    def display_plan(self, log):
        log.info(f" * Plugin:PamPassThrough Configure")

    def display_post(self, log):
        log.info(f" * [ ] - Review Plugin:PAMPassThrough Migrated SASLauthd Configuration is Correct")


class PluginUnknownManual(MigrationAction):
    def __init__(self, overlay):
        self.overlay = overlay

    def __unicode__(self):
        return f"PluginUnknownManual -> {self.overlay.name}, {self.overlay.classes}"

    def display_post(self, log):
        log.info(f" * [ ] - Review Unsupported Overlay:{self.overlay.name}:{self.overlay.classes} Configuration and Possible Alternatives for 389 Directory Server")


class Migration(object):
    def __init__(self, inst, olschema=None, oldatabases=None, ldifs=None, skip_schema_oids=[], skip_overlays=[], skip_entry_attributes=[]):
        """Generate a migration plan from an openldap config, the instance to migrate too
        and an optional dictionary of { suffix: ldif_path }.

        The migration plan once generate still needs to be executed, but the idea is that
        this module connects to a UI program that can allow the plan to be reviewed and
        accepted. Plan modification is "out of scope", but possible as the array could
        be manipulated in place.
        """
        self.olschema = olschema
        self.oldatabases = oldatabases
        self.inst = inst
        self.plan = []
        self.ldifs = ldifs
        self._overlay_do_not_migrate = set(skip_overlays)
        self._schema_oid_do_not_migrate = set([
            # We pre-modified these as they are pretty core, and we don't want
            # them tampered with
            '2.5.4.2', # knowledgeInformation
            '2.5.4.7', # l, locality
            '2.5.4.29', # presentationAddress
            '2.5.4.30', # supportedApplication Context
            '2.5.4.42', # givenName
            '2.5.4.48', # protocolInformation
            '2.5.4.54', # dmdName
            '2.5.6.7', # organizationalPerson
            '2.5.6.9', # groupOfNames
            '2.5.6.10', # residentialPerson
            '2.5.6.12', # applicationEntity
            '2.5.6.13', # dsa
            '2.5.6.17', # groupOfUniqueNames
            '2.5.6.20', # dmd
            # This has to be excluded as 389 syntax had an issue in 4872
            '1.3.6.1.1.16.4',
            # We ignore all of the conflicts/changes from rfc2307 and rfc2307bis
            # as we provide rfc2307compat, which allows both to coexist.
            '1.3.6.1.1.1.1.16', # ipServiceProtocol
            '1.3.6.1.1.1.1.19', # ipHostNumber
            '1.3.6.1.1.1.1.20', # ipNetworkNumber
            '1.3.6.1.1.1.1.26', # nisMapName
            '1.3.6.1.1.1.1.28', # nisPublicKey
            '1.3.6.1.1.1.1.29', # nisSecretKey
            '1.3.6.1.1.1.1.30', # nisDomain
            '1.3.6.1.1.1.1.31', # automountMapName
            '1.3.6.1.1.1.1.32', # automountKey
            '1.3.6.1.1.1.1.33', # automountInformation
            '1.3.6.1.1.1.2.2', # posixGroup
            '1.3.6.1.1.1.2.6', # ipHost
            '1.3.6.1.1.1.2.7', # ipNetwork
            '1.3.6.1.1.1.2.9', # nisMap
            '1.3.6.1.1.1.2.11', # ieee802Device
            '1.3.6.1.1.1.2.12', # bootableDevice
            '1.3.6.1.1.1.2.13', # nisMap
            '1.3.6.1.1.1.2.14', # nisKeyObject
            '1.3.6.1.1.1.2.15', # nisDomainObject
            '1.3.6.1.1.1.2.16', # automountMap
            '1.3.6.1.1.1.2.17', # automount
            # This schema is buggy, we always skip it as we know the 389 version is correct.
            '0.9.2342.19200300.100.4.14',
            # ppolicy overlay - these names conflict to 389-ds values, but have the same function.
            '1.3.6.1.4.1.42.2.27.8.1.2', # pwdMinAge
            '1.3.6.1.4.1.42.2.27.8.1.3', # pwdMaxAge
            '1.3.6.1.4.1.42.2.27.8.1.4', # pwdInHistory
            '1.3.6.1.4.1.42.2.27.8.1.6', # pwdMinLength
            '1.3.6.1.4.1.42.2.27.8.1.7', # pwdExpireWarning
            '1.3.6.1.4.1.42.2.27.8.1.8', # pwdGraceAuthNLimit
            '1.3.6.1.4.1.42.2.27.8.1.9', # pwdLockout
            '1.3.6.1.4.1.42.2.27.8.1.10', # pwdLockoutDuration
            '1.3.6.1.4.1.42.2.27.8.1.11', # pwdMaxFailure
            '1.3.6.1.4.1.42.2.27.8.1.12', # pwdFailureCountInt
            '1.3.6.1.4.1.42.2.27.8.1.13', # pwdMustChange
            '1.3.6.1.4.1.42.2.27.8.1.14', # pwdAllowUserChange
            '1.3.6.1.4.1.4754.2.99.1', # pwdPolicyChecker
            '1.3.6.1.4.1.42.2.27.8.2.1', # pwdPolicy objectClass
            # Openldap supplies some schema which conflicts to ours, skip them
            'NetscapeLDAPattributeType:198', # memberUrl
            'NetscapeLDAPobjectClass:33', # groupOfURLs

            # Dynamic Directory Services can't be supported due to missing syntax oid below, so we
            # exclude the "otherwise" supported attrs / ocs
            'DynGroupAttr:1', # dgIdentity
            'DynGroupOC:1', # dgIdentityAux
        ] + skip_schema_oids)
        self._schema_oid_unsupported = set([
            # RFC4517 othermailbox syntax is not supported on 389.
            '0.9.2342.19200300.100.1.22',
            # The dsaquality syntax was removed in rfc4517
            '0.9.2342.19200300.100.1.49',
            # single level quality syntax is removed
            '0.9.2342.19200300.100.1.50',
            '0.9.2342.19200300.100.1.51',
            '0.9.2342.19200300.100.1.52',
            # Pilot person depends no otherMailbox
            '0.9.2342.19200300.100.4.4',
            # Pilot DSA needs dsaquality
            '0.9.2342.19200300.100.4.21',
            '0.9.2342.19200300.100.4.22',
            # These ppolicy module are unsupported so they are skipped.
            '1.3.6.1.4.1.42.2.27.8.1.1', # pwdAttribute
            '1.3.6.1.4.1.42.2.27.8.1.5', # pwdCheckQuality
            '1.3.6.1.4.1.42.2.27.8.1.15', # pwdSafeModify
            '1.3.6.1.4.1.4754.1.99.1', # pwdCheckModule
            '1.3.6.1.4.1.42.2.27.8.1.30', # pwdMaxRecordedFailure
            # OpenLDAP dynamic directory services defines an internal
            # oid ( 1.3.6.1.4.1.4203.666.2.7 )for dynamic group authz, but has very little docs about this.
            'DynGroupAttr:2', # dgAuthz
        ])

        self._skip_entry_attributes = set(
            [
                # Core openldap attrs that we can't use, and don't matter.
                'entrycsn',
                'structuralobjectclass',
                # pwd attributes from ppolicy which are not supported.
                'pwdattribute',
                'pwdcheckquality',
                'pwdsafemodify',
                'pwdcheckmodule',
                'pwdmaxrecordedfailure',
                # dds attributes we don't support
                'dgidentity',
                'dgauthz'
            ] +
            [x.lower() for x in skip_entry_attributes]
        )
        # These tend to be be from overlays we don't support
        self._skip_entry_objectclasses = set([
            'pwdpolicychecker',
            'pwdpolicy',
            'dgidentityaux'
        ])

        self._gen_migration_plan()

    def __unicode__(self):
        buff = ""
        for item in self.plan:
            buff += f"{item.__unicode__()}\n"
        return buff

    def _gen_schema_plan(self):
        if self.olschema is None:
            return
        # Get the server schema so that we can query it repeatedly.
        schema = Schema(self.inst)
        schema_attrs = schema.get_attributetypes()
        schema_objects = schema.get_objectclasses()

        resolver = Resolver(schema_attrs)

        # Examine schema attrs
        for attr in self.olschema.attrs:
            # If we have been instructed to ignore this oid, skip.
            if attr.oid in self._schema_oid_do_not_migrate:
                continue
            if attr.oid in self._schema_oid_unsupported:
                self.plan.append(SchemaAttributeUnsupported(attr))
                continue
            if attr.oid.lower().startswith('x-'):
                self.plan.append(SchemaAttributeInvalid(attr, "descr-oid format is ambiguous with x- prefix. You MUST use a numeric oid."))
                continue
            # For the attr, find if anything has a name overlap in any capacity.
            # overlaps = [ (names, ds_attr) for (names, ds_attr) in schema_attr_names if len(names.intersection(attr.name_set)) > 0]
            overlaps = [ ds_attr for ds_attr in schema_attrs if ds_attr.oid == attr.oid]
            if len(overlaps) == 0:
                # We need to add attr
                self.plan.append(SchemaAttributeCreate(attr))
            elif len(overlaps) == 1:
                # We need to possibly adjust attr
                ds_attr = overlaps[0]
                # We need to have a way to compare the two.
                if attr.inconsistent(ds_attr):
                    self.plan.append(SchemaAttributeInconsistent(attr, ds_attr))
            else:
                # Ambiguous attr, the admin must intervene to migrate it.
                self.plan.append(SchemaAttributeAmbiguous(attr, overlaps))

        # Examine schema classes
        for obj in self.olschema.classes:
            # If we have been instructed to ignore this oid, skip.
            if obj.oid in self._schema_oid_do_not_migrate:
                continue
            if obj.oid in self._schema_oid_unsupported:
                self.plan.append(SchemaClassUnsupported(obj))
                continue
            if obj.oid.lower().startswith('x-'):
                self.plan.append(SchemaClassInvalid(obj, "descr-oid format is ambiguous with x- prefix. You MUST use a numeric oid."))
                continue
            # For the attr, find if anything has a name overlap in any capacity.
            overlaps = [ ds_obj for ds_obj in schema_objects if ds_obj.oid == obj.oid]
            if len(overlaps) == 0:
                # We need to add attr
                self.plan.append(SchemaClassCreate(obj))
            elif len(overlaps) == 1:
                # We need to possibly adjust the objectClass as it exists
                ds_obj = overlaps[0]
                if obj.inconsistent(ds_obj, resolver):
                    self.plan.append(SchemaClassInconsistent(obj, ds_obj))
            else:
                # This should be an impossible state.
                raise Exception('impossible state')

    def _gen_be_exist_plan(self, oldb, be):
        # For each index
        indexes = be.get_indexes()
        for olindex in oldb.index:
            # Assert they exist
            try:
                indexes.get(olindex[0])
            except ldap.NO_SUCH_OBJECT:
                self.plan.append(DatabaseIndexCreate(oldb.suffix, olindex))

        # Reindex the db
        self.plan.append(DatabaseReindex(oldb.suffix))

    def _gen_be_create_plan(self, oldb):
        # Req db create
        self.plan.append(DatabaseCreate(oldb.suffix, oldb.uuid))
        # For each index
        # Assert we have the index on the db, or req it's creation
        for olindex in oldb.index:
            self.plan.append(DatabaseIndexCreate(oldb.suffix, olindex))

        # Reindex the db.
        self.plan.append(DatabaseReindex(oldb.suffix))

    def _gen_plugin_plan(self, oldb):
        for overlay in oldb.overlays:
            if overlay in self._overlay_do_not_migrate:
                # We have been instructed to ignore this, so move on.
                continue
            if overlay.otype == olOverlayType.UNKNOWN:
                self.plan.append(PluginUnknownManual(overlay))
            elif overlay.otype == olOverlayType.MEMBEROF:
                # Assert memberof enabled.
                self.plan.append(PluginMemberOfEnable())
                # Member of scope
                self.plan.append(PluginMemberOfScope(oldb.suffix))
                # Memberof fixup task.
                self.plan.append(PluginMemberOfFixup(oldb.suffix))
            elif overlay.otype == olOverlayType.REFINT:
                self.plan.append(PluginRefintEnable())
                for attr in overlay.attrs:
                    self.plan.append(PluginRefintAttributes(attr))
                self.plan.append(PluginRefintScope(oldb.suffix))
            elif overlay.otype == olOverlayType.UNIQUE:
                for attr in overlay.attrs:
                    self.plan.append(PluginUniqueConfigure(oldb.suffix, attr, oldb.uuid))
            elif overlay.otype == olOverlayType.PPOLICY:
                self.plan.append(PluginPwdPolicyAudit(oldb.suffix, self.inst.serverid))
            else:
                raise Exception("Unknown overlay type, this is a bug!")
        # We can't programatically detect the use of sasl authd. We can however
        # create a configuration that "isn't harmful" in the case it's not used,
        # and that does work in the case that it is present.
        self.plan.append(PluginPassThroughDisable())
        self.plan.append(PluginPAMPassThroughEnable())
        self.plan.append(PluginPAMPassThroughConfigure(oldb.suffix))


    def _gen_db_plan(self):
        # Create/Manage dbs
        # Get the set of current dbs.
        if self.oldatabases is None:
            return

        backends = Backends(self.inst)

        for db in self.oldatabases:
            # Get the suffix
            suffix = db.suffix
            try:
                # Do we have a db with that suffix already?
                be = backends.get(suffix)
                self._gen_be_exist_plan(db, be)
            except ldap.NO_SUCH_OBJECT:
                self._gen_be_create_plan(db)

            self._gen_plugin_plan(db)

    def _gen_import_plan(self):
        # Given external ldifs and suffixes, generate plans to handle these.
        if self.ldifs is None:
            return
        for (suffix, ldif_path) in self.ldifs.items():
            self.plan.append(DatabaseLdifImport(suffix, ldif_path, self._skip_entry_attributes, self._skip_entry_objectclasses))

    def _gen_migration_plan(self):
        """Order of this module is VERY important!!!
        """
        self._gen_schema_plan()
        self._gen_db_plan()
        self._gen_import_plan()


    def execute_plan(self, log=None):
        """ Do it!"""
        if log is None:
            log = logger

        # Do we have anything to do?
        if len(self.plan) == 0:
            raise Exception("Migration has no actions to perform")

        count = 1

        # First apply everything
        for item in self.plan:
            item.apply(self.inst)
            log.info(f"migration: {count} / {len(self.plan)} complete ...")
            count += 1

        # Before we do post actions, restart the instance.
        if not self.inst._containerised:
            self.inst.restart()
        else:
            log.info("Restart your instance now!")
            _ = input("Press enter when complete")
            self.inst.open()

        # Then do post actions
        count = 1
        for item in self.plan:
            item.post(self.inst)
            log.info(f"post: {count} / {len(self.plan)} complete ...")
            count += 1

    def display_plan_review(self, log):
        """Given an output log sink, display the migration plan"""
        if len(self.plan) == 0:
            raise Exception("Migration has no actions to perform")
        for item in self.plan:
            item.display_plan(log)

    def display_plan_post_review(self, log):
        """Given an output log sink, display the post migration checklist elements"""
        # There are some items we must always provide.
        log.info(f" * [ ] - Create/Migrate Database Access Controls (ACI)")
        log.info(f" * [ ] - Enable and Verify TLS (LDAPS) Operation")
        log.info(f" * [ ] - Schedule Automatic Backups")
        log.info(f" * [ ] - Verify Accounts Can Bind Correctly")

        for item in self.plan:
            item.display_post(log)

