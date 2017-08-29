Indexes
==========

Usage example
--------------
::

    from lib389.index import Indexes
     
    indexes = Indexes(standalone)
     
    # create and delete a default index.
    index = indexes.create(properties={
        'cn': 'modifytimestamp',
        'nsSystemIndex': 'false',
        'nsIndexType': 'eq'
        })
     
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

.. autoclass:: lib389.index.Indexes
   :members:
