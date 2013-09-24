Domain
==========

Usage example
--------------
::

    # After the creating a backend, sometimes you don't need a lot of entries under the created suffix
    # So instead of using BACKEND_SAMPLE_ENTRIES you can create simple domain entry using the next object:
    from lib389.idm.domain import Domain
    domain = Domain(standalone], 'dc=test,dc=com')
    domain.create(properties={'dc': 'test', 'description': 'dc=test,dc=com'})
     
    # It will be deleted with the 'backend.delete()'


Module documentation
-----------------------

.. autoclass:: lib389.idm.domain.Domain
   :members:
