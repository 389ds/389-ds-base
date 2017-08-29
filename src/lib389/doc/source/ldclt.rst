Config
==========

Usage example
--------------
::

    This class will allow general usage of ldclt. It's not meant to expose all the functions. Just use ldclt for that.
    # Creates users as user<min through max>. Password will be set to password<number>
    # This will automatically work with the bind loadtest.
    # Template
    # objectClass: top
    # objectclass: person
    # objectClass: organizationalPerson
    # objectClass: inetorgperson
    # objectClass: posixAccount
    # objectClass: shadowAccount
    # sn: user[A]
    # cn: user[A]
    # givenName: user[A]
    # description: description [A]
    # userPassword: user[A]
    # mail: user[A]@example.com
    # uidNumber: 1[A]
    # gidNumber: 2[A]
    # shadowMin: 0
    # shadowMax: 99999
    # shadowInactive: 30
    # shadowWarning: 7
    # homeDirectory: /home/user[A]
    # loginShell: /bin/false
    topology.instance.ldclt.create_users('ou=People,{}'.format(DEFAULT_SUFFIX), max=1999)
     
    # Run the load test for a few rounds
    topology.instance.ldclt.bind_loadtest('ou=People,{}'.format(DEFAULT_SUFFIX), max=1999)


Module documentation
-----------------------

.. autoclass:: lib389.ldclt.Ldclt
   :members:
