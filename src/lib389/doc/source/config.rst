Config
==========

Usage example
---------------
::

    # Set config attribute:
    standalone.config.set('passwordStorageScheme', 'SSHA')
     
    # Reset config attribute (by deleting it):
    standalone.config.reset('passwordStorageScheme')
     
    # Enable/disable logs (error, access, audit):
    standalone.config.enable_log('error')
    standalone.config.disable_log('access')
     
    # Set loglevel for errors log.
    # If 'update' set to True, it will add the 'vals' to existing values in the loglevel attribute
    standalone.config.loglevel(vals=(LOG_DEFAULT,), service='error', update=False)
    # You can get log levels from lib389._constants
    (LOG_TRACE,
    LOG_TRACE_PACKETS,
    LOG_TRACE_HEAVY,
    LOG_CONNECT,
    LOG_PACKET,
    LOG_SEARCH_FILTER,
    LOG_CONFIG_PARSER,
    LOG_ACL,
    LOG_ENTRY_PARSER,
    LOG_HOUSEKEEPING,
    LOG_REPLICA,
    LOG_DEFAULT,
    LOG_CACHE,
    LOG_PLUGIN,
    LOG_MICROSECONDS,
    LOG_ACL_SUMMARY) = [1 << x for x in (list(range(8)) + list(range(11, 19)))]

    # Set 'nsslapd-accesslog-logbuffering' to 'on' if True, otherwise set it to 'off'
    standalone.config.logbuffering(True)


Module documentation
-----------------------

.. autoclass:: lib389.config.Config
   :members:
