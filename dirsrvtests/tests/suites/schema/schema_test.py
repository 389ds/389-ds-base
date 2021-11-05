# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on Dec 18, 2013

@author: rmeggins
'''
import logging

import ldap
import pytest
import six
from ldap.cidict import cidict
from ldap.schema import SubSchema
from lib389.schema import SchemaLegacy
from lib389._constants import *
from lib389.topologies import topology_st, topology_m2 as topo_m2
from lib389.idm.user import UserAccounts, UserAccount
from lib389.replica import ReplicationManager
from lib389.utils import ensure_bytes

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

attrclass = ldap.schema.models.AttributeType
occlass = ldap.schema.models.ObjectClass
syntax_len_supported = False


def ochasattr(subschema, oc, mustormay, attr, key):
    """See if the oc and any of its parents and ancestors have the
    given attr"""
    rc = False
    if not key in oc.__dict__:
        dd = cidict()
        for ii in oc.__dict__[mustormay]:
            dd[ii] = ii
        oc.__dict__[key] = dd
    if attr in oc.__dict__[key]:
        rc = True
    else:
        # look in parents
        for noroid in oc.sup:
            ocpar = subschema.get_obj(occlass, noroid)
            assert (ocpar)
            rc = ochasattr(subschema, ocpar, mustormay, attr, key)
            if rc:
                break
    return rc


def ochasattrs(subschema, oc, mustormay, attrs):
    key = mustormay + "dict"
    ret = []
    for attr in attrs:
        if not ochasattr(subschema, oc, mustormay, attr, key):
            ret.append(attr)
    return ret


def mycmp(v1, v2):
    v1ary, v2ary = [v1], [v2]
    if isinstance(v1, list) or isinstance(v1, tuple):
        v1ary, v2ary = list(set([x.lower() for x in v1])), list(set([x.lower() for x in v2]))
    if not len(v1ary) == len(v2ary):
        return False
    for v1, v2 in zip(v1ary, v2ary):
        if isinstance(v1, six.string_types):
            if not len(v1) == len(v2):
                return False
        if not v1 == v2:
            return False
    return True


def ocgetdiffs(ldschema, oc1, oc2):
    fields = ['obsolete', 'names', 'desc', 'must', 'may', 'kind', 'sup']
    ret = ''
    for field in fields:
        v1, v2 = oc1.__dict__[field], oc2.__dict__[field]
        if field == 'may' or field == 'must':
            missing = ochasattrs(ldschema, oc1, field, oc2.__dict__[field])
            if missing:
                ret = ret + '\t%s is missing %s\n' % (field, missing)
            missing = ochasattrs(ldschema, oc2, field, oc1.__dict__[field])
            if missing:
                ret = ret + '\t%s is missing %s\n' % (field, missing)
        elif not mycmp(v1, v2):
            ret = ret + '\t%s differs: [%s] vs. [%s]\n' % (field, oc1.__dict__[field], oc2.__dict__[field])
    return ret


def atgetparfield(subschema, at, field):
    v = None
    for nameoroid in at.sup:
        atpar = subschema.get_obj(attrclass, nameoroid)
        assert (atpar)
        v = atpar.__dict__.get(field, atgetparfield(subschema, atpar, field))
        if v is not None:
            break
    return v


def atgetdiffs(ldschema, at1, at2):
    fields = ['names', 'desc', 'obsolete', 'sup', 'equality', 'ordering', 'substr', 'syntax',
              'single_value', 'collective', 'no_user_mod', 'usage']
    if syntax_len_supported:
        fields.append('syntax_len')
    ret = ''
    for field in fields:
        v1 = at1.__dict__.get(field) or atgetparfield(ldschema, at1, field)
        v2 = at2.__dict__.get(field) or atgetparfield(ldschema, at2, field)
        if not mycmp(v1, v2):
            ret = ret + '\t%s differs: [%s] vs. [%s]\n' % (field, at1.__dict__[field], at2.__dict__[field])
    return ret


def test_schema_comparewithfiles(topology_st):
    '''Compare the schema from ldap cn=schema with the schema files'''

    log.info('Running test_schema_comparewithfiles...')

    retval = True
    schemainst = topology_st.standalone
    ldschema = schemainst.schema.get_subschema()
    assert ldschema
    for fn in schemainst.schema.list_files():
        try:
            fschema = schemainst.schema.file_to_subschema(fn)
            if fschema is None:
                raise Exception("Empty schema file %s" % fn)
        except:
            log.warning("Unable to parse %s as a schema file - skipping" % fn)
            continue
        log.info("Parsed %s as a schema file - checking" % fn)
        for oid in fschema.listall(occlass):
            se = fschema.get_obj(occlass, oid)
            assert se
            ldse = ldschema.get_obj(occlass, oid)
            if not ldse:
                log.error("objectclass in %s but not in %s: %s" % (fn, DN_SCHEMA, se))
                retval = False
                continue
            ret = ocgetdiffs(ldschema, ldse, se)
            if ret:
                log.error("name %s oid %s\n%s" % (se.names[0], oid, ret))
                retval = False
        for oid in fschema.listall(attrclass):
            se = fschema.get_obj(attrclass, oid)
            assert se
            ldse = ldschema.get_obj(attrclass, oid)
            if not ldse:
                log.error("attributetype in %s but not in %s: %s" % (fn, DN_SCHEMA, se))
                retval = False
                continue
            ret = atgetdiffs(ldschema, ldse, se)
            if ret:
                log.error("name %s oid %s\n%s" % (se.names[0], oid, ret))
                retval = False
    assert retval

    log.info('test_schema_comparewithfiles: PASSED')

def test_gecos_directoryString(topology_st):
    """Check that gecos supports directoryString value

    :id: aee422bb-6299-4124-b5cd-d7393dac19d3

    :setup: Standalone instance

    :steps:
        1. Add a common user
        2. replace gecos with a direstoryString value

    :expectedresults:
        1. Success
        2. Success
    """

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)

    user_properties = {
        'uid': 'testuser',
        'cn' : 'testuser',
        'sn' : 'user',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/testuser',
    }
    testuser = users.create(properties=user_properties)

    # Add a gecos UTF value
    testuser.replace('gecos', 'Hélène')

def test_gecos_mixed_definition_topo(topo_m2, request):
    """Check that replication is still working if schema contains
       definitions that does not conform with a replicated entry

    :id: d5940e71-d18a-4b71-aaf7-b9185361fffe
    :setup: Two suppliers replication setup
    :steps:
        1. Create a testuser on M1
        2  Stop M1 and M2
        3  Change gecos def on M2 to be IA5
        4  Update testuser with gecos directoryString value
        5  Check replication is still working
    :expectedresults:
        1. success
        2. success
        3. success
        4. success
        5. success

    """

    repl = ReplicationManager(DEFAULT_SUFFIX)
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]
    

    # create a test user
    testuser_dn = 'uid={},{}'.format('testuser', DEFAULT_SUFFIX)
    testuser = UserAccount(m1, testuser_dn)
    try:
        testuser.create(properties={
            'uid': 'testuser',
            'cn': 'testuser',
            'sn': 'testuser',
            'uidNumber' : '1000',
            'gidNumber' : '2000',
            'homeDirectory' : '/home/testuser',
        })
    except ldap.ALREADY_EXISTS:
        pass
    repl.wait_for_replication(m1, m2)

    # Stop suppliers to update the schema
    m1.stop()
    m2.stop()

    # on M1: gecos is DirectoryString (default)
    # on M2: gecos is IA5
    schema_filename = (m2.schemadir + "/99user.ldif")
    try:
        with open(schema_filename, 'w') as schema_file:
            schema_file.write("dn: cn=schema\n")
            schema_file.write("attributetypes: ( 1.3.6.1.1.1.1.2 NAME " +
                              "'gecos' DESC 'The GECOS field; the common name' " +
                              "EQUALITY caseIgnoreIA5Match " +
                              "SUBSTR caseIgnoreIA5SubstringsMatch " +
                              "SYNTAX 1.3.6.1.4.1.1466.115.121.1.26 " +
                              "SINGLE-VALUE )\n")
        os.chmod(schema_filename, 0o777)
    except OSError as e:
        log.fatal("Failed to update schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    # start the instances
    m1.start()
    m2.start()

    # Check that gecos is IA5 on M2
    schema = SchemaLegacy(m2)
    attributetypes = schema.query_attributetype('gecos')
    assert attributetypes[0].syntax == "1.3.6.1.4.1.1466.115.121.1.26"


    # Add a gecos UTF value on M1
    testuser.replace('gecos', 'Hélène')

    # Check replication is still working
    testuser.replace('displayName', 'ascii value')
    repl.wait_for_replication(m1, m2)
    testuser_m2 = UserAccount(m2, testuser_dn)
    assert testuser_m2.exists()
    assert testuser_m2.get_attr_val_utf8('displayName') == 'ascii value'

    def fin():
        m1.start()
        m2.start()
        testuser.delete()
        repl.wait_for_replication(m1, m2)

        # on M2 restore a default 99user.ldif
        m2.stop()
        os.remove(m2.schemadir + "/99user.ldif")
        schema_filename = (m2.schemadir + "/99user.ldif")
        try:
            with open(schema_filename, 'w') as schema_file:
                schema_file.write("dn: cn=schema\n")
            os.chmod(schema_filename, 0o777)
        except OSError as e:
            log.fatal("Failed to update schema file: " +
                      "{} Error: {}".format(schema_filename, str(e)))
        m2.start()
        m1.start()

    request.addfinalizer(fin)

def test_gecos_directoryString_wins_M1(topo_m2, request):
    """Check that if inital syntax are IA5(M2) and DirectoryString(M1)
    Then directoryString wins when nsSchemaCSN M1 is the greatest

    :id: ad119fa5-7671-45c8-b2ef-0b28ffb68fdb
    :setup: Two suppliers replication setup
    :steps:
        1. Create a testuser on M1
        2  Stop M1 and M2
        3  Change gecos def on M2 to be IA5
        4  Start M1 and M2
        5  Update M1 schema so that M1 has greatest nsSchemaCSN
        6  Update testuser with gecos directoryString value
        7  Check replication is still working
        8  Check gecos is DirectoryString on M1 and M2
    :expectedresults:
        1. success
        2. success
        3. success
        4. success
        5. success
        6. success
        7. success
        8. success

    """

    repl = ReplicationManager(DEFAULT_SUFFIX)
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]
    

    # create a test user
    testuser_dn = 'uid={},{}'.format('testuser', DEFAULT_SUFFIX)
    testuser = UserAccount(m1, testuser_dn)
    try:
        testuser.create(properties={
            'uid': 'testuser',
            'cn': 'testuser',
            'sn': 'testuser',
            'uidNumber' : '1000',
            'gidNumber' : '2000',
            'homeDirectory' : '/home/testuser',
        })
    except ldap.ALREADY_EXISTS:
        pass
    repl.wait_for_replication(m1, m2)

    # Stop suppliers to update the schema
    m1.stop()
    m2.stop()

    # on M1: gecos is DirectoryString (default)
    # on M2: gecos is IA5
    schema_filename = (m2.schemadir + "/99user.ldif")
    try:
        with open(schema_filename, 'w') as schema_file:
            schema_file.write("dn: cn=schema\n")
            schema_file.write("attributetypes: ( 1.3.6.1.1.1.1.2 NAME " +
                              "'gecos' DESC 'The GECOS field; the common name' " +
                              "EQUALITY caseIgnoreIA5Match " +
                              "SUBSTR caseIgnoreIA5SubstringsMatch " +
                              "SYNTAX 1.3.6.1.4.1.1466.115.121.1.26 " +
                              "SINGLE-VALUE )\n")
        os.chmod(schema_filename, 0o777)
    except OSError as e:
        log.fatal("Failed to update schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    # start the instances
    m1.start()
    m2.start()

    # Check that gecos is IA5 on M2
    schema = SchemaLegacy(m2)
    attributetypes = schema.query_attributetype('gecos')
    assert attributetypes[0].syntax == "1.3.6.1.4.1.1466.115.121.1.26"


    # update M1 schema to increase its nsschemaCSN
    new_at = "( dummy-oid NAME 'dummy' DESC 'dummy attribute' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 SINGLE-VALUE X-ORIGIN 'RFC 2307' )"
    m1.schema.add_schema('attributetypes', ensure_bytes(new_at))

    # Add a gecos UTF value on M1
    testuser.replace('gecos', 'Hélène')

    # Check replication is still working
    testuser.replace('displayName', 'ascii value')
    repl.wait_for_replication(m1, m2)
    testuser_m2 = UserAccount(m2, testuser_dn)
    assert testuser_m2.exists()
    assert testuser_m2.get_attr_val_utf8('displayName') == 'ascii value'

    # Check that gecos is DirectoryString on M1
    schema = SchemaLegacy(m1)
    attributetypes = schema.query_attributetype('gecos')
    assert attributetypes[0].syntax == "1.3.6.1.4.1.1466.115.121.1.15"

    # Check that gecos is DirectoryString on M2
    schema = SchemaLegacy(m2)
    attributetypes = schema.query_attributetype('gecos')
    assert attributetypes[0].syntax == "1.3.6.1.4.1.1466.115.121.1.15"

    def fin():
        m1.start()
        m2.start()
        testuser.delete()
        m1.schema.del_schema('attributetypes', ensure_bytes(new_at))
        repl.wait_for_replication(m1, m2)

        # on M2 restore a default 99user.ldif
        m2.stop()
        os.remove(m2.schemadir + "/99user.ldif")
        schema_filename = (m2.schemadir + "/99user.ldif")
        try:
            with open(schema_filename, 'w') as schema_file:
                schema_file.write("dn: cn=schema\n")
            os.chmod(schema_filename, 0o777)
        except OSError as e:
            log.fatal("Failed to update schema file: " +
                      "{} Error: {}".format(schema_filename, str(e)))
        m2.start()
        m1.start()

    request.addfinalizer(fin)

def test_gecos_directoryString_wins_M2(topo_m2, request):
    """Check that if inital syntax are IA5(M2) and DirectoryString(M1)
    Then directoryString wins when nsSchemaCSN M2 is the greatest

    :id: 2da7f1b1-f86d-4072-a940-ba56d4bc8348
    :setup: Two suppliers replication setup
    :steps:
        1. Create a testuser on M1
        2  Stop M1 and M2
        3  Change gecos def on M2 to be IA5
        4  Start M1 and M2
        5  Update M2 schema so that M2 has greatest nsSchemaCSN
        6  Update testuser on M2 and trigger replication to M1
        7  Update testuser on M2 with gecos directoryString value
        8  Check replication is still working
        9  Check gecos is DirectoryString on M1 and M2
    :expectedresults:
        1. success
        2. success
        3. success
        4. success
        5. success
        6. success
        7. success
        8. success
        9. success

    """

    repl = ReplicationManager(DEFAULT_SUFFIX)
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]
    

    # create a test user
    testuser_dn = 'uid={},{}'.format('testuser', DEFAULT_SUFFIX)
    testuser = UserAccount(m1, testuser_dn)
    try:
        testuser.create(properties={
            'uid': 'testuser',
            'cn': 'testuser',
            'sn': 'testuser',
            'uidNumber' : '1000',
            'gidNumber' : '2000',
            'homeDirectory' : '/home/testuser',
        })
    except ldap.ALREADY_EXISTS:
        pass
    testuser.replace('displayName', 'to trigger replication M1-> M2')
    repl.wait_for_replication(m1, m2)

    # Stop suppliers to update the schema
    m1.stop()
    m2.stop()

    # on M1: gecos is DirectoryString (default)
    # on M2: gecos is IA5
    schema_filename = (m2.schemadir + "/99user.ldif")
    try:
        with open(schema_filename, 'w') as schema_file:
            schema_file.write("dn: cn=schema\n")
            schema_file.write("attributetypes: ( 1.3.6.1.1.1.1.2 NAME " +
                              "'gecos' DESC 'The GECOS field; the common name' " +
                              "EQUALITY caseIgnoreIA5Match " +
                              "SUBSTR caseIgnoreIA5SubstringsMatch " +
                              "SYNTAX 1.3.6.1.4.1.1466.115.121.1.26 " +
                              "SINGLE-VALUE )\n")
        os.chmod(schema_filename, 0o777)
    except OSError as e:
        log.fatal("Failed to update schema file: " +
                  "{} Error: {}".format(schema_filename, str(e)))

    # start the instances
    m1.start()
    m2.start()

    # Check that gecos is IA5 on M2
    schema = SchemaLegacy(m2)
    attributetypes = schema.query_attributetype('gecos')
    assert attributetypes[0].syntax == "1.3.6.1.4.1.1466.115.121.1.26"

    # update M2 schema to increase its nsschemaCSN
    new_at = "( dummy-oid NAME 'dummy' DESC 'dummy attribute' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 SINGLE-VALUE X-ORIGIN 'RFC 2307' )"
    m2.schema.add_schema('attributetypes', ensure_bytes(new_at))

    # update just to trigger replication M2->M1
    # and update of M2 schema
    testuser_m2 = UserAccount(m2, testuser_dn)
    testuser_m2.replace('displayName', 'to trigger replication M2-> M1')

    # Add a gecos UTF value on M1
    testuser.replace('gecos', 'Hélène')

    # Check replication is still working
    testuser.replace('displayName', 'ascii value')
    repl.wait_for_replication(m1, m2)
    assert testuser_m2.exists()
    assert testuser_m2.get_attr_val_utf8('displayName') == 'ascii value'

    # Check that gecos is DirectoryString on M1
    schema = SchemaLegacy(m1)
    attributetypes = schema.query_attributetype('gecos')
    assert attributetypes[0].syntax == "1.3.6.1.4.1.1466.115.121.1.15"

    # Check that gecos is DirectoryString on M2
    schema = SchemaLegacy(m2)
    attributetypes = schema.query_attributetype('gecos')
    assert attributetypes[0].syntax == "1.3.6.1.4.1.1466.115.121.1.15"

    def fin():
        m1.start()
        m2.start()
        testuser.delete()
        m1.schema.del_schema('attributetypes', ensure_bytes(new_at))
        repl.wait_for_replication(m1, m2)

        # on M2 restore a default 99user.ldif
        m2.stop()
        os.remove(m2.schemadir + "/99user.ldif")
        schema_filename = (m2.schemadir + "/99user.ldif")
        try:
            with open(schema_filename, 'w') as schema_file:
                schema_file.write("dn: cn=schema\n")
            os.chmod(schema_filename, 0o777)
        except OSError as e:
            log.fatal("Failed to update schema file: " +
                      "{} Error: {}".format(schema_filename, str(e)))
        m2.start()

    request.addfinalizer(fin)

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
