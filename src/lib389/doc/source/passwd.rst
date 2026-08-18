LDCLT
==========

Usage example
--------------
::

    from lib389.passwd import password_hash, password_generate
     
    bindir = standalone.ds_paths.bin_dir
    PWSCHEMES = [
        'SHA1',
        'SHA256',
        'SHA512',
        'SSHA',
        'SSHA256',
        'SSHA512',
        'PBKDF2_SHA256',
        'PBKDF2-SHA256',
    ]
     
    # Generate password
    raw_secure_password = password_generate()
     
    # Encrypt the password
    # default scheme is 'SSHA512'
    secure_password = password_hash(raw_secure_password, scheme='SSHA256', bin_dir=bindir)


Module documentation
-----------------------

.. automodule:: lib389.passwd
   :members:
