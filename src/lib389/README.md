Lib389
======

This repository contains tools and libraries for testing 389 Directory Server.

It can be used in place, or it can be built and installed with the provided
Makefile.

To compile the python code:

    make build

To install the python code:

    sudo make install

To build an rpm or srpm

    make [s]rpm

To test the lib389 system (Requires an installation of 389-ds-base)

    sudo make test



