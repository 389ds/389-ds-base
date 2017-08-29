Agreement
==========

Usage example
--------------
::

    master = topology.ms["master1"]
    consumer = topology.ms["consumer1"]
    # Create
    repl_agreement = master.agreement.create(suffix=DEFAULT_SUFFIX,
                                             host=consumer.host,
                                             port=consumer.port)
    # List
    ents = master.agreement.list(suffix=DEFAULT_SUFFIX,
                                 consumer_host=consumer.host,
                                 consumer_port=consumer.port)
    # Delete
    ents = master1.agreement.delete(suffix=DEFAULT_SUFFIX)


Module documentation
----------------------

.. autoclass:: lib389.agreement.Agreement
   :members:

