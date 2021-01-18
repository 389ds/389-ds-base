# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import os
import logging
import ldap.schema
from enum import Enum
from ldif import LDIFParser
from lib389.utils import ensure_list_str, ensure_str

logger = logging.getLogger(__name__)

class SimpleParser(LDIFParser):
    def __init__(self, f):
        self.entries = []
        super().__init__(f)
        pass

    def handle(self, dn, entry):
        self.entries.append((dn, entry))


def ldif_parse(path, rpath):
    with open(os.path.join(path, rpath), 'r') as f:
        sp = SimpleParser(f)
        sp.parse()
        return sp.entries

def db_cond(name):
    if name == 'olcDatabase={0}config.ldif':
        return False
    if name == 'olcDatabase={-1}frontend.ldif':
        return False
    if name.startswith('olcDatabase=') and name.endswith('.ldif'):
        return True
    return False


class olOverlayType(Enum):
    UNKNOWN = 0
    MEMBEROF = 1
    REFINT = 2
    UNIQUE = 3


class olOverlay(object):
    def __init__(self, path, name, log):
        self.log = log
        self.log.debug(f"olOverlay path -> {path}/{name}")
        entries = ldif_parse(path, name)
        assert len(entries) == 1
        self.config = entries.pop()
        self.log.debug(f"{self.config}")

        # olcOverlay

        self.name = ensure_str(self.config[1]['olcOverlay'][0]).split('}', 1)[1]
        self.classes = ensure_list_str(self.config[1]['objectClass'])
        self.log.debug(f"{self.name} {self.classes}")

        if 'olcMemberOf' in self.classes:
            self.otype = olOverlayType.MEMBEROF
            #
        elif 'olcRefintConfig' in self.classes:
            self.otype = olOverlayType.REFINT
            # olcRefintAttribute
            self.attrs = ensure_list_str(self.config[1]['olcRefintAttribute'])
        elif 'olcUniqueConfig' in self.classes:
            self.otype = olOverlayType.UNIQUE
            # olcUniqueURI
            self.attrs = ensure_list_str([
                # This is a ldap:///?uid?sub, so split ? [1] will give uid.
                attr.split('?')[1]
                for attr in ensure_list_str(self.config[1]['olcUniqueURI'])
            ])
        else:
            self.otype = olOverlayType.UNKNOWN
            # Should we stash extra details?


class olDatabase(object):
    def __init__(self, path, name, log):
        self.log = log
        self.log.debug(f"olDatabase path -> {path}")
        entries = ldif_parse(path, f'{name}.ldif')
        assert len(entries) == 1
        self.config = entries.pop()
        self.log.debug(f"{self.config}")

        # olcSuffix, olcDbIndex, entryUUID
        self.suffix = ensure_str(self.config[1]['olcSuffix'][0])
        self.idx = name.split('}', 1)[0].split('{', 1)[1]
        self.uuid = ensure_str(self.config[1]['entryUUID'][0])

        self.index = [
            tuple(ensure_str(x).split(' '))
            for x in self.config[1]['olcDbIndex']
        ]

        self.log.debug(f"settings -> {self.suffix}, {self.idx}, {self.uuid}, {self.index}")


        overlay_path = os.path.join(path, name)
        self.overlays = [
            olOverlay(overlay_path, x, log)
            for x in sorted(os.listdir(overlay_path))
        ]

# See https://www.python-ldap.org/en/latest/reference/ldap-schema.html
class olAttribute(ldap.schema.models.AttributeType):
    def __init__(self, value, log):
        self.log = log
        self.log.debug(f"olAttribute value -> {value}")
        # This split takes {0}(stuff) and will only leave stuf.
        super().__init__(value.split('}', 1)[1])
        self.name_set = set([x.lower() for x in self.names])

    def schema_str(self):
        return super().__str__()

    def __str__(self):
        return self.__unicode__()

    def __unicode__(self):
        return f"{self.names}"

    def inconsistent(self, ds_attr):
        # Okay, we are attempting to merge self into ds_attr. What do we need to do?
        #         self.log.debug(f"""
        # Assert ->
        # oid {self.oid} ->  {ds_attr.oid}
        # single_value {self.single_value} ->  {ds_attr.single_value}
        # sup {self.sup} ->  {ds_attr.sup}
        # Merge ->
        # names {self.names} ->  {ds_attr.names}
        # NOT checking ->
        # desc {self.desc} ->  {ds_attr.desc}
        # equality {self.equality} ->  {ds_attr.equality}
        # substr {self.substr} ->  {ds_attr.substr}
        # ordering {self.ordering} ->  {ds_attr.ordering}
        #         """)

        # Assert these are the same:
        # oid
        # single_value
        # sup
        assert self.oid == ds_attr.oid
        if self.single_value != ds_attr.single_value:
            self.log.debug("Inconsistent single_value declaration")
            return True
        if set([s.lower() for s in self.sup]) != set([s.lower() for s in ds_attr.sup]):
            self.log.debug("Inconsistent superior declaration")
            return True
        # names
        if self.name_set != set([n.lower() for n in ds_attr.names]):
            self.log.debug("Inconsistent name aliases")
            return True

        # ignore all else.
        return False


class olClass(ldap.schema.models.ObjectClass):
    def __init__(self, value, log):
        self.log = log
        self.log.debug(f"olClass value -> {value}")
        super().__init__(value.split('}', 1)[1])
        self.name_set = set([x.lower() for x in self.names])

    def schema_str(self):
        return super().__str__()

    def __str__(self):
        return self.__unicode__()

    def __unicode__(self):
        return f"""{self.oid} {self.names} may -> {self.may} must -> {self.must} sup -> {self.sup}"""

    def debug_full(self, ds_obj):
        self.log.debug(f"""
Assert ->
oid {self.oid} == {ds_obj.oid}
names {self.names} == {ds_obj.names}
kind {self.kind} == {ds_obj.kind}
sup {self.sup} == {ds_obj.sup}
must {self.must} ⊇ {ds_obj.must}
may {self.may} ⊇ {ds_obj.may}

Merge ->
must {self.must} -> iff ⊇ {ds_obj.must}
may {self.may} -> iff ⊇ {ds_obj.may}

NOT checking ->
desc {self.desc} -> {ds_obj.desc}
obsolete {self.obsolete} -> {ds_obj.obsolete}""")

    def inconsistent(self, ds_obj, resolver):
        assert self.oid == ds_obj.oid
        # names
        if self.name_set != set([n.lower() for n in ds_obj.names]):
            self.log.debug("Inconsistent name aliases")
            self.debug_full(ds_obj)
            return True
        if self.kind != ds_obj.kind:
            self.log.debug("Inconsistent kind")
            self.debug_full(ds_obj)
            return True
        if set([s.lower() for s in self.sup]) != set([s.lower() for s in ds_obj.sup]):
            self.log.debug("Inconsistent superior declaration")
            self.debug_full(ds_obj)
            return True
        if set([resolver.resolve(s) for s in self.must]) != set([resolver.resolve(s) for s in ds_obj.must]):
            self.log.debug("Inconsistent Must Set")
            self.debug_full(ds_obj)
            return True
        if set([resolver.resolve(s) for s in self.may]) != set([resolver.resolve(s) for s in ds_obj.may]):
            self.log.debug("Inconsistent May Set")
            self.log.debug("ol -> %s" % [resolver.resolve(s) for s in self.may])
            self.log.debug("ds -> %s" % [resolver.resolve(s) for s in ds_obj.may])
            self.debug_full(ds_obj)
            return True
        # ignore all else.
        return False

class olSchema(object):
    def __init__(self, path, log):
        self.log = log
        self.log.debug(f"olSchema path -> {path}")
        schemas = sorted(os.listdir(path))
        self.log.debug(f"olSchemas -> {schemas}")

        self.raw_schema = []

        for schema in schemas:
            entries = ldif_parse(path, schema)
            assert len(entries) == 1
            self.raw_schema.append(entries.pop())
        # self.log.debug(f"raw_schema -> {self.raw_schema}")

        self.raw_attrs = []
        self.raw_classes = []

        for (cn, rs) in self.raw_schema:
            self.raw_attrs += ensure_list_str(rs['olcAttributeTypes'])
            self.raw_classes += ensure_list_str(rs['olcObjectClasses'])

        self.attrs = [olAttribute(x, self.log) for x in self.raw_attrs]
        self.classes = [olClass(x, self.log) for x in self.raw_classes]
        # self.log.debug(f'attrs -> {self.attrs}')
        # self.log.debug(f'classes -> {self.classes}')


class olConfig(object):
    def __init__(self, path, log=None):
        self.log = log
        if self.log is None:
            self.log = logger
        self.log.info("Examining OpenLDAP Configuration ...")
        self.log.debug(f"olConfig path -> {path}")
        config_entries = ldif_parse(path, 'cn=config.ldif')
        assert len(config_entries) == 1
        self.config_entry = config_entries.pop()
        self.log.debug(self.config_entry)

        # Parse all the child values.
        self.schema = olSchema(os.path.join(path, 'cn=config/cn=schema/'), self.log)

        dbs = sorted([
            os.path.split(x)[1].replace('.ldif', '')
            for x in os.listdir(os.path.join(path, 'cn=config'))
            if db_cond(x)
        ])
        self.log.debug(f"olDatabases -> {dbs}")

        self.databases = [
            olDatabase(os.path.join(path, f'cn=config/'), db, self.log)
            for db in dbs
        ]
        self.log.info('Completed OpenLDAP Configuration Parsing.')


