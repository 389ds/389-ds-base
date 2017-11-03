============================================================
Guidelines for using pytest and lib389
============================================================
The guide covers basic workflow with git, py.test, lib389 and python-ldap.

For a saving place purposes, I'll replace topology_m2.ms["master1"]
with master1 , etc.


Basic workflow
==============

1. Clone ds repo:
    + git clone ssh://git@pagure.io/389-ds-base.git
        + One can change the old repos like this in .git/config files
        + For ds (dirsrv) use link - https://pagure.io/389-ds-base.git
        + For lib389 use link - https://pagure.io/lib389.git

2. Go to the cloned directory
3. Create a new branch for your work:
   ::

       git checkout -b new_test_suite
4. Check out PEP8 cheat sheet:
    + https://gist.github.com/RichardBronosky/454964087739a449da04
    + https://www.python.org/dev/peps/pep-0008/
    + It is not fully mandatory in our project, but let's make our code a
      bit cleaner for other's sake

5. Use ./dirsrvtests/create_test.py tool to generate new test.py file.
    + Usage:

      ::

        create_ticket.py -t|--ticket <ticket number> -s|--suite <suite name>
        [ i|--instances <number of standalone instances> [ -m|--masters
        <number of masters> -h|--hubs <number of hubs> -c|--consumers <number
        of consumers> ] -o|--outputfile]

        Create a test suite script using "-s|--suite" instead of using
        "-t|–ticket". One day, all 'tickets' will be transferred to 'suites',
        so try to avoid the 'tickets' and try to find the place in 'suites'
        for you case. Ask around is you have doubts.
        Option "-i" can add multiple standalone instances. However, you can
        not mix "-i" with the replication options(-m, -h ,-c).


    + For example:

      :: 

        create_test.py -s basic -m 2 -o ./dirsrvtests/tests/suites/basic/basic_test.py
        # It will create basic_test.py with two masters set up and put the file to right dir
    
    
    + If you are creating a test suite, the script will add one test case
      for you with generated ID in the docstring (and it will check it for
      uniqueness)
    + Please, add more ID (to new test cases) with the next command and
      check if it is unique for other tests

      ::

        python -c 'import uuid; print(uuid.uuid4())'


6. Add some fixture(s), if needed. The purpose of test fixtures is to
   provide a fixed baseline upon which tests can reliably and repeatedly
   execute.

    + For example:

      ::

        @pytest.fixture
        def rdn_write_setup(topology_m2):
            topology_m2.ms["master1"].add_s(ENTRY)
            def fin():
                topology_m2.ms["master1"].delete_s(ENTRY_DN)
            request.addfinalizer(fin)



    + It will add some entry to the master1 in the beginning of the test
      case and delete this entry after test case is finished.

7. Add test case(s). It should be defined as function which name
   starts with "test\_"

    + For example:

      ::

        def test_search_limits_fail(topology, rdn_write_setup):

    + You can put any amount of created fixtures as the arguments

8. Write some good code with encapsulations, assertions etc.
9. Commit and push your code to your repo:

   ::

    git add ./dirsrvtests/tests/suites/basic/basic_test.py
    git commit
    git push $(whoami)


10. Test your script:

      ::

        py.test -v -s /mnt/testarea/test/ds/dirsrvtests/suites/basic


11. If everything is alright, then create a patch file for a review:

    + Go back to ds or lib389 dir (depends on where you want to send the patch) and do:

      ::

        git checkout master
        git pull
        git checkout new_test_suite
        git rebase master
        git format-patch -1


    + Basic guidelines for the commit message format

        + Separate subject from body with a blank line
        + Limit the subject line to 50 characters
        + Capitalizethesubject line
        + Do not end the subject line with a period
        + Use the imperative mood in the subject line
        + Wrap the body at 72 characters
        + Use the body to explain *what* and *why* vs. *how*
        + In the end, put a link to the ticket
        + Add "Reviewed by: ?" line. Example:

        Issue 48085 - Expand the repl acceptance test suite
        Description: Add 6 more test cases to the replication
        test suite as a part of the TET to
        pytest porting initiative.
        Increase the number of seconds we wait before the results check.

        https://pagure.io/389-ds-base/issue/48085
        
        Reviewed by: ?


12. Fixing Review Issues

    + If there are issues with your patch, git allows you to fix your
      commits.
    + If you're not already in that branch
    + git checkout new_test_suite
    + Make changes to some file
    + Add changes to your commit and fix the commit message if necessary

      ::

        git commit -a --amend

    + You can also use “ git rebase -i ” to “squash” or combine several
      commits into one commit.


Fixtures
=========

Basic info about fixtures - http://pytest.org/latest/fixture.html#fixtures

Scope
~~~~~

+ the scope for which this fixture is shared, one of “function”
  (default), “class”, “module”, “session”
+ Use “function”, if you want fixture to be applied for every test
  case where it appears
+ Use “module”, if you want fixture to be applied for a whole test
  suite (file you run)

Parametrizing
~~~~~~~~~~~~~

+ Fixture functions can be parametrized in which case they will be
  called multiple times, each time executing the set of dependent tests,
  i. e. the tests that depend on this fixture.
+ You should put your params in list and then access it within you
  fixture with request.param. For example:

  ::

    # First it will test with adding and deleting ENTRY to the first master then to the second
    @pytest.fixture(params=[0, 1])
    def rdn_write_setup(topology_m2):
        m_num = request.param
        topology_m2.ms["master{}".format(m_num)].add_s(ENTRY)
        def fin():
            topology_m2.ms["master{}".format(m_num)].delete_s(ENTRY_DN)
        request.addfinalizer(fin)


Test cases
==========

Parametrizing
~~~~~~~~~~~~~

+ The built-in pytest.mark.parametrize decorator enables
  parameterization of arguments for a test function. For example:

  ::

    ROOTDSE_DEF_ATTR_LIST = ('namingContexts',
                             'supportedLDAPVersion',
                             'supportedControl',
                             'supportedExtension',
                             'supportedSASLMechanisms',
                             'vendorName',
                             'vendorVersion')
    @pytest.mark.parametrize("rootdse_attr_name", ROOTDSE_DEF_ATTR_LIST)
    def test_def_rootdse_attr(topology_st, import_example_ldif, rootdse_attr_name):
        """Tests that operational attributes
        are not returned by default in rootDSE searches
        """
    
        log.info("Assert rootdse search hasn't {} attr".format(rootdse_attr_name))
        entries = topology_st.standalone.search_s("", ldap.SCOPE_BASE)
        entry = str(entries[0])
        assert rootdse_attr_name not in entry


+ As you can see, unlike the fixture parametrizing, in the test case
  you should first put the name of attributes, then the list (or tuple)
  with values, and then put the attribute to the function declaration.
+ You can specify a few attributes for parametrizing

  ::

    @pytest.mark.parametrize("test_input,expected", [
        ("3+5", 8),
        ("2+4", 6),
        ("6*9", 42),])
    def test_eval(test_input, expected):
        assert eval(test_input) == expected


Marking test functions and selecting them for a run
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

+ You can “mark” a test function with custom meta data like this:

  ::

    @pytest.mark.ssl
    def test_search_sec_port():
        pass # perform some search through sec port


+ You can also set a module level marker in which case it will be
  applied to all functions and methods defined in the module:

  ::

    import pytest
    pytestmark = pytest.mark.ssl


+ You can then restrict a test run to only run tests marked with ssl:

  ::

    py.test -v -m ssl

+ Or the inverse, running all tests except the ssl ones:

  ::

    py.test -v -m "not ssl"

+ Select tests based on their node ID

    + You can provide one or more node IDs as positional arguments to
      select only specified tests. This makes it easy to select tests based
      on their module, class, method, or function name:
    + py.test -v test_server.py::test_function1
      test_server.py::test_function2

+ Use -k expr to select tests based on their name

    + You can use the -k command line option to specify an expression
      which implements a substring match on the test names instead of the
      exact match on markers that -m provides. This makes it easy to select
      tests based on their names

      ::

        py.test -v -k search
        py.test -v -k "search or modify"
        py.test -v -k "not modify"

Asserting
~~~~~~~~~

+ pytest allows you to use the standard python assert for verifying
  expectations and values in Python tests. For example, you can write
  the following:

  ::

    def f():
        return 3
    def test_function():
        assert f() == 4


+ You can put the message to assert , it will be shown when error
  appears:

  ::

    assert a % 2 == 0, "value was odd, should be even"


+ In order to write assertions about raised exceptions, you can use
  pytest.raises as a context manager like this:

  ::

    import pytest
    def test_zero_division():
        with pytest.raises(ZeroDivisionError):
            1 / 0


+ Or even like this, if you expect some particular exception:

  ::

    def test_recursion_depth():
        with pytest.raises(RuntimeError) as excinfo:
            def f():
                f()
            f()
        assert 'maximum recursion' in str(excinfo.value)


Python 3 support
================

Our project should support Python 3. Python-ldap works with 'byte' strings only.
So we should use lib389 functions as much as possible because they take care of this issue.

If you still must use 'modify_s', 'add_s' or other python-ldap functions, you should consider defining the attribute as 'byte'. You can do this like this, with b'' symbol:

::

        # Modify an entry
        standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'cn', b'Mark Reynolds')])


Constants
==========

Basic constants
~~~~~~~~~~~~~~~

::

        DEFAULT_SUFFIX = “dc=example,dc=com”
        DN_DM = "cn=Directory Manager"
        PW_DM = "password"
        DN_CONFIG = "cn=config"
        DN_SCHEMA = "cn=schema"
        DN_LDBM = "cn=ldbm database,cn=plugins,cn=config"
        DN_CONFIG_LDBM = "cn=config,cn=ldbm database,cn=plugins,cn=config"
        DN_USERROOT_LDBM = "cn=userRoot,cn=ldbm database,cn=plugins,cn=config"
        DN_MONITOR = "cn=monitor"
        DN_MONITOR_SNMP = "cn=snmp,cn=monitor"
        DN_MONITOR_LDBM = "cn=monitor,cn=ldbm database,cn=plugins,cn=config"
        CMD_PATH_SETUP_DS = "setup-ds.pl"
        CMD_PATH_REMOVE_DS = "remove-ds.pl"
        CMD_PATH_SETUP_DS_ADMIN = "setup-ds-admin.pl"
        CMD_PATH_REMOVE_DS_ADMIN = "remove-ds-admin.pl"


For more info check the source code at
https://pagure.io/lib389/blob/master/f/lib389/_constants.py . If
you need a constant, use this kind of import.
If you need a lot of constants, import with *

::

    from lib389._constants import CONSTANT_YOU_NEED
    from lib389._constants import *


Add, Modify, and Delete Operations
===================================

Please, use these methods for the operations that can't be performed
by DSLdapObjects.

::

    # Add an entry
    USER_DN = 'cn=mreynolds,{}'.format(DEFAULT_SUFFIX)
    standalone.add_s(Entry((USER_DN, {
                                  'objectclass': b'top',
                                  'objectclass': b'person',
                                  'cn': b'mreynolds',
                                  'sn': b'reynolds',
                                  'userpassword': b'password'
                              })))
    
    # Modify an entry
    standalone.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'cn', b'Mark Reynolds')])
    
    # Delete an entry
    standalone.delete_s(USER_DN)


Search and Bind Operations
===================================

+ By default when an instance is created and opened, it is already
  authenticated as the Root DN(Directory Manager).
+ So you can just start searching without having to “bind”

::

    # Search
    entries = standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(cn=*)', ['cn'])
    for entry in entries:
        if 'Mark Reynolds' in entry.data['cn']:
            log.info('Search found "Mark"')
            print(entry.data['cn'])
    
    # Anonymous bind
    bind_dn = ""
    bind_pwd = ""
    
    # Bind as our test entry
    bind_dn = USER_DN
    bind_pwd = "password"
    
    # Bind as Directory Manager
    bind_dn = DN_DM
    bind_pwd = 1
    
    standalone.simple_bind_s(bind_dn, bind_pwd)


Basic instance operations
===================================

::

    # First, create a new “instance” of a “DirSrv” object
    standalone = DirSrv(verbose=False)
     
    # Set up the instance arguments (note - args_instance is a global dictionary
    # in lib389, it contains other default values)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    # Allocate the instance - initialize the “DirSrv” object with our arguments
    standalone.allocate(args_standalone)
    # Check if the instance with the args exists
    assert not standalone.exists() 
    # Create the instance - this runs setup-ds.pl and starts the server
    standalone.create()
    
    # Open the instance - create a connection to the instance,
    # and authenticates as the Root DN (cn=directory manager)
    standalone.open()
    # Done, you can start using the new instance
    # While working with DirSrv object, you can set 'verbose' parameter to True in any moment
    standalone.verbose = True
    # To remove an instance, simply use:
    standalone.delete()
    # Start, Stop, and Restart the Server
    standalone.start(timeout=10)
    standalone.stop(timeout=10)
    standalone.restart(timeout=10)
     
    # Returns True, if the instance was shutdowned disorderly
    standalone.detectDisorderlyShutdown()


Setting up SSL/TLS
===================================

::

    from lib389._constants import DEFAULT_SUFFIX, SECUREPORT_STANDALONE1
    
    standalone.stop()
     
    # Re-init (create) the nss db
    # pin.txt is created here and the password randomly generated
    assert(standalone.nss_ssl.reinit() is True)
     
    # Create a self signed CA
    # noise.txt is created here
    assert(standalone.nss_ssl.create_rsa_ca() is True)
     
    # Create a key and a cert that is signed by the self signed ca
    # This will use the hostname from the DS instance, and takes a list of extra names to take.
    assert(standalone.nss_ssl.create_rsa_key_and_cert() is True)
        
    standalone.start()
    
    # Create "cn=RSA,cn=encryption,cn=config" with next properties:
    # {'cn': 'RSA', 'nsSSLPersonalitySSL': 'Server-Cert', 'nsSSLActivation': 'on', 'nsSSLToken': 'internal (software)'}
    standalone.rsa.create()
    # Set the secure port and nsslapd-security
    standalone.config.set('nsslapd-secureport', str(SECUREPORT_STANDALONE1))
    standalone.config.set('nsslapd-security', 'on')
    standalone.sslport = SECUREPORT_STANDALONE1
    
    # Restart to allow certmaps to be re-read: Note, we CAN NOT use post_open
    standalone.restart(post_open=False)


Certification-based authentication
===================================

You need to setup and turn on SSL first (use the previous chapter).

::

    from lib389.config import CertmapLegacy
    
    standalone.stop()
     
    # Create a user
    assert(standalone.nss_ssl.create_rsa_user('testuser') is True)
     
    # Get the details of where the key and crt are
    #  {'ca': ca_path, 'key': key_path, 'crt': crt_path}
    tls_locs = standalone.nss_ssl.get_rsa_user('testuser')
    
    standalone.start()
    
    # Create user in the directory 
    users = UserAccounts(standalone, DEFAULT_SUFFIX)
    users.create(properties={
            'uid': 'testuser',
            'cn' : 'testuser',
            'sn' : 'user',
            'uidNumber' : '1000',
            'gidNumber' : '2000',
            'homeDirectory' : '/home/testuser'
    })
    
    # Turn on the certmap
    cm = CertmapLegacy(standalone)
    certmaps = cm.list()
    certmaps['default']['DNComps'] = ''
    certmaps['default']['FilterComps'] = ['cn']
    certmaps['default']['VerifyCert'] = 'off'
    cm.set(certmaps)
    
    # Restart to allow certmaps to be re-read: Note, we CAN NOT use post_open
    standalone.restart(post_open=False)
    
    # Now attempt a bind with TLS external
    conn = standalone.openConnection(saslmethod='EXTERNAL', connOnly=True, certdir=standalone.get_cert_dir(), userkey=tls_locs['key'], usercert=tls_locs['crt'])
    
    assert(conn.whoami_s() == "dn: uid=testuser,ou=People,dc=example,dc=com")


Replication
===================================

Basic configuration

+ After the instance is created, you can enable it for replication and
  set up a replication agreement.

::

    from lib389.replica import Replicas
     
    # Enable replication 
    replicas = Replicas(standalone)
    replica = replicas.enable(suffix=DEFAULT_SUFFIX,
                              role=REPLICAROLE_MASTER,
                              replicaID=REPLICAID_MASTER_1)
    # Set up replication agreement properties
    properties = {RA_NAME:           r'meTo_{}:{}'.format(master2.host, port=master2.port),
                  RA_BINDDN:         defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:         defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:         defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    
    # Create the agreement
    repl_agreement = standalone.agreement.create(suffix=DEFAULT_SUFFIX, 
                                                 host=master2.host,
                                                 port=master2.port,
                                                 properties=properties)
    # “master2” refers to another, already created, DirSrv instance(like “standalone”)
    # “repl_agreement” is the “DN” of the newly created agreement - this DN is needed later to do certain tasks
    
    # Initialize the agreement, wait for it complete, and test that replication is really working
    standalone.agreement.init(DEFAULT_SUFFIX, master2.host, master2.port)
    replica.start_and_wait(repl_agreement)
    assert replicas.test(master2)
