389 Directory Server
====================

389 Directory Server is a highly usable, fully featured, reliable
and secure LDAP server implementation. It handles many of the
largest LDAP deployments in the world.

All our code has been extensively tested with sanitisation tools.
As well as a rich feature set of fail-over and backup technologies
gives administrators confidence their accounts are safe.

License
-------

The 389 Directory Server is subject to the terms detailed in the
license agreement file called LICENSE.

Late-breaking news and information on the 389 Directory Server is
available on our wiki page:

    http://www.port389.org/

Building
--------

    autoreconf -fiv
    ./configure --enable-debug --with-openldap --enable-cmocka --enable-asan
    make
    make lib389
    make check
    sudo make install
    sudo make lib389-install

Testing
-------

    sudo py.test -s 389-ds-base/dirsrvtests/tests/suites/basic/

More information
----------------

Please see our contributing guide online:

    http://www.port389.org/docs/389ds/contributing.html

