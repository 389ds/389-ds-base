Backend
==========

Usage example
--------------
::

    from lib389.backend import Backends
     
    backends = Backends(standalone)
    backend = backends.create(properties={BACKEND_SUFFIX: 'o=new_suffix', # mandatory
                                          BACKEND_NAME: new_backend,      # mandatory
                                          BACKEND_SAMPLE_ENTRIES: '001003006'})
     
    # Create sample entries
    backend.create_sample_entries(version='001003006')

    backend.delete()

Backend properties
-------------------

- BACKEND_NAME - 'somename'
- BACKEND_READONLY - 'on' | 'off'
- BACKEND_REQ_INDEX - 'on' | 'off'
- BACKEND_CACHE_ENTRIES - 1 to (2^32 - 1) on 32-bit systems or (2^63 - 1)
  on 64-bit systems or -1, which means limitless
- BACKEND_CACHE_SIZE - 500 kilobytes to (2^32 - 1)
  on 32-bit systems and to (2^63 - 1) on 64-bit systems
- BACKEND_DNCACHE_SIZE - 500 kilobytes to (2^32 - 1)
  on 32-bit systems and to (2^63 - 1) on 64-bit systems
- BACKEND_DIRECTORY - Any valid path to the database instance
- BACKEND_CHAIN_BIND_DN - DN of the multiplexor
- BACKEND_CHAIN_BIND_PW - password of the multiplexor
- BACKEND_CHAIN_URLS - Any valid remote server LDAP URL
- BACKEND_SUFFIX - 'o=somesuffix'
- BACKEND_SAMPLE_ENTRIES - version of confir i.e. '001003006'
     

Module documentation
-----------------------

.. autoclass:: lib389.backend.Backends
   :members:
   :inherited-members:

.. autoclass:: lib389.backend.Backend
   :members:
   :inherited-members:
