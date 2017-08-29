Mapping Tree
=============

Usage example
---------------
::

    # In the majority of test cases, it is better to use 'Backends' for the operation,
    # because it creates the mapping tree for you. Though if you need to create a mapping tree, you can.
    # Just work with it as with usual DSLdapObject and DSLdapObjects
    # For instance:
    from lib389.mappingTree import MappingTrees
    mts = MappingTrees(standalone)
    mt = mts.create(properties={
            'cn': ["dc=newexample,dc=com",],
            'nsslapd-state' : 'backend',
            'nsslapd-backend' : 'someRoot',
            })
    # It will be deleted with the 'backend.delete()'


Module documentation
-----------------------

.. autoclass:: lib389.mappingTree.MappingTrees
   :members:
   :inherited-members:

.. autoclass:: lib389.mappingTree.MappingTree
   :members:
   :inherited-members:
