Indexes
==========

Usage example
--------------
::

    from lib389.index import Indexes
     
    indexes = Indexes(standalone)
     
    # Create a default index
    index = indexes.create(properties={
        'cn': 'modifytimestamp',
        'nsSystemIndex': 'false',
        'nsIndexType': 'eq'
        })

    # Get an index by DN
    index = indexes.get(dn=YOUR_INDEX_DN)

    # Set index types
    index.replace('nsIndexType', ['eq', 'sub', 'pres'])

    # Set matching rules (matching_rules - variable with matching rules)
    index.replace('caseIgnoreOrderingMatch', matching_rules)
     
    default_index_list = indexes.list()
    found = False
    for i in default_index_list:
        if i.dn.startswith('cn=modifytimestamp'):
            found = True
    assert found
    index.delete()
     
    default_index_list = indexes.list()
    found = False
    for i in default_index_list:
        if i.dn.startswith('cn=modifytimestamp'):
            found = True
    assert not found


Module documentation
-----------------------

.. autoclass:: lib389.index.Index
   :members:
   :inherited-members:

.. autoclass:: lib389.index.Indexes
   :members:
   :inherited-members:
