import pytest
from lib389.utils import *
from lib389.topologies import topology_st

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]


def test_ticket48961_storagescheme(topology_st):
    """
    Test deleting of the storage scheme.
    """

    default = topology_st.standalone.config.get_attr_val('passwordStorageScheme')
    # Change it
    topology_st.standalone.config.set('passwordStorageScheme', 'CLEAR')
    # Now delete it
    topology_st.standalone.config.remove('passwordStorageScheme', None)
    # Now check it's been reset.
    assert (default == topology_st.standalone.config.get_attr_val('passwordStorageScheme'))
    log.info(default)
    log.info('Test PASSED')


def _reset_config_value(inst, attrname):
    # None to value here means remove all instances of the attr.
    inst.config.remove(attrname, None)
    newval = inst.config.get_attr_val(attrname)
    log.info("Reset %s to %s" % (attrname, newval))


def test_ticket48961_deleteall(topology_st):
    """
    Test that we can delete all valid attrs, and that a few are rejected.
    """
    attr_to_test = {
        'nsslapd-listenhost': 'localhost',
        'nsslapd-securelistenhost': 'localhost',
        'nsslapd-allowed-sasl-mechanisms': 'GSSAPI',
        'nsslapd-svrtab': 'Some bogus data',  # This one could reset?
    }
    attr_to_fail = {
        # These are the values that should always be dn dse.ldif too
        'nsslapd-localuser': 'dirsrv',
        'nsslapd-defaultnamingcontext': 'dc=example,dc=com',  # Can't delete
        'nsslapd-accesslog': '/opt/dirsrv/var/log/dirsrv/slapd-standalone/access',
        'nsslapd-auditlog': '/opt/dirsrv/var/log/dirsrv/slapd-standalone/audit',
        'nsslapd-errorlog': '/opt/dirsrv/var/log/dirsrv/slapd-standalone/errors',
        'nsslapd-tmpdir': '/tmp',
        'nsslapd-rundir': '/opt/dirsrv/var/run/dirsrv',
        'nsslapd-bakdir': '/opt/dirsrv/var/lib/dirsrv/slapd-standalone/bak',
        'nsslapd-certdir': '/opt/dirsrv/etc/dirsrv/slapd-standalone',
        'nsslapd-instancedir': '/opt/dirsrv/lib/dirsrv/slapd-standalone',
        'nsslapd-ldifdir': '/opt/dirsrv/var/lib/dirsrv/slapd-standalone/ldif',
        'nsslapd-lockdir': '/opt/dirsrv/var/lock/dirsrv/slapd-standalone',
        'nsslapd-schemadir': '/opt/dirsrv/etc/dirsrv/slapd-standalone/schema',
        'nsslapd-workingdir': '/opt/dirsrv/var/log/dirsrv/slapd-standalone',
        'nsslapd-localhost': 'localhost.localdomain',
        # These can't be reset, but might be in dse.ldif. Probably in libglobs.
        'nsslapd-certmap-basedn': 'cn=certmap,cn=config',
        'nsslapd-port': '38931',  # Can't delete
        'nsslapd-secureport': '636',  # Can't delete
        'nsslapd-conntablesize': '1048576',
        'nsslapd-rootpw': '{SSHA512}...',
        # These are hardcoded server magic.
        'nsslapd-hash-filters': 'off',  # Can't delete
        'nsslapd-requiresrestart': 'cn=config:nsslapd-port',  # Can't change
        'nsslapd-plugin': 'cn=case ignore string syntax,cn=plugins,cn=config',  # Can't change
        'nsslapd-privatenamespaces': 'cn=schema',  # Can't change
        'nsslapd-allowed-to-delete-attrs': 'None',  # Can't delete
        'nsslapd-accesslog-list': 'List!',  # Can't delete
        'nsslapd-auditfaillog-list': 'List!',
        'nsslapd-auditlog-list': 'List!',
        'nsslapd-errorlog-list': 'List!',
        'nsslapd-config': 'cn=config',
        'nsslapd-versionstring': '389-Directory/1.3.6.0',
        'objectclass': '',
        'cn': '',
        # These are the odd values
        'nsslapd-backendconfig': 'cn=config,cn=userRoot,cn=ldbm database,cn=plugins,cn=config',  # Doesn't exist?
        'nsslapd-betype': 'ldbm database',  # Doesn't exist?
        'nsslapd-connection-buffer': 1,  # Has an ldap problem
        'nsslapd-malloc-mmap-threshold': '-10',  # Defunct anyway
        'nsslapd-malloc-mxfast': '-10',
        'nsslapd-malloc-trim-threshold': '-10',
        'nsslapd-referralmode': '',
        'nsslapd-saslpath': '',
        'passwordadmindn': '',
    }

    config_entry = topology_st.standalone.config.raw_entry()

    for attr in config_entry.getAttrs():
        if attr.lower() in attr_to_fail:
            # We know this will fail, so skip
            pass
        else:
            log.info("Reseting %s" % (attr))
            # Check if we have to do some override of this attr.
            # Some attributes need specific syntax, so we override just these.
            newval = topology_st.standalone.config.get_attr_vals(attr)
            log.info("         --> %s" % newval)
            if attr.lower() in attr_to_test:
                newval = attr_to_test[attr]
                log.info("override --> %s" % newval)
            # We need to set the attr to its own value
            # so that it's "written".
            topology_st.standalone.config.set(attr, newval)
            # Now we can really reset
            _reset_config_value(topology_st.standalone, attr)

    for attr in sorted(attr_to_fail):
        log.info("Removing %s" % attr)
        try:
            _reset_config_value(topology_st.standalone, attr)
            # Shouldn't reach here, the reset should fail!
            assert (False)
        except ldap.UNWILLING_TO_PERFORM:
            log.info('Change was rejected')
        except ldap.OPERATIONS_ERROR:
            log.info('Change was rejected')
        except ldap.OBJECT_CLASS_VIOLATION:
            log.info('Change was rejected')
        except ldap.NO_SUCH_ATTRIBUTE:
            log.info("This attribute isn't part of cn=config, so is already default!")
            pass

    topology_st.standalone.restart()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
