#!/bin/bash
set -e

autoreconf -fiv
./configure --enable-debug --with-openldap --enable-cmocka --enable-asan
make
make lib389
make install
make lib389-install