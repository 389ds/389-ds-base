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

    https://www.port389.org/

Build Requirements (as of 2020-02-12)
-------------------------------------

nspr-devel
nss-devel
perl-generators
openldap-devel
libdb-devel
cyrus-sasl-devel
icu
libicu-devel
pcre-devel
cracklib-devel
libatomic
clang
gcc
gcc-c++
net-snmp-devel
lm_sensors-devel
bzip2-devel
zlib-devel
openssl-devel
pam-devel
systemd-units
systemd-devel
libasan
cargo
rust
pkgconfig
pkgconfig(systemd)
pkgconfig(krb5)
autoconf
automake
libtool
doxygen
libcmocka-devel
libevent-devel
python3-devel
python3-setuptools
python3-ldap
python3-six
python3-pyasn1
python3-pyasn1-modules
python3-dateutil
python3-argcomplete
python3-argparse-manpage
python3-libselinux
python3-policycoreutils
rsync
npm
nodejs
nspr-devel
nss-devel
openldap-devel
libdb-devel
cyrus-sasl-devel
libicu-devel
pcre-devel
libtalloc-devel
libevent-devel
libtevent-devel
systemd-devel

Building
--------

    autoreconf -fiv
    ./configure --enable-debug --with-openldap --enable-cmocka --enable-asan
    make
    make lib389
    sudo make install
    sudo make lib389-install

Note: **--enable-asan** is optional, and it should only be used for debugging/development purposes.

See also:  <https://www.port389.org/docs/389ds/development/building.html>

Testing
-------

    make check
    sudo py.test -s 389-ds-base/dirsrvtests/tests/suites/basic/

To debug the make check item's, you'll need libtool to help:

    libtool --mode=execute gdb /home/william/build/ds/test_slapd

More information
----------------

Please see our contributing guide online:

    https://www.port389.org/docs/389ds/contributing.html

