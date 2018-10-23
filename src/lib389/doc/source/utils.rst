Utils
==========

Usage example
--------------
::

    standalone.ldif2db(bename, suffixes, excludeSuffixes, encrypt, import_file)
    standalone.db2ldif(bename, suffixes, excludeSuffixes, encrypt, repl_data, outputfile)
    standalone.bak2db(archive_dir)
    standalone.db2bak(archive_dir)
    standalone.db2index(bename=None, suffixes=None, attrs=None, vlvTag=None)
    standalone.dbscan(bename=None, index=None, key=None, width=None, isRaw=False)
     
    # Generate a simple ldif file using the dbgen.pl script, and set the ownership and permissions to match the user that the server runs as
    standalone.buildLDIF(number_of_entries, path_to_ldif, suffix='dc=example,dc=com')


Module documentation
-----------------------

.. automodule:: lib389.utils
   :members:
