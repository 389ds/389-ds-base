Group
========

Usage example
--------------
::

    # group and groups additionaly have 'is_member', 'add_member' and 'remove_member' methods
    # posixgroup and posixgroups have 'check_member' and 'add_member'
    from lib389.idm.group import Groups
    from lib389.idm.posixgroup import PosixGroups

    groups = Groups(standalone, DEFAULT_SUFFIX)
    posix_groups = PosixGroups(standalone, DEFAULT_SUFFIX)
    group_properties = {
       'cn' : 'group1',
       'description' : 'testgroup'
       }
    group = groups.create(properties=group_properties)

    # So now you can:
    # Check the membership - shouldn't we make it consistent?
    assert(not group.is_member(testuser.dn))
    assert(not posix_groups.check_member(testuser.dn))

    group.add_member(testuser.dn)
    posix_groups.add_member(testuser.dn)

    # Remove member - add the method to PosixGroups too?
    group.remove_member(testuser.dn)

    group.delete():


Module documentation
-----------------------

.. autoclass:: lib389.idm.group.Groups
   :members:
   :inherited-members:

.. autoclass:: lib389.idm.group.Group
   :members:
   :inherited-members:
