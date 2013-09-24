DirSrv log
==========

Usage example
--------------
::

    # Get array of all lines (including rotated and compresed logs):
    standalone.ds_access_log.readlines_archive()
    standalone.ds_error_log.readlines_archive()
    # Get array of all lines (without rotated and compresed logs):
    standalone.ds_access_log.readlines()
    standalone.ds_error_log.readlines()
    # Get array of lines that match the regex pattern:
    standalone.ds_access_log.match_archive('.*fd=.*')
    standalone.ds_error_log.match_archive('.*fd=.*')
    standalone.ds_access_log.match('.*fd=.*')
    standalone.ds_error_log.match('.*fd=.*')
    # Break up the log line into the specific fields:
    assert(standalone.ds_error_log.parse_line('[27/Apr/2016:13:46:35.775670167 +1000]     slapd started.  Listening on All Interfaces port 54321 for LDAP requests') == {'timestamp': '[27/Apr/2016:13:46:35.775670167 +1000]', 'message': 'slapd starte    d.  Listening on All Interfaces port 54321 for LDAP requests', 'datetime': datetime.datetime(2016, 4, 27, 13, 0, 0, 775670, tzinfo=tzoffset(No    ne, 36000))})


Module documentation
-----------------------

.. autoclass:: lib389.dirsrv_log.DirsrvAccessLog
   :members:

.. autoclass:: lib389.dirsrv_log.DirsrvErrorLog
   :members:
