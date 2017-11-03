User Accounts
=============

Usage example
--------------
::

    # There is a basic way to work with it
    from lib389.idm.user import UserAccounts

    users = UserAccounts(standalone, DEFAULT_SUFFIX)
    user_properties = {
           'uid': USER_NAME,
           'cn' : USER_NAME,
           'sn' : USER_NAME,
           'userpassword' : USER_PWD,
           'uidNumber' : '1000',
           'gidNumber' : '2000',1
           'homeDirectory' : '/home/{}'.format(USER_NAME)
            }
    testuser = users.create(properties=user_properties)
     
    # After this you can:
    # Get the list of them
    users.list()
     
    # Get some user:
    testuser = users.get('testuser')
    # or
    testuser = users.list()[0] # You can loop through 'for user in users:'
     
    # Set some attribute to the entry
    testuser.set('userPassword', 'password')
     
    # Bind as the user
    conn = testuser.bind('password') # It will create a new connection
    conn.modify_s()
    conn.unbind_s()
     
    # Delete
    testuser.delete()


Module documentation
-----------------------

.. autoclass:: lib389.idm.user.UserAccounts
   :members:
   :inherited-members:

.. autoclass:: lib389.idm.user.UserAccount
   :members:
   :inherited-members:
