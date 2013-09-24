Root DSE
==========

Usage example
--------------
::

    # Get attribute values of 'supportedSASLMechanisms'
    standalone.rootdse.supported_sasl()
     
    # Returns True or False
    assert(standalone.rootdse.supports_sasl_gssapi()
    assert(standalone.rootdse.supports_sasl_ldapssotoken()
    assert(standalone.rootdse.supports_sasl_plain()
    assert(standalone.rootdse.supports_sasl_external()
    assert(standalone.rootdse.supports_exop_whoami()
    assert(standalone.rootdse.supports_exop_ldapssotoken_request()
    assert(standalone.rootdse.supports_exop_ldapssotoken_revoke()


Module documentation
-----------------------

.. autoclass:: lib389.rootdse.RootDSE
   :members:
