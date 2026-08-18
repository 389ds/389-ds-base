Agreement
==========

Usage example
--------------
::

    supplier = topology.ms["supplier1"]
    consumer = topology.ms["consumer1"]
    # Create
    repl_agreement = supplier.agreement.create(suffix=DEFAULT_SUFFIX,
                                               host=consumer.host,
                                               port=consumer.port)
    # List
    ents = supplier.agreement.list(suffix=DEFAULT_SUFFIX,
                                   consumer_host=consumer.host,
                                   consumer_port=consumer.port)
    # Delete
    ents = supplier.agreement.delete(suffix=DEFAULT_SUFFIX)


Module documentation
----------------------

.. autoclass:: lib389.agreement.Agreement
   :members:

