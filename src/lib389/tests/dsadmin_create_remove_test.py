""" Test creation and deletion of instances
"""
import ldap
import os
from dsadmin import DSAdmin, DN_CONFIG
from dsadmin.tools import DSAdminTools
from nose import *

added_instances = []


def setup():
    global added_instances


def teardown():
    global added_instances
    for instance in added_instances:
        cmd = "remove-ds.pl -i slapd-%s" % instance
        try:
            os.system(cmd)
        except:
            log.exception("error executing %r" % cmd)


def default_test():
    host = 'localhost'
    port = 10200
    binddn = "cn=directory manager"
    bindpw = "password"
    suffix = 'dc=example,dc=com'
    basedn = DN_CONFIG
    scope = ldap.SCOPE_BASE
    filt = "(objectclass=*)"
    instance_name = ['m1', 'm2']

    instance_config = {
        'cfgdshost': host,
        'cfgdsport': port,
        'cfgdsuser': 'admin',
        'cfgdspwd': 'admin',
        'newrootpw': 'password',
        'newhost': host,
        'newport': port,
        'newinstance': instance_name[0],
        'newsuffix': suffix,
        'setup_admin': True,
    }
    try:
        m1 = DSAdmin(host, port, binddn, bindpw)
    except:
        m1 = DSAdminTools.createInstance(instance_config, verbose=1)
        added_instances.append(instance_config['newinstance'])

#        filename = "%s/slapd-%s/ldif/Example.ldif" % (m1.sroot, m1.inst)
#        m1.importLDIF(filename, "dc=example,dc=com", None, True)
#        m1.exportLDIF('/tmp/ldif', "dc=example,dc=com", False, True)
    print m1.sroot, m1.inst, m1.errlog
    ent = m1.getEntry(basedn, scope, filt, None)
    if ent:
        print ent.passwordmaxage
    instance_config.update({
                           'newinstance': instance_name[1],
                           'newport': port + 10,

                           })
    m1 = DSAdminTools.createInstance(instance_config, verbose=1)
    added_instances.append(instance_config['newinstance'])
#     m1.stop(True)
#     m1.start(True)
    cn = m1.setupBackend("dc=example2,dc=com")
    rc = m1.setupSuffix("dc=example2,dc=com", cn)
    entry = m1.getEntry(DN_CONFIG, ldap.SCOPE_SUBTREE, "(cn=" + cn + ")")
    print "new backend entry is:"
    print entry
    print entry.getValues('objectclass')
    print entry.OBJECTCLASS
    results = m1.search_s("cn=monitor", ldap.SCOPE_SUBTREE)
    print results
    results = m1.getBackendsForSuffix("dc=example,dc=com")
    print results

    print "done"
