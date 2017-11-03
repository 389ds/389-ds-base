Backend
==========

Usage example
--------------
::

    from lib389.backend import Backends
     
    backends = Backends(standalone)
    backend = backends.create(properties={'nsslapd-suffix': 'o=new_suffix', # mandatory
                                          'cn': 'new_backend'})             # mandatory
     
    # Create sample entries
    backend.create_sample_entries(version='001003006')

    backend.delete()
     

Module documentation
-----------------------

.. autoclass:: lib389.backend.Backends
   :members:
   :inherited-members:

.. autoclass:: lib389.backend.Backend
   :members:
   :inherited-members:
